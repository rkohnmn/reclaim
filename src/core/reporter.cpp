#include "reclaim/reporter.hpp"
#include "reclaim/format.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace reclaim {

Result<void> write_text_file_atomic(const std::filesystem::path& target,
                                    const std::string& content) {
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            return Error{ErrorCode::IoFailure, "Cannot create parent directory for: " + target.string()};
        }
    }

    const auto tmp_path = target.string() + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Error{ErrorCode::IoFailure, "Cannot open temp file for writing: " + tmp_path};
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            return Error{ErrorCode::IoFailure, "Write failed for: " + tmp_path};
        }
    }

    std::filesystem::rename(tmp_path, target, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return Error{ErrorCode::IoFailure, "Atomic rename failed for: " + target.string()};
    }
    return {};
}

std::string export_scan_json(const ScanReport& report) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"generated_utc\": \"" << json_escape(format_iso8601_utc()) << "\",\n";
    oss << "  \"root_path\": \"" << json_escape(report.root_path.string()) << "\",\n";
    oss << "  \"total_scanned_bytes\": " << report.total_scanned_bytes << ",\n";
    oss << "  \"total_reclaimable_bytes\": " << report.total_reclaimable_bytes << ",\n";
    oss << "  \"files_visited\": " << report.total_files_visited << ",\n";
    oss << "  \"dirs_visited\": " << report.total_dirs_visited << ",\n";
    oss << "  \"duration_ms\": " << std::fixed << std::setprecision(2) << report.duration_ms << ",\n";

    oss << "  \"categories\": [\n";
    for (size_t i = 0; i < report.categories.size(); ++i) {
        const auto& c = report.categories[i];
        oss << "    {\"label\": \"" << json_escape(c.label)
            << "\", \"total_bytes\": " << c.total_bytes
            << ", \"item_count\": " << c.item_count
            << ", \"risk\": \"" << safety_to_string(c.highest_risk) << "\"}"
            << (i + 1 < report.categories.size() ? "," : "") << "\n";
    }
    oss << "  ],\n";

    oss << "  \"items\": [\n";
    for (size_t i = 0; i < report.items.size(); ++i) {
        const auto& item = report.items[i];
        oss << "    {\"path\": \"" << json_escape(item.path.string())
            << "\", \"size_bytes\": " << item.size_bytes
            << ", \"rule\": \"" << json_escape(item.rule_id)
            << "\", \"category\": \"" << json_escape(category_to_string(item.category))
            << "\", \"safety\": \"" << safety_to_string(item.safety)
            << "\", \"is_directory\": " << (item.is_directory ? "true" : "false") << "}"
            << (i + 1 < report.items.size() ? "," : "") << "\n";
    }
    oss << "  ]\n";
    oss << "}\n";
    return oss.str();
}

std::string export_scan_csv(const ScanReport& report) {
    std::ostringstream oss;
    oss << "path,size_bytes,size_human,category,rule_id,safety,is_directory\n";
    for (const auto& item : report.items) {
        oss << csv_escape(item.path.string()) << ","
            << item.size_bytes << ","
            << csv_escape(format_bytes(item.size_bytes)) << ","
            << csv_escape(category_to_string(item.category)) << ","
            << csv_escape(item.rule_id) << ","
            << safety_to_string(item.safety) << ","
            << (item.is_directory ? "true" : "false") << "\n";
    }
    return oss.str();
}

std::string export_scan_html(const ScanReport& report) {
    std::ostringstream oss;
    oss << "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        << "<title>Reclaim Storage Audit Report</title>"
        << "<style>"
        << "body{font-family:'Segoe UI',system-ui,sans-serif;background:#0D1117;color:#E6EDF3;margin:0;padding:32px;}"
        << ".wrap{max-width:1080px;margin:0 auto;}"
        << "h1{font-size:24px;margin:0 0 6px;color:#F59E0B;}"
        << ".sub{color:#8B949E;font-size:13px;margin-bottom:24px;}"
        << ".kpis{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin-bottom:24px;}"
        << ".card{background:#161B22;border:1px solid #30363D;border-radius:8px;padding:16px;}"
        << ".label{font-size:11px;text-transform:uppercase;color:#8B949E;letter-spacing:.05em;}"
        << ".val{font-size:22px;font-weight:600;margin-top:6px;font-variant-numeric:tabular-nums;}"
        << "table{width:100%;border-collapse:collapse;background:#161B22;border:1px solid #30363D;}"
        << "th,td{text-align:left;padding:10px 12px;border-bottom:1px solid #30363D;font-size:13px;}"
        << "th{color:#8B949E;font-weight:600;}"
        << ".num{font-variant-numeric:tabular-nums;text-align:right;}"
        << ".badge{padding:2px 8px;border-radius:4px;font-size:11px;font-weight:600;}"
        << ".Safe{background:#132E1C;color:#56D364;}.Review{background:#2F230E;color:#E3B341;}.Caution{background:#341616;color:#FF7B72;}"
        << "</style></head><body><div class=\"wrap\">"
        << "<h1>Reclaim Storage Audit Report</h1>"
        << "<div class=\"sub\">Target: " << html_escape(report.root_path.string())
        << " | Generated: " << html_escape(format_iso8601_utc()) << "</div>"
        << "<div class=\"kpis\">"
        << "<div class=\"card\"><div class=\"label\">Reclaimable Space</div><div class=\"val\">" << format_bytes(report.total_reclaimable_bytes) << "</div></div>"
        << "<div class=\"card\"><div class=\"label\">Total Scanned</div><div class=\"val\">" << format_bytes(report.total_scanned_bytes) << "</div></div>"
        << "<div class=\"card\"><div class=\"label\">Detected Items</div><div class=\"val\">" << report.items.size() << "</div></div>"
        << "<div class=\"card\"><div class=\"label\">Scan Duration</div><div class=\"val\">" << format_duration_ms(report.duration_ms) << "</div></div>"
        << "</div>"
        << "<table><thead><tr><th>Path</th><th>Category</th><th>Safety</th><th class=\"num\">Size</th></tr></thead><tbody>";

    const size_t limit = std::min<size_t>(report.items.size(), 250);
    for (size_t i = 0; i < limit; ++i) {
        const auto& item = report.items[i];
        const char* safety = safety_to_string(item.safety);
        oss << "<tr><td>" << html_escape(item.path.string()) << "</td>"
            << "<td>" << html_escape(category_to_string(item.category)) << "</td>"
            << "<td><span class=\"badge " << safety << "\">" << safety << "</span></td>"
            << "<td class=\"num\">" << format_bytes(item.size_bytes) << "</td></tr>";
    }

    oss << "</tbody></table></div></body></html>";
    return oss.str();
}

std::string export_duplicates_json(const DuplicateReport& report) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"files_considered\": " << report.files_considered << ",\n";
    oss << "  \"partial_hash_calls\": " << report.partial_hash_calls << ",\n";
    oss << "  \"full_hash_calls\": " << report.full_hash_calls << ",\n";
    oss << "  \"total_reclaimable_bytes\": " << report.total_reclaimable_bytes << ",\n";
    oss << "  \"duration_ms\": " << std::fixed << std::setprecision(2) << report.duration_ms << ",\n";
    oss << "  \"groups\": [\n";
    for (size_t i = 0; i < report.groups.size(); ++i) {
        const auto& g = report.groups[i];
        oss << "    {\"sha256\": \"" << g.sha256_hex
            << "\", \"file_size_bytes\": " << g.file_size_bytes
            << ", \"files\": [";
        for (size_t j = 0; j < g.files.size(); ++j) {
            oss << "{\"path\": \"" << json_escape(g.files[j].path.string())
                << "\", \"keep\": " << (g.files[j].keep ? "true" : "false") << "}"
                << (j + 1 < g.files.size() ? ", " : "");
        }
        oss << "]}" << (i + 1 < report.groups.size() ? "," : "") << "\n";
    }
    oss << "  ]\n}\n";
    return oss.str();
}

}  // namespace reclaim
