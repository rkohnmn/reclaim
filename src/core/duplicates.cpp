#include "reclaim/duplicates.hpp"
#include "reclaim/format.hpp"
#include "reclaim/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace reclaim {

namespace {

int64_t get_mtime(const std::filesystem::path& p) {
    std::error_code ec;
    const auto ftime = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
}

void apply_keep_policy(DuplicateGroup& group, KeepPolicy policy) {
    if (group.files.empty()) return;

    for (auto& f : group.files) {
        f.keep = false;
    }

    auto best = group.files.begin();
    for (auto it = group.files.begin() + 1; it != group.files.end(); ++it) {
        if (policy == KeepPolicy::KeepOldest) {
            if (it->modified_epoch < best->modified_epoch) best = it;
        } else if (policy == KeepPolicy::KeepNewest) {
            if (it->modified_epoch > best->modified_epoch) best = it;
        } else {
            if (it->path.string().size() < best->path.string().size()) best = it;
        }
    }
    // Safety invariant: always keep at least one copy
    best->keep = true;
}

}  // namespace

Result<DuplicateReport> DuplicateFinder::find(const DuplicateOptions& options) const {
    const auto t0 = std::chrono::steady_clock::now();

    std::error_code ec;
    if (options.root_path.empty() || !std::filesystem::exists(options.root_path, ec)) {
        return Error{ErrorCode::NotFound, "Duplicate scan root does not exist: " + options.root_path.string()};
    }

    DuplicateReport report;

    // Stage 1: Bucket regular files by exact byte size
    std::unordered_map<uint64_t, std::vector<std::filesystem::path>> by_size;
    std::filesystem::recursive_directory_iterator it(
        options.root_path,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec) {
        return Error{ErrorCode::IoFailure, "Unable to traverse directory: " + options.root_path.string()};
    }

    std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (options.cancel_flag && options.cancel_flag->load(std::memory_order_relaxed)) {
            return Error{ErrorCode::Cancelled, "Duplicate scan cancelled by user"};
        }

        const auto& p = it->path();
        if (is_excluded_path(p, options.exclusions)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            ec.clear();
            continue;
        }

        std::error_code st_ec;
        if (it->is_symlink(st_ec)) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(st_ec) || st_ec) continue;

        const uint64_t sz = it->file_size(st_ec);
        if (st_ec || sz < options.min_file_size_bytes) continue;
        if (is_protected_system_path(p)) continue;

        ++report.files_considered;
        by_size[sz].push_back(p);
    }

    // Stage 2: Partial 64-bit fingerprint (first 4 KB + last 4 KB + size) on size collisions
    for (auto& [size, candidates] : by_size) {
        if (candidates.size() < 2) continue;

        std::unordered_map<uint64_t, std::vector<std::filesystem::path>> by_partial;
        for (const auto& p : candidates) {
            if (options.cancel_flag && options.cancel_flag->load(std::memory_order_relaxed)) {
                return Error{ErrorCode::Cancelled, "Duplicate scan cancelled by user"};
            }
            ++report.partial_hash_calls;
            auto partial = hash_file_partial(p);
            if (!partial) continue;
            by_partial[partial.value()].push_back(p);
        }

        // Stage 3: Full SHA-256 verification on partial-hash collisions
        for (auto& [partial_hash, matches] : by_partial) {
            if (matches.size() < 2) continue;

            std::unordered_map<std::string, DuplicateGroup> by_sha;
            for (const auto& p : matches) {
                if (options.cancel_flag && options.cancel_flag->load(std::memory_order_relaxed)) {
                    return Error{ErrorCode::Cancelled, "Duplicate scan cancelled by user"};
                }
                ++report.full_hash_calls;
                auto sha = hash_file_sha256(p);
                if (!sha) continue;

                auto& grp = by_sha[sha.value()];
                grp.sha256_hex = sha.value();
                grp.file_size_bytes = size;
                grp.files.push_back(DuplicateFile{p, get_mtime(p), false});
            }

            for (auto& [digest, grp] : by_sha) {
                if (grp.files.size() < 2) continue;
                apply_keep_policy(grp, options.keep_policy);
                report.total_reclaimable_bytes += grp.reclaimable_bytes();
                report.groups.push_back(std::move(grp));
            }
        }
    }

    // Sort duplicate groups by wasted space descending
    std::sort(report.groups.begin(), report.groups.end(),
              [](const DuplicateGroup& a, const DuplicateGroup& b) {
                  return a.reclaimable_bytes() > b.reclaimable_bytes();
              });

    const auto t1 = std::chrono::steady_clock::now();
    report.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return report;
}

}  // namespace reclaim
