#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "reclaim/types.hpp"

namespace reclaim {

struct AppConfig {
    bool dark_theme{true};
    bool dry_run_default{false};
    int quarantine_retention_days{30};
    uint64_t duplicate_min_bytes{4096};
    std::string last_scan_path;
    std::vector<std::string> exclusions;
};

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path config_path);

    Result<AppConfig> load() const;
    Result<void> save(const AppConfig& cfg) const;

    static std::filesystem::path default_data_dir();

private:
    std::filesystem::path config_path_;
};

}  // namespace reclaim
