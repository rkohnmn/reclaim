#include "reclaim/sha256.hpp"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace reclaim {

namespace {

constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

inline uint32_t rotr(uint32_t x, uint32_t n) noexcept {
    return (x >> n) | (x << (32u - n));
}

inline uint64_t fnv1a_update(uint64_t h, const uint8_t* data, size_t len) noexcept {
    constexpr uint64_t kPrime = 1099511628211ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint64_t>(data[i]);
        h *= kPrime;
    }
    return h;
}

}  // namespace

Sha256::Sha256() {
    state_[0] = 0x6a09e667u;
    state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u;
    state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu;
    state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu;
    state_[7] = 0x5be0cd19u;
}

void Sha256::transform(const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        buffer_[buffer_len_++] = data[i];
        if (buffer_len_ == 64) {
            transform(buffer_);
            bit_count_ += 512;
            buffer_len_ = 0;
        }
    }
}

void Sha256::update(const std::string& text) {
    update(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

std::array<uint8_t, 32> Sha256::finalize() {
    bit_count_ += static_cast<uint64_t>(buffer_len_) * 8ULL;

    buffer_[buffer_len_++] = 0x80u;
    if (buffer_len_ > 56) {
        while (buffer_len_ < 64) buffer_[buffer_len_++] = 0x00u;
        transform(buffer_);
        buffer_len_ = 0;
    }
    while (buffer_len_ < 56) buffer_[buffer_len_++] = 0x00u;

    for (int i = 7; i >= 0; --i) {
        buffer_[56 + (7 - i)] = static_cast<uint8_t>((bit_count_ >> (i * 8)) & 0xFFu);
    }
    transform(buffer_);

    std::array<uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<uint8_t>((state_[i] >> 24) & 0xFFu);
        digest[i * 4 + 1] = static_cast<uint8_t>((state_[i] >> 16) & 0xFFu);
        digest[i * 4 + 2] = static_cast<uint8_t>((state_[i] >> 8) & 0xFFu);
        digest[i * 4 + 3] = static_cast<uint8_t>(state_[i] & 0xFFu);
    }
    return digest;
}

std::string Sha256::to_hex(const std::array<uint8_t, 32>& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint8_t b : digest) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0Fu]);
    }
    return out;
}

std::string Sha256::hash_bytes(const uint8_t* data, size_t length) {
    Sha256 hasher;
    hasher.update(data, length);
    return to_hex(hasher.finalize());
}

std::string Sha256::hash_string(const std::string& text) {
    return hash_bytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

Result<uint64_t> hash_file_partial(const std::filesystem::path& path) {
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return Error{ErrorCode::IoFailure, "Cannot stat file: " + path.string()};
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Error{ErrorCode::PermissionDenied, "Cannot open file: " + path.string()};
    }

    constexpr uint64_t kOffsetBasis = 14695981039346656037ULL;
    uint64_t h = kOffsetBasis;
    const uint8_t size_bytes[8] = {
        static_cast<uint8_t>(size & 0xFFu),
        static_cast<uint8_t>((size >> 8) & 0xFFu),
        static_cast<uint8_t>((size >> 16) & 0xFFu),
        static_cast<uint8_t>((size >> 24) & 0xFFu),
        static_cast<uint8_t>((size >> 32) & 0xFFu),
        static_cast<uint8_t>((size >> 40) & 0xFFu),
        static_cast<uint8_t>((size >> 48) & 0xFFu),
        static_cast<uint8_t>((size >> 56) & 0xFFu),
    };
    h = fnv1a_update(h, size_bytes, 8);

    std::array<uint8_t, 4096> buf{};
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    const auto first_read = static_cast<size_t>(in.gcount());
    h = fnv1a_update(h, buf.data(), first_read);

    if (size > buf.size()) {
        in.clear();
        in.seekg(-static_cast<std::streamoff>(buf.size()), std::ios::end);
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const auto tail_read = static_cast<size_t>(in.gcount());
        h = fnv1a_update(h, buf.data(), tail_read);
    }

    return h;
}

Result<std::string> hash_file_sha256(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Error{ErrorCode::PermissionDenied, "Cannot open file for SHA-256: " + path.string()};
    }

    Sha256 hasher;
    std::array<uint8_t, 65536> buf{};
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const auto count = static_cast<size_t>(in.gcount());
        if (count > 0) {
            hasher.update(buf.data(), count);
        }
    }
    if (in.bad()) {
        return Error{ErrorCode::IoFailure, "I/O error while reading: " + path.string()};
    }
    return Sha256::to_hex(hasher.finalize());
}

}  // namespace reclaim
