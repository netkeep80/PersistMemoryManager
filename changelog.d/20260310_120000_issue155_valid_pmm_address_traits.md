---
bump: minor
---

### Changed
- Replaced 14 repeated `static_assert` pairs in `manager_configs.h` with a single `ValidPmmAddressTraits<AT>` C++20 concept (Issue #155)
