---
bump: patch
---

### Changed
- Compress `block_state.h` docstring to a short Russian scope-note: FSM =
  allocator/free-tree domain; `pmap`/`pstringview` do not traverse
  `FreeBlock ↔ AllocatedBlock`; `BlockStateBase<AT>::*` is a low-level helper
  layer for allocator/repair. Canonical explanation stays in
  `docs/atomic_writes.md`; `docs/architecture.md` keeps a one-line reference.
