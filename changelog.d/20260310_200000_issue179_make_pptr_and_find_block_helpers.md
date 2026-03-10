---
bump: patch
---

### Changed
- Extracted repeated `raw → pptr` conversion in `allocate_typed<T>()`, `allocate_typed<T>(count)`,
  and `create_typed<T>(args...)` into a private helper `make_pptr_from_raw<T>()`, eliminating three
  identical copies of the same formula (Issue #179)
- Extracted the repeated block-header lookup prologue shared by `deallocate()`,
  `lock_block_permanent()`, and `is_permanently_locked()` into two private overloaded helpers
  `find_block_from_user_ptr(void*)` / `find_block_from_user_ptr(const void*)`, reducing the risk
  of behavioural divergence between these methods (Issue #179)
