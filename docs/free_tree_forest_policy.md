# Free-Tree Forest Policy

## Document status

> **Canonical invariant reference:** [core_invariants.md](core_invariants.md) §E (Free-Tree Policy).

This document defines the **canonical forest-policy for the free-tree domain** in `PersistMemoryManager`.

It resolves the semantic gap between:

- the general forest model, where `weight` is a universal granule-key;
- the free-tree implementation, where ordering is derived from linear PAP geometry.

Related documents:

- general forest model: [pmm_avl_forest.md](pmm_avl_forest.md)
- field semantics: [block_and_treenode_semantics.md](block_and_treenode_semantics.md)
- low-level layout and algorithms: [architecture.md](architecture.md)
- code: [../include/pmm/free_block_tree.h](../include/pmm/free_block_tree.h)

## 1. Free-tree as a specialized forest domain

The free-tree is the **primary system domain** of the AVL-forest.
It indexes free blocks of the persistent address space (PAP) for best-fit allocation.

Within the general forest model, the free-tree is a **specialized forest-policy**:
it shares the same AVL tree substrate (`TreeNode` fields, shared rotations and rebalancing
from `avl_tree_mixin.h`), but has its own domain-specific ordering rules
and its own interpretation of block header fields.

This specialization is architecturally intentional.
The free-tree is not an exception to the forest model — it is a concrete instance
of the forest model with an explicit policy.

## 2. Ordering policy

### 2.1. Sort key

The free-tree sort key is: **(block_size_in_granules, block_index)**.

This is a strict total ordering:

1. **Primary key**: block size in granules — smaller blocks go left.
2. **Tie-breaker**: block granule index (physical position in PAP) — lower index goes left.

The tie-breaker guarantees uniqueness: no two blocks can have the same index.

### 2.2. How block size is computed

Block size in granules is computed from linear PAP geometry:

```
if (next_offset != no_block):
    block_size = next_offset - block_index
else:
    block_size = total_granules - block_index   // last block in PAP
```

where `total_granules = total_size / granule_size` from the manager header.

This is a **derived key** — the free-tree computes its ordering key at traversal time
from the linear PAP layout (`prev_offset` / `next_offset` chain),
rather than reading a pre-stored key from a tree-node field.

### 2.3. Why the key is derived, not stored

For the free-tree domain, the sort key is not stored in the `weight` field.
Instead, `weight == 0` is used as part of the block state encoding:

- `weight == 0 && root_offset == 0` — block is free (canonical `is_free()` check)
- `weight > 0 && root_offset == own_idx` — block is allocated

This encoding is a fundamental invariant of the block state machine
(see `block_state.h`: `FreeBlock`, `AllocatedBlock` and their transitions).

The free-tree derives its ordering key from linear geometry because:

1. Block size is always recoverable from the linear PAP chain (`next_offset - block_index`).
2. `weight == 0` provides O(1) free/allocated discrimination without additional fields.
3. The derived key is always consistent with the physical layout — no risk of stale values.
4. After `split` / `coalesce`, block size changes; deriving it avoids maintaining a redundant field.

This is an explicit design choice, not an oversight.

## 3. Role of `weight` in the free-tree domain

In the free-tree domain, `weight` serves as a **state discriminator**, not as a sort key:

| State        | `weight` | `root_offset` | Meaning                |
|--------------|----------|---------------|------------------------|
| Free block   | 0        | 0             | Block belongs to free-tree |
| Allocated    | > 0      | own_idx       | Block is in use (data granules count) |

When a block transitions from free to allocated:
- `weight` changes from 0 to `data_granules` (user payload size)
- `root_offset` changes from 0 to `own_idx`

When a block transitions from allocated to free:
- `weight` changes from `data_granules` to 0
- `root_offset` changes from `own_idx` to 0

This means that in the free-tree domain, `weight` does not carry tree-ordering semantics.
The free-tree's forest-policy derives its ordering key externally,
while `weight` fulfills a different but equally important role as a state marker.

## 4. Relationship to the general forest model

The general forest model states that `weight` is a **universal granule-key / granule-scalar**
whose semantics are determined by the owning domain.

The free-tree domain is consistent with this principle:

- The domain determines the semantics of `weight` → in the free-tree domain,
  `weight` means "state discriminator" (0 = free).
- The domain determines the ordering policy → the free-tree domain derives
  its ordering key from linear PAP geometry.
- The domain uses the same AVL substrate → shared rotations, rebalancing,
  and min_node operations via `BlockPPtr` adapter.

Other forest domains (e.g., `pstringview`, `pmap`, user domains)
use `weight` as their actual sort key. The free-tree's use of `weight`
as a state discriminator rather than a sort key is a domain-specific policy choice
within the general forest model, not a departure from it.

## 5. Best-fit search

`AvlFreeTree::find_best_fit(base, hdr, needed_granules)` performs
an O(log n) search for the smallest block with `block_size >= needed_granules`.

Algorithm:

1. Start at `hdr->free_tree_root`.
2. At each node, compute `block_size` from linear geometry.
3. If `block_size >= needed_granules`: record this node as the current best candidate,
   then go left (smaller blocks may still fit).
4. If `block_size < needed_granules`: go right (need a larger block).
5. Return the best candidate (or `no_block` if none found).

This is a standard AVL best-fit search with the sort key computed on-the-fly.

## 6. Consideration: bucketed free forest

A bucketed free forest would partition free blocks into multiple AVL trees
by size class, with each bucket indexed by block address.
This is analogous to size-class allocators (e.g., jemalloc, tcmalloc).

### 6.1. Potential structure

```
bucket[0]:  blocks of size 1–2 granules       (sorted by block_index)
bucket[1]:  blocks of size 3–4 granules       (sorted by block_index)
bucket[2]:  blocks of size 5–8 granules       (sorted by block_index)
...
bucket[k]:  blocks of size >= 2^(k+1) granules (sorted by block_index)
```

### 6.2. Trade-offs

**Advantages:**

- Allocation becomes O(1) for the bucket lookup + O(log n_bucket) for finding
  a block within the bucket, where n_bucket << n_total.
- Address-ordered intra-bucket sorting improves spatial locality.
- Reduces fragmentation for common allocation patterns.

**Disadvantages:**

- More complex coalescing — a coalesced block may move between buckets.
- Multiple tree roots to manage in the forest registry.
- Increased bootstrap complexity.
- Current single-tree design is sufficient for most PMM workloads.

### 6.3. Conclusion

A bucketed free forest is a valid future optimization, but is **out of scope**
for the current implementation. The current single-tree best-fit design is
architecturally sound and consistent with the forest model.

If bucketed allocation is needed, it should be implemented as a set of
forest domains (one per bucket) managed by the allocator policy,
using the existing `ForestDomainRegistry` infrastructure.

## 7. Canonical summary

| Aspect | Free-tree policy |
|--------|-----------------|
| **Domain name** | `system/free_tree` |
| **Sort key** | `(block_size_in_granules, block_index)` — derived from linear PAP |
| **Tie-breaker** | Block granule index (physical position) |
| **`weight` role** | State discriminator: 0 = free, > 0 = allocated |
| **`root_offset` role** | State discriminator: 0 = free-tree, own_idx = allocated |
| **Search** | Best-fit, O(log n) |
| **Binding kind** | `kForestBindingFreeTree` (special binding in forest registry) |
| **Root storage** | `ManagerHeader::free_tree_root` |
| **AVL substrate** | Shared rotations/rebalancing via `BlockPPtr` adapter |
| **Key derivation** | Computed at traversal time from `next_offset` / `total_granules` |
