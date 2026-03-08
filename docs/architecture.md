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
┌───────────────────────────────────────────────────────────┐
│  PersistMemoryManager<ConfigT, InstanceId>                │
│  (static public API: create / load / destroy)             │
│  allocate_typed / deallocate_typed (pptr<T>)              │
│  for_each_block / for_each_free_block                     │
├───────────────────────────────────────────────────────────┤
│  AllocatorPolicy<FreeBlockTreeT, AddressTraitsT>          │
│  (best-fit via AVL tree, splitting, coalescing,           │
│   repair_linked_list, rebuild_free_tree,                  │
│   recompute_counters)                                     │
├───────────────────────────────────────────────────────────┤
│  BlockState machine (block_state.h)                       │
│  (type-safe state transitions: Free → Allocated → Free)   │
├───────────────────────────────────────────────────────────┤
│  Block<AddressTraitsT> raw memory layout                  │
│  (LinkedListNode<A> + TreeNode<A>, granule indices)       │
├───────────────────────────────────────────────────────────┤
│  StorageBackend: HeapStorage / MMapStorage / StaticStorage│
│  LockPolicy: NoLock / SharedMutexLock                     │
└───────────────────────────────────────────────────────────┘
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

User blocks start at granule 6 (byte offset 96 from the buffer start).

---

## ManagerHeader

Located inside `Block<A>_0` at byte offset `sizeof(Block<AddressTraitsT>)` = 32 bytes
from the buffer start. Contains:

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
| `prev_owns_memory` | `bool` | Runtime-only: true if previous buffer was owned |
| `granule_size` | `uint16_t` | Granule size at creation time; checked on `load()` |
| `prev_total_size` | `uint64_t` | Runtime-only: previous buffer size after `expand()` |
| `prev_base_ptr` | `void*` | Runtime-only: previous buffer pointer after `expand()` |

---

## Block layout: `Block<AddressTraitsT>`

Every block (header + data) is aligned to `kGranuleSize` (16 bytes). The header
`Block<A>` is 32 bytes = 2 granules and is placed immediately before the user data.

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

Minimum size for the new free block: `sizeof(Block<A>) + kMinBlockSize` (32 + 16 = 48 bytes).

### Storage backend expansion

When memory is exhausted:

```
1. Allocate a new buffer of size max(old_size * grow_numerator / grow_denominator,
                                    old_size + required_bytes)
2. Copy the contents of the old buffer to the new buffer
3. Extend or add a free block at the end of the new buffer
4. Update the singleton to point to the new buffer
5. Keep the old buffer in prev_base_ptr for cleanup on destroy()
```

---

## Persistence

All inter-block references are stored as **granule indices** (`uint32_t`) — offsets
from the buffer start, not absolute pointers. This enables:

1. Saving the memory image to a file (`fwrite` the entire region).
2. Loading it at a different base address (`mmap` or `malloc`).
3. Using in shared memory segments.

On `load()`, the library validates the magic number, total size, and granule size, then
calls `repair_linked_list()`, `recompute_counters()`, and `rebuild_free_tree()` to
restore a consistent state.

`pptr<T>` stores a 32-bit granule index, making it persistent: after loading the image
at a different address, the same index points to the same data.

---

## Alignment

All blocks are aligned to `kGranuleSize` (16 bytes). User data starts immediately after
the `Block<A>` header with no additional padding:

```
[Block<A> header (32 bytes)][user_data (aligned to 16 bytes)]
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

## Lock policies

| Policy | Description |
|--------|-------------|
| `config::NoLock` | No-op locks; zero overhead; single-threaded only |
| `config::SharedMutexLock` | `std::shared_mutex`; `shared_lock` for reads, `unique_lock` for writes |

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
