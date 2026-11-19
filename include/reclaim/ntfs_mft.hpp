#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "reclaim/types.hpp"

namespace reclaim {

struct MftRecord {
    uint64_t frn{0};         // NTFS File Reference Number
    uint64_t parent_frn{0};  // Parent Directory FRN
    std::string name;
    uint64_t size_bytes{0};
    bool is_directory{false};
};

struct MftScanResult {
    FileNode root_tree;
    size_t records_indexed{0};
    size_t files_indexed{0};
    size_t dirs_indexed{0};
    double duration_ms{0.0};
};

// Returns true when running on Windows with Administrator privileges capable of raw NTFS $MFT access.
bool can_use_ntfs_mft(const std::filesystem::path& root_path);

// Cross-platform testable FRN parent-child graph reconstructor.
// Builds a cycle-safe, size-aggregated FileNode hierarchy from flat NTFS MFT records.
FileNode reconstruct_mft_tree(const std::vector<MftRecord>& records,
                              uint64_t root_frn,
                              const std::filesystem::path& volume_root,
                              size_t max_tree_depth = 6);

// Reads the NTFS Master File Table on Windows (when elevated on a local NTFS volume root)
// via FSCTL_GET_NTFS_VOLUME_DATA + FSCTL_ENUM_USN_DATA.
// Returns ErrorCode::PermissionDenied or InvalidPath if unavailable (caller falls back cleanly).
Result<MftScanResult> scan_ntfs_mft(const std::filesystem::path& volume_root,
                                    const std::atomic<bool>* cancel_flag = nullptr,
                                    size_t max_tree_depth = 6);

}  // namespace reclaim
