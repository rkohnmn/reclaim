# Reclaim Architecture (v1.1.0)

## 1. Design Goals

1. **Zero External Dependencies:** Only the C++20 Standard Library (`src/core/`, `src/cli/`) and built-in Windows SDK libraries (`gdiplus`, `dwmapi`, `comctl32`, `shell32`, `comdlg32`, `advapi32` in `src/win32/`).
2. **Strict Layer Separation:** Business logic (`src/core/`) has zero dependency on Win32 UI headers, enabling 100% native unit, integration, and AddressSanitizer/UBSan verification on Linux and Windows alike.
3. **Explicit Error Handling & RAII:** Every fallible operation returns `reclaim::Result<T>` with a typed `ErrorCode` and actionable message. All OS handles (`HANDLE`, `HKEY`, GDI+ tokens, worker threads) use RAII ownership.
4. **Non-Blocking UI:** All filesystem walks, NTFS `$MFT` reads, SHA-256 hashing, and quarantine moves execute on background `std::thread` workers communicating via `WM_APP_*` messages and a lock-free `std::atomic<bool>` cancellation flag.

---

## 2. Module Map

| Module | Responsibility | Key Invariants |
|---|---|---|
| `core/sha256` | Self-contained FIPS 180-4 SHA-256 + 64-bit FNV-1a partial fingerprinter | Fixed 64 KB streaming buffer; constant memory regardless of file size |
| `core/format` | Formatting, escaping (JSON/CSV/HTML), path safety | `is_protected_system_path()` normalizes slashes cross-platform and hard-blocks OS roots |
| `core/json` | Zero-dependency RFC 8259 JSON parser | Line/column error reporting; max nesting depth (64) prevents stack exhaustion |
| `core/rules` | Built-in + external `rules.json` loader/exporter | Validates schema, category, and safety tier; never includes registry keys |
| `core/ntfs_mft` | Optional elevated Windows NTFS `$MFT` scanner + FRN tree builder | Read-only volume handle; cycle-safe `visited` set; automatic fallback to filesystem scan |
| `core/shell_integration` | Per-user `HKCU` Windows Explorer context-menu integration | Requires zero UAC admin elevation; clean uninstall via `RegDeleteTreeW` |
| `core/scanner` | Recursive storage scanner, `$MFT` dispatch, category aggregator | Skips symlinks by default; respects `cancel_flag` & user exclusions |
| `core/duplicates` | 3-stage duplicate detector (size -> 4 KB head/tail hash -> SHA-256) | Every returned `DuplicateGroup` strictly marks at least one file `keep = true` |
| `core/treemap` | Bruls–Huizing–van Wijk squarified treemap layout | Total tile area matches container area (Lie Factor = 1.00) |
| `core/quarantine` | Atomic Quarantine Vault (`RECLAIM_MANIFEST_V2`, V1-compatible) | Batch + single-entry restore, retention auto-expiry, cross-volume copy+remove fallback |
| `core/config` | Persistent local preferences (`reclaim.cfg`) | Atomic write-then-rename; safe defaults on missing/malformed entries |
| `core/reporter` | JSON, CSV, and self-contained HTML audit exporter | Strict HTML/JSON/CSV escaping prevents injection |
| `win32/*` | Native Win32/GDI+ double-buffered workbench UI | Explicit `AppState` state machine; PerMonitorV2 DPI; `--scan` launch arg support |

---

## 3. Data Flow

```text
[Win32 GUI / Explorer Context Menu (--scan) / CLI]
        │
        ├──► [rules.json (External Rule Pack) or Built-In Catalog]
        ▼
[StorageScanner] ──► (Optional Elevated NTFS $MFT OR Default std::filesystem)
        │
        ├──► [FileNode Tree] ──► [compute_squarified_treemap]
        ├──► [ScannedItem List + CategorySummary]
        └──► [DuplicateFinder (3-Stage SHA-256)]
        │
        ▼
[QuarantineVault (RECLAIM_MANIFEST_V2)]
        ├──► restore_batch()       (Full batch undo)
        ├──► restore_entry(idx)    (Single-file granular undo)
        ├──► purge_batch()         (Explicit permanent delete)
        └──► purge_expired_batches (Retention auto-expiry)
```
