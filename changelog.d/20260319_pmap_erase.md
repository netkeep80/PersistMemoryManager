---
bump: minor
---

### Added
- `pmap::erase(key)` — remove a node by key with O(log n) AVL removal and memory deallocation (#196)
- `pmap::size()` — return the number of elements in the dictionary (#196)
- `pmap::begin()`/`end()` — iterator for in-order (sorted key) traversal (#196)
- `pmap::clear()` — remove all elements with memory deallocation (#196)
- 22 new tests for pmap erase, size, iterator, and clear functionality
