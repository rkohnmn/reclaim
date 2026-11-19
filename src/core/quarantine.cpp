#include "reclaim/quarantine.hpp"
#include "reclaim/format.hpp"
#include "reclaim/reporter.hpp"
#include "reclaim/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace reclaim {

namespace {

std::string generate_batch_id() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &tt);
#else
    gmtime_r(&tt, &utc_tm);
#endif
    std::ostringstream oss;
    oss << "batch_" << std::put_time(&utc_tm, "%Y%m%d_%H%M%S") << "_" << (ms % 100000);
    return oss.str();
}

int64_t parse_iso8601_utc_epoch(const std::string& iso) {
    // Expected format: YYYY-MM-DDTHH:MM:SSZ
    if (iso.size() < 19) return 0;
    std::tm tm{};
    std::istringstream iss(iso.substr(0, 19));
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) return 0;
#if defined(_WIN32)
    return static_cast<int64_t>(_mkgmtime(&tm));
#else
    return static_cast<int64_t>(timegm(&tm));
#endif
}

uint64_t entry_byte_size(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        const uint64_t sz = std::filesystem::file_size(path, ec);
        return ec ? 0 : sz;
    }
    if (!std::filesystem::is_directory(path, ec)) return 0;

    uint64_t total = 0;
    std::filesystem::recursive_directory_iterator it(
        path, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code st_ec;
        if (it->is_regular_file(st_ec)) {
            const uint64_t sz = it->file_size(st_ec);
            if (!st_ec) total += sz;
        }
    }
    return total;
}

Result<void> move_path_safely(const std::filesystem::path& src,
                              const std::filesystem::path& dst) {
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        return Error{ErrorCode::IoFailure, "Failed to create destination parent: " + dst.parent_path().string()};
    }

    std::filesystem::rename(src, dst, ec);
    if (!ec) return {};

    ec.clear();
    if (std::filesystem::is_directory(src, ec)) {
        std::filesystem::copy(
            src, dst,
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::copy_symlinks,
            ec);
        if (ec) return Error{ErrorCode::IoFailure, "Cross-volume copy failed for directory: " + src.string()};
        std::filesystem::remove_all(src, ec);
        if (ec) return Error{ErrorCode::IoFailure, "Copied directory but failed to remove source: " + src.string()};
    } else {
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return Error{ErrorCode::IoFailure, "Cross-volume copy failed for file: " + src.string()};
        std::filesystem::remove(src, ec);
        if (ec) return Error{ErrorCode::IoFailure, "Copied file but failed to remove source: " + src.string()};
    }
    return {};
}

std::string escape_field(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out.push_back(c);
    }
    return out;
}

std::string unescape_field(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char next = s[++i];
            if (next == 't') out.push_back('\t');
            else if (next == 'n') out.push_back('\n');
            else if (next == 'r') out.push_back('\r');
            else out.push_back(next);
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t pos = line.find('\t', start);
        if (pos == std::string::npos) {
            parts.push_back(unescape_field(line.substr(start)));
            break;
        }
        parts.push_back(unescape_field(line.substr(start, pos - start)));
        start = pos + 1;
    }
    return parts;
}

}  // namespace

QuarantineVault::QuarantineVault(std::filesystem::path vault_root)
    : vault_root_(std::move(vault_root)) {}

std::filesystem::path QuarantineVault::manifests_dir() const {
    return vault_root_ / "manifests";
}

std::filesystem::path QuarantineVault::payload_dir(const std::string& batch_id) const {
    return vault_root_ / "payloads" / batch_id;
}

Result<void> QuarantineVault::save_manifest(const QuarantineBatch& batch) const {
    std::error_code ec;
    std::filesystem::create_directories(manifests_dir(), ec);
    if (ec) {
        return Error{ErrorCode::IoFailure, "Cannot create quarantine manifests directory"};
    }

    std::ostringstream oss;
    oss << "RECLAIM_MANIFEST_V2\n";
    oss << "BATCH_ID\t" << escape_field(batch.batch_id) << "\n";
    oss << "CREATED\t" << escape_field(batch.created_iso8601) << "\n";
    oss << "REASON\t" << escape_field(batch.reason) << "\n";
    oss << "TOTAL_BYTES\t" << batch.total_bytes << "\n";
    oss << "RESTORED\t" << (batch.restored ? "1" : "0") << "\n";
    for (const auto& e : batch.entries) {
        oss << "ENTRY\t"
            << escape_field(e.original_path) << "\t"
            << escape_field(e.stored_relative_path) << "\t"
            << e.size_bytes << "\t"
            << escape_field(e.sha256_prefix) << "\t"
            << (e.is_directory ? "1" : "0") << "\t"
            << (e.restored ? "1" : "0") << "\n";
    }

    const auto target = manifests_dir() / (batch.batch_id + ".manifest");
    return write_text_file_atomic(target, oss.str());
}

Result<QuarantineBatch> QuarantineVault::load_manifest(const std::filesystem::path& file) const {
    std::ifstream in(file);
    if (!in) {
        return Error{ErrorCode::NotFound, "Cannot open manifest: " + file.string()};
    }

    std::string header;
    if (!std::getline(in, header) ||
        (header != "RECLAIM_MANIFEST_V1" && header != "RECLAIM_MANIFEST_V2")) {
        return Error{ErrorCode::CorruptManifest, "Invalid manifest header: " + file.string()};
    }

    QuarantineBatch batch;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto cols = split_tabs(line);
        if (cols.size() < 2) continue;

        if (cols[0] == "BATCH_ID") batch.batch_id = cols[1];
        else if (cols[0] == "CREATED") batch.created_iso8601 = cols[1];
        else if (cols[0] == "REASON") batch.reason = cols[1];
        else if (cols[0] == "TOTAL_BYTES") batch.total_bytes = std::stoull(cols[1]);
        else if (cols[0] == "RESTORED") batch.restored = (cols[1] == "1");
        else if (cols[0] == "ENTRY" && cols.size() >= 6) {
            const bool entry_restored = (cols.size() >= 7) ? (cols[6] == "1") : batch.restored;
            batch.entries.push_back(QuarantineEntry{
                cols[1],
                cols[2],
                std::stoull(cols[3]),
                cols[4],
                cols[5] == "1",
                entry_restored
            });
        }
    }

    if (batch.batch_id.empty()) {
        return Error{ErrorCode::CorruptManifest, "Manifest missing BATCH_ID: " + file.string()};
    }
    return batch;
}

Result<CleanResult> QuarantineVault::quarantine_items(
    const std::vector<std::filesystem::path>& paths,
    const std::string& reason,
    bool dry_run) {
    CleanResult res;
    res.batch_id = generate_batch_id();
    res.dry_run = dry_run;

    QuarantineBatch batch;
    batch.batch_id = res.batch_id;
    batch.created_iso8601 = format_iso8601_utc();
    batch.reason = reason.empty() ? "Manual cleanup" : reason;

    const auto batch_payload = payload_dir(batch.batch_id);
    size_t idx = 0;

    for (const auto& raw_path : paths) {
        std::error_code ec;
        const auto abs_path = std::filesystem::weakly_canonical(raw_path, ec);
        const auto target_path = ec ? raw_path : abs_path;

        if (is_protected_system_path(target_path)) {
            ++res.items_failed;
            res.warnings.push_back("Blocked protected system path: " + target_path.string());
            continue;
        }
        if (is_subpath_of(target_path, vault_root_)) {
            ++res.items_failed;
            res.warnings.push_back("Blocked quarantine self-reference: " + target_path.string());
            continue;
        }
        if (!std::filesystem::exists(target_path, ec)) {
            ++res.items_failed;
            res.warnings.push_back("Path no longer exists: " + target_path.string());
            continue;
        }

        const bool is_dir = std::filesystem::is_directory(target_path, ec);
        const uint64_t bytes = entry_byte_size(target_path);

        if (dry_run) {
            ++res.items_processed;
            res.bytes_reclaimed += bytes;
            continue;
        }

        std::string sha_prefix;
        if (!is_dir) {
            auto sha = hash_file_sha256(target_path);
            if (sha) sha_prefix = sha.value().substr(0, 16);
        }

        const std::string rel_name = std::to_string(idx++) + "_" + target_path.filename().string();
        const auto dst = batch_payload / rel_name;

        auto moved = move_path_safely(target_path, dst);
        if (!moved) {
            ++res.items_failed;
            res.warnings.push_back(moved.error().message);
            continue;
        }

        batch.entries.push_back(QuarantineEntry{
            target_path.string(), rel_name, bytes, sha_prefix, is_dir, false
        });
        batch.total_bytes += bytes;
        ++res.items_processed;
        res.bytes_reclaimed += bytes;
    }

    if (!dry_run && !batch.entries.empty()) {
        auto saved = save_manifest(batch);
        if (!saved) return saved.error();
    }

    return res;
}

Result<std::vector<QuarantineBatch>> QuarantineVault::list_batches() const {
    std::vector<QuarantineBatch> batches;
    std::error_code ec;
    if (!std::filesystem::exists(manifests_dir(), ec)) return batches;

    for (const auto& entry : std::filesystem::directory_iterator(manifests_dir(), ec)) {
        if (ec) break;
        if (entry.path().extension() != ".manifest") continue;
        auto loaded = load_manifest(entry.path());
        if (loaded) batches.push_back(std::move(loaded.value()));
    }

    std::sort(batches.begin(), batches.end(),
              [](const QuarantineBatch& a, const QuarantineBatch& b) {
                  return a.batch_id > b.batch_id;
              });
    return batches;
}

Result<RestoreResult> QuarantineVault::restore_entry(const std::string& batch_id, size_t entry_index) {
    const auto manifest_path = manifests_dir() / (batch_id + ".manifest");
    auto loaded = load_manifest(manifest_path);
    if (!loaded) return loaded.error();

    QuarantineBatch batch = std::move(loaded.value());
    if (entry_index >= batch.entries.size()) {
        return Error{ErrorCode::InvalidArgument, "Entry index out of range for batch " + batch_id};
    }

    auto& entry = batch.entries[entry_index];
    if (entry.restored) {
        return Error{ErrorCode::InvalidArgument, "Entry is already restored: " + entry.original_path};
    }

    const auto src = payload_dir(batch_id) / entry.stored_relative_path;
    const std::filesystem::path dst(entry.original_path);

    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) {
        return Error{ErrorCode::NotFound, "Missing quarantined payload: " + src.string()};
    }
    if (std::filesystem::exists(dst, ec)) {
        return Error{ErrorCode::IoFailure, "Original location already occupied: " + dst.string()};
    }

    auto moved = move_path_safely(src, dst);
    if (!moved) return moved.error();

    entry.restored = true;
    const bool all_restored = std::all_of(batch.entries.begin(), batch.entries.end(),
                                          [](const QuarantineEntry& e) { return e.restored; });
    if (all_restored) {
        batch.restored = true;
        std::filesystem::remove_all(payload_dir(batch_id), ec);
    }

    auto saved = save_manifest(batch);
    if (!saved) return saved.error();

    RestoreResult res;
    res.batch_id = batch_id;
    res.items_restored = 1;
    res.bytes_restored = entry.size_bytes;
    return res;
}

Result<RestoreResult> QuarantineVault::restore_batch(const std::string& batch_id) {
    const auto manifest_path = manifests_dir() / (batch_id + ".manifest");
    auto loaded = load_manifest(manifest_path);
    if (!loaded) return loaded.error();

    QuarantineBatch batch = std::move(loaded.value());
    RestoreResult res;
    res.batch_id = batch_id;

    const auto batch_payload = payload_dir(batch_id);
    for (auto& entry : batch.entries) {
        if (entry.restored) continue;

        const auto src = batch_payload / entry.stored_relative_path;
        const std::filesystem::path dst(entry.original_path);

        std::error_code ec;
        if (!std::filesystem::exists(src, ec)) {
            ++res.items_failed;
            res.warnings.push_back("Missing quarantined payload: " + src.string());
            continue;
        }
        if (std::filesystem::exists(dst, ec)) {
            ++res.items_failed;
            res.warnings.push_back("Original location already occupied: " + dst.string());
            continue;
        }

        auto moved = move_path_safely(src, dst);
        if (!moved) {
            ++res.items_failed;
            res.warnings.push_back(moved.error().message);
            continue;
        }

        entry.restored = true;
        ++res.items_restored;
        res.bytes_restored += entry.size_bytes;
    }

    const bool all_restored = std::all_of(batch.entries.begin(), batch.entries.end(),
                                          [](const QuarantineEntry& e) { return e.restored; });
    if (all_restored) {
        batch.restored = true;
        std::error_code ec;
        std::filesystem::remove_all(batch_payload, ec);
    }

    auto saved = save_manifest(batch);
    if (!saved) return saved.error();

    return res;
}

Result<uint64_t> QuarantineVault::purge_batch(const std::string& batch_id) {
    const auto manifest_path = manifests_dir() / (batch_id + ".manifest");
    auto loaded = load_manifest(manifest_path);
    if (!loaded) return loaded.error();

    const uint64_t bytes = loaded.value().total_bytes;
    std::error_code ec;
    std::filesystem::remove_all(payload_dir(batch_id), ec);
    if (ec) return Error{ErrorCode::IoFailure, "Failed to purge payload directory for " + batch_id};
    std::filesystem::remove(manifest_path, ec);
    if (ec) return Error{ErrorCode::IoFailure, "Failed to remove manifest for " + batch_id};
    return bytes;
}

Result<PurgeExpiredResult> QuarantineVault::purge_expired_batches(int retention_days,
                                                                  bool dry_run,
                                                                  int64_t now_epoch) {
    if (retention_days < 1) {
        return Error{ErrorCode::InvalidArgument, "retention_days must be >= 1"};
    }

    const int64_t ref_now = (now_epoch > 0)
                                ? now_epoch
                                : std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
    const int64_t max_age_sec = static_cast<int64_t>(retention_days) * 86400LL;

    auto batches = list_batches();
    if (!batches) return batches.error();

    PurgeExpiredResult res;
    res.dry_run = dry_run;

    for (const auto& b : batches.value()) {
        const int64_t created_sec = parse_iso8601_utc_epoch(b.created_iso8601);
        if (created_sec <= 0) continue;
        if ((ref_now - created_sec) >= max_age_sec) {
            if (!dry_run) {
                auto purged = purge_batch(b.batch_id);
                if (!purged) continue;
                res.bytes_freed += purged.value();
            } else {
                res.bytes_freed += b.total_bytes;
            }
            ++res.batches_purged;
            res.purged_batch_ids.push_back(b.batch_id);
        }
    }

    return res;
}

uint64_t QuarantineVault::total_quarantined_bytes() const {
    auto batches = list_batches();
    if (!batches) return 0;
    uint64_t total = 0;
    for (const auto& b : batches.value()) {
        if (b.restored) continue;
        for (const auto& e : b.entries) {
            if (!e.restored) total += e.size_bytes;
        }
    }
    return total;
}

}  // namespace reclaim
