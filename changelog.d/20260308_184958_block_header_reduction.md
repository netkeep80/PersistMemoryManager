---
bump: minor
---

### Changed
- Reduced `Block<A>` header from 32 bytes (2 granules) to 16 bytes (1 granule) (Issue #136)
- Moved `prev_offset`, `left_offset`, `right_offset`, `parent_offset` from block header into a new `FreeBlockData<A>` structure stored in the data area of free blocks
- `kMinBlockSize` reduced from 48 to 32 bytes; `kMinMemorySize` reduced from 176 to 128 bytes
- Coalescing no longer requires `prev_offset` to be pre-set in the block header; the allocator performs a forward scan to locate the previous block when needed

### Added
- New `FreeBlockData<A>` structure (`include/pmm/free_block_data.h`) storing doubly-linked-list and AVL tree pointers in free blocks' data area
- Static utility accessors in `BlockStateBase<A>` for reading and writing `FreeBlockData` fields
- Tests for the new architecture in `tests/test_issue136_embedded_list_node.cpp`

### Fixed
- `rebuild_free_tree()` now only resets AVL-related `FreeBlockData` fields for free blocks, avoiding corruption of user data in allocated blocks during `load()` operations
