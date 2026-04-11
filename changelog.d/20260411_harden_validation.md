### Added
- `include/pmm/validation.h` — unified pointer and block validation layer with cheap (fast-path) and full (verify-level) checks.
- `docs/validation_model.md` — low-level validation model specification.
- `detail::validate_block_index<AT>()` — validates granule index is in-range.
- `detail::validate_user_ptr<AT>()` — validates user-data pointer provenance and alignment.
- `detail::validate_link_index<AT>()` — validates linked-list/AVL index (accepts no_block sentinel).
- `detail::validate_block_header_full<AT>()` — full verify-level block header integrity check.
- `detail::block_at_checked<AT>()` — bounds-checked variant of `block_at()`.
- `detail::resolve_granule_ptr_checked<AT>()` — bounds-checked variant of `resolve_granule_ptr()`.
- `detail::ptr_to_granule_idx_checked<AT>()` — validated variant of `ptr_to_granule_idx()`.
- Full block header validation phase in verify path (next/prev bounds, node_type, data range).
- 36 negative tests covering invalid pointer, bad alignment, bad index, broken header, wrong domain/root.

### Fixed
- `resolve<T>()` now returns nullptr (with `PmmError::InvalidPointer`) instead of UB on out-of-bounds pptr.
- `block_raw_ptr_from_pptr()` / `block_raw_mut_ptr_from_pptr()` now return nullptr on invalid offset.
- `make_pptr_from_raw()` now validates pointer is within the managed region.
