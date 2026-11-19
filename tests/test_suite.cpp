#include "test_runner.hpp"

#include "reclaim/config.hpp"
#include "reclaim/duplicates.hpp"
#include "reclaim/format.hpp"
#include "reclaim/json.hpp"
#include "reclaim/ntfs_mft.hpp"
#include "reclaim/quarantine.hpp"
#include "reclaim/reporter.hpp"
#include "reclaim/rules.hpp"
#include "reclaim/scanner.hpp"
#include "reclaim/sha256.hpp"
#include "reclaim/treemap.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

namespace {

namespace fs = std::filesystem;

class TempDir {
public:
    explicit TempDir(const std::string& tag) {
        path_ = fs::temp_directory_path() / ("reclaim_test_" + tag);
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

void write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

}  // namespace

RECLAIM_TEST(Sha256NistVectors) {
    EXPECT_EQ(reclaim::Sha256::hash_string(""),
              std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    EXPECT_EQ(reclaim::Sha256::hash_string("abc"),
              std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    EXPECT_EQ(reclaim::Sha256::hash_string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

RECLAIM_TEST(ProtectedSystemPathsAreHardBlocked) {
    EXPECT_TRUE(reclaim::is_protected_system_path("C:\\"));
    EXPECT_TRUE(reclaim::is_protected_system_path("C:/Windows"));
    EXPECT_TRUE(reclaim::is_protected_system_path("C:\\Windows\\System32\\kernel32.dll"));
    EXPECT_TRUE(reclaim::is_protected_system_path("C:\\Windows\\WinSxS\\amd64_test"));
    EXPECT_TRUE(reclaim::is_protected_system_path("/"));
    EXPECT_TRUE(reclaim::is_protected_system_path("/etc/passwd"));
    EXPECT_FALSE(reclaim::is_protected_system_path("C:/Users/Alice/AppData/Local/Temp/scratch.tmp"));
    EXPECT_FALSE(reclaim::is_protected_system_path("/tmp/reclaim_user_cache/foo.tmp"));
}

RECLAIM_TEST(ScannerDetectsBloatCategoriesAndRespectsExclusions) {
    TempDir tmp("scanner");
    write_file(tmp.path() / "AppData/Local/Google/Chrome/User Data/Cache_Data/chunk.bin", std::string(4096, 'A'));
    write_file(tmp.path() / "Games/DXCache/shader01.bin", std::string(8192, 'B'));
    write_file(tmp.path() / "Projects/web/node_modules/pkg/index.js", std::string(2048, 'C'));
    write_file(tmp.path() / "Projects/keep_me/node_modules/pkg/index.js", std::string(2048, 'C'));
    write_file(tmp.path() / "Documents/report.pdf", std::string(5000, 'D'));
    write_file(tmp.path() / "Scratch/stale.tmp", std::string(1500, 'E'));

    reclaim::ScanOptions opts;
    opts.root_path = tmp.path();
    opts.exclusions = {"keep_me"};

    reclaim::StorageScanner scanner;
    auto res = scanner.scan(opts);
    EXPECT_TRUE(res.ok());

    const auto& r = res.value();
    EXPECT_EQ(r.items.size(), static_cast<size_t>(4));
    EXPECT_EQ(r.total_reclaimable_bytes, static_cast<uint64_t>(4096 + 8192 + 2048 + 1500));
    EXPECT_TRUE(r.categories.size() >= 3);
    EXPECT_TRUE(r.root_tree.size_bytes >= r.total_reclaimable_bytes);
}

RECLAIM_TEST(DuplicateFinderThreeStageAndZeroFalsePositives) {
    TempDir tmp("dupes");
    std::string payload_a(16384, 'X');
    payload_a[8192] = '1';
    write_file(tmp.path() / "a/photo_orig.bin", payload_a);
    write_file(tmp.path() / "b/photo_copy.bin", payload_a);

    std::string payload_b = payload_a;
    payload_b[8192] = '2';
    write_file(tmp.path() / "c/photo_diff_mid.bin", payload_b);

    reclaim::DuplicateOptions opts;
    opts.root_path = tmp.path();
    opts.min_file_size_bytes = 1024;
    opts.keep_policy = reclaim::KeepPolicy::KeepShortestPath;

    reclaim::DuplicateFinder finder;
    auto res = finder.find(opts);
    EXPECT_TRUE(res.ok());

    const auto& r = res.value();
    EXPECT_EQ(r.groups.size(), static_cast<size_t>(1));
    EXPECT_EQ(r.groups[0].files.size(), static_cast<size_t>(2));
    EXPECT_EQ(r.total_reclaimable_bytes, static_cast<uint64_t>(16384));

    size_t kept = 0;
    for (const auto& f : r.groups[0].files) {
        if (f.keep) ++kept;
    }
    EXPECT_EQ(kept, static_cast<size_t>(1));
}

RECLAIM_TEST(SquarifiedTreemapConservesExactArea) {
    reclaim::FileNode root{"root", "/root", 1000, true, {
        {"alpha", "/root/alpha", 500, true, {}},
        {"beta", "/root/beta", 250, true, {}},
        {"gamma", "/root/gamma", 150, false, {}},
        {"delta", "/root/delta", 100, false, {}}
    }};

    const reclaim::RectF bounds{0.0, 0.0, 800.0, 600.0};
    const auto tiles = reclaim::compute_squarified_treemap(root, bounds);
    EXPECT_EQ(tiles.size(), static_cast<size_t>(4));

    double area_sum = 0.0;
    for (const auto& t : tiles) {
        EXPECT_TRUE(t.rect.width > 0.0);
        EXPECT_TRUE(t.rect.height > 0.0);
        area_sum += t.rect.width * t.rect.height;
    }
    EXPECT_TRUE(std::abs(area_sum - (bounds.width * bounds.height)) < 1.0);
}

RECLAIM_TEST(QuarantineVaultDryRunMoveRestoreAndPurge) {
    TempDir tmp("vault");
    const fs::path vault_dir = tmp.path() / "vault";
    const fs::path f1 = tmp.path() / "work/cache_a.tmp";
    const fs::path d1 = tmp.path() / "work/Cache_Data";
    write_file(f1, "hello reclaim");
    write_file(d1 / "entry.bin", "cached bytes");

    reclaim::QuarantineVault vault(vault_dir);

    auto dry = vault.quarantine_items({f1, d1}, "dry test", true);
    EXPECT_TRUE(dry.ok());
    EXPECT_TRUE(dry.value().dry_run);
    EXPECT_EQ(dry.value().items_processed, static_cast<size_t>(2));
    EXPECT_TRUE(fs::exists(f1));

    auto applied = vault.quarantine_items({f1, d1, fs::path("C:/Windows/System32")}, "real test", false);
    EXPECT_TRUE(applied.ok());
    EXPECT_EQ(applied.value().items_processed, static_cast<size_t>(2));
    EXPECT_EQ(applied.value().items_failed, static_cast<size_t>(1));
    EXPECT_FALSE(fs::exists(f1));

    auto restored = vault.restore_batch(applied.value().batch_id);
    EXPECT_TRUE(restored.ok());
    EXPECT_EQ(restored.value().items_restored, static_cast<size_t>(2));
    EXPECT_TRUE(fs::exists(f1));
    EXPECT_TRUE(fs::exists(d1 / "entry.bin"));

    auto purged = vault.purge_batch(applied.value().batch_id);
    EXPECT_TRUE(purged.ok());
    auto after_purge = vault.list_batches();
    EXPECT_TRUE(after_purge.ok());
    EXPECT_EQ(after_purge.value().size(), static_cast<size_t>(0));
}

RECLAIM_TEST(QuarantineGranularSingleFileRestore) {
    TempDir tmp("vault_granular");
    const fs::path vault_dir = tmp.path() / "vault";
    const fs::path f1 = tmp.path() / "work/one.tmp";
    const fs::path f2 = tmp.path() / "work/two.tmp";
    write_file(f1, std::string(1000, '1'));
    write_file(f2, std::string(2500, '2'));

    reclaim::QuarantineVault vault(vault_dir);
    auto clean = vault.quarantine_items({f1, f2}, "granular test", false);
    EXPECT_TRUE(clean.ok());
    EXPECT_EQ(vault.total_quarantined_bytes(), static_cast<uint64_t>(3500));

    // Restore only entry #0 (f1), leaving f2 quarantined
    auto r0 = vault.restore_entry(clean.value().batch_id, 0);
    EXPECT_TRUE(r0.ok());
    EXPECT_TRUE(fs::exists(f1));
    EXPECT_FALSE(fs::exists(f2));
    EXPECT_EQ(vault.total_quarantined_bytes(), static_cast<uint64_t>(2500));

    // Restoring entry #0 again must fail cleanly
    EXPECT_FALSE(vault.restore_entry(clean.value().batch_id, 0).ok());

    // Restore entry #1 (f2) -> batch auto-transitions to fully restored
    auto r1 = vault.restore_entry(clean.value().batch_id, 1);
    EXPECT_TRUE(r1.ok());
    EXPECT_TRUE(fs::exists(f2));
    EXPECT_EQ(vault.total_quarantined_bytes(), static_cast<uint64_t>(0));

    auto batches = vault.list_batches();
    EXPECT_TRUE(batches.ok());
    EXPECT_TRUE(batches.value()[0].restored);
}

RECLAIM_TEST(QuarantineRetentionAutoExpiry) {
    TempDir tmp("vault_expiry");
    const fs::path vault_dir = tmp.path() / "vault";
    const fs::path f1 = tmp.path() / "work/old.tmp";
    write_file(f1, std::string(4096, 'Q'));

    reclaim::QuarantineVault vault(vault_dir);
    auto clean = vault.quarantine_items({f1}, "expiry test", false);
    EXPECT_TRUE(clean.ok());

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    // Not expired at +5 days for 30-day retention
    auto not_yet = vault.purge_expired_batches(30, false, now + 5 * 86400);
    EXPECT_TRUE(not_yet.ok());
    EXPECT_EQ(not_yet.value().batches_purged, static_cast<size_t>(0));

    // Dry run at +31 days previews purge without deleting
    auto dry = vault.purge_expired_batches(30, true, now + 31 * 86400);
    EXPECT_TRUE(dry.ok());
    EXPECT_EQ(dry.value().batches_purged, static_cast<size_t>(1));
    EXPECT_EQ(dry.value().bytes_freed, static_cast<uint64_t>(4096));
    EXPECT_EQ(vault.list_batches().value().size(), static_cast<size_t>(1));

    // Real purge at +31 days removes batch permanently
    auto applied = vault.purge_expired_batches(30, false, now + 31 * 86400);
    EXPECT_TRUE(applied.ok());
    EXPECT_EQ(applied.value().batches_purged, static_cast<size_t>(1));
    EXPECT_EQ(vault.list_batches().value().size(), static_cast<size_t>(0));
}

RECLAIM_TEST(JsonParserValidAndMalformedInputs) {
    auto ok = reclaim::parse_json(R"({"name":"Reclaim","ver":1.1,"ok":true,"arr":[1,2,"a\u0042"]})");
    EXPECT_TRUE(ok.ok());
    EXPECT_EQ(ok.value().find("name")->as_string(), std::string("Reclaim"));
    EXPECT_EQ(ok.value().find("ok")->as_bool(), true);
    EXPECT_EQ(ok.value().find("arr")->as_array()[2].as_string(), std::string("aB"));

    EXPECT_FALSE(reclaim::parse_json("{unquoted: 1}").ok());
    EXPECT_FALSE(reclaim::parse_json("[1, 2,]").ok());
    EXPECT_FALSE(reclaim::parse_json("\"unterminated").ok());
}

RECLAIM_TEST(ExternalRulesJsonRoundTripAndAiCacheMatch) {
    auto loaded_assets = reclaim::load_rules_from_json_file("assets/rules.json");
    if (!loaded_assets.ok()) {
        loaded_assets = reclaim::load_rules_from_json_file("../assets/rules.json");
    }
    EXPECT_TRUE(loaded_assets.ok());
    EXPECT_TRUE(loaded_assets.value().size() >= 8);

    const std::string exported = reclaim::export_rules_json(loaded_assets.value());
    auto reparsed = reclaim::parse_rules_json(exported);
    EXPECT_TRUE(reparsed.ok());
    EXPECT_EQ(reparsed.value().size(), loaded_assets.value().size());

    // Verify AI model cache rule matches Hugging Face Hub cache
    auto matched = reclaim::match_cleanup_rule(
        "C:/Users/Dev/.cache/huggingface/hub", true, 0, reparsed.value());
    EXPECT_TRUE(matched.has_value());
    EXPECT_EQ(matched->id, std::string("ai_model_caches"));

    // Invalid category must be rejected cleanly
    EXPECT_FALSE(reclaim::parse_rules_json(R"({"rules":[{"id":"x","name":"X","category":"FakeCat","safety":"Safe"}]})").ok());
}

RECLAIM_TEST(NtfsMftTreeReconstructionAndCycleProtection) {
    std::vector<reclaim::MftRecord> records = {
        {5, 5, "C:", 0, true},
        {10, 5, "Users", 0, true},
        {20, 10, "Alice", 0, true},
        {30, 20, "video.mp4", 9000, false},
        {31, 20, "notes.txt", 1000, false},
        {40, 5, "Games", 0, true},
        {41, 40, "shader.bin", 4000, false},
        // Intentional cycle (90 <-> 91) to verify cycle protection never infinite-loops
        {90, 91, "loop_a", 0, true},
        {91, 90, "loop_b", 0, true}
    };

    const auto tree = reclaim::reconstruct_mft_tree(records, 5, "C:\\");
    EXPECT_EQ(tree.size_bytes, static_cast<uint64_t>(14000));
    EXPECT_EQ(tree.children.size(), static_cast<size_t>(2));
    // Largest child ("Users" = 10000) must sort before "Games" (4000)
    EXPECT_EQ(tree.children[0].name, std::string("Users"));
    EXPECT_EQ(tree.children[0].size_bytes, static_cast<uint64_t>(10000));
    EXPECT_EQ(tree.children[1].name, std::string("Games"));
    EXPECT_EQ(tree.children[1].size_bytes, static_cast<uint64_t>(4000));
}

RECLAIM_TEST(ConfigPersistenceAndReporterEscaping) {
    TempDir tmp("cfg");
    reclaim::ConfigStore store(tmp.path() / "reclaim.cfg");

    reclaim::AppConfig cfg;
    cfg.dark_theme = false;
    cfg.dry_run_default = true;
    cfg.quarantine_retention_days = 14;
    cfg.duplicate_min_bytes = 8192;
    cfg.last_scan_path = "/tmp/sample";
    cfg.exclusions = {".git", "Important"};

    EXPECT_TRUE(store.save(cfg).ok());
    auto loaded = store.load();
    EXPECT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value().dark_theme, false);
    EXPECT_EQ(loaded.value().dry_run_default, true);
    EXPECT_EQ(loaded.value().quarantine_retention_days, 14);
    EXPECT_EQ(loaded.value().exclusions.size(), static_cast<size_t>(2));

    EXPECT_EQ(reclaim::html_escape("<script>\"a&b\"</script>"),
              std::string("&lt;script&gt;&quot;a&amp;b&quot;&lt;/script&gt;"));
    EXPECT_EQ(reclaim::format_bytes(1536), std::string("1.50 KB"));
}

RECLAIM_TEST(PerformanceBenchmark500Files) {
    TempDir tmp("perf");
    const std::string body(2048, 'Z');
    for (int i = 0; i < 500; ++i) {
        write_file(tmp.path() / ("dir_" + std::to_string(i % 10)) / ("file_" + std::to_string(i) + ".tmp"), body);
    }

    reclaim::ScanOptions opts;
    opts.root_path = tmp.path();
    reclaim::StorageScanner scanner;
    auto res = scanner.scan(opts);
    EXPECT_TRUE(res.ok());
    EXPECT_EQ(res.value().items.size(), static_cast<size_t>(500));
    EXPECT_TRUE(res.value().duration_ms < 2000.0);
    std::cout << "    (Benchmark: 500 files scanned in "
              << reclaim::format_duration_ms(res.value().duration_ms) << ")\n";
}

int main() {
    return reclaim::test::run_all_tests();
}
