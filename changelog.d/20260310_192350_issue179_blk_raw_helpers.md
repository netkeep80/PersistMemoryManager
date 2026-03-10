---
bump: patch
---

### Changed
- Extracted repeated `blk_raw` pointer computation in `PersistMemoryManager` into two private
  helper methods (`block_raw_ptr_from_pptr` and `block_raw_mut_ptr_from_pptr`), eliminating
  ten copies of the same formula across `get_tree_left_offset`, `get_tree_right_offset`,
  `get_tree_parent_offset`, `set_tree_left_offset`, `set_tree_right_offset`,
  `set_tree_parent_offset`, `get_tree_weight`, `set_tree_weight`, `get_tree_height`,
  `set_tree_height`, and `tree_node` (Issue #179)
