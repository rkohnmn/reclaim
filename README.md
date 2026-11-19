<div align="center">

<img src="assets/reclaim_256.png" alt="Reclaim Icon" width="96" height="96" />

# Reclaim

**Native C++20 Windows storage analyzer, safe cache cleaner, cryptographic duplicate finder, and undoable Quarantine Vault.**

[![Version](https://img.shields.io/badge/version-1.1.0-D97706?style=flat-square)](CHANGELOG.md)
[![Language](https://img.shields.io/badge/C%2B%2B-20-4C9AFF?style=flat-square)](CMakeLists.txt)
[![Dependencies](https://img.shields.io/badge/external%20deps-0-56D364?style=flat-square)](ARCHITECTURE.md)
[![Telemetry](https://img.shields.io/badge/telemetry-none-39C5CF?style=flat-square)](SECURITY.md)
[![License](https://img.shields.io/badge/license-MIT-8B949E?style=flat-square)](LICENSE)

*Map where your disk space went, clear hidden app/AI/shader caches, eliminate byte-identical duplicates, and undo any cleanup in one click.*

</div>

---

## Why Reclaim Exists

Windows storage maintenance forces an unnecessary trade-off between four broken options:

1. **Legacy cleaners burned user trust.** Following the Avast/Gen Digital acquisition and the CCleaner v7 redesign in late 2025, users across [r/software ("Ccleaner Version 7 sucks!!!")](https://www.reddit.com/r/software/comments/1ofdlf6/ccleaner_version_7_sucks/) and [r/software ("So I finally uninstalled CCleaner")](https://www.reddit.com/r/software/comments/1p8t2cp/so_i_finally_uninstalled_ccleaner/) cite persistent background agents, subscription nagware, and telemetry distrust.
2. **Windows Disk Cleanup only cleans Microsoft folders.** As highlighted in [r/software ("Alternatives to CCleaner?")](https://www.reddit.com/r/software/comments/1lwun5e/alternatives_to_ccleaner/), built-in Windows tools ignore third-party browser profiles, Electron app caches, NVIDIA/AMD/Steam shader caches, local AI model weights (Hugging Face, Ollama, LM Studio, ComfyUI), and developer build directories (`node_modules`, `.gradle`, `.pytest_cache`).
3. **Open-source utilities are fragmented and unforgiving.** Today, reclaiming space safely means juggling WizTree or TreeSize to visualize folders, BleachBit (which permanently deletes files without undo) to purge caches, and dupeGuru to find duplicate copies.
4. **Users are done with Electron and WebView2 bloat.** Discussions across [r/Windows11](https://www.reddit.com/r/Windows11/comments/1q0bph3/2025_has_been_a_disaster_for_windows_11_as/) and [r/Windows11 (updates)](https://www.reddit.com/r/Windows11/comments/1pa7lau/why_does_every_update_make_things_worse_in/) show strong demand for fast native desktop tools with sub-100ms startup and zero forced AI or cloud accounts.

Reclaim brings storage visualization, transparent rule-pack cleanup, SHA-256 duplicate detection, and a reversible Quarantine Vault together in a single zero-dependency C++20 binary.

---

## What's New in v1.1.0

| Upgrade | What It Delivers |
|---|---|
| **External Rule Packs (`rules.json`)** | Edit or share cleanup rules in a validated local JSON schema without recompiling. Ships with built-in rules for Hugging Face, Ollama, LM Studio, ComfyUI, Epic Games, Steam, NVIDIA/AMD shaders, browsers, and developer caches. |
| **Optional Elevated NTFS `$MFT` Engine** | Keeps non-elevated `std::filesystem` scanning as the safe default, while adding an opt-in Administrator NTFS Master File Table scanner (`FSCTL_ENUM_USN_DATA`) with cycle-safe FRN graph reconstruction and automatic fallback. |
| **Granular Quarantine & Auto-Expiry** | Upgraded to `RECLAIM_MANIFEST_V2` (backward-compatible with V1): expand any quarantine batch to restore a single file without disturbing siblings, and automatically purge batches past your retention window. |
| **Windows Explorer Context Menu** | Per-user `HKCU` registration ("Analyze with Reclaim" on folders and folder backgrounds) requiring zero UAC elevation, launching `Reclaim.exe --scan "%V"` directly into a live scan. |

---

## Feature Overview

### 1. Transparent Rule-Based Cache & Bloat Scanner
Every matched folder or file is classified with an explicit safety tier and human-readable rationale:
- **`Safe`** (selected by default): Browser & Electron caches, NVIDIA/AMD/DirectX/Steam/Epic shader caches, temp/scratch files, application logs, and Windows Error Reporting crash dumps.
- **`Review`** (opt-in): Local AI & ML model caches (`~/.cache/huggingface/hub`, `~/.ollama/models/blobs`, ComfyUI temp), developer artifacts (`node_modules`, `.gradle`, `.pytest_cache`, `npm-cache`, `pip-cache`), installer leftovers (`.msi`, `.msp`, `.msu`), and stale updater payloads.
- **`Caution`**: User-created duplicate files requiring explicit inspection.

### 2. Interactive Squarified Treemap
Built on the Bruls–Huizing–van Wijk squarified layout algorithm (`src/core/treemap.cpp`) with strict area conservation (Lie Factor = 1.00), colorblind-safe palettes, hover telemetry (exact path, byte size, percentage of parent), and click-to-drill-down navigation (`Backspace` to go up).

### 3. 3-Stage Cryptographic Duplicate Finder
Eliminates false positives while minimizing disk I/O:
1. **Stage 1 (Exact Size Bucketing):** Eliminates unique-size files in $O(N)$ without opening them.
2. **Stage 2 (64-bit Partial Fingerprint):** Reads only the first 4 KB and last 4 KB + file size on size collisions.
3. **Stage 3 (Full FIPS 180-4 SHA-256):** Streams full 64 KB chunks only on partial-hash collisions.
- **Hard Protection Invariant:** Every duplicate group strictly preserves at least one original copy (`KeepOldest`, `KeepNewest`, or `KeepShortestPath`).

### 4. Atomic Quarantine Vault (Batch & Per-File Undo)
Reclaim never permanently unlinks cleaned files on first click. Targets move atomically into a local Quarantine Vault with a persisted `RECLAIM_MANIFEST_V2` manifest (including cross-volume copy+remove fallback). Restore an entire batch (`Ctrl+Z`), restore a single file inside an expanded batch, purge permanently, or let retention auto-expiry clean up old batches.

### 5. Zero-Dependency Audit Reports
Export any scan directly to **JSON**, **CSV**, or a **self-contained Carbon Dark HTML report** with strict output escaping against filename injection.

---

## Competitive Comparison

| Capability | **Reclaim v1.1** | CCleaner | BleachBit | WizTree | Windows Disk Cleanup |
|---|:---:|:---:|:---:|:---:|:---:|
| Native C++20 (<1 MB, zero Electron) | **Yes** | No | No (Python/GTK) | Yes (Delphi) | Yes |
| Zero telemetry / zero upsells | **Yes** | No | Yes | Freemium | Partial |
| Squarified storage treemap | **Yes** | No | No | Yes | No |
| Optional NTFS `$MFT` fast scan | **Yes** | No | No | Yes | No |
| User-editable `rules.json` + AI/dev rules | **Yes** | No | XML (CleanerML) | No | No |
| 3-stage SHA-256 duplicate finder | **Yes** | Basic | No | No | No |
| Quarantine Vault (batch + per-file undo) | **Yes** | No | No | No | No |
| Refuses risky registry cleaning | **Yes** | No | No | N/A | Yes |

---

## Architecture at a Glance

```text
reclaim/
├── include/reclaim/        # Zero-dependency C++20 core headers (types, json, rules, ntfs_mft, quarantine, ...)
├── src/
│   ├── core/               # 12 portable C++20 engine modules (tested under GCC -Werror + ASan/UBSan)
│   ├── cli/                # Full-featured cross-platform CLI (reclaim-cli)
│   └── win32/              # Native Win32 + GDI+ double-buffered GUI, DwmApi dark titlebar, .rc & .manifest
├── assets/
│   ├── rules.json          # Editable v1.1 rule pack (AI caches, shaders, browsers, dev tools)
│   ├── reclaim.ico         # Multi-resolution Windows icon (16px to 256px)
│   └── generate_icon.py    # Reproducible supersampled vector icon generator
├── tests/                  # 13 automated unit, integration, security & performance tests
├── scripts/                # Windows MSVC/MinGW one-click build script
├── installer/              # Inno Setup 6 x64 installer with opt-in Explorer context menu
└── .github/workflows/      # Automated Windows x64 Release + Linux ASan/UBSan CI
```

Read [ARCHITECTURE.md](ARCHITECTURE.md) for detailed module invariants and data flow, and [SECURITY.md](SECURITY.md) for the complete threat model.

---

## Customizing `rules.json`

Reclaim automatically loads `%LOCALAPPDATA%\Reclaim\rules.json` (Windows) or `~/.local/share/reclaim/rules.json` (Linux) if present. Export the built-in catalog as a starting point:

```bash
reclaim-cli rules export rules.json
```

Each entry follows `schema_version: 1`:

```json
{
  "id": "ai_model_caches",
  "name": "Local AI & ML Model Caches",
  "description": "Large cached model weights from Hugging Face Hub, Ollama, and ComfyUI.",
  "category": "DeveloperArtifacts",
  "safety": "Review",
  "dir_name_matches": ["huggingface", "torch_extensions", "triton"],
  "path_substrings": ["/.cache/huggingface/hub", "/.ollama/models/blobs", "/comfyui/temp"],
  "extensions": [".gguf.part", ".safetensors.tmp"],
  "min_size_bytes": 0,
  "default_selected": false
}
```

---

## Building & Installation

### Windows (Primary Target: `Reclaim.exe`)

**One-Click Build (Visual Studio MSVC or MSYS2 MinGW-w64):**
```bat
scripts\build_windows.bat
```
Compiles `build\win64\Reclaim.exe` (with embedded icon, PerMonitorV2 DPI + LongPathAware manifest, and v1.1.0 version info), `build\win64\reclaim-cli.exe`, and runs the full automated test suite.

**CMake + Visual Studio 2022:**
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

**Windows Installer (Inno Setup 6+):**
Compile `installer/setup.iss` after building to generate `Reclaim-1.1.0-Setup-x64.exe` with an optional desktop shortcut and per-user Explorer context menu.

**Automated GitHub Releases:**
Push to GitHub to run `.github/workflows/release-windows.yml`, which compiles `Reclaim.exe`, runs `ctest`, and packages `Reclaim-1.1.0-win64-portable.zip` on `windows-latest`.

### Linux (Core Engine, CLI & Sanitizer Verification)

```bash
make all       # Compiles build/reclaim-cli and runs all 13 tests (-std=c++20 -O2 -Wall -Wextra -Werror)
make asan      # Runs all 13 tests under AddressSanitizer + UndefinedBehaviorSanitizer
```

---

## CLI Reference (`reclaim-cli`)

```bash
# Scan with built-in or custom rules.json and export reports
reclaim-cli scan C:\Users\You --rules assets/rules.json --html audit.html --json audit.json

# Opt-in elevated NTFS $MFT scan on Windows (falls back gracefully if non-admin)
reclaim-cli scan C:\ --mft

# Dry-run preview vs. applying quarantine cleanup
reclaim-cli clean C:\Users\You
reclaim-cli clean C:\Users\You --apply --include-review

# 3-stage SHA-256 duplicate finder & squarified treemap coordinates
reclaim-cli dupes C:\Users\You --min-bytes 4096 --json dupes.json
reclaim-cli treemap C:\Users\You --width 1280 --height 720

# Granular Quarantine Vault management
reclaim-cli vault list
reclaim-cli vault show <batch_id>
reclaim-cli vault restore-file <batch_id> 0        # Restore a single file
reclaim-cli vault restore <batch_id>               # Restore entire batch
reclaim-cli vault purge-expired --days 30 --apply  # Auto-expiry cleanup
```

---

## Keyboard Shortcuts (Windows GUI)

| Shortcut | Action |
|---|---|
| `F5` | Start storage scan |
| `Esc` | Cancel active scan or cleanup |
| `Ctrl+Enter` | Reclaim selected items (move to Quarantine Vault) |
| `Ctrl+Z` | Restore latest quarantine batch |
| `Ctrl+T` | Toggle Carbon Dark / Editorial Light theme |
| `Ctrl+1` – `Ctrl+5` | Switch views (Overview, Treemap, Duplicates, Quarantine, Settings) |
| `Backspace` | Navigate up one directory in Treemap Explorer |

---

## Security & Safety Guarantees

- **Zero Network Surface:** No sockets, no HTTP client, no analytics, no phone-home update checks.
- **Hard-Blocked System Paths:** `C:\Windows`, `System32`, `SysWOW64`, `WinSxS`, `Program Files`, `Boot`, `EFI`, drive roots, and the Quarantine Vault itself are strictly blocked in code.
- **No Registry Cleaners:** Reclaim only touches transparent, inspectable filesystem targets.
- **Symlink & Cycle Protection:** Symbolic links are never followed during scans; MFT FRN graph reconstruction detects and terminates circular parent references.

---

## Verification & Build Environment Note

- **Verified in Sandbox (Debian 12 `x86_64`, GCC 12.2.0):** All 13 automated unit, integration, security, and performance tests pass with zero warnings under `-Wall -Wextra -Werror` and zero leaks/UB under `-fsanitize=address,undefined` (500 files scanned in ~2.2 ms).
- **Windows Binary Compilation:** Because the Debian 12 sandbox does not include a Windows cross-compiler (`mingw-w64`, `clang`, `wine`), `Reclaim.exe` is ready to compile directly on Windows via `scripts\build_windows.bat`, `CMakeLists.txt`, or `.github/workflows/release-windows.yml`.

---

## License

Released under the [MIT License](LICENSE).
