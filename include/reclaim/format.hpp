#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reclaim {

std::string format_bytes(uint64_t bytes);
std::string format_duration_ms(double ms);
std::string format_iso8601_utc();
std::string to_lower_ascii(std::string value);
std::string json_escape(const std::string& input);
std::string html_escape(const std::string& input);
std::string csv_escape(const std::string& input);

// Security invariant: returns true if path is a critical OS/system root or directory
// that Reclaim must NEVER clean or quarantine.
bool is_protected_system_path(const std::filesystem::path& path);

// Returns true if path is safely contained within allowed_root (prevents path traversal).
bool is_subpath_of(const std::filesystem::path& candidate,
                   const std::filesystem::path& allowed_root);

// Checks whether a path matches any user exclusion prefix or substring.
bool is_excluded_path(const std::filesystem::path& path,
                      const std::vector<std::string>& exclusions);

}  // namespace reclaim
