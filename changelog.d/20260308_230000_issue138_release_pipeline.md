---
bump: patch
---

### Fixed
- Fixed release pipeline (Issue #138): updated `git add` paths in `.github/workflows/release.yml` to use `single_include/pmm/*.h` instead of the old `include/pmm_*.h` location, so generated single-header presets are correctly committed during auto and manual releases
