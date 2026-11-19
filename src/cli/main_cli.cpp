#include "reclaim/config.hpp"
#include "reclaim/duplicates.hpp"
#include "reclaim/format.hpp"
#include "reclaim/quarantine.hpp"
#include "reclaim/reporter.hpp"
#include "reclaim/rules.hpp"
#include "reclaim/scanner.hpp"
#include "reclaim/treemap.hpp"

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout
        << "Reclaim CLI v1.1.0 — Fast, Zero-Telemetry Storage Analyzer & Quarantine Cleaner\n\n"
        << "Usage:\n"
        << "  reclaim-cli scan <path> [--rules <rules.json>] [--mft] [--json <f> | --csv <f> | --html <f>] [--exclude <s>]\n"
        << "  reclaim-cli clean <path> [--rules <rules.json>] [--apply] [--include-review] [--vault <dir>]\n"
        << "  reclaim-cli dupes <path> [--min-bytes <n>] [--json <file>]\n"
        << "  reclaim-cli treemap <path> [--width <px>] [--height <px>]\n"
        << "  reclaim-cli rules export <out.json>\n"
        << "  reclaim-cli vault list [--vault <dir>]\n"
        << "  reclaim-cli vault show <batch_id> [--vault <dir>]\n"
        << "  reclaim-cli vault restore <batch_id> [--vault <dir>]\n"
        << "  reclaim-cli vault restore-file <batch_id> <entry_index> [--vault <dir>]\n"
        << "  reclaim-cli vault purge <batch_id> [--vault <dir>]\n"
        << "  reclaim-cli vault purge-expired [--days <n>] [--apply] [--vault <dir>]\n"
        << "  reclaim-cli --version\n";
}

std::filesystem::path resolve_vault(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--vault") {
            return std::filesystem::path(argv[i + 1]);
        }
    }
    return reclaim::ConfigStore::default_data_dir() / "quarantine";
}

reclaim::Result<std::vector<reclaim::CleanupRule>> resolve_rules(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--rules") {
            return reclaim::load_rules_from_json_file(argv[i + 1]);
        }
    }
    const auto user_rules = reclaim::ConfigStore::default_data_dir() / "rules.json";
    std::error_code ec;
    if (std::filesystem::exists(user_rules, ec)) {
        return reclaim::load_rules_from_json_file(user_rules);
    }
    return reclaim::default_cleanup_rules();
}

int cmd_scan(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Error: scan requires a target directory path.\n";
        return 2;
    }

    auto rules_res = resolve_rules(argc, argv);
    if (!rules_res) {
        std::cerr << "Rule pack error: " << rules_res.error().message << "\n";
        return 1;
    }

    reclaim::ScanOptions opts;
    opts.root_path = argv[2];
    opts.rules = std::move(rules_res.value());
    std::string json_out, csv_out, html_out;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--json" && i + 1 < argc) json_out = argv[++i];
        else if (arg == "--csv" && i + 1 < argc) csv_out = argv[++i];
        else if (arg == "--html" && i + 1 < argc) html_out = argv[++i];
        else if (arg == "--exclude" && i + 1 < argc) opts.exclusions.push_back(argv[++i]);
        else if (arg == "--mft") opts.prefer_ntfs_mft = true;
    }

    reclaim::StorageScanner scanner;
    auto res = scanner.scan(opts);
    if (!res) {
        std::cerr << "Scan failed: " << res.error().message << "\n";
        return 1;
    }

    const auto& r = res.value();
    std::cout << "Reclaim Scan Summary\n";
    std::cout << "  Root:        " << r.root_path.string() << "\n";
    std::cout << "  Backend:     " << (r.backend_used == reclaim::ScanBackend::NtfsMft ? "NTFS $MFT" : "Filesystem")
              << " (" << opts.rules.size() << " rules active)\n";
    std::cout << "  Scanned:     " << reclaim::format_bytes(r.total_scanned_bytes)
              << " (" << r.total_files_visited << " files, " << r.total_dirs_visited << " dirs)\n";
    std::cout << "  Reclaimable: " << reclaim::format_bytes(r.total_reclaimable_bytes)
              << " (" << r.items.size() << " items)\n";
    std::cout << "  Duration:    " << reclaim::format_duration_ms(r.duration_ms) << "\n\n";

    if (!r.categories.empty()) {
        std::cout << "Category Breakdown:\n";
        for (const auto& c : r.categories) {
            std::cout << "  [" << std::setw(7) << std::left << reclaim::safety_to_string(c.highest_risk) << "] "
                      << std::setw(34) << std::left << c.label
                      << std::setw(12) << std::right << reclaim::format_bytes(c.total_bytes)
                      << "  (" << c.item_count << " items)\n";
        }
    }

    if (!json_out.empty()) {
        auto w = reclaim::write_text_file_atomic(json_out, reclaim::export_scan_json(r));
        if (!w) { std::cerr << w.error().message << "\n"; return 1; }
        std::cout << "\nWrote JSON report: " << json_out << "\n";
    }
    if (!csv_out.empty()) {
        auto w = reclaim::write_text_file_atomic(csv_out, reclaim::export_scan_csv(r));
        if (!w) { std::cerr << w.error().message << "\n"; return 1; }
        std::cout << "Wrote CSV report:  " << csv_out << "\n";
    }
    if (!html_out.empty()) {
        auto w = reclaim::write_text_file_atomic(html_out, reclaim::export_scan_html(r));
        if (!w) { std::cerr << w.error().message << "\n"; return 1; }
        std::cout << "Wrote HTML report: " << html_out << "\n";
    }
    return 0;
}

int cmd_clean(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Error: clean requires a target directory path.\n";
        return 2;
    }

    auto rules_res = resolve_rules(argc, argv);
    if (!rules_res) {
        std::cerr << "Rule pack error: " << rules_res.error().message << "\n";
        return 1;
    }

    bool apply = false;
    bool include_review = false;
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--apply") apply = true;
        else if (arg == "--include-review") include_review = true;
    }

    reclaim::ScanOptions opts;
    opts.root_path = argv[2];
    opts.rules = std::move(rules_res.value());
    opts.build_tree = false;

    reclaim::StorageScanner scanner;
    auto scanned = scanner.scan(opts);
    if (!scanned) {
        std::cerr << "Scan failed: " << scanned.error().message << "\n";
        return 1;
    }

    std::vector<std::filesystem::path> targets;
    for (const auto& item : scanned.value().items) {
        if (item.safety == reclaim::SafetyLevel::Safe ||
            (include_review && item.safety == reclaim::SafetyLevel::Review)) {
            targets.push_back(item.path);
        }
    }

    reclaim::QuarantineVault vault(resolve_vault(argc, argv));
    auto res = vault.quarantine_items(targets, "CLI cleanup of " + std::string(argv[2]), !apply);
    if (!res) {
        std::cerr << "Clean failed: " << res.error().message << "\n";
        return 1;
    }

    const auto& c = res.value();
    std::cout << (c.dry_run ? "[DRY RUN] " : "[APPLIED] ")
              << "Batch " << c.batch_id << ": "
              << c.items_processed << " items (" << reclaim::format_bytes(c.bytes_reclaimed) << ")";
    if (c.items_failed > 0) std::cout << ", " << c.items_failed << " skipped";
    std::cout << "\n";
    if (!c.dry_run && c.items_processed > 0) {
        std::cout << "Undo all:  reclaim-cli vault restore " << c.batch_id << "\n";
        std::cout << "Inspect:   reclaim-cli vault show " << c.batch_id << "\n";
    }
    return 0;
}

int cmd_dupes(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Error: dupes requires a target directory path.\n";
        return 2;
    }

    reclaim::DuplicateOptions opts;
    opts.root_path = argv[2];
    std::string json_out;
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--min-bytes" && i + 1 < argc) opts.min_file_size_bytes = std::stoull(argv[++i]);
        else if (arg == "--json" && i + 1 < argc) json_out = argv[++i];
    }

    reclaim::DuplicateFinder finder;
    auto res = finder.find(opts);
    if (!res) {
        std::cerr << "Duplicate scan failed: " << res.error().message << "\n";
        return 1;
    }

    const auto& r = res.value();
    std::cout << "Duplicate Finder Summary\n";
    std::cout << "  Files Considered: " << r.files_considered
              << " | Partial Hashes: " << r.partial_hash_calls
              << " | Full SHA-256: " << r.full_hash_calls << "\n";
    std::cout << "  Duplicate Groups: " << r.groups.size()
              << " | Reclaimable: " << reclaim::format_bytes(r.total_reclaimable_bytes)
              << " | Duration: " << reclaim::format_duration_ms(r.duration_ms) << "\n";

    for (size_t i = 0; i < std::min<size_t>(r.groups.size(), 15); ++i) {
        const auto& g = r.groups[i];
        std::cout << "\n  Group " << (i + 1) << " (" << reclaim::format_bytes(g.file_size_bytes)
                  << " each, sha256:" << g.sha256_hex.substr(0, 12) << ")\n";
        for (const auto& f : g.files) {
            std::cout << "    [" << (f.keep ? "KEEP" : "DUPE") << "] " << f.path.string() << "\n";
        }
    }

    if (!json_out.empty()) {
        auto w = reclaim::write_text_file_atomic(json_out, reclaim::export_duplicates_json(r));
        if (!w) { std::cerr << w.error().message << "\n"; return 1; }
    }
    return 0;
}

int cmd_treemap(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Error: treemap requires a target directory path.\n";
        return 2;
    }
    double w = 1000.0, h = 600.0;
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--width" && i + 1 < argc) w = std::stod(argv[++i]);
        else if (arg == "--height" && i + 1 < argc) h = std::stod(argv[++i]);
    }

    reclaim::ScanOptions opts;
    opts.root_path = argv[2];
    reclaim::StorageScanner scanner;
    auto res = scanner.scan(opts);
    if (!res) {
        std::cerr << "Scan failed: " << res.error().message << "\n";
        return 1;
    }

    const auto tiles = reclaim::compute_squarified_treemap(res.value().root_tree, {0, 0, w, h}, 20);
    std::cout << "Squarified Treemap (" << w << "x" << h << ", " << tiles.size() << " tiles):\n";
    for (const auto& t : tiles) {
        std::cout << "  " << std::setw(28) << std::left << t.name
                  << std::setw(12) << std::right << reclaim::format_bytes(t.size_bytes)
                  << "  (" << std::fixed << std::setprecision(1) << (t.fraction_of_parent * 100.0) << "%)"
                  << "  rect=[" << static_cast<int>(t.rect.x) << "," << static_cast<int>(t.rect.y)
                  << " " << static_cast<int>(t.rect.width) << "x" << static_cast<int>(t.rect.height) << "]\n";
    }
    return 0;
}

int cmd_rules(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[2]) == "export") {
        const auto json = reclaim::export_rules_json(reclaim::default_cleanup_rules());
        auto w = reclaim::write_text_file_atomic(argv[3], json);
        if (!w) { std::cerr << w.error().message << "\n"; return 1; }
        std::cout << "Exported default rule pack to: " << argv[3] << "\n";
        return 0;
    }
    print_usage();
    return 2;
}

int cmd_vault(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Error: vault requires subcommand (list | show | restore | restore-file | purge | purge-expired).\n";
        return 2;
    }
    reclaim::QuarantineVault vault(resolve_vault(argc, argv));
    std::string sub = argv[2];

    if (sub == "list") {
        auto batches = vault.list_batches();
        if (!batches) { std::cerr << batches.error().message << "\n"; return 1; }
        if (batches.value().empty()) {
            std::cout << "Quarantine Vault is empty.\n";
            return 0;
        }
        for (const auto& b : batches.value()) {
            std::cout << b.batch_id << " | " << b.created_iso8601
                      << " | " << reclaim::format_bytes(b.total_bytes)
                      << " | " << b.entries.size() << " items"
                      << " | " << (b.restored ? "RESTORED" : "ACTIVE")
                      << " | " << b.reason << "\n";
        }
        return 0;
    }
    if (sub == "show" && argc >= 4) {
        auto batches = vault.list_batches();
        if (!batches) { std::cerr << batches.error().message << "\n"; return 1; }
        for (const auto& b : batches.value()) {
            if (b.batch_id == argv[3]) {
                std::cout << "Batch " << b.batch_id << " (" << b.created_iso8601 << ")\n";
                for (size_t i = 0; i < b.entries.size(); ++i) {
                    const auto& e = b.entries[i];
                    std::cout << "  [" << i << "] " << (e.restored ? "[RESTORED] " : "[ACTIVE]   ")
                              << std::setw(10) << std::right << reclaim::format_bytes(e.size_bytes)
                              << "  " << e.original_path << "\n";
                }
                return 0;
            }
        }
        std::cerr << "Batch not found: " << argv[3] << "\n";
        return 1;
    }
    if (sub == "restore" && argc >= 4) {
        auto r = vault.restore_batch(argv[3]);
        if (!r) { std::cerr << r.error().message << "\n"; return 1; }
        std::cout << "Restored " << r.value().items_restored << " items ("
                  << reclaim::format_bytes(r.value().bytes_restored) << ").\n";
        return 0;
    }
    if (sub == "restore-file" && argc >= 5) {
        const size_t idx = std::stoull(argv[4]);
        auto r = vault.restore_entry(argv[3], idx);
        if (!r) { std::cerr << r.error().message << "\n"; return 1; }
        std::cout << "Restored entry #" << idx << " from " << argv[3]
                  << " (" << reclaim::format_bytes(r.value().bytes_restored) << ").\n";
        return 0;
    }
    if (sub == "purge" && argc >= 4) {
        auto p = vault.purge_batch(argv[3]);
        if (!p) { std::cerr << p.error().message << "\n"; return 1; }
        std::cout << "Permanently purged " << argv[3] << " ("
                  << reclaim::format_bytes(p.value()) << ").\n";
        return 0;
    }
    if (sub == "purge-expired") {
        int days = 30;
        bool apply = false;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--days" && i + 1 < argc) days = std::stoi(argv[++i]);
            else if (arg == "--apply") apply = true;
        }
        auto p = vault.purge_expired_batches(days, !apply);
        if (!p) { std::cerr << p.error().message << "\n"; return 1; }
        std::cout << (p.value().dry_run ? "[DRY RUN] " : "[APPLIED] ")
                  << "Expired batches (>=" << days << " days): " << p.value().batches_purged
                  << " (" << reclaim::format_bytes(p.value().bytes_freed) << ")\n";
        return 0;
    }

    print_usage();
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 0;
    }
    const std::string cmd = argv[1];
    if (cmd == "--version" || cmd == "-v") {
        std::cout << "Reclaim v1.1.0\n";
        return 0;
    }
    if (cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    }
    if (cmd == "scan") return cmd_scan(argc, argv);
    if (cmd == "clean") return cmd_clean(argc, argv);
    if (cmd == "dupes") return cmd_dupes(argc, argv);
    if (cmd == "treemap") return cmd_treemap(argc, argv);
    if (cmd == "rules") return cmd_rules(argc, argv);
    if (cmd == "vault") return cmd_vault(argc, argv);

    std::cerr << "Unknown command: " << cmd << "\n";
    print_usage();
    return 2;
}
