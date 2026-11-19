#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "reclaim/types.hpp"

namespace reclaim {

// Self-contained FIPS 180-4 SHA-256 implementation (zero external crypto library dependencies).
class Sha256 {
public:
    Sha256();
    void update(const uint8_t* data, size_t length);
    void update(const std::string& text);
    std::array<uint8_t, 32> finalize();
    static std::string to_hex(const std::array<uint8_t, 32>& digest);
    static std::string hash_bytes(const uint8_t* data, size_t length);
    static std::string hash_string(const std::string& text);

private:
    void transform(const uint8_t block[64]);
    uint32_t state_[8];
    uint64_t bit_count_{0};
    uint8_t buffer_[64]{};
    size_t buffer_len_{0};
};

// Fast 64-bit FNV-1a partial file fingerprint (first 4 KB + last 4 KB + file size)
// used as Stage 2 filter before full SHA-256 verification.
Result<uint64_t> hash_file_partial(const std::filesystem::path& path);

// Full streaming SHA-256 hex digest of a file (64 KB read buffer).
Result<std::string> hash_file_sha256(const std::filesystem::path& path);

}  // namespace reclaim
