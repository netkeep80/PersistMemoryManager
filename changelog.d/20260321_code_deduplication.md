---
bump: minor
---

### Changed
- refactor(#188): eliminate code duplication across persistent containers using C++ template metaprogramming
  - Extract shared `avl_inorder_successor`, `avl_init_node`, `avl_subtree_count`, `avl_clear_subtree` to `avl_tree_mixin.h` — eliminates ~100 lines of duplicated AVL traversal/initialization code from pvector, pmap, pstringview
  - Extract `resolve_granule_ptr` and `ptr_to_granule_idx` helpers to `types.h` — eliminates repeated index↔pointer conversion patterns from parray, pstring, ppool, pstringview
  - Extract `crc32_accumulate_byte` helper — eliminates 4 duplicated CRC32 bit-rotation loops in `types.h`
  - Deduplicate pvector `front()`/`back()`/`pop_back()`/`begin()` using shared `avl_min_node`/`avl_max_node`
  - Unify pstringview AVL node initialization to use shared `avl_init_node` with correct `no_block` sentinel
  - Extract `StaticConfig` base template in `manager_configs.h` — eliminates duplicated struct bodies for `SmallEmbeddedStaticConfig` and `EmbeddedStaticConfig`
  - Regenerate `single_include/` headers
