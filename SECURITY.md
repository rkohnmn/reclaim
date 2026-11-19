# Security Policy & Safety Model

## Security Design Principles

Reclaim operates on user filesystems, making safety and predictability first-class security requirements.

### 1. Zero Network Surface
Reclaim contains **no** networking code, no HTTP client, no update checker phone-home, and no telemetry SDK. It cannot exfiltrate filenames, hashes, or usage data.

### 2. Hard-Blocked System Paths (`is_protected_system_path`)
Both `StorageScanner` and `QuarantineVault` enforce a non-overridable blocklist before any cleanup or move:
- Drive roots (`C:\`, `/`)
- Critical Windows directories (`C:\Windows`, `C:\Windows\System32`, `C:\Windows\SysWOW64`, `C:\Windows\WinSxS`, `C:\Program Files`, `C:\Program Files (x86)`, `C:\Users`, `C:\ProgramData`, `C:\Recovery`, `C:\Boot`, `C:\EFI`)
- Critical POSIX directories (`/bin`, `/sbin`, `/usr`, `/etc`, `/lib`, `/lib64`, `/boot`, `/dev`, `/proc`, `/sys`, `/home`)
- The Quarantine Vault directory itself (`is_subpath_of(target, vault_root)`).

### 3. Symlink & Reparse-Point Safety
`StorageScanner` and `DuplicateFinder` set `follow_symlinks = false` by default and disable recursion on symbolic links, preventing infinite directory loops and symlink escape attacks.

### 4. Quarantine-First Reversible Operations
Cleanup operations move targets into a local Quarantine Vault with an atomic manifest (`write_text_file_atomic` via `.tmp` + `rename`) rather than immediately unlinking files. Permanent deletion happens only when the user explicitly purges a batch.

### 5. Duplicate Original Preservation Invariant
`DuplicateFinder` guarantees that every returned `DuplicateGroup` marks exactly one file `keep = true`. Bulk duplicate cleanup only acts on files with `keep == false`.

### 6. Report Output Escaping
All exported HTML, JSON, and CSV reports escape special characters (`html_escape`, `json_escape`, `csv_escape`) so maliciously named files on disk cannot inject scripts into exported HTML reports or corrupt CSV/JSON output.

## Reporting a Vulnerability
If you discover a path-safety bypass, manifest parsing bug, or memory safety issue, please open a private security advisory on GitHub with reproduction steps and expected vs. actual behavior.
