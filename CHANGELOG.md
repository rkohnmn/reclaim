# Changelog

All notable changes to Reclaim are documented in this file.

## [1.1.0] - 2026-11-18

### Added
- **External Editable Rule Packs (`rules.json`):**
  - Implemented zero-dependency RFC 8259 JSON parser (`src/core/json.cpp`) with line/column error reporting and nesting depth guard.
  - Added `load_rules_from_json_file()`, `parse_rules_json()`, and `export_rules_json()` in `src/core/rules.cpp`.
  - Shipped default `assets/rules.json` adding Local AI & ML model caches (Hugging Face Hub, Ollama, LM Studio, ComfyUI) and Epic Games Launcher web caches.
  - Added `reclaim-cli scan --rules <file>` and `reclaim-cli rules export <out.json>`, plus auto-loading and export in the Win32 GUI.
- **Optional Elevated NTFS `$MFT` Fast-Scan Engine (`src/core/ntfs_mft.cpp`):**
  - Added `FSCTL_QUERY_USN_JOURNAL` + `FSCTL_ENUM_USN_DATA` MFT reader on elevated Windows NTFS volumes with automatic fallback to non-elevated `std::filesystem` scanning.
  - Implemented cross-platform testable, cycle-safe `reconstruct_mft_tree()` FRN graph builder.
  - Added `--mft` CLI flag and Settings toggle in Win32 GUI.
- **Granular Quarantine Vault (`RECLAIM_MANIFEST_V2`):**
  - Added single-entry `restore_entry(batch_id, entry_index)` with per-entry restoration tracking (backward-compatible with V1 manifests).
  - Added `purge_expired_batches(retention_days, dry_run)` for automatic retention-based cleanup.
  - Added expandable batch rows with per-file `Restore File` buttons and `Purge Expired` controls in the Win32 GUI, plus `vault show`, `vault restore-file`, and `vault purge-expired` CLI commands.
- **Windows Explorer Context-Menu Integration (`src/core/shell_integration.cpp`):**
  - Added per-user `HKCU` registration ("Analyze with Reclaim" on folders and folder backgrounds, zero UAC required).
  - Added `--scan "<path>"` CLI argument parsing in `win_main.cpp` to auto-launch scans from Explorer.
  - Added opt-in context menu task to `installer/setup.iss` and Settings toggle in Win32 GUI.
- **Expanded Verification:** Added 5 new automated tests (13 total) covering JSON parsing, `rules.json` round-trip & AI cache matching, MFT FRN graph reconstruction & cycle protection, per-file quarantine restore, and retention auto-expiry.

## [1.0.0] - 2026-11-18

### Added
- Initial release with C++20 Core Engine (storage scanner, FIPS 180-4 SHA-256 duplicate finder, squarified treemap, atomic Quarantine Vault, config store, JSON/CSV/HTML reporter), cross-platform `reclaim-cli`, native Win32/GDI+ GUI, Inno Setup installer, and GitHub Actions CI.
