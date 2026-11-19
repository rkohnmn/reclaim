#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "reclaim/types.hpp"

namespace reclaim {

struct CleanResult {
    std::string batch_id;
    size_t items_processed{0};
    size_t items_failed{0};
    uint64_t bytes_reclaimed{0};
    bool dry_run{false};
    std::vector<std::string> warnings;
};

struct RestoreResult {
    std::string batch_id;
    size_t items_restored{0};
    size_t items_failed{0};
    uint64_t bytes_restored{0};
    std::vector<std::string> warnings;
};

struct PurgeExpiredResult {
    size_t batches_purged{0};
    uint64_t bytes_freed{0};
    bool dry_run{false};
    std::vector<std::string> purged_batch_ids;
};

// Atomic Quarantine Vault (v1.1: per-file restore + retention auto-expiry):
class QuarantineVault {
public:
    explicit QuarantineVault(std::filesystem::path vault_root);

    const std::filesystem::path& vault_root() const noexcept { return vault_root_; }

    Result<CleanResult> quarantine_items(const std::vector<std::filesystem::path>& paths,
                                         const std::string& reason,
                                         bool dry_run);

    Result<std::vector<QuarantineBatch>> list_batches() const;

    // Restores all un-restored entries of a batch to their original filesystem locations.
    Result<RestoreResult> restore_batch(const std::string& batch_id);

    // Granular restore: restores a single entry by index inside a batch without disturbing siblings.
    Result<RestoreResult> restore_entry(const std::string& batch_id, size_t entry_index);

    // Permanently deletes a batch from the quarantine vault.
    Result<uint64_t> purge_batch(const std::string& batch_id);

    // Auto-expiry: purges batches older than retention_days (now_epoch == 0 uses current UTC clock).
    Result<PurgeExpiredResult> purge_expired_batches(int retention_days,
                                                     bool dry_run = false,
                                                     int64_t now_epoch = 0);

    uint64_t total_quarantined_bytes() const;

private:
    std::filesystem::path vault_root_;
    std::filesystem::path manifests_dir() const;
    std::filesystem::path payload_dir(const std::string& batch_id) const;
    Result<void> save_manifest(const QuarantineBatch& batch) const;
    Result<QuarantineBatch> load_manifest(const std::filesystem::path& file) const;
};

}  // namespace reclaim
