---
bump: patch
---

### Changed
- Replaced deprecated `detail::granules_to_bytes()` calls with `address_traits::granules_to_bytes()` in `persist_memory_manager.h` `used_size()` and `free_size()` methods, ensuring correct granule size is used for any `address_traits` (#166)
- Removed redundant `static_assert(ValidPmmAddressTraits<X>)` from `SmallEmbeddedStaticConfig` and `EmbeddedStaticConfig`; these are already verified at namespace scope (#166)

### Added
- `detail::kNoBlock_v<AT>` — template variable alias for `AT::no_block` sentinel, enabling type-safe sentinel comparisons in generic (templated) code across all address traits (#166)
- `detail::required_block_granules_t<AT>()` — templated variant of `required_block_granules()` that uses `AddressTraitsT::granule_size` and `kBlockHeaderGranules_t<AT>`, eliminating the non-templated `DefaultAddressTraits`-specific version as the only option (#166)
