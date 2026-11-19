#pragma once

#include <filesystem>
#include <string>

#include "reclaim/types.hpp"

namespace reclaim {

// Generates structured audit reports in JSON, CSV, or self-contained HTML.
std::string export_scan_json(const ScanReport& report);
std::string export_scan_csv(const ScanReport& report);
std::string export_scan_html(const ScanReport& report);
std::string export_duplicates_json(const DuplicateReport& report);

Result<void> write_text_file_atomic(const std::filesystem::path& target,
                                    const std::string& content);

}  // namespace reclaim
