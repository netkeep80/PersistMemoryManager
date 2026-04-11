# Contributing to PersistMemoryManager

Thank you for contributing! This guide covers the development workflow and quality standards.

## Development Setup

### Prerequisites

- CMake 3.16+
- C++20 compiler (GCC 10+, Clang 10+, or MSVC 2019 16.3+)
- clang-format (for code formatting)
- cppcheck (for static analysis)
- Python 3 + pip (for pre-commit hooks)

### Setting Up Pre-commit Hooks

Install and enable local quality gates that run before each commit:

```bash
pip install pre-commit
pre-commit install
```

Pre-commit hooks enforce:
- Trailing whitespace removal
- End-of-file newlines
- YAML/TOML validation
- Secrets detection (via gitleaks)
- clang-format (C++ code style)
- File size limits (max 1500 lines per file)
- cppcheck (C++ static analysis)

### Building and Testing

```bash
# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure
```

## Code Style

The project uses [clang-format](https://clang.llvm.org/docs/ClangFormat.html) with the configuration in `.clang-format`:

```bash
# Check formatting
clang-format --dry-run --Werror src/your_file.cpp

# Auto-fix formatting
clang-format -i src/your_file.cpp
```

Key style rules:
- Based on LLVM style
- 4-space indentation
- 120-character line limit
- Left pointer alignment (`int* p`, not `int *p`)

## Comment Policy

Comments must serve one of four purposes:

| Type                     | Purpose                                           |
|--------------------------|---------------------------------------------------|
| **Invariant**            | States what must always be true                   |
| **Persistence contract** | States what must survive reload / relocation      |
| **Safety note**          | Warns about UB, corruption, or non-obvious risk   |
| **Design note**          | Short explanation of a non-obvious decision       |

**Prohibited patterns** — do not add:
- Issue references (`Issue #N`, `TODO for issue #N`, `implemented in #N`)
- Refactoring history (`was previously ...`, `moved from ...`)
- Temporal promises (`temporarily left`, `remove later`)
- Narrative without invariant (multi-line retelling of code)

History belongs in Git, issues, and pull requests — not in source files.
See [`docs/comment_policy.md`](docs/comment_policy.md) for the full policy.

## File Size Limits

Each source file must not exceed **1500 lines**. This constraint:
- Keeps files comprehensible and maintainable
- Encourages modular design
- Ensures AI tools can read entire files within their context window

## Changelog Fragments

Every pull request that modifies source code must include a **changelog fragment** in `changelog.d/`. This system prevents merge conflicts between parallel PRs and automates release notes.

### Creating a Fragment

```bash
# Create a fragment with a timestamped filename
touch changelog.d/$(date +%Y%m%d_%H%M%S)_description.md
```

### Fragment Format

```markdown
---
bump: patch
---

### Fixed
- Description of what was fixed and why
```

**Bump types:**
- `major` — Breaking API changes
- `minor` — New features (backward compatible)
- `patch` — Bug fixes and improvements (backward compatible)

**Content categories:** `Added`, `Changed`, `Fixed`, `Removed`, `Deprecated`

See [`changelog.d/README.md`](changelog.d/README.md) for detailed instructions.

### What Requires a Fragment

The CI will fail if source files change without a changelog fragment. Source files include:
- `include/**/*.h`
- `tests/**/*.cpp`
- `demo/**/*`
- `examples/**/*`
- `scripts/**/*`
- `CMakeLists.txt`

Docs-only changes (`docs/`, `*.md`, `.github/workflows/`) do not require a fragment.

## Pull Request Process

1. Create a feature branch from `main`
2. Make your changes
3. Add a changelog fragment (if source files changed)
4. Ensure all CI checks pass locally:
   ```bash
   # Format check
   find . \( -name '*.cpp' -o -name '*.h' \) ! -path './third_party/*' \
     -print0 | xargs -0 clang-format --dry-run --Werror

   # Static analysis
   cppcheck --enable=warning,performance --std=c++20 --error-exitcode=1 \
     --suppress=missingIncludeSystem -I include \
     $(find . -name '*.cpp' ! -path './third_party/*')

   # File size check
   find . \( -name '*.cpp' -o -name '*.h' \) ! -path './third_party/*' \
     -print0 | xargs -0 wc -l | awk '$1 > 1500 {print "FAIL:", $0; exit 1}'

   # Build and test
   cmake -B build && cmake --build build
   ctest --test-dir build --output-on-failure
   ```
5. Open a pull request targeting `main`

## Release Process

Releases are automated. When a PR with changelog fragments merges to `main`:
1. The release workflow collects all fragments
2. Determines the version bump (highest bump type wins)
3. Updates `CMakeLists.txt` version and `CHANGELOG.md`
4. Creates a GitHub Release with the compiled changelog

Manual releases can be triggered via `Actions → Release → Run workflow`.

## References

- [Best Practices for AI-Driven Development](https://github.com/link-assistant/hive-mind/blob/main/docs/BEST-PRACTICES.md)
- [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
- [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
- [Code Architecture Principles](https://github.com/link-foundation/code-architecture-principles)
