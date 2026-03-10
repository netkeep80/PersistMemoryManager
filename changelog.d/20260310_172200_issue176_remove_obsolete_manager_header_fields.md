---
bump: patch
---

### Removed
- `prev_owns_memory` (bool) and `prev_base_ptr` (void*) fields removed from `ManagerHeader`
  (Issue #176). These runtime-only fields were obsolete and unused. Their bytes are now
  occupied by reserved padding (`_pad` and `_reserved[8]`) to maintain the 64-byte struct layout.
