# Mutation Ordering Rules

## Document status

Canonical specification for the order of persistent-state mutations in
PersistMemoryManager. For each critical mutation path this document
specifies: the exact write sequence, which partial states are tolerable,
which are corruption, and what verify/repair observes after an
interrupted update.

This document is the companion to [storage_seams.md](storage_seams.md)
and builds on the write-criticality analysis in [atomic_writes.md](atomic_writes.md).

Related documents:

- storage seams: [storage_seams.md](storage_seams.md)
- write criticality: [atomic_writes.md](atomic_writes.md)
- core invariants: [core_invariants.md](core_invariants.md)
- verify/repair contract: [verify_repair_contract.md](verify_repair_contract.md)
- diagnostics taxonomy: [diagnostics_taxonomy.md](diagnostics_taxonomy.md)
- recovery: [recovery.md](recovery.md)
- bootstrap: [bootstrap.md](bootstrap.md)

---

## Terminology

- **CRITICAL write** — interruption after this write, before the next,
  leaves the image in a state that requires `repair_linked_list()` or
  `recover_state()` to fix.
- **NON-CRITICAL write** — interruption after this write is safe because
  the affected data is rebuilt from scratch on `load()` (AVL tree,
  counters, `last_block_offset`).
- **Trust anchor** — data that `load()` trusts unconditionally and cannot
  reconstruct: `magic`, `total_size`, `granule_size`, `next_offset` chain,
  `first_block_offset`.
- **Derived data** — data reconstructed deterministically from the trust
  anchors: counters, AVL tree, `prev_offset`, `last_block_offset`.

---

## M1. Root updates

### M1a. `hdr->root_offset` — forest registry pointer

The header's `root_offset` field points to the `ForestDomainRegistry`
block. It is written in two contexts:

**Context 1: `create()` bootstrap**

| Step | Write | Critical? | Code |
|------|-------|-----------|------|
| 1 | Allocate registry block (modifies free-tree, counters, linked list) | See M4 (allocation) | `forest_domain_mixin.inc:275` |
| 2 | Initialize registry (magic, version, domain_count) | NON-CRITICAL | `forest_domain_mixin.inc:293–296` |
| 3 | Lock block permanently (`node_type = kNodeReadOnly`) | NON-CRITICAL | `forest_domain_mixin.inc:300` |
| 4 | `W(hdr->root_offset, registry_granule_idx)` | **CRITICAL** | `forest_domain_mixin.inc:306` |
| 5 | Register system domains in registry | See M2 | `forest_domain_mixin.inc:309–326` |

**Interruption between steps 3 and 4:**
- Registry block is allocated and locked but `hdr->root_offset` is still
  `no_block`.
- **Partial state:** tolerable. On next `load()`,
  `validate_or_bootstrap_forest_registry_unlocked()` detects the missing
  registry and creates a new one.
- **Verify observation:** `ForestRegistryMissing`.
- **Repair action:** `Repaired` — bootstrap fresh registry.

**Interruption between steps 4 and 5:**
- `hdr->root_offset` points to a valid registry, but system domains are
  not yet registered.
- **Partial state:** tolerable. `validate_or_bootstrap_forest_registry_unlocked()`
  re-registers missing system domains.
- **Verify observation:** `ForestDomainMissing`.
- **Repair action:** `Repaired` — register missing domains.

**Context 2: `load()` recovery**

During `load()`, `hdr->root_offset` may be updated if the existing
registry is missing or invalid. The update follows the same bootstrap
sequence. Since `load()` holds a unique write lock, no concurrent
mutations are possible.

### M1b. `ForestDomainRecord::root_offset` — per-domain root

Each domain in the `ForestDomainRegistry` has a `root_offset` field
pointing to the root of that domain's AVL tree.

**Updated by:** `set_domain_root()` (`persist_memory_manager.h:928`).

| Step | Write | Critical? |
|------|-------|-----------|
| 1 | `W(rec->root_offset, new_root.offset())` | **CRITICAL** |

This is a single atomic write to a `uint32_t` field. On most
architectures, a naturally aligned 4-byte write is atomic. However, PMM
does not rely on hardware atomicity — the forest registry is a system
block that persists across saves, and if the write is interrupted:

- **Partial state:** the domain root may be stale (pointing to the
  previous root or containing a partial value).
- **Verify observation:** no direct detection — domain root validity is
  not checked by the current verify pass.
- **Repair:** not automatically repairable. Upper-layer journal (see
  [storage_seams.md](storage_seams.md) Seam 5) is needed for domain-root
  atomicity across crash.

---

## M2. Registry updates

### M2a. Domain registration

Adding a new domain to the `ForestDomainRegistry`.

| Step | Write | Critical? | Code |
|------|-------|-----------|------|
| 1 | `W(rec->binding_id, id)` | **CRITICAL** | `forest_domain_mixin.inc:163` |
| 2 | `W(rec->binding_kind, kind)` | **CRITICAL** | `forest_domain_mixin.inc:164` |
| 3 | `W(rec->root_offset, root)` | **CRITICAL** | `forest_domain_mixin.inc:165` |
| 4 | `W(rec->flags, flags)` | **CRITICAL** | `forest_domain_mixin.inc:166` |
| 5 | `W(rec->symbol_offset, 0)` | NON-CRITICAL | `forest_domain_mixin.inc:167` |
| 6 | `W(reg->domain_count, count + 1)` | **CRITICAL** | `forest_domain_mixin.inc:178` |

**Interruption between steps 1–5 and step 6:**
- Domain record is partially or fully written, but `domain_count` has
  not incremented — the new domain is invisible.
- **Partial state:** tolerable. The partially written record occupies a
  slot that will be overwritten on the next registration attempt.
- **Verify observation:** `ForestDomainMissing` (if it was a required
  system domain).
- **Repair action:** `Repaired` — re-register the missing domain.

**Interruption after step 6:**
- Domain is fully registered.

### M2b. Domain update (existing domain)

Updating an existing domain's fields (e.g., changing root, flags).

| Step | Write | Critical? | Code |
|------|-------|-----------|------|
| 1 | `W(rec->flags, new_flags)` | **CRITICAL** | `forest_domain_mixin.inc:147` |
| 2 | `W(rec->binding_kind, new_kind)` | **CRITICAL** | `forest_domain_mixin.inc:148` |
| 3 | `W(rec->root_offset, new_root)` | **CRITICAL** | `forest_domain_mixin.inc:149` |

Each field update is independent. Partial update leaves a mix of old
and new values.

- **Partial state:** partially updated domain record.
- **Verify observation:** `ForestDomainFlagsMissing` (if system flags
  were being set).
- **Repair action:** `Repaired` — re-apply correct flags on `load()`.

---

## M3. Dictionary / symbol updates

### M3a. Symbol interning (`intern_symbol_unlocked`)

Interning a new symbol in the `system/symbols` pstringview dictionary.

| Step | Write | Critical? | Code |
|------|-------|-----------|------|
| 1 | Allocate block for pstringview | See M4 | `forest_domain_mixin.inc:206` |
| 2 | Initialize pstringview header (length) | **CRITICAL** | `forest_domain_mixin.inc:213` |
| 3 | Copy string content into block | **CRITICAL** | `forest_domain_mixin.inc:214–215` |
| 4 | Initialize AVL node fields | NON-CRITICAL | `forest_domain_mixin.inc:217` |
| 5 | Lock block permanently (`kNodeReadOnly`) | NON-CRITICAL | `forest_domain_mixin.inc:218` |
| 6 | AVL insert into symbol tree (updates tree links and `symbol_domain->root_offset`) | NON-CRITICAL (tree) / **CRITICAL** (root) | `forest_domain_mixin.inc:223–230` |

Note: the AVL tree itself is NON-CRITICAL (rebuilt on `load()`), but the
`symbol_domain->root_offset` update embedded in the `avl_insert()` call
is **CRITICAL** — it is the only persistent pointer to the symbol tree root.

**Interruption between steps 1 and 2:**
- Block is allocated but not initialized as a pstringview.
- **Partial state:** allocated block with garbage content. On `load()`,
  the block remains allocated (`weight > 0, root_offset == own_idx`).
- **Verify observation:** none (block appears as a normal allocated block).
- **Repair:** not automatically repairable — leaked block. The upper
  layer can detect this via `for_each_block()` audit.

**Interruption between steps 3 and 6 (after content copy, before AVL insert):**
- pstringview block is allocated, initialized, and permanently locked,
  but not inserted into the symbol AVL tree.
- **Partial state:** the block is valid and permanently locked but
  unreachable via tree search. On next `load()`,
  `bootstrap_system_symbols_unlocked()` calls `intern_symbol_unlocked()`
  for each system symbol. That function searches only the AVL tree
  (`avl_find()`); it does not scan blocks by content. Since the orphaned
  block is not in the tree, it will not be found — a **new** block is
  allocated for the same string, and the orphaned block remains as an
  **unreachable permanently locked leak**.
- **Verify observation:** none (orphaned block appears as a valid
  allocated block).
- **Repair:** not automatically repairable. The orphaned symbol block
  persists as a small leak. An upper-layer audit via `for_each_block()`
  could detect permanently locked blocks that are not reachable from any
  domain tree. A future recovery scan for orphaned symbol blocks would
  be needed to reclaim them.

**Interruption after step 6:**
- Symbol is fully interned.

### M3b. Bootstrap symbol sequence

During bootstrap, symbols are interned in a fixed order
(`forest_domain_mixin.inc:245–249`):

1. `system/free_tree`
2. `system/symbols`
3. `system/domain_registry`
4. `type/forest_registry`
5. `type/forest_domain_record`
6. `type/pstringview`
7. `service/legacy_root`
8. `service/domain_root`
9. `service/domain_symbol`

This order is deterministic — identical `create()` calls produce
identical symbol layouts (invariant D1d).

---

## M4. Free-tree updates

### M4a. Allocation with splitting

The full write sequence is documented in [atomic_writes.md](atomic_writes.md)
Algorithm 1. The ordering rules relevant to crash consistency:

| Phase | Writes | Critical? | Recovery if interrupted |
|-------|--------|-----------|------------------------|
| 1 | Remove block from AVL | NON-CRITICAL | AVL rebuilt on `load()` |
| 2 | Initialize new split-block header | NON-CRITICAL | New block invisible |
| 3 | Link new block: `W(new->next, old_next)` | **CRITICAL** | See below |
| 4 | Link new block: `W(new->prev, blk_idx)` | **CRITICAL** | See below |
| 5 | Update old next: `W(old_next->prev, new_idx)` | **CRITICAL** | `repair_linked_list()` |
| 6 | Update split block: `W(blk->next, new_idx)` | **CRITICAL** | `repair_linked_list()` |
| 7 | Update `last_block_offset` | NON-CRITICAL | Rebuilt on `load()` |
| 8 | Update counters | NON-CRITICAL | Recomputed on `load()` |
| 9 | Insert new free block into AVL | NON-CRITICAL | AVL rebuilt on `load()` |
| 10 | Mark block allocated: `W(blk->weight, N)` + `W(blk->root_offset, blk_idx)` | **CRITICAL** | `recover_state()` |
| 11 | Clear AVL fields of allocated block | NON-CRITICAL | AVL rebuilt on `load()` |
| 12 | Update counters | NON-CRITICAL | Recomputed on `load()` |

**Critical ordering rule:** steps 3–6 must occur in this order. The
linked-list forward chain (`next_offset`) is the trust anchor. Steps 3–4
prepare the new block's links before it becomes visible. Steps 5–6 splice
the new block into the list.

**Interruption between steps 6 and 10:**
- New block is linked, but the original block is still marked free
  (`weight == 0`).
- **Partial state:** tolerable. On `load()`, the block is treated as
  free and returned to the free list. User data from the partially
  completed allocation is lost.
- **Verify observation:** no violation (block is in a valid free state).

### M4b. Deallocation with coalescing

The full write sequence is documented in [atomic_writes.md](atomic_writes.md)
Algorithm 2.

| Phase | Writes | Critical? | Recovery if interrupted |
|-------|--------|-----------|------------------------|
| 1 | Mark free: `W(blk->weight, 0)` + `W(blk->root_offset, 0)` | **CRITICAL** | `recover_state()` |
| 2 | Update counters | NON-CRITICAL | Recomputed on `load()` |
| 3 | Coalesce with next: remove next from AVL | NON-CRITICAL | AVL rebuilt |
| 4 | Coalesce: `W(blk->next, nxt->next)` | **CRITICAL** | `repair_linked_list()` |
| 5 | Coalesce: `W(nxt_nxt->prev, blk_idx)` | **CRITICAL** | `repair_linked_list()` |
| 6 | Coalesce: `memset(nxt_header, 0)` | **CRITICAL** | Merged area covered by new block |
| 7 | Coalesce with prev: analogous to steps 3–6 | See above | See above |
| 8 | Insert result into AVL | NON-CRITICAL | AVL rebuilt |

**Critical ordering rule:** step 1 must precede all coalescing. The
block must be marked free before any neighbor merging begins. This
ensures that if coalescing is interrupted, the freed block is in a
valid free state.

**Interruption between steps 4 and 5 (forward coalesce):**
- `blk->next_offset` is updated but `nxt_nxt->prev_offset` is stale.
- **Partial state:** linked-list forward/backward inconsistency.
- **Verify observation:** `PrevOffsetMismatch`.
- **Repair action:** `Repaired` by `repair_linked_list()`.

### M4c. Free-tree AVL operations

All AVL insertions and removals (rotations, height updates, parent
pointer updates) are **NON-CRITICAL**. The entire AVL tree is rebuilt
from scratch during `load()` via `rebuild_free_tree()`.

**Ordering rule:** AVL mutations have no ordering requirements relative
to each other. They may be interrupted at any point.

### M4d. `rebuild_free_tree()` sequence

During `load()`, the free-tree is rebuilt in a strict order
(`allocator_policy.h:303–329`):

| Step | Write | Purpose |
|------|-------|---------|
| 1 | `W(hdr->free_tree_root, no_block)` | Reset AVL root |
| 2 | `W(hdr->last_block_offset, no_block)` | Reset last-block tracker |
| 3 | For each block: reset AVL fields (left/right/parent/height) | Clear stale tree links |
| 4 | For each block: `recover_state()` | Fix weight/root_offset inconsistencies |
| 5 | For each free block: `avl_insert()` | Rebuild AVL tree |
| 6 | Track and set `last_block_offset` | Restore last-block pointer |

**Ordering rule:** step 4 (recover_state) must precede step 5
(avl_insert) — a block's free/allocated status must be correct before
it is considered for AVL insertion.

---

## M5. Header / state transitions

### M5a. Counter updates

The four counters (`block_count`, `free_count`, `alloc_count`,
`used_size`) are updated during allocation, deallocation, splitting,
and coalescing. All counter updates are **NON-CRITICAL** because
`recompute_counters()` rebuilds them from scratch on `load()`.

**Ordering rule:** no ordering constraints. Counters may be stale after
any interruption.

### M5b. `first_block_offset`

Set during `init_layout()` (`layout_mixin.inc:32`) and during `expand()`
(`layout_mixin.inc:135`). This is a trust anchor — it is the starting
point of the linked-list traversal.

**Ordering rule:** `first_block_offset` must be written before any block
that depends on it is linked into the list. In practice, this is
guaranteed by the bootstrap sequence (Block_0 is the first block).

### M5c. `magic`

Written once during `init_layout()` (`layout_mixin.inc:30`). Zeroed on
`destroy()` (`persist_memory_manager.h:410`).

`magic` is an **identity / format check**, not a bootstrap-completion
marker. In the current implementation `init_layout()` writes `magic`
**first** (immediately after `memset`-zeroing the header), followed by
`total_size`, `first_block_offset`, and the remaining header fields.
A crash after the `magic` write but before the rest of `init_layout()`
or the full bootstrap sequence completes would leave an image that
passes the magic check yet is only partially initialized.

**Current ordering rule:** `magic` is the first header field written
during `init_layout()` and the first field zeroed during `destroy()`.
On `load()`, `magic` is checked in Phase 1 to reject images that were
never initialized or that have already been destroyed — it does **not**
guarantee that bootstrap ran to completion.

### M5d. `total_size` and `granule_size`

Written during `init_layout()`. Updated during `expand()` (total_size
only). These are trust anchors validated on `load()`.

**Ordering rule:** `total_size` must be updated before any new blocks
are created in the expanded region.

---

## M6. Load repair sequence

The `load()` repair phases have a strict ordering:

```
Phase 1: Validate header (magic, total_size, granule_size)
  │  ↓ fail → Aborted
Phase 2: Reset runtime fields (owns_memory, prev_total_size)
  │
Phase 3: repair_linked_list()
  │  → fixes prev_offset using next_offset as trust anchor
  │
Phase 4: recompute_counters()
  │  → recalculates block_count, free_count, alloc_count, used_size
  │  → depends on correct linked list from Phase 3
  │
Phase 5: rebuild_free_tree()
  │  → resets all AVL fields
  │  → calls recover_state() for each block
  │  → inserts free blocks into AVL
  │  → depends on correct linked list (Phase 3) and counters (Phase 4)
  │
Phase 6: validate_or_bootstrap_forest_registry_unlocked()
  │  → validates or re-creates the forest registry
  │  → registers system domains
  │  → interns system symbols
  │  → depends on working allocator (Phase 5)
  │
Phase 7: validate_bootstrap_invariants_unlocked()
         → final consistency check
```

**Ordering rules:**
- Phase 3 before Phase 4: counters depend on a correct linked list.
- Phase 4 before Phase 5: `rebuild_free_tree()` uses the linked list
  and correct block states.
- Phase 5 before Phase 6: forest registry bootstrap requires a
  functioning allocator (to allocate registry and symbol blocks).
- Phase 7 is always last: validates the entire post-repair state.

---

## Crash-consistency summary

### Trust anchors (not reconstructible)

| Data | Where | Why trusted |
|------|-------|-------------|
| `magic` | ManagerHeader | Image identity; if wrong, image is rejected |
| `total_size` | ManagerHeader | Defines address space boundary |
| `granule_size` | ManagerHeader | Defines addressing granularity |
| `first_block_offset` | ManagerHeader | Start of linked-list traversal |
| `next_offset` chain | Every block header | Forward linked-list traversal; `repair_linked_list()` trusts this |

### Derived data (reconstructed on `load()`)

| Data | Reconstructed by | Source of truth |
|------|-------------------|-----------------|
| `prev_offset` | `repair_linked_list()` | `next_offset` chain |
| `block_count` | `recompute_counters()` | Linked-list walk |
| `free_count` | `recompute_counters()` | Linked-list walk (weight == 0) |
| `alloc_count` | `recompute_counters()` | Linked-list walk (weight > 0) |
| `used_size` | `recompute_counters()` | Linked-list walk |
| `last_block_offset` | `rebuild_free_tree()` | Linked-list walk |
| AVL tree (all fields) | `rebuild_free_tree()` | Free blocks from linked-list walk |
| Block state (root_offset) | `recover_state()` | Determined by `weight` |

### Partial states by operation

| Operation | Interrupted at | Partial state | Tolerable? | Repair |
|-----------|---------------|---------------|------------|--------|
| **Allocate** | Before linking new block | New block invisible | Yes | No repair needed |
| **Allocate** | During linked-list splice | Forward/backward mismatch | Yes | `repair_linked_list()` |
| **Allocate** | After splice, before mark allocated | Block remains free | Yes | Returned to free list |
| **Deallocate** | Before mark free | Block remains allocated | Yes | No action (not freed) |
| **Deallocate** | After mark free, before coalesce | Free block not in AVL | Yes | `rebuild_free_tree()` |
| **Coalesce** | During linked-list update | Forward/backward mismatch | Yes | `repair_linked_list()` |
| **Coalesce** | After list update, before header zero | Old header unreachable | Yes | Covered by merged block |
| **Bootstrap** | Before `root_offset` update | No registry visible | Yes | Bootstrap on `load()` |
| **Bootstrap** | After registry, before domains | Missing system domains | Yes | Re-register on `load()` |
| **Symbol intern** | Before AVL insert | Block allocated but not in tree (orphaned leak) | Yes | Leaked — new block allocated on `load()` |
| **Symbol intern** | After AVL insert | Fully interned | Yes | No repair needed |
| **Save** | Before atomic rename | Old file intact | Yes | `.tmp` file can be deleted |
| **Save** | After atomic rename | New file complete | Yes | No repair needed |

### Corruption states (not tolerable)

| Condition | Detection | Recovery |
|-----------|-----------|----------|
| `magic` overwritten | `load()` Phase 1 | **None** — image rejected |
| `total_size` wrong | `load()` Phase 1 | **None** — image rejected |
| `granule_size` wrong | `load()` Phase 1 | **None** — image rejected |
| `next_offset` corrupted | `repair_linked_list()` walks into invalid memory | **None** — linked-list traversal terminates |
| `first_block_offset` wrong | `load()` starts at wrong location | **None** — entire linked-list traversal is wrong |
| Block header in the middle of user data | Not detected | **None** — structural assumption violated |

---

## Ordering rules summary

| Rule | Description | Rationale |
|------|-------------|-----------|
| **O1** | New block links (`next`, `prev`) must be set before splicing into list | Prevents dangling pointers in linked list |
| **O2** | `blk->weight` = 0 must precede coalescing with neighbors | Block must be in free state before merging |
| **O3** | `recover_state()` must precede `avl_insert()` during rebuild | Block state must be correct before tree insertion |
| **O4** | `repair_linked_list()` before `recompute_counters()` | Counters depend on correct list traversal |
| **O5** | `recompute_counters()` before `rebuild_free_tree()` | Free-tree rebuild depends on correct block states |
| **O6** | `rebuild_free_tree()` before forest registry bootstrap | Bootstrap allocates blocks via the free-tree |
| **O7** | Trust anchors must be written before dependent data | `first_block_offset` before blocks; `total_size` before expansion |
| **O8** | `magic` is the first header write during `init_layout()` and first zeroed on `destroy()` | Identity / format check — does not signal bootstrap completion |
| **O9** | Atomic rename is the last step in `save_manager()` | File integrity — old file is never partially overwritten |
