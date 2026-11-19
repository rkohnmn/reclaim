#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "reclaim/types.hpp"

namespace reclaim {

enum class KeepPolicy {
    KeepOldest = 0,
    KeepNewest = 1,
    KeepShortestPath = 2
};

struct DuplicateOptions {
    std::filesystem::path root_path;
    uint64_t min_file_size_bytes{1024};  // Ignore tiny files < 1 KB by default
    KeepPolicy keep_policy{KeepPolicy::KeepOldest};
    std::vector<std::string> exclusions;
    const std::atomic<bool>* cancel_flag{nullptr};
};

// 3-Stage Cryptographic Duplicate Finder:
//   Stage 1: Group regular files by exact byte size (eliminates unique sizes in O(N)).
//   Stage 2: Compute 64-bit partial fingerprint (first 4 KB + last 4 KB) on size collisions.
//   Stage 3: Verify byte-for-byte identity via full streaming SHA-256.
// Safety Invariant: Every duplicate group ALWAYS marks at least one file `keep = true`.
class DuplicateFinder {
public:
    Result<DuplicateReport> find(const DuplicateOptions& options) const;
};

}  // namespace reclaim
