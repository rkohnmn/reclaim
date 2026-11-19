#include "reclaim/config.hpp"
#include "reclaim/reporter.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace reclaim {

ConfigStore::ConfigStore(std::filesystem::path config_path)
    : config_path_(std::move(config_path)) {}

std::filesystem::path ConfigStore::default_data_dir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(appdata) / "Reclaim";
    }
    return std::filesystem::path("C:/ProgramData/Reclaim");
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        return std::filesystem::path(xdg) / "reclaim";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".local" / "share" / "reclaim";
    }
    return std::filesystem::temp_directory_path() / "reclaim";
#endif
}

Result<AppConfig> ConfigStore::load() const {
    AppConfig cfg;
    std::error_code ec;
    if (!std::filesystem::exists(config_path_, ec)) {
        return cfg;
    }

    std::ifstream in(config_path_);
    if (!in) {
        return Error{ErrorCode::IoFailure, "Unable to read config: " + config_path_.string()};
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "dark_theme") cfg.dark_theme = (val == "1" || val == "true");
        else if (key == "dry_run_default") cfg.dry_run_default = (val == "1" || val == "true");
        else if (key == "quarantine_retention_days") {
            try {
                const int d = std::stoi(val);
                if (d >= 1 && d <= 365) cfg.quarantine_retention_days = d;
            } catch (...) {
            }
        } else if (key == "duplicate_min_bytes") {
            try {
                cfg.duplicate_min_bytes = std::stoull(val);
            } catch (...) {
            }
        } else if (key == "last_scan_path") {
            cfg.last_scan_path = val;
        } else if (key == "exclude") {
            if (!val.empty()) cfg.exclusions.push_back(val);
        }
    }

    return cfg;
}

Result<void> ConfigStore::save(const AppConfig& cfg) const {
    std::error_code ec;
    if (config_path_.has_parent_path()) {
        std::filesystem::create_directories(config_path_.parent_path(), ec);
    }

    std::ostringstream oss;
    oss << "# Reclaim User Configuration (zero telemetry, local only)\n";
    oss << "dark_theme=" << (cfg.dark_theme ? "1" : "0") << "\n";
    oss << "dry_run_default=" << (cfg.dry_run_default ? "1" : "0") << "\n";
    oss << "quarantine_retention_days=" << cfg.quarantine_retention_days << "\n";
    oss << "duplicate_min_bytes=" << cfg.duplicate_min_bytes << "\n";
    oss << "last_scan_path=" << cfg.last_scan_path << "\n";
    for (const auto& ex : cfg.exclusions) {
        oss << "exclude=" << ex << "\n";
    }

    return write_text_file_atomic(config_path_, oss.str());
}

}  // namespace reclaim
