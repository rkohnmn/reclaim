# Reclaim — Project Progress & Decision Log

**Product:** Reclaim (v1.1.0)  
**Tagline:** Fast, zero-telemetry native Windows desktop app that maps where your disk space went, safely clears hidden app/AI/shader caches, eliminates duplicate files, and protects every cleanup with granular quarantine undo.  
**Current Stage:** v1.1.0 Complete & Verified (Release Package Ready)  
**Last Updated:** 2026-11-18  

---

## 1. Original Market Research & Product Selection

- **Verified 2025–2026 Market Signals:** CCleaner v7 distrust ([r/software](https://www.reddit.com/r/software/comments/1ofdlf6/ccleaner_version_7_sucks/), [r/software](https://www.reddit.com/r/software/comments/1p8t2cp/so_i_finally_uninstalled_ccleaner/)), Windows Disk Cleanup ignoring third-party/dev/AI caches ([r/software](https://www.reddit.com/r/software/comments/1lwun5e/alternatives_to_ccleaner/)), and Windows 11 Electron/WebView2 backlash ([r/Windows11](https://www.reddit.com/r/Windows11/comments/1q0bph3/2025_has_been_a_disaster_for_windows_11_as/)).
- **10-Opportunity Matrix Winner:** **Reclaim** (weighted score **92.5 / 100** across demand, severity, market size, competitive weakness, marketing ease, differentiation, viral potential, feasibility, dev-vs-value, and retention).

---

## 2. Completed v1.0.0 & v1.1.0 Milestones

### Core Engine (`src/core/`, 12 Modules, Zero External Dependencies)
- [x] `sha256.cpp`: Self-contained FIPS 180-4 SHA-256 + 64-bit partial file fingerprinter.
- [x] `format.cpp`: Formatting, JSON/CSV/HTML escaping, cross-platform protected OS path guardrails.
- [x] `json.cpp` *(v1.1)*: Zero-dependency RFC 8259 JSON parser with line/column errors and nesting limit.
- [x] `rules.cpp` *(v1.1)*: External `rules.json` loader/validator/exporter + built-in AI model cache (Hugging Face, Ollama, LM Studio, ComfyUI), game shader, browser, and dev artifact rules.
- [x] `ntfs_mft.cpp` *(v1.1)*: Optional elevated Windows NTFS `$MFT` scanner (`FSCTL_ENUM_USN_DATA`) + cross-platform cycle-safe FRN graph reconstructor with automatic filesystem fallback.
- [x] `shell_integration.cpp` *(v1.1)*: Per-user `HKCU` Windows Explorer context-menu registration ("Analyze with Reclaim", zero UAC required).
- [x] `scanner.cpp` *(v1.1)*: Recursive storage scanner with opt-in `$MFT` dispatch, symlink guard, and exclusions.
- [x] `duplicates.cpp`: 3-stage duplicate finder (size -> 4 KB head/tail hash -> full SHA-256) with keep-original invariant.
- [x] `treemap.cpp`: Bruls–Huizing–van Wijk squarified treemap layout (Lie Factor = 1.00).
- [x] `quarantine.cpp` *(v1.1)*: Atomic Quarantine Vault (`RECLAIM_MANIFEST_V2`, V1-compatible) with batch restore, granular single-entry `restore_entry()`, and `purge_expired_batches()` retention auto-expiry.
- [x] `config.cpp` & `reporter.cpp`: Atomic preferences persistence and JSON/CSV/HTML audit exporters.

### Native Windows Desktop GUI (`src/win32/`) & CLI (`src/cli/`)
- [x] Double-buffered Win32 + GDI+ workbench with Carbon Dark / Editorial Light themes, DwmApi dark titlebar, PerMonitorV2 DPI + LongPathAware manifest, v1.1.0 `VERSIONINFO`, and multi-resolution `reclaim.ico`.
- [x] v1.1 GUI wiring: `--scan "<path>"` Explorer context-menu launch support, expandable quarantine batches with per-file `Restore File` buttons, `Purge Expired` button, NTFS `$MFT` toggle, `Export rules.json`, and Explorer context-menu toggle.
- [x] Full `reclaim-cli` v1.1.0 supporting `scan --rules --mft`, `clean`, `dupes`, `treemap`, `rules export`, and `vault list/show/restore/restore-file/purge/purge-expired`.

### Testing & Verification
- [x] 13/13 automated tests pass under `g++ -std=c++20 -O2 -Wall -Wextra -Werror`.
- [x] 13/13 automated tests pass under AddressSanitizer + UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`).
- [x] End-to-end CLI smoke test covering `rules.json`, `scan`, `dupes`, `treemap`, `clean`, `vault show`, `vault restore-file`, and `vault purge-expired`.

---

## 3. Documented Build Environment Limitation
- The Debian 12 `x86_64` sandbox has no Windows cross-compiler (`mingw-w64`, `clang`, `wine`) and no outbound internet/package installation. All 12 core modules, CLI, and 13 tests (including ASan/UBSan) are compiled and verified natively on Linux; `Reclaim.exe` builds on Windows via `scripts\build_windows.bat`, `CMakeLists.txt`, or `.github/workflows/release-windows.yml`.
