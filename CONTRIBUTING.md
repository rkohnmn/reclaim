# Contributing to Reclaim

Thank you for helping keep Windows utilities fast, transparent, and trustworthy.

## Development Setup

- **Windows:** Visual Studio 2022 (Desktop development with C++) or MSYS2 MinGW-w64 + CMake 3.20+.
- **Linux/macOS (Core & CLI):** GCC 11+ or Clang 14+ with C++20 support and GNU Make.

## Coding Standards

- Target **C++20** with zero third-party dependencies in `src/core/`.
- Compile cleanly with `-Wall -Wextra -Werror` (GCC/Clang) or `/W4 /WX` (MSVC).
- Prefer RAII and standard containers; never use raw `new`/`delete`.
- Return `reclaim::Result<T>` for all fallible operations — never silently swallow errors.
- Any new cleanup rule in `src/core/rules.cpp` must include a clear description, conservative `SafetyLevel`, and a corresponding unit test in `tests/test_suite.cpp`.

## Verification Before Opening a Pull Request

```bash
make all asan
```

All unit, integration, security, and sanitizer checks must pass with zero warnings.
