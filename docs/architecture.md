# PersistMemoryManager Architecture

## Overview

`PersistMemoryManager` is a header-only C++20 library for persistent heap memory management.
All metadata is stored inside the managed memory region, which allows saving and restoring
a memory image from a file or shared memory. Interaction with data is done through
persistent typed pointers `pptr<T, ManagerT>`.

The library is fully static: there are no instances, and all API is accessed through static
methods on the manager type. Multiple independent managers with the same configuration can
coexist through the `InstanceId` template parameter (multiton pattern).

---

## Architecture layers

```
┌───────────────────────────────────────────────────────────────────────┐
│  PersistMemoryManager<ConfigT, InstanceId>                            │
│  (static public API: create / load / destroy)                         │
│  allocate_typed / deallocate_typed (pptr<T>)                          │
│  for_each_block / for_each_free_block                                 │
│  pstringview<ManagerT> / pmap<_K,_V,ManagerT>                        │
├───────────────────────────────────────────────────────────────────────┤
│  avl_tree_mixin.h (detail::avl_*)                                     │
│  (shared AVL: rotate, rebalance, insert, remove, find, iterators)    │
│  used by pmap, pstringview, pvector, AvlFreeTree (via BlockPPtr)     │
├───────────────────────────────────────────────────────────────────────┤
│  AllocatorPolicy<FreeBlockTreeT, AddressTraitsT>                      │
│  (best-fit via AVL tree, splitting, coalescing,                       │
│   repair_linked_list, rebuild_free_tree,                              │
│   recompute_counters)                                                 │
├───────────────────────────────────────────────────────────────────────┤
│  BlockState machine (block_state.h)                                   │
│  (type-safe state transitions: Free → Allocated → Free)               │
├───────────────────────────────────────────────────────────────────────┤
│  Block<AddressTraitsT> raw memory layout                              │
│  (LinkedListNode<A> + TreeNode<A>, granule indices)                   │
├───────────────────────────────────────────────────────────────────────┤
│  StorageBackend: HeapStorage / MMapStorage / StaticStorage            │
│  LockPolicy: NoLock / SharedMutexLock                                 │
└───────────────────────────────────────────────────────────────────────┘
```

---

## Managed memory region structure

```
[Block<A>_0][[ManagerHeader]]              ← granules 0–5 (2 + 4 granules)
[Block<A>_1][user_data_1...][padding]
...
[Block<A>_N][free space....]
```

`ManagerHeader` is stored as the user data of `Block<A>_0` (granule 0, byte offset 32).
The memory layout is homogeneous: every region is a block. `Block<A>_0` has
`root_offset=0` (its own index) and `weight=kManagerHeaderGranules`.

User blocks start at granule 6 (byte offset 96 from the buffer start) for
`DefaultAddressTraits`. The exact layout depends on `address_traits::granule_size`.

---

## ManagerHeader

Located inside `Block<A>_0` at byte offset `sizeof(Block<AddressTraitsT>)` from the
buffer start. Contains:

| Field | Type | Description |
|-------|------|-------------|
| `magic` | `uint64_t` | Magic number `"PMM_V083"` for validation |
| `total_size` | `uint64_t` | Total managed region size in bytes |
| `used_size` | `uint32_t` | Used size in granules |
| `block_count` | `uint32_t` | Total block count |
| `free_count` | `uint32_t` | Free block count |
| `alloc_count` | `uint32_t` | Allocated block count |
| `first_block_offset` | `uint32_t` | Granule index of the first block |
| `last_block_offset` | `uint32_t` | Granule index of the last block |
| `free_tree_root` | `uint32_t` | Root of the AVL free block tree (granule index) |
| `owns_memory` | `bool` | Runtime-only: true if manager owns the buffer |
| `_pad` | `uint8_t` | Reserved padding byte (Issue #176: was `prev_owns_memory`) |
| `granule_size` | `uint16_t` | Granule size at creation time; checked on `load()` |
| `prev_total_size` | `uint64_t` | Runtime-only: previous buffer size after `expand()` |
| `_reserved[8]` | `uint8_t[8]` | Reserved bytes (Issue #176: was `prev_base_ptr`) |

---

## Block layout: `Block<AddressTraitsT>`

Every block (header + data) is aligned to the granule size. The header `Block<A>` is
32 bytes = 2 granules (for `DefaultAddressTraits`) and is placed immediately before the
user data.

`Block<A>` = `LinkedListNode<A>` + `TreeNode<A>`:

| Field | Bytes | Type | Description |
|-------|-------|------|-------------|
| `prev_offset` | 0–3 | `uint32_t` | Previous block granule index (`kNoBlock` = none) |
| `next_offset` | 4–7 | `uint32_t` | Next block granule index (`kNoBlock` = last) |
| `weight` | 8–11 | `uint32_t` | User data size in granules (0 = free block) |
| `left_offset` | 12–15 | `uint32_t` | Left child AVL node (granule index) |
| `right_offset` | 16–19 | `uint32_t` | Right child AVL node (granule index) |
| `parent_offset` | 20–23 | `uint32_t` | Parent AVL node (granule index) |
| `root_offset` | 24–27 | `uint32_t` | 0 = free block; own index = allocated block |
| `avl_height` | 28–29 | `int16_t` | AVL subtree height (0 = not in tree) |
| `node_type` | 30–31 | `uint16_t` | 0=`kNodeReadWrite`, 1=`kNodeReadOnly` (permanently locked) |

Field sizes above are for `DefaultAddressTraits` (`uint32_t` index, 16-byte granule).
For `SmallAddressTraits` (`uint16_t`) and `LargeAddressTraits` (`uint64_t`), the field
types change accordingly.

**Key invariants:**
- `weight == 0` → free block; `root_offset == 0`.
- `weight > 0` → allocated block; `root_offset == own_granule_index`.
- `node_type == kNodeReadOnly` → permanently locked; cannot be freed via `deallocate()`.

---

## Algorithms

### Memory allocation (`allocate` / `allocate_typed`)

```
1. Compute required_block_granules = kBlockHeaderGranules + ceil(user_size / kGranuleSize)
2. Search AVL free block tree for best-fit (smallest block >= required_block_granules) — O(log n)
3. If found:
   a. Remove from AVL tree
   b. If the block is significantly larger (splitting possible):
      - Initialize new free block from the remainder
      - Insert into linked list and AVL tree
   c. Mark block as allocated (set weight and root_offset)
   d. Update ManagerHeader counters
   e. Return pointer to user data
4. If not found: expand storage backend by growth ratio and retry
```

### Memory deallocation (`deallocate` / `deallocate_typed`)

```
1. If pointer is null or block is permanently locked: no-op
2. Locate Block<A> from user pointer — O(1)
3. If block is allocated:
   - Set weight = 0, root_offset = 0 (mark as free)
   - Update ManagerHeader counters
   - Attempt coalescing with adjacent free blocks
   - Insert resulting block into AVL tree
```

### Free block coalescing

When a block is freed, adjacent blocks are checked. If they are free, they are merged
into one larger block:

```
[Block (free)][free space][Block (free)] → [Block (free)][larger free space]
```

Coalescing always checks both the previous and next block.

### Block splitting

When allocating, if the found free block is significantly larger than needed, it is split:

```
[Block (allocated)][user data][Block (free)][remaining free space...]
```

Minimum size for the new free block: `sizeof(Block<A>) + kMinBlockSize`.

### Storage backend expansion

When memory is exhausted:

```
1. Allocate a new buffer of size max(old_size * grow_numerator / grow_denominator,
                                    old_size + required_bytes)
2. Copy the contents of the old buffer to the new buffer
3. Extend or add a free block at the end of the new buffer
4. Update the singleton to point to the new buffer
5. Keep the old buffer for cleanup on destroy()
```

---

## Persistence

All inter-block references are stored as **granule indices** — offsets from the buffer
start, not absolute pointers. This enables:

1. Saving the memory image to a file (`fwrite` the entire region).
2. Loading it at a different base address (`mmap` or `malloc`).
3. Using in shared memory segments.

On `load()`, the library validates the magic number, total size, and granule size, then
calls `repair_linked_list()`, `recompute_counters()`, and `rebuild_free_tree()` to
restore a consistent state.

`pptr<T>` stores a granule index (2, 4, or 8 bytes depending on `address_traits`),
making it persistent: after loading the image at a different address, the same index
points to the same data.

---

## Alignment

All blocks are aligned to the granule size. User data starts immediately after the
`Block<A>` header with no additional padding:

```
[Block<A> header (2 granules)][user_data (aligned to granule size)]
```

---

## Thread safety

Thread safety is controlled by the `lock_policy` configuration field:

- **`SharedMutexLock`**: uses `std::shared_mutex`.
  - Read operations: `shared_lock` (concurrent execution allowed).
  - Write operations: `unique_lock` (exclusive access).
- **`NoLock`**: no-op locks (zero overhead, single-threaded use only).

---

## Storage backends

| Backend | Description | Use case |
|---------|-------------|----------|
| `HeapStorage<A>` | Dynamically allocated via `std::malloc` / `std::realloc` | General purpose |
| `StaticStorage<A, Size>` | Fixed compile-time buffer (no dynamic allocation) | Embedded systems |
| `MMapStorage<A>` | Memory-mapped file (`mmap` / `MapViewOfFile`) | File-backed persistence |

---

## Address traits

The address space is parameterized by `AddressTraits<IndexT, GranuleSz>`:

| Predefined alias | `index_type` | Granule | Max size | `sizeof(pptr<T>)` |
|-----------------|-------------|---------|----------|-------------------|
| `TinyAddressTraits` | `uint8_t` | 8 B | ~2 KB | 1 byte |
| `SmallAddressTraits` | `uint16_t` | 16 B | ~1 MB | 2 bytes |
| `DefaultAddressTraits` | `uint32_t` | 16 B | 64 GB | 4 bytes |
| `LargeAddressTraits` | `uint64_t` | 64 B | Petabyte+ | 8 bytes |

Choosing a smaller `index_type` reduces `pptr<T>` size at the cost of a lower address
space limit.

---

## Lock policies

| Policy | Description |
|--------|-------------|
| `config::NoLock` | No-op locks; zero overhead; single-threaded only |
| `config::SharedMutexLock` | `std::shared_mutex`; `shared_lock` for reads, `unique_lock` for writes |

---

## Persistent data structures

### `pstringview<ManagerT>`

An interned read-only persistent string. Multiple calls with the same content return the
same `pptr` (deduplication). Uses the built-in `TreeNode` fields of each allocated block
as AVL tree links — no separate AVL node allocations. Blocks are permanently locked via
`lock_block_permanent()`.

```
static pstringview::_root_idx  (singleton per ManagerT specialization)
│
├── [Block_A][pstringview: chars_idx=X, length=5]   "hello"
│     left_offset → Block_B
│     right_offset → Block_C
│
├── [Block_B][pstringview: chars_idx=Y, length=3]   "abc"
│
└── [Block_C][pstringview: chars_idx=Z, length=5]   "world"
```

### `pmap<_K, _V, ManagerT>`

A persistent AVL tree dictionary. The `pmap` object itself lives on the stack (holds only
`_root_idx`). Each node is an allocated block in PAP containing `pmap_node<_K, _V>`. The
built-in `TreeNode` fields serve as AVL tree links. Nodes are **not** permanently locked
(unlike `pstringview`), so they can be freed.

```
pmap::_root_idx
│
├── [Block][pmap_node: key=42, value=100]
│     left_offset → Block with key=10
│     right_offset → Block with key=99
│
├── [Block][pmap_node: key=10, value=200]
│
└── [Block][pmap_node: key=99, value=300]
```

### Shared AVL operations (`avl_tree_mixin.h`)

All persistent containers (`pstringview`, `pmap`, `pvector`) and the free block tree
(`AvlFreeTree`) share a single AVL implementation via free template functions in
`pmm::detail`. This eliminates ~250 lines of previously duplicated code through
C++ template metaprogramming (Issue #188).

#### Core AVL functions

- `avl_height(p)` — get height (0 if null)
- `avl_update_height(p)` — recompute height from children
- `avl_balance_factor(p)` — `height(left) - height(right)`
- `avl_rotate_right(y, root_idx, update_node)` — right rotation with custom node update
- `avl_rotate_left(x, root_idx, update_node)` — left rotation with custom node update
- `avl_rebalance_up(p, root_idx, update_node)` — walk up the tree, fixing balance
- `avl_insert(new_node, root_idx, go_left, resolve, update_node)` — insert and rebalance
- `avl_remove(target, root_idx, update_node)` — BST removal with in-order successor
- `avl_find(root_idx, compare_three_way, resolve)` — generic search with custom comparison
- `avl_min_node(p)` / `avl_max_node(p)` — subtree min/max
- `avl_inorder_successor(cur)` — next node in sorted order
- `avl_init_node(p)` — initialize node (left/right/parent = `no_block` sentinel, height = 1)
- `avl_subtree_count(p)` — recursive subtree size
- `avl_clear_subtree(p, dealloc)` — recursive deallocation with custom callback

All functions are parameterized by `PPtr` (the persistent pointer type) and `IndexType`,
making them usable with any `pptr<T, ManagerT>` specialization.

#### Custom node update callbacks (`NodeUpdateFn`)

All rotation and rebalancing functions accept an optional `NodeUpdateFn` callback
invoked after structural changes. This enables different containers to maintain
different node invariants:

- **`pmap`, `pstringview`**: use default `AvlUpdateHeightOnly` (height field only)
- **`pvector`**: uses `PvectorNodeUpdateFn` that updates both height **and** the
  order-statistic weight field (subtree size), enabling O(log n) indexed access
- **`AvlFreeTree`**: uses `AvlUpdateHeightOnly` via `BlockPPtr` adapter

#### `BlockPPtr<AT>` adapter (for free block tree)

`BlockPPtr<AddressTraitsT>` is a lightweight adapter wrapping raw `(base_ptr, block_index)`
pairs, making them behave like `pptr` for the shared AVL functions. This enables
`AvlFreeTree` to reuse shared rotation, rebalancing, and min_node operations instead
of maintaining ~120 lines of duplicate code.

- `BlockPPtrManagerTag<AT>` — provides `address_traits` for template resolution
- `BlockTreeNodeProxy<AT>` — proxy for `TreeNode`-like interface, delegating to `BlockStateBase`
- `pptr_make(BlockPPtr, idx)` — specialization propagating `base_ptr`

#### `AvlInorderIterator<NodePPtr>`

A shared in-order AVL tree iterator template that replaces identical iterator structs
previously duplicated in `pmap` and `pvector`. Provides `operator*`, `operator++`
(via `avl_inorder_successor`), and comparison operators.

---

## Data structures diagram

```
buffer_start (= Block<A>_0)
│
├── Block<A>_0 (granule 0, allocated — holds ManagerHeader)
│     weight = kManagerHeaderGranules
│     root_offset = 0 (own index)
│     prev_offset = kNoBlock
│     next_offset ──────────────────────────┐
│   [ManagerHeader inside (32 bytes offset)]│
│     magic = "PMM_V083"                    │
│     first_block_offset = 0 (self)         │
│     free_tree_root ──────────────────┐    │
│                                      │    │
├── Block<A>_free ◄────────────────────┘◄───┘ (granule 6, free block)
│     weight = 0
│     root_offset = 0
│     prev_offset = 0 (Block<A>_0)
│     next_offset ────────────────────────┐
│     [free space...]                     │
│                                         │
├── Block<A>_user ◄───────────────────────┘ (allocated user block)
│     weight > 0
│     root_offset = own_granule_idx
│     [user data...]
│
└── (end of managed region)
```

---

## Block state machine

Blocks transition between two correct states and several transient states. See
[`docs/atomic_writes.md`](atomic_writes.md) for the full state diagram and crash
recovery analysis.

### Correct states

| State | `weight` | `root_offset` | In AVL tree |
|-------|----------|---------------|-------------|
| `FreeBlock` | 0 | 0 | Yes |
| `AllocatedBlock` | >0 | own index | No |

### Forbidden states

| State | `weight` | `root_offset` | Valid? |
|-------|----------|---------------|--------|
| Free block not in AVL | 0 | 0 | Transient only |
| weight=0, root_offset≠0 | 0 | ≠0 | Never |
| weight>0, root_offset=0 | >0 | 0 | Never |
| weight>0 and in AVL | >0 | own idx | Never |

---

## Configuration composition

The full type of a manager is determined by composing four independent policies:

```cpp
// Example: custom multi-threaded manager backed by MMapStorage
struct MyConfig {
    using address_traits  = pmm::DefaultAddressTraits;
    using storage_backend = pmm::MMapStorage<pmm::DefaultAddressTraits>;
    using free_block_tree = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;
    using lock_policy     = pmm::config::SharedMutexLock;
    static constexpr std::size_t granule_size     = 16;
    static constexpr std::size_t max_memory_gb    = 64;
    static constexpr std::size_t grow_numerator   = 5;
    static constexpr std::size_t grow_denominator = 4;
};

using MyManager = pmm::PersistMemoryManager<MyConfig, 0>;
```

---

## Multiton pattern

Each unique `(ConfigT, InstanceId)` pair is a distinct static manager with completely
independent storage. This allows multiple persistent heaps in a single process:

```cpp
using Cache  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 0>;
using Buffer = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 1>;

Cache::create(64 * 1024);
Buffer::create(32 * 1024);

Cache::pptr<int>  cp = Cache::allocate_typed<int>();
Buffer::pptr<int> bp = Buffer::allocate_typed<int>();

*cp = 42;
*bp = 100;

Cache::deallocate_typed(cp);
Buffer::deallocate_typed(bp);
Cache::destroy();
Buffer::destroy();
```

`Cache::pptr<int>` and `Buffer::pptr<int>` are **different types** — the compiler
prevents accidental cross-manager pointer use.
