---
bump: minor
---

### Changed
- **`BasicConfig<>` template** added to `manager_configs.h` (Issue #160): eliminates ~250 lines of
  duplicated code across 5 heap-based config structs. `CacheManagerConfig`, `PersistentDataConfig`,
  `EmbeddedManagerConfig`, `IndustrialDBConfig`, and `LargeDBConfig` are now type aliases of
  `BasicConfig<AddressTraitsT, LockPolicyT, GrowNum, GrowDen, MaxMemoryGB>`, preserving full
  backward compatibility.
- **Byte/granule conversion deduplication** in `types.h` (Issue #160): non-templated
  `detail::bytes_to_granules()`, `detail::granules_to_bytes()`, `detail::idx_to_byte_off()`, and
  `detail::byte_off_to_idx()` now delegate to their `_t<DefaultAddressTraits>` counterparts,
  eliminating ~60 lines of duplicated arithmetic logic. Behavior is unchanged.
- **`block_total_granules` unified** to a single templated implementation (Issue #160):
  the non-templated `DefaultAddressTraits` overload is removed; the templated variant covers all
  address traits including `DefaultAddressTraits`.
