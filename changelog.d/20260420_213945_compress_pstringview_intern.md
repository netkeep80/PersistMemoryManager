---
bump: patch
---

### Changed
- Collapsed `pstringview::_intern` and `intern_symbol_unlocked` into a single
  canonical symbol-domain interning path. `intern_symbol_unlocked` is now the
  only place that allocates a block, writes the pstringview payload, runs the
  permanent lock, and inserts into the `system/symbols` AVL tree.
  `pstringview::_intern` shrank to a writer-lock acquisition that delegates to
  the canonical helper, eliminating ~60 lines of duplicated raw→pptr→payload→
  avl_init→lock→insert plumbing in `include/pmm/pstringview.h`.
