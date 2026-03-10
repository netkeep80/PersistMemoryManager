---
bump: patch
---

### Changed
- Removed duplicate wrapper functions `reset_block_avl_fields()`, `repair_block_prev_offset()`, `read_block_next_offset()`, `read_block_weight()` from `block_state.h` (Issue #168). `AllocatorPolicy` now calls `BlockStateBase<AT>::*` static methods directly, eliminating ~50 lines of one-liner delegation.
- Deleted `detail::kBlockHeaderGranules` from `types.h`; all call sites updated to use `detail::kBlockHeaderGranules_t<DefaultAddressTraits>` directly (Issue #168).
- Added `using BlockState = BlockStateBase<AddressTraitsT>` alias in `AllocatorPolicy` for consistent, readable access to `BlockStateBase` static methods (Issue #168).
- Regenerated `single_include/pmm/pmm.h` after deduplication (5739 lines, -47 lines from removed wrappers).
