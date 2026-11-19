#include "reclaim/format.hpp"
#include "reclaim/types.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace reclaim {

namespace {

std::string normalize_slashes_lower(const std::filesystem::path& p) {
    std::string s = to_lower_ascii(p.lexically_normal().generic_string());
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

}  // namespace

const char* category_to_string(BloatCategory cat) noexcept {
    switch (cat) {
        case BloatCategory::SystemTemp: return "System & Temp Caches";
        case BloatCategory::BrowserCache: return "Browser & Electron Caches";
        case BloatCategory::DeveloperArtifacts: return "Developer & Build Artifacts";
        case BloatCategory::ShaderAndGameCache: return "GPU & Game Shader Caches";
        case BloatCategory::OrphanedAppData: return "Orphaned AppData & Stale Caches";
        case BloatCategory::InstallerLeftovers: return "Installer & Package Leftovers";
        case BloatCategory::LogsAndCrashDumps: return "Logs & Crash Dumps";
        case BloatCategory::DuplicateFiles: return "Duplicate Files";
    }
    return "Other";
}

const char* safety_to_string(SafetyLevel level) noexcept {
    switch (level) {
        case SafetyLevel::Safe: return "Safe";
        case SafetyLevel::Review: return "Review";
        case SafetyLevel::Caution: return "Caution";
    }
    return "Review";
}

std::string format_bytes(uint64_t bytes) {
    static constexpr std::array<const char*, 6> kUnits = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < kUnits.size()) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream oss;
    if (unit == 0) {
        oss << bytes << " B";
    } else if (value >= 100.0) {
        oss << std::fixed << std::setprecision(1) << value << " " << kUnits[unit];
    } else {
        oss << std::fixed << std::setprecision(2) << value << " " << kUnits[unit];
    }
    return oss.str();
}

std::string format_duration_ms(double ms) {
    std::ostringstream oss;
    if (ms < 1000.0) {
        oss << std::fixed << std::setprecision(1) << ms << " ms";
    } else {
        oss << std::fixed << std::setprecision(2) << (ms / 1000.0) << " s";
    }
    return oss.str();
}

std::string format_iso8601_utc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &tt);
#else
    gmtime_r(&tt, &utc_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string to_lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char c : input) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    std::ostringstream oss;
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c);
                    out += oss.str();
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string html_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '\"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string csv_escape(const std::string& input) {
    bool needs_quotes = false;
    for (char c : input) {
        if (c == ',' || c == '\"' || c == '\n' || c == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) return input;
    std::string out = "\"";
    for (char c : input) {
        if (c == '\"') out += "\"\"";
        else out.push_back(c);
    }
    out.push_back('\"');
    return out;
}

bool is_protected_system_path(const std::filesystem::path& path) {
    if (path.empty()) return true;

    std::string norm = normalize_slashes_lower(path);
    while (norm.size() > 1 && norm.back() == '/') {
        if (norm.size() == 3 && norm[1] == ':') break;
        norm.pop_back();
    }

    // Drive roots or filesystem root
    if (norm == "/" || (norm.size() == 2 && norm[1] == ':') ||
        (norm.size() == 3 && norm[1] == ':' && norm[2] == '/')) {
        return true;
    }

    static constexpr std::array<const char*, 22> kProtectedExact = {
        "c:/windows",
        "c:/windows/system32",
        "c:/windows/syswow64",
        "c:/windows/winsxs",
        "c:/program files",
        "c:/program files (x86)",
        "c:/users",
        "c:/programdata",
        "c:/recovery",
        "c:/boot",
        "c:/efi",
        "/bin",
        "/sbin",
        "/usr",
        "/etc",
        "/lib",
        "/lib64",
        "/boot",
        "/dev",
        "/proc",
        "/sys",
        "/home"
    };

    for (const char* p : kProtectedExact) {
        if (norm == p) return true;
    }

    static constexpr std::array<const char*, 6> kProtectedPrefixes = {
        "c:/windows/system32/",
        "c:/windows/syswow64/",
        "c:/windows/winsxs/",
        "c:/boot/",
        "c:/efi/",
        "/etc/"
    };
    for (const char* prefix : kProtectedPrefixes) {
        if (norm.rfind(prefix, 0) == 0) return true;
    }

    return false;
}

bool is_subpath_of(const std::filesystem::path& candidate,
                   const std::filesystem::path& allowed_root) {
    std::error_code ec1;
    std::error_code ec2;
    const auto norm_cand = std::filesystem::weakly_canonical(candidate, ec1);
    const auto norm_root = std::filesystem::weakly_canonical(allowed_root, ec2);
    if (ec1 || ec2) return false;

    auto cand_it = norm_cand.begin();
    for (auto root_it = norm_root.begin(); root_it != norm_root.end(); ++root_it, ++cand_it) {
        if (cand_it == norm_cand.end() || *cand_it != *root_it) {
            return false;
        }
    }
    return true;
}

bool is_excluded_path(const std::filesystem::path& path,
                      const std::vector<std::string>& exclusions) {
    if (exclusions.empty()) return false;
    const std::string norm = normalize_slashes_lower(path);
    for (const auto& ex : exclusions) {
        if (ex.empty()) continue;
        const std::string ex_norm = normalize_slashes_lower(std::filesystem::path(ex));
        if (norm.find(ex_norm) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace reclaim
