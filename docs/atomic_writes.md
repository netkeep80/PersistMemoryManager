# Atomic Writes and Block State Machine

## Overview

This document describes the algorithms for modifying blocks in the persistent address
space (PAP — Persistent Address space), the criticality analysis of each write operation,
and the algorithms for verifying and recovering the image after an interrupted operation.

**Goal:** provide a mathematically rigorous guarantee that if the manager was interrupted
at any stage of a write, the image can be verified, the interruption point identified, and
the operation completed (or rolled back).

**Scope:** covers the core block allocator operations. Persistent data structures
(`pstringview`, `pmap`) build their own AVL trees using the same block headers — their
AVL rotations follow the same "non-critical" guarantee: on `load()`, only the free-block
AVL tree is rebuilt; user-data AVL trees (pstringview interning, pmap) are not affected
by `rebuild_free_tree()` and must be managed by the user across process restarts.

---

## Data structures

### ManagerHeader (64 bytes, 4 granules for DefaultAddressTraits)

```
Bytes 0–7:   magic           — manager magic number ("PMM_V083")
Bytes 8–15:  total_size      — total managed region size in bytes
Bytes 16–19: used_size       — used size in granules
Bytes 20–23: block_count     — total block count
Bytes 24–27: free_count      — free block count
Bytes 28–31: alloc_count     — allocated block count
Bytes 32–35: first_block_offset — first block (granule index)
Bytes 36–39: last_block_offset  — last block (granule index)
Bytes 40–43: free_tree_root  — AVL free block tree root (granule index)
Bytes 44:    owns_memory     — runtime-only (not persistent)
Bytes 45:    _pad            — reserved (Issue #176: was prev_owns_memory)
Bytes 46–47: granule_size    — granule size at creation time; validated on load()
Bytes 48–55: prev_total_size — runtime-only (not persistent)
Bytes 56–63: _reserved[8]   — reserved (Issue #176: was prev_base_ptr)
```

The `granule_size` field is checked on `load()`: if it does not match the compile-time
`address_traits::granule_size`, `load()` returns `false` (incompatible image).

### Block\<A\> (32 bytes = 2 granules for DefaultAddressTraits)

`Block<A>` = `LinkedListNode<A>` + `TreeNode<A>`:

```
[TreeNode<A>]
  Bytes 0–3:   weight        — user data size in granules (0 = free block)
  Bytes 4–7:   left_offset   — left AVL child (granule index)
  Bytes 8–11:  right_offset  — right AVL child (granule index)
  Bytes 12–15: parent_offset — parent AVL node (granule index)
  Bytes 16–19: root_offset   — 0 = free block; own_idx = allocated block
  Bytes 20–21: avl_height    — AVL subtree height (0 = not in tree)
  Bytes 22–23: node_type     — 0 = kNodeReadWrite, 1 = kNodeReadOnly (permanently locked)
[LinkedListNode<A>]
  Bytes 24–27: prev_offset   — previous block (granule index)
  Bytes 28–31: next_offset   — next block (granule index)
```

**Note:** The `TreeNode` fields (`left_offset`, `right_offset`, `parent_offset`,
`avl_height`) are shared between two separate tree uses:
1. **Free-block AVL tree** — used by the allocator when `weight == 0` (block is free).
2. **User-data AVL trees** — used by `pstringview` and `pmap` when `weight > 0` (block
   is allocated and permanently locked or in a user-managed data structure).
These two uses are mutually exclusive by the block state machine invariants.

---

## Block validity invariants

A block is considered **valid** if **all** of the following conditions hold:

1. **`weight < total_granules`**: `weight` is strictly less than the total number of
   granules up to the next block (computed via `next_offset`).
2. **`prev_offset < own_idx < next_offset`**: the block's granule index is strictly
   between `prev_offset` and `next_offset` (when they are not `kNoBlock`).
3. **`avl_height < 32`**: the AVL subtree height does not exceed 32 (a reasonable
   upper bound for a tree with up to 2³² nodes).
4. **AVL pointers**: either all of `left_offset`, `right_offset`, `parent_offset` equal
   `kNoBlock` (block not in the tree), or all three are distinct.

### Pseudocode: `is_valid_block`

```cpp
bool is_valid_block(const uint8_t* base, const ManagerHeader* hdr, uint32_t idx) {
    if (idx == kNoBlock) return false;
    if (idx_to_byte_off(idx) + sizeof(Block<A>) > hdr->total_size) return false;

    const auto* blk = reinterpret_cast<const Block<DefaultAddressTraits>*>(
                          base + idx_to_byte_off(idx));

    // 1. weight vs total_granules
    uint32_t total_gran = block_total_granules(base, hdr, blk);
    if (blk->weight >= total_gran) return false;

    // 2. prev_offset < own_idx < next_offset
    if (blk->prev_offset != kNoBlock && blk->prev_offset >= idx) return false;
    if (blk->next_offset != kNoBlock && blk->next_offset <= idx) return false;

    // 3. avl_height < 32
    if (blk->avl_height >= 32) return false;

    // 4. AVL pointer uniqueness (applies only when block is free / in AVL)
    bool l = (blk->left_offset   != kNoBlock);
    bool r = (blk->right_offset  != kNoBlock);
    bool p = (blk->parent_offset != kNoBlock);
    if (l || r || p) {
        if (l && r && blk->left_offset == blk->right_offset)   return false;
        if (l && p && blk->left_offset == blk->parent_offset)  return false;
        if (r && p && blk->right_offset == blk->parent_offset) return false;
    }

    return true;
}
```

**Note:** For allocated blocks used as `pstringview` or `pmap` nodes, the AVL pointer
fields contain user-data tree links (not the free-block tree). These are not validated
by `is_valid_block` — they are managed exclusively by the user-data data structure.

---

## Critical operations

**Notation:**
- **W(addr, val)** — write value `val` to address `addr`
- **CRITICAL** — interruption after this step leaves the image in an inconsistent state
- **NON-CRITICAL** — safe to interrupt; image remains consistent

---

## Algorithm 1: Block allocation with splitting

When allocating memory, if the found free block can be split:

### Write steps

| # | Operation | Critical? | Reason |
|---|-----------|-----------|--------|
| 1 | `avl_remove(blk_idx)` — remove block from AVL | NON-CRITICAL | Block still in linked list; AVL is rebuilt on `load()` |
| 2 | `memset(new_blk, 0)` — zero new split-block header | NON-CRITICAL | New block not yet in linked list |
| 3 | `W(new_blk->next_offset, blk->next_offset)` | **CRITICAL** | `new_blk->next_offset` not yet set |
| 4 | `W(new_blk->prev_offset, blk_idx)` | **CRITICAL** | Back-link to the block being split |
| 5 | `W(old_next->prev_offset, new_idx)` (if next exists) | **CRITICAL** | Forward-link from the next block |
| 6 | `W(blk->next_offset, new_idx)` | **CRITICAL** | Forward-link from split block to new block |
| 7 | `W(hdr->last_block_offset, new_idx)` (if last) | NON-CRITICAL | Optimization; rebuilt on `load()` |
| 8 | `W(hdr->block_count, +1)` | NON-CRITICAL | Counter rebuilt on `load()` |
| 9 | `W(hdr->free_count, +1)` | NON-CRITICAL | Counter rebuilt on `load()` |
| 10 | `W(hdr->used_size, += kBlockHeaderGranules)` | NON-CRITICAL | Recomputed on `load()` |
| 11 | `avl_insert(new_idx)` — insert new free block into AVL | NON-CRITICAL | AVL rebuilt on `load()` |
| 12 | `W(blk->weight, data_gran)` + `W(blk->root_offset, blk_idx)` | **CRITICAL** | Marks block as allocated |
| 13 | Clear AVL fields of `blk` | NON-CRITICAL | AVL rebuilt on `load()` |
| 14 | `W(hdr->alloc_count, +1)` | NON-CRITICAL | Counter rebuilt on `load()` |
| 15 | `W(hdr->free_count, -1)` | NON-CRITICAL | Counter rebuilt on `load()` |
| 16 | `W(hdr->used_size, += data_gran)` | NON-CRITICAL | Recomputed on `load()` |

### Interruption analysis

**Interruption between steps 2 and 3** (new block zeroed, `next_offset` not yet set):
- `blk->next_offset` still points to the old next block; `new_blk` is invisible.
- **Recovery:** `new_blk` is not reachable via the linked list — image is consistent.

**Interruption between steps 5 and 6** (`old_next->prev_offset` updated, `blk->next_offset` not):
- Linked list is inconsistent (forward ≠ backward).
- **Recovery:** `repair_linked_list()` detects and corrects the mismatch.

**Interruption between steps 6 and 12** (new block linked, but original block not marked allocated):
- `blk` is seen as free by `rebuild_free_tree()` and re-inserted into AVL.
- **Recovery:** block is returned to free list (user data lost — acceptable for crash recovery).

---

## Algorithm 2: Block deallocation with coalescing

### Write steps

| # | Operation | Critical? | Reason |
|---|-----------|-----------|--------|
| 1 | `W(blk->weight, 0)` + `W(blk->root_offset, 0)` | **CRITICAL** | Marks block as free |
| 2 | `W(hdr->alloc_count, -1)` | NON-CRITICAL | Counter rebuilt on `load()` |
| 3 | `W(hdr->free_count, +1)` | NON-CRITICAL | Counter rebuilt on `load()` |
| 4 | `W(hdr->used_size, -= freed)` | NON-CRITICAL | Recomputed on `load()` |
| **5** | **Coalesce with next block:** | | |
| 5a | `avl_remove(nxt_idx)` — remove next free block from AVL | NON-CRITICAL | AVL rebuilt on `load()` |
| 5b | `W(blk->next_offset, nxt->next_offset)` | **CRITICAL** | Modifies linked list |
| 5c | `W(nxt->next->prev_offset, blk_idx)` (if exists) | **CRITICAL** | Back-link update |
| 5d | `W(hdr->last_block_offset, blk_idx)` (if `nxt` was last) | NON-CRITICAL | Optimization |
| 5e | `memset(nxt, 0)` — zero merged block header | **CRITICAL** | Destroys merged block header |
| 5f | `W(hdr->block_count, -1)` | NON-CRITICAL | Counter rebuilt on `load()` |
| 5g | `W(hdr->free_count, -1)` | NON-CRITICAL | Counter rebuilt on `load()` |
| **6** | **Coalesce with previous block:** | | |
| 6a | `avl_remove(prv_idx)` | NON-CRITICAL | AVL rebuilt on `load()` |
| 6b | `W(prv->next_offset, blk->next_offset)` | **CRITICAL** | Modifies linked list |
| 6c | `W(blk->next->prev_offset, prv_idx)` (if exists) | **CRITICAL** | Back-link update |
| 6d | `W(hdr->last_block_offset, prv_idx)` (if `blk` was last) | NON-CRITICAL | Optimization |
| 6e | `memset(blk, 0)` — zero current block header | **CRITICAL** | Destroys current block header |
| 6f | `W(hdr->block_count, -1)` | NON-CRITICAL | Counter rebuilt on `load()` |
| 6g | `W(hdr->free_count, -1)` | NON-CRITICAL | Counter rebuilt on `load()` |
| 7 | `avl_insert(result_blk_idx)` — insert merged block | NON-CRITICAL | AVL rebuilt on `load()` |

### Interruption analysis (coalesce)

**Interruption between 5b and 5c** (`blk->next_offset` updated, `new_next->prev_offset` not):
- Linked list inconsistent: forward and backward traversals diverge.
- **Recovery:** `repair_linked_list()` corrects `new_next->prev_offset = blk_idx`.

**Interruption between 5c and 5e** (links updated, but `nxt` header not yet zeroed):
- `nxt` is unreachable via linked list but its header still looks valid.
- **Recovery:** `nxt` is not reachable during linear list traversal; the region is covered
  by the merged block.

---

## Algorithm 3: Reallocation (not critical)

`reallocate` is not a direct API; the pattern is:

1. `allocate_typed` (Algorithm 1) — allocate new block.
2. `memcpy` — copy data.
3. `deallocate_typed` (Algorithm 2) — free old block.

**Interruption after step 1, before step 3:** both blocks exist; data is duplicated.
User loses the new pointer but the old data is intact.

**Interruption after step 3:** old block freed, new block contains data copy.
New pointer was not returned to user but data is preserved.

---

## Algorithm 4: AVL tree rebalancing (not critical)

AVL rebalancing (rotations, height updates) is performed as part of `avl_insert` and
`avl_remove`. These operations are **not critical** because:

1. On `load()`, `rebuild_free_tree()` fully reconstructs the AVL tree from scratch by
   traversing the linear linked list of all blocks.
2. Blocks are always reachable via the linked list (`first_block_offset → next_offset`),
   independently of the AVL structure.

Therefore, any interruption during AVL operations does not affect image correctness
after `load() + rebuild_free_tree()`.

---

## Verification and recovery on load

### Phase 1: Validate ManagerHeader

```
1. Check hdr->magic == kMagic ("PMM_V083")
2. Check hdr->total_size == passed size argument
3. Check hdr->granule_size == kGranuleSize
4. If any check fails: return false (image invalid)
```

### Phase 2: Reset runtime fields

```
hdr->owns_memory     = false;
hdr->prev_total_size = 0;
```

### Phase 3: Repair linked list (`repair_linked_list`)

```cpp
void repair_linked_list(uint8_t* base, ManagerHeader* hdr) {
    uint32_t idx = hdr->first_block_offset;
    while (idx != kNoBlock) {
        Block<A>* blk = block_at(base, idx);
        if (blk->next_offset != kNoBlock) {
            Block<A>* nxt = block_at(base, blk->next_offset);
            if (nxt->prev_offset != idx) {
                // Fix forward/backward inconsistency
                nxt->prev_offset = idx;
            }
        }
        idx = blk->next_offset;
    }
}
```

### Phase 4: Recompute counters (`recompute_counters`)

```cpp
void recompute_counters(uint8_t* base, ManagerHeader* hdr) {
    uint32_t block_count = 0, free_count = 0, alloc_count = 0, used_gran = 0;
    uint32_t idx = hdr->first_block_offset;
    while (idx != kNoBlock) {
        Block<A>* blk = block_at(base, idx);
        block_count++;
        used_gran += kBlockHeaderGranules;
        if (blk->weight > 0) {
            alloc_count++;
            used_gran += blk->weight;
        } else {
            free_count++;
        }
        idx = blk->next_offset;
    }
    hdr->block_count = block_count;
    hdr->free_count  = free_count;
    hdr->alloc_count = alloc_count;
    hdr->used_size   = used_gran;
}
```

### Phase 5: Rebuild AVL tree (`rebuild_free_tree`)

```
1. Reset hdr->free_tree_root = kNoBlock
2. Reset hdr->last_block_offset = kNoBlock
3. Clear all AVL fields (left/right/parent/height) in each block
4. Traverse linked list (first_block_offset → next_offset):
   - For each free block (weight == 0): avl_insert
   - Track last_block_offset
```

### Full `load()` with recovery

```cpp
bool load_with_recovery(void* memory, size_t size) {
    auto* hdr = reinterpret_cast<ManagerHeader*>((uint8_t*)memory + sizeof(Block<A>));
    if (hdr->magic != kMagic || hdr->total_size != size || hdr->granule_size != kGranuleSize)
        return false;

    hdr->owns_memory     = false;
    hdr->prev_total_size = 0;

    repair_linked_list(memory, hdr);
    recompute_counters(memory, hdr);
    rebuild_free_tree(memory, hdr);

    _initialized = true;
    return true;
}
```

---

## Block state machine

### State diagram

Blocks transition between two **correct states** and several **transient states**.
Transient states arise only during atomic operations and cannot appear in a saved image.

#### Correct states

```
┌─────────────────────────┐                    ┌─────────────────────────┐
│      FreeBlock          │                    │    AllocatedBlock       │
│  weight == 0            │ ── allocate ──►   │  weight > 0             │
│  root_offset == 0       │                    │  root_offset == own_idx │
│  in AVL tree            │ ◄── deallocate ── │  not in AVL tree        │
└─────────────────────────┘                    └─────────────────────────┘
```

#### Allocation state transitions

```
FreeBlock
  │  (1) avl_remove(blk_idx)
  ▼
FreeBlockRemovedAVL          ← transient: weight=0, root_offset=0, not in AVL
  │  (2) [if split] begin_splitting()
  │  (3) [if split] initialize new block
  │  (4) [if split] link new block into linked list
  │  (5) [if split] avl_insert(new_idx)
  │  (6) mark_as_allocated(data_gran, blk_idx)
  ▼
AllocatedBlock               ← correct: weight>0, root_offset=own_idx
```

#### Deallocation state transitions

```
AllocatedBlock
  │  (1) mark_as_free()
  │       → weight = 0, root_offset = 0
  ▼
FreeBlockNotInAVL            ← transient: weight=0, root_offset=0, not in AVL
  │  [if coalesce with next]
  │  (2) avl_remove(nxt_idx)
  │  (3) coalesce_with_next(nxt, nxt_nxt, own_idx)
  │       → blk->next_offset = nxt->next_offset
  │       → nxt->next->prev_offset = blk_idx
  │       → memset(nxt, 0)
  │  [if coalesce with prev]
  │  (4) avl_remove(prv_idx)
  │  (5) coalesce_with_prev(prv, next, prv_idx)
  │       → prv->next_offset = blk->next_offset
  │       → next->prev_offset = prv_idx
  │       → memset(blk, 0)
  ▼
CoalescingBlock              ← transient: coalescing complete, not yet in AVL
  │  finalize_coalesce()
  │  avl_insert(result_idx)
  ▼
FreeBlock                    ← correct: weight=0, root_offset=0, in AVL
```

### Block state table

| State | `weight` | `root_offset` | In AVL | Valid? | Notes |
|-------|----------|---------------|--------|--------|-------|
| `FreeBlock` | 0 | 0 | Yes | ✅ | Correct — free block |
| `AllocatedBlock` | >0 | own idx | No | ✅ | Correct — allocated block |
| `FreeBlockRemovedAVL` | 0 | 0 | No | ⚠️ | Transient — only during allocate |
| `FreeBlockNotInAVL` | 0 | 0 | No | ⚠️ | Transient — only during deallocate |
| `SplittingBlock` | 0 | 0 | No | ⚠️ | Transient — during split in allocate |
| `CoalescingBlock` | 0 | 0 | No | ⚠️ | Transient — during coalesce in deallocate |
| — | 0 | ≠0 | — | ❌ | Invalid — contradiction |
| — | >0 | 0 | — | ❌ | Invalid — contradiction |
| — | >0 | own idx | Yes | ❌ | Invalid — allocated block in AVL |

### State machine API

The state machine is implemented via typed wrappers over `Block<A>` that allow only
methods valid for the current state:

```cpp
// FreeBlock — correct state
template <typename A> class FreeBlock : public BlockStateBase<A> {
public:
    static FreeBlock* cast_from_raw(void* raw) noexcept;
    bool verify_invariants() const noexcept;
    FreeBlockRemovedAVL<A>* remove_from_avl() noexcept; // → transient
};

// FreeBlockRemovedAVL — transient state
template <typename A> class FreeBlockRemovedAVL : public BlockStateBase<A> {
public:
    AllocatedBlock<A>* mark_as_allocated(index_type data_granules, index_type own_idx);
    SplittingBlock<A>* begin_splitting();
    FreeBlock<A>* insert_to_avl(); // rollback
};

// AllocatedBlock — correct state
template <typename A> class AllocatedBlock : public BlockStateBase<A> {
public:
    bool verify_invariants(index_type own_idx) const noexcept;
    void* user_ptr() noexcept;
    FreeBlockNotInAVL<A>* mark_as_free() noexcept; // → transient
};

// FreeBlockNotInAVL — transient state
template <typename A> class FreeBlockNotInAVL : public BlockStateBase<A> {
public:
    FreeBlock<A>* insert_to_avl() noexcept;        // → correct
    CoalescingBlock<A>* begin_coalescing() noexcept;
};

// CoalescingBlock — transient state
template <typename A> class CoalescingBlock : public BlockStateBase<A> {
public:
    void coalesce_with_next(void* next, void* next_next, index_type own_idx);
    CoalescingBlock* coalesce_with_prev(void* prev, void* next, index_type prev_idx);
    FreeBlock<A>* finalize_coalesce() noexcept; // → correct
};
```

**Implementation:** `include/pmm/block_state.h`

---

## Recovery guarantees

### Detection and recovery of transient states on `load()`

| Transient state | Detection | Recovery |
|-----------------|-----------|----------|
| `FreeBlockRemovedAVL` | `weight=0`, `root_offset=0`, not reachable in AVL after rebuild | `avl_insert(idx)` |
| `FreeBlockNotInAVL` | Same as above | `avl_insert(idx)` |
| Partial coalesce | Forward/backward inconsistency in linked list | `repair_linked_list()` |

### Crash recovery guarantees by interruption scenario

| Interruption scenario | Result after `load()` |
|-----------------------|----------------------|
| Before any write | Image unchanged, consistent |
| During AVL operations | AVL rebuilt; image consistent |
| During splitting (before writing `blk->next_offset`) | New block invisible; consistent |
| During splitting (after writing `blk->next_offset`) | List repaired by `repair_linked_list()` |
| After splitting, before marking allocated | Block returned to free list (user data lost) |
| During coalesce (links partially updated) | List repaired by `repair_linked_list()` |
| After coalesce (old header not zeroed) | Old header unreachable; area covered by merged block |

### Important note on guarantees

This algorithm provides **crash consistency** in the sense that the image after recovery
is structurally valid and the manager can continue operating. However, **full atomicity**
(guaranteeing that an operation is either fully applied or fully rolled back) requires
write-ahead logging (WAL), which is outside the scope of this implementation.

What is guaranteed: **structural correctness of the image after `load()` for any failure
point**, where partially completed allocation operations are treated as "not performed"
(blocks are returned to the free list).

---

## User-data AVL trees (`pstringview`, `pmap`)

`pstringview` and `pmap` use the same `TreeNode` fields inside allocated blocks to
organize their own AVL trees. This is entirely separate from the free-block AVL tree.

### Persistence of user-data trees

`load()` only rebuilds the **free-block** AVL tree. User-data AVL trees (`pstringview`
interning dictionary, `pmap` root) are **not** automatically restored.

To persist and restore user-data trees across process restarts:

1. **`pstringview` interning dictionary**: The `pstringview` blocks themselves are
   preserved in the image (they are permanently locked). To restore the dictionary root,
   store `pstringview::_root_idx` in a known location in PAP (e.g., in a root header
   block), and restore it after `load()` by calling `pstringview::_root_idx = saved_idx`.

2. **`pmap` dictionary**: The `pmap` struct contains only `_root_idx`. Store the `pmap`
   object itself in PAP (as a persistent root), and after `load()`, retrieve it via its
   saved `pptr`.

### Crash consistency of user-data AVL operations

User-data AVL operations (insert via `pmap::insert`, `pstringview::intern`) are
**non-critical** with respect to memory structure recovery, because:

- All node blocks remain reachable via the linked list (allocated, `weight > 0`).
- `rebuild_free_tree()` correctly skips allocated blocks.
- A partially completed AVL rotation leaves the blocks allocated and reachable.

However, a partially completed `pmap::insert` or `pstringview::intern` may leave the
user-data tree in an inconsistent state (e.g., a new key not yet linked). This is
**not** automatically repaired by `load()`. The application must handle this if crash
consistency is required (e.g., via WAL at the application level).
