---
bump: minor
---

### Changed
- **avl_tree_mixin.h**: Added `NodeUpdateFn` hook parameter to `avl_rotate_right`, `avl_rotate_left`, `avl_rebalance_up`, and `avl_insert` — enables custom node-attribute updates (e.g. order-statistic weight) without duplicating rotation/rebalance code (Issue #188).
- **avl_tree_mixin.h**: Added shared `avl_remove`, `avl_min_node`, `avl_max_node` functions for reuse across persistent containers (Issue #188).
- **pvector.h**: Refactored to delegate AVL rotation, rebalancing, insertion and removal to `avl_tree_mixin.h` via `_WeightUpdateFn`, eliminating ~280 lines of duplicated AVL code (Issue #188).
