---
bump: minor
---

### Added
- `reallocate_typed<T>(pptr<T>, old_count, new_count)` — native reallocation for typed persistent pointers (Issue #210, Phase 4.3)
  - In-place shrink with optional split of remainder into free block (with coalescing)
  - In-place grow by absorbing adjacent free block
  - Fallback: allocate new block + memmove + deallocate old (all under same lock)
  - Safe for all AddressTraits: SmallAddressTraits, DefaultAddressTraits, LargeAddressTraits
  - On failure the old block is preserved (caller retains ownership)
  - `T` must be `trivially_copyable`; enforced via `static_assert`
  - Sets `PmmError::InvalidSize`, `PmmError::Overflow`, `PmmError::NotInitialized`, or `PmmError::OutOfMemory` on failure
- 15 tests in `test_issue210_reallocate_typed.cpp`
