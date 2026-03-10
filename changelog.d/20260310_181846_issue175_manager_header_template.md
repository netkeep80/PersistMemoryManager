---
bump: minor
---

### Added
- `ManagerHeader<AddressTraitsT>` is now a template parameterized on `AddressTraitsT` (Issue #175). All 7 index/counter fields (`block_count`, `free_count`, `alloc_count`, `first_block_offset`, `last_block_offset`, `free_tree_root`, `used_size`) now use `AT::index_type` instead of hardcoded `std::uint32_t`, enabling correct 16-bit and 64-bit index support.
- `FreeBlockTreePolicyForTraitsConcept<Policy, AT>` — new C++20 concept that validates a free block tree policy against a specific `AddressTraitsT` (Issue #175). Allows `AllocatorPolicy<SmallAddressTraits, AvlFreeTree<SmallAddressTraits>, ...>` and `AllocatorPolicy<LargeAddressTraits, AvlFreeTree<LargeAddressTraits>, ...>` to compile and work correctly.

### Changed
- `kManagerHeaderGranules_t<AT>` now returns `AT::index_type` instead of `std::uint32_t` (Issue #175).
- `kBlockHeaderGranules_t<AT>` now returns `AT::index_type` instead of `std::uint32_t` (Issue #175).
- `required_block_granules_t<AT>()` now returns `AT::index_type` instead of `std::uint32_t` (Issue #175).
- All internal index variables in `AllocatorPolicy`, `AvlFreeTree`, and `PersistMemoryManager` now use `index_type` (i.e. `AT::index_type`) instead of hardcoded `std::uint32_t`, enabling correct operation with 16-bit and 64-bit address traits (Issue #175).
- `AllocatorPolicy` now uses `FreeBlockTreePolicyForTraitsConcept<FreeBlockTreeT, AddressTraitsT>` instead of `is_free_block_tree_policy_v<FreeBlockTreeT>` for the static_assert, enabling non-DefaultAddressTraits specializations (Issue #175).
- All `detail::kNoBlock` sentinel comparisons with `index_type` fields replaced with `AddressTraitsT::no_block` or `address_traits::no_block` to avoid integer promotion issues between differently-sized sentinels (Issue #175).

### Fixed
- SEGFAULT when using `SmallAddressTraits` (uint16_t index) heaps: `detail::kNoBlock` (`uint32_t 0xFFFFFFFF`) was being compared against `uint16_t` index fields. Due to integer promotion, `0xFFFF != 0xFFFFFFFF`, so the sentinel check failed silently, causing invalid block accesses. Fixed by replacing all `detail::kNoBlock` comparisons with `address_traits::no_block` throughout `persist_memory_manager.h` (Issue #175).
