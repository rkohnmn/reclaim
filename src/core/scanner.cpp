#include "reclaim/scanner.hpp"
#include "reclaim/format.hpp"
#include "reclaim/ntfs_mft.hpp"
#include "reclaim/rules.hpp"

#include <algorithm>
#include <chrono>
#include <map>

namespace reclaim {

namespace {

int64_t file_mtime_epoch(const std::filesystem::path& p) {
    std::error_code ec;
    const auto ftime = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
}

uint64_t compute_directory_size(const std::filesystem::path& dir,
                                size_t& files_visited,
                                size_t& dirs_visited,
                                size_t& skipped,
                                const std::atomic<bool>* cancel_flag) {
    uint64_t total = 0;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        dir,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec) {
        ++skipped;
        return 0;
    }

    std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ++skipped;
            ec.clear();
            continue;
        }
        if (cancel_flag && cancel_flag->load(std::memory_order_relaxed)) break;
        std::error_code st_ec;
        if (it->is_symlink(st_ec)) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(st_ec)) {
            ++dirs_visited;
        } else if (it->is_regular_file(st_ec)) {
            ++files_visited;
            const uint64_t sz = it->file_size(st_ec);
            if (!st_ec) total += sz;
        }
    }
    return total;
}

uint64_t walk_recursive(const std::filesystem::path& current,
                        size_t depth,
                        const ScanOptions& opts,
                        const std::vector<CleanupRule>& rules,
                        ScanReport& report,
                        FileNode* tree_node) {
    if (opts.cancel_flag && opts.cancel_flag->load(std::memory_order_relaxed)) return 0;
    if (depth > opts.max_depth) return 0;

    std::error_code ec;
    std::filesystem::directory_iterator it(
        current,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec) {
        ++report.skipped_entries;
        return 0;
    }

    uint64_t dir_total = 0;

    for (const auto& entry : it) {
        if (opts.cancel_flag && opts.cancel_flag->load(std::memory_order_relaxed)) break;

        const auto& p = entry.path();
        if (is_excluded_path(p, opts.exclusions)) continue;

        std::error_code st_ec;
        if (!opts.follow_symlinks && entry.is_symlink(st_ec)) continue;

        const bool is_dir = entry.is_directory(st_ec);
        if (st_ec) {
            ++report.skipped_entries;
            continue;
        }

        if (is_dir) {
            ++report.total_dirs_visited;
            const auto matched = match_cleanup_rule(p, true, 0, rules);
            if (matched.has_value() && !is_protected_system_path(p)) {
                const uint64_t sub_size = compute_directory_size(
                    p, report.total_files_visited, report.total_dirs_visited,
                    report.skipped_entries, opts.cancel_flag);
                dir_total += sub_size;
                report.total_reclaimable_bytes += sub_size;
                report.items.push_back(ScannedItem{
                    p, sub_size, file_mtime_epoch(p),
                    matched->id, matched->name, matched->category, matched->safety,
                    true, matched->default_selected
                });
                if (tree_node && depth < opts.max_tree_depth && sub_size > 0) {
                    tree_node->children.push_back(FileNode{p.filename().string(), p, sub_size, true, {}});
                }
            } else {
                FileNode child_node;
                FileNode* child_ptr = nullptr;
                if (tree_node && depth < opts.max_tree_depth) {
                    child_node.name = p.filename().string();
                    child_node.path = p;
                    child_node.is_directory = true;
                    child_ptr = &child_node;
                }
                const uint64_t sub_size = walk_recursive(p, depth + 1, opts, rules, report, child_ptr);
                dir_total += sub_size;
                if (child_ptr && sub_size > 0) {
                    child_node.size_bytes = sub_size;
                    tree_node->children.push_back(std::move(child_node));
                }
            }
        } else if (entry.is_regular_file(st_ec)) {
            ++report.total_files_visited;
            const uint64_t fsize = entry.file_size(st_ec);
            if (st_ec) {
                ++report.skipped_entries;
                continue;
            }
            dir_total += fsize;

            const auto matched = match_cleanup_rule(p, false, fsize, rules);
            if (matched.has_value() && !is_protected_system_path(p)) {
                report.total_reclaimable_bytes += fsize;
                report.items.push_back(ScannedItem{
                    p, fsize, file_mtime_epoch(p),
                    matched->id, matched->name, matched->category, matched->safety,
                    false, matched->default_selected
                });
            }

            if (tree_node && depth < opts.max_tree_depth && fsize > 0) {
                tree_node->children.push_back(FileNode{p.filename().string(), p, fsize, false, {}});
            }
        }

        if (opts.progress_cb && (report.total_files_visited % 200 == 0)) {
            opts.progress_cb(ScanProgress{
                report.total_files_visited,
                report.total_dirs_visited,
                dir_total,
                report.total_reclaimable_bytes,
                p.string()
            });
        }
    }

    if (tree_node) {
        std::sort(tree_node->children.begin(), tree_node->children.end(),
                  [](const FileNode& a, const FileNode& b) { return a.size_bytes > b.size_bytes; });
    }

    return dir_total;
}

void collect_mft_rules(const FileNode& node,
                       const std::vector<CleanupRule>& rules,
                       const std::vector<std::string>& exclusions,
                       ScanReport& report) {
    if (is_excluded_path(node.path, exclusions)) return;
    const auto matched = match_cleanup_rule(node.path, node.is_directory, node.size_bytes, rules);
    if (matched.has_value() && !is_protected_system_path(node.path)) {
        report.total_reclaimable_bytes += node.size_bytes;
        report.items.push_back(ScannedItem{
            node.path, node.size_bytes, 0,
            matched->id, matched->name, matched->category, matched->safety,
            node.is_directory, matched->default_selected
        });
        if (node.is_directory) return;
    }
    for (const auto& child : node.children) {
        collect_mft_rules(child, rules, exclusions, report);
    }
}

}  // namespace

Result<ScanReport> StorageScanner::scan(const ScanOptions& options) const {
    const auto t0 = std::chrono::steady_clock::now();

    std::error_code ec;
    if (options.root_path.empty() || !std::filesystem::exists(options.root_path, ec)) {
        return Error{ErrorCode::NotFound, "Scan root does not exist: " + options.root_path.string()};
    }
    if (!std::filesystem::is_directory(options.root_path, ec)) {
        return Error{ErrorCode::InvalidPath, "Scan root is not a directory: " + options.root_path.string()};
    }

    const auto rules = options.rules.empty() ? default_cleanup_rules() : options.rules;

    ScanReport report;
    report.root_path = std::filesystem::weakly_canonical(options.root_path, ec);
    if (ec) report.root_path = options.root_path;

    // Optional elevated NTFS $MFT fast path (falls back cleanly to filesystem scan if unavailable)
    bool used_mft = false;
    if (options.prefer_ntfs_mft && can_use_ntfs_mft(report.root_path)) {
        auto mft = scan_ntfs_mft(report.root_path, options.cancel_flag, options.max_tree_depth);
        if (mft) {
            report.backend_used = ScanBackend::NtfsMft;
            report.root_tree = std::move(mft.value().root_tree);
            report.total_files_visited = mft.value().files_indexed;
            report.total_dirs_visited = mft.value().dirs_indexed;
            report.total_scanned_bytes = report.root_tree.size_bytes;
            collect_mft_rules(report.root_tree, rules, options.exclusions, report);
            used_mft = true;
        }
    }

    if (!used_mft) {
        report.backend_used = ScanBackend::Filesystem;
        report.root_tree.name = report.root_path.filename().empty()
                                    ? report.root_path.string()
                                    : report.root_path.filename().string();
        report.root_tree.path = report.root_path;
        report.root_tree.is_directory = true;

        report.total_scanned_bytes = walk_recursive(
            report.root_path, 0, options, rules, report,
            options.build_tree ? &report.root_tree : nullptr);
        report.root_tree.size_bytes = report.total_scanned_bytes;
    }

    if (options.cancel_flag && options.cancel_flag->load(std::memory_order_relaxed)) {
        return Error{ErrorCode::Cancelled, "Scan cancelled by user"};
    }

    std::sort(report.items.begin(), report.items.end(),
              [](const ScannedItem& a, const ScannedItem& b) {
                  return a.size_bytes > b.size_bytes;
              });

    std::map<BloatCategory, CategorySummary> summary_map;
    for (const auto& item : report.items) {
        auto& s = summary_map[item.category];
        s.category = item.category;
        s.label = category_to_string(item.category);
        s.total_bytes += item.size_bytes;
        if (item.selected) s.selected_bytes += item.size_bytes;
        ++s.item_count;
        if (static_cast<int>(item.safety) > static_cast<int>(s.highest_risk)) {
            s.highest_risk = item.safety;
        }
    }
    for (auto& [cat, summary] : summary_map) {
        report.categories.push_back(std::move(summary));
    }
    std::sort(report.categories.begin(), report.categories.end(),
              [](const CategorySummary& a, const CategorySummary& b) {
                  return a.total_bytes > b.total_bytes;
              });

    const auto t1 = std::chrono::steady_clock::now();
    report.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return report;
}

}  // namespace reclaim
