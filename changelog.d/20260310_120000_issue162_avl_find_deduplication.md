---
bump: minor
---

### Changed
- Extracted generic `detail::avl_find()` template into `avl_tree_mixin.h` (Issue #162), eliminating duplicate AVL traversal loops in `pstringview` and `pmap`. Both classes now share the single `detail::avl_find()` implementation from `avl_tree_mixin.h`, consistent with how `detail::avl_insert()` is already shared.
