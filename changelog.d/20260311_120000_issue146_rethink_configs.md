---
bump: minor
---

### Added
- `SmallEmbeddedStaticConfig<N>` — new configuration for small embedded systems with 16-bit index (`SmallAddressTraits`, `StaticStorage`, `NoLock`) supporting pools up to ~1 MB (Issue #146).
- `LargeDBConfig` — new configuration for large databases with 64-bit index (`LargeAddressTraits`, `HeapStorage`, `SharedMutexLock`, 100% growth) (Issue #146).
- `SmallEmbeddedStaticHeap<N>` preset — ready-to-use manager alias for 16-bit embedded systems with static storage (Issue #146).
- `LargeDBHeap` preset — ready-to-use manager alias for large databases with 64-bit index (Issue #146).
- `kMinGranuleSize = 4` constant in `manager_configs.h` — explicit minimum granule size (architecture word size) (Issue #146).
- `ValidPmmAddressTraits` C++20 concept in `manager_configs.h` — validates that `AddressTraitsT` has `granule_size >= kMinGranuleSize` and `granule_size` is a power of two (Issue #146).
- New tests: `test_issue146_configs`, `test_issue146_index_sizes`, and single-header tests for embedded static, small embedded static, and large DB presets (Issue #146).
- Single-header files for new presets: `pmm_embedded_static_heap.h`, `pmm_small_embedded_static_heap.h`, `pmm_large_db_heap.h` (Issue #146).

### Changed
- `AddressTraits<IndexT, GranuleSz>` static assertion strengthened: `GranuleSz >= 4` (was `>= 1`) — enforces minimum architecture word size (Issue #146).
- `address_traits.h` documentation updated to list valid index types (uint16_t, uint32_t, uint64_t) and explain granule selection rules (Issue #146).
- `manager_configs.h` documentation updated with configuration selection rules, granule waste analysis, and architecture scenarios (Issue #146).
- `pmm_presets.h` documentation updated: "tiny embedded" renamed to "small embedded" to align with `SmallAddressTraits` terminology (Issue #146).
- Tests `test_issue87_phase1/2/3/5/abstraction` updated: references to removed `TinyAddressTraits` alias replaced with explicit `AddressTraits<uint8_t, 8>` (Issue #146).

### Removed
- `TinyAddressTraits` alias (`AddressTraits<uint8_t, 8>`) removed from `address_traits.h` — a uint8_t index allows only 255 granules (~2 KB), which is insufficient for any real PMM use case (Issue #146).
