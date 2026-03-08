---
bump: patch
---

### Changed
- Consolidated `blk_at()` duplication (Issue #141): removed per-file private `blk_at()` helpers from `AllocatorPolicy` and `AvlFreeTree`; all call sites now use the single canonical `detail::block_at<AddressTraitsT>(base, idx)` template from `types.h`
- Unified `user_ptr()` duplication (Issue #141): `detail::user_ptr()` in `types.h` is now a template `detail::user_ptr<AddressTraitsT>(block)`, and `AllocatorPolicy::allocate_from_block()` now delegates to it instead of repeating the inline pointer arithmetic
- Documented 12 `get_tree_*/set_tree_*` wrapper methods in `persist_memory_manager.h` as intentional safe-adapters over `BlockStateBase` (Issue #141)
- Added explanatory comment for byte/granule conversion functions in `detail` namespace of `types.h` clarifying why they coexist with `AddressTraits` methods (Issue #141)
