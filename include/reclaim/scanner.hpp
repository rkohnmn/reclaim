#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include "reclaim/types.hpp"

namespace reclaim {

struct ScanOptions {
    std::filesystem::path root_path;
    std::vector<CleanupRule> rules;
    std::vector<std::string> exclusions;
    size_t max_depth{64};
    size_t max_tree_depth{5};
    bool follow_symlinks{false};
    bool build_tree{true};
    bool prefer_ntfs_mft{false};  // Opt-in elevated Windows NTFS $MFT acceleration
    const std::atomic<bool>* cancel_flag{nullptr};
    ProgressCallback progress_cb;
};

class StorageScanner {
public:
    Result<ScanReport> scan(const ScanOptions& options) const;
};

}  // namespace reclaim
