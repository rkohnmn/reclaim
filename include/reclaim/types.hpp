#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace reclaim {

namespace fs = std::filesystem;

enum class ErrorCode {
    None = 0,
    InvalidPath,
    ProtectedPath,
    PermissionDenied,
    IoFailure,
    NotFound,
    Cancelled,
    CorruptManifest,
    InvalidArgument
};

struct Error {
    ErrorCode code{ErrorCode::None};
    std::string message;

    bool ok() const noexcept { return code == ErrorCode::None; }
};

template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error err) : error_(std::move(err)) {}

    bool ok() const noexcept { return error_.ok() && value_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    const T& value() const& { return *value_; }
    T& value() & { return *value_; }
    T&& value() && { return std::move(*value_); }

    const Error& error() const noexcept { return error_; }

private:
    std::optional<T> value_;
    Error error_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error err) : error_(std::move(err)) {}

    bool ok() const noexcept { return error_.ok(); }
    explicit operator bool() const noexcept { return ok(); }
    const Error& error() const noexcept { return error_; }

private:
    Error error_;
};

enum class SafetyLevel {
    Safe = 0,
    Review = 1,
    Caution = 2
};

enum class BloatCategory {
    SystemTemp = 0,
    BrowserCache,
    DeveloperArtifacts,
    ShaderAndGameCache,
    OrphanedAppData,
    InstallerLeftovers,
    LogsAndCrashDumps,
    DuplicateFiles
};

enum class ScanBackend {
    Filesystem = 0,
    NtfsMft = 1
};

struct CleanupRule {
    std::string id;
    std::string name;
    std::string description;
    BloatCategory category{BloatCategory::SystemTemp};
    SafetyLevel safety{SafetyLevel::Safe};
    std::vector<std::string> dir_name_matches;
    std::vector<std::string> path_substrings;
    std::vector<std::string> extensions;
    uint64_t min_size_bytes{0};
    bool default_selected{true};
};

struct ScannedItem {
    fs::path path;
    uint64_t size_bytes{0};
    int64_t modified_epoch{0};
    std::string rule_id;
    std::string rule_name;
    BloatCategory category{BloatCategory::SystemTemp};
    SafetyLevel safety{SafetyLevel::Safe};
    bool is_directory{false};
    bool selected{true};
};

struct CategorySummary {
    BloatCategory category{BloatCategory::SystemTemp};
    std::string label;
    uint64_t total_bytes{0};
    uint64_t selected_bytes{0};
    size_t item_count{0};
    SafetyLevel highest_risk{SafetyLevel::Safe};
};

struct FileNode {
    std::string name;
    fs::path path;
    uint64_t size_bytes{0};
    bool is_directory{false};
    std::vector<FileNode> children;
};

struct ScanReport {
    fs::path root_path;
    ScanBackend backend_used{ScanBackend::Filesystem};
    uint64_t total_scanned_bytes{0};
    uint64_t total_reclaimable_bytes{0};
    size_t total_files_visited{0};
    size_t total_dirs_visited{0};
    size_t skipped_entries{0};
    double duration_ms{0.0};
    std::vector<ScannedItem> items;
    std::vector<CategorySummary> categories;
    FileNode root_tree;
};

struct DuplicateFile {
    fs::path path;
    int64_t modified_epoch{0};
    bool keep{false};
};

struct DuplicateGroup {
    std::string sha256_hex;
    uint64_t file_size_bytes{0};
    std::vector<DuplicateFile> files;

    uint64_t reclaimable_bytes() const noexcept {
        if (files.size() <= 1) return 0;
        size_t delete_count = 0;
        for (const auto& f : files) {
            if (!f.keep) ++delete_count;
        }
        return file_size_bytes * delete_count;
    }
};

struct DuplicateReport {
    size_t files_considered{0};
    size_t partial_hash_calls{0};
    size_t full_hash_calls{0};
    uint64_t total_reclaimable_bytes{0};
    double duration_ms{0.0};
    std::vector<DuplicateGroup> groups;
};

struct QuarantineEntry {
    std::string original_path;
    std::string stored_relative_path;
    uint64_t size_bytes{0};
    std::string sha256_prefix;
    bool is_directory{false};
    bool restored{false};
};

struct QuarantineBatch {
    std::string batch_id;
    std::string created_iso8601;
    std::string reason;
    uint64_t total_bytes{0};
    bool restored{false};
    std::vector<QuarantineEntry> entries;
};

struct ScanProgress {
    size_t files_visited{0};
    size_t dirs_visited{0};
    uint64_t bytes_scanned{0};
    uint64_t reclaimable_bytes{0};
    std::string current_path;
};

using ProgressCallback = std::function<void(const ScanProgress&)>;

const char* category_to_string(BloatCategory cat) noexcept;
const char* safety_to_string(SafetyLevel level) noexcept;

}  // namespace reclaim
