---
bump: patch
---

### Removed
- Removed 10 redundant `pptr` instance methods that duplicated `tree_node()` functionality (Issue #164):
  `get_tree_left()`, `get_tree_right()`, `get_tree_parent()`,
  `set_tree_left()`, `set_tree_right()`, `set_tree_parent()`,
  `get_tree_weight()`, `set_tree_weight()`,
  `get_tree_height()`, `set_tree_height()`.
  Use `p.tree_node()` to access `TreeNode` fields directly via `get_left()`,
  `set_left()`, `get_right()`, `set_right()`, `get_parent()`, `set_parent()`,
  `get_weight()`, `set_weight()`, `get_height()`, `set_height()`.
- Updated `avl_tree_mixin.h` to use `tree_node()` API instead of removed `pptr` methods.
- Rewrote `test_pptr.cpp` tree-related tests to use `tree_node()` API.
- Updated `README.md` and `docs/api_reference.md` to reflect the new API.
- `pptr::resolve()` retained for low-level/array use; prefer `*p` and `p->field` for scalar access.
