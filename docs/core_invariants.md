# Core Invariants of PersistMemoryManager

## Document status

This is the **single canonical document** listing all core invariants of PMM
after issues 01–07. It replaces scattered invariant descriptions across other
documents and serves as the frozen invariant set on which issues 08–10 build.

For each invariant the document provides:

- a concise statement;
- a link to the code checkpoint (assertion, validation, or runtime guard);
- a link to the test(s) that exercise the invariant.

Related documents:

- architectural model: [pmm_avl_forest.md](pmm_avl_forest.md)
- field semantics: [block_and_treenode_semantics.md](block_and_treenode_semantics.md)
- low-level layout: [architecture.md](architecture.md)
- bootstrap sequence: [bootstrap.md](bootstrap.md)
- free-tree policy: [free_tree_forest_policy.md](free_tree_forest_policy.md)
- recovery: [recovery.md](recovery.md)

---

## A. PMM Model Boundary

PMM is a **persistent address space manager / storage kernel**.
It manages a linear persistent address space (PAP) and an intrusive AVL-forest
over it. PMM does not know `pjson` semantics or any upper-layer schema.

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| A1 | PMM = persistent address space manager; the only first-class abstractions are blocks, granule addressing, persistent pointers, and intrusive AVL-forest. | Enforced by API surface: `persist_memory_manager.h` exposes only allocator, container, and forest primitives. | All tests operate through the PMM API without upper-layer types. |
| A2 | PMM does not interpret user-data payload. Block contents are opaque to the storage kernel. | `load()` restores structure (linked list, AVL tree, counters) but never inspects user data (`allocator_policy.h:303–399`). | `test_block_modernization.cpp` — "save_load_new_format" verifies data survives round-trip without kernel interpretation. |
| A3 | AVL-forest is a first-class abstraction. The free-tree is the primary system domain; `pstringview`, `pmap`, and user domains are additional forest domains sharing the same AVL substrate. | `avl_tree_mixin.h` — shared AVL operations parameterized by `PPtr`/`IndexType`. `BlockPPtr` adapter enables free-tree to reuse the same substrate. | `test_issue243_free_tree_policy.cpp`, `test_avl_tree_view.cpp`, `test_issue153_pmap.cpp`, `test_issue151_pstringview.cpp`. |

---

## B. Block and TreeNode Semantics

Each block is an atom of the linear PAP **and** an atom of the intrusive forest.
`Block<A>` carries the physical layer; `TreeNode<A>` carries the forest-slot.

### B1. Linear neighbors

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B1a | `prev_offset` is the granule index of the previous physical block in PAP. It must not be overloaded with forest/meta/type semantics. | `repair_linked_list()` in `allocator_policy.h:340–355` — repairs `prev_offset` by walking `next_offset` chain. `verify_linked_list()` in `allocator_policy.h:413–434` detects mismatches. | `test_issue245_verify_repair.cpp` — "verify detects prev_offset mismatch". |
| B1b | `next_offset` is the granule index of the next physical block. It defines the block's physical extent (`block_size = next_offset - block_index`). | Used by `rebuild_free_tree()` (`allocator_policy.h:303–329`), coalescing, and splitting logic. | `test_coalesce.cpp` — all coalescing tests rely on correct `next_offset` chain. |

### B2. Intrusive tree links

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B2a | `left_offset`, `right_offset`, `parent_offset` are AVL structural links within the current tree-slot. They have no relation to physical block position. | `avl_tree_mixin.h` — all rotations and rebalancing operate exclusively through these fields. `avl_init_node()` sets them to `no_block`. | `test_avl_tree_view.cpp`, `test_issue243_free_tree_policy.cpp`. |
| B2b | `avl_height` is the structural AVL subtree height. `avl_height == 0` means the slot is not participating in any tree. | `avl_update_height()` in `avl_tree_mixin.h`. `rebuild_free_tree()` resets all AVL fields before reinsertion. | `test_block_state.cpp` — "insert_to_avl" verifies `avl_height=1` after insertion. |

### B3. Weight

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B3a | `weight` is a universal `index_type` key/scalar whose semantics are determined by the owning forest domain. | Domain-specific interpretation: free-tree uses `weight` as state discriminator; `pmap`/`pstringview` use it as sort key. | `test_issue243_free_tree_policy.cpp`, `test_issue153_pmap.cpp`. |
| B3b | In the free-tree domain: `weight == 0` means free block; `weight > 0` means allocated block (user data granule count). | `BlockStateBase::is_free()` and `is_allocated()` in `block_state.h`. `FreeBlock::cast_from_raw()` asserts `weight == 0` (`block_state.h:406`). | `test_block_state.cpp` — "is_free / is_allocated", "FreeBlock cast_from_raw and verify_invariants". |

### B4. Root offset

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B4a | `root_offset` is the owner-domain / owner-tree marker of the intrusive tree-slot. | `BlockStateBase::verify_state()` in `block_state.h:183–197` checks consistency with `weight`. | `test_issue245_verify_repair.cpp` — "verify detects block state inconsistency". |
| B4b | Free block: `root_offset == 0`. Allocated block: `root_offset == own_granule_index`. | `FreeBlock::verify_invariants()` (`block_state.h:428`), `AllocatedBlock::verify_invariants()` (`block_state.h:637`). `recover_state()` fixes violations (`block_state.h:162–171`). | `test_block_state.cpp` — "FreeBlock cast_from_raw and verify_invariants", "AllocatedBlock cast_from_raw and verify_invariants", "recover_block_state". |

### B5. Node type

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B5a | `node_type` is a coarse-grained type/mode marker. Currently: `kNodeReadWrite` (0) and `kNodeReadOnly` (1). | `lock_block_permanent()` sets `kNodeReadOnly`. Deallocation checks `node_type` — permanently locked blocks cannot be freed. | `test_issue241_bootstrap.cpp` — bootstrap symbols are permanently locked. `test_issue151_pstringview.cpp` — interned strings are locked. |

### B6. One intrusive tree-slot per block

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B6a | Each block has exactly one set of tree fields (`left_offset`, `right_offset`, `parent_offset`, `root_offset`, `avl_height`, `weight`, `node_type`). A block cannot simultaneously belong to two different AVL trees. | Enforced by `Block<A>` layout: `sizeof(Block<DefaultAddressTraits>) == 32` (`block.h:78`). | `test_block_state.cpp` — "BlockStateBase size" (32 bytes). |

### B7. Block header size

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B7a | `Block<DefaultAddressTraits>` = 32 bytes = 2 granules. This size must not increase. | `static_assert(sizeof(Block<DefaultAddressTraits>) == 32)` in `block.h:78`. `static_assert` in `types.h:152–158` and `block_state.h:369–372`. | `test_block_state.cpp` — "BlockStateBase size", "All states same size". |

---

## C. Forest / Domain Model

### C1. System domains

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| C1a | After `create()`, three system domains must exist: `system/free_tree`, `system/symbols`, `system/domain_registry`. | `validate_bootstrap_invariants_unlocked()` in `forest_domain_mixin.inc:343–393` — checks at least 3 domains with `kForestDomainFlagSystem`. | `test_issue241_bootstrap.cpp` — "bootstrap invariants hold after create(size)". `test_forest_registry.cpp` — "forest registry bootstraps system domains". |
| C1b | All system domains must have the `kForestDomainFlagSystem` flag set. | `validate_bootstrap_invariants_unlocked()` — flag check. `verify_forest_registry_unlocked()` in `verify_repair_mixin.inc:64–95`. | `test_issue245_verify_repair.cpp` — "verify detects missing system domain flags". |

### C2. Named roots and persistent registry

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| C2a | The `ForestDomainRegistry` is a persistent locked block containing up to 32 domain slots. Its granule index is stored in `ManagerHeader::root_offset`. | `bootstrap_forest_registry_unlocked()` allocates and locks the registry. `validate_bootstrap_invariants_unlocked()` checks `hdr->root_offset` matches the registry domain root. | `test_issue241_bootstrap.cpp` — "bootstrap invariants hold after save/load". `test_forest_registry.cpp` — "forest registry persists user domains and legacy root". |
| C2b | `ForestDomainRegistry` has magic `0x50465247` ("PFRG") and version 1. | `validate_or_bootstrap_forest_registry_unlocked()` in `forest_domain_mixin.inc:397–451` validates magic and version. `verify_forest_registry_unlocked()` in `verify_repair_mixin.inc:64–95`. | `test_issue245_verify_repair.cpp` — "verify detects forest registry corruption". |

### C3. Symbol dictionary

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| C3a | The symbol dictionary is a `pstringview` AVL tree registered as the `system/symbols` domain. All system domain names are interned as permanently locked blocks (`kNodeReadOnly`). | `bootstrap_system_symbols_unlocked()` in `forest_domain_mixin.inc`. `validate_bootstrap_invariants_unlocked()` checks non-zero symbol dictionary root and non-zero `symbol_offset` for each system domain. | `test_issue241_bootstrap.cpp` — checks system symbol names are discoverable. `test_forest_registry.cpp` — symbols survive save/load. |

---

## D. Bootstrap Model

### D1. What must exist after `create()`

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| D1a | `ManagerHeader` (Block_0) with valid magic, `total_size`, `granule_size`, and `root_offset` pointing to the forest registry. | `init_layout()` in `layout_mixin.inc`. `validate_bootstrap_invariants_unlocked()` verifies all header fields. | `test_issue241_bootstrap.cpp` — "bootstrap invariants hold after create(size)". |
| D1b | At least one free block (Block_1) spanning remaining space, inserted into the free-tree. | After `init_layout()`, Block_1 is the free-tree root. | `test_allocate.cpp` — "create_basic". |
| D1c | Forest domain registry, three system domains, and symbol dictionary — all as permanently locked blocks. | `bootstrap_forest_registry_unlocked()` + `bootstrap_system_symbols_unlocked()` + `validate_bootstrap_invariants_unlocked()`. | `test_issue241_bootstrap.cpp`, `test_forest_registry.cpp`. |
| D1d | Bootstrap is deterministic: identical `create(N)` calls produce identical block layouts, binding IDs, and symbol offsets. | Enforced by sequential bootstrap order. | `test_issue241_bootstrap.cpp` — "bootstrap is deterministic". |

### D2. How state is restored after `load()`

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| D2a | `load()` performs a 5-phase recovery: (1) validate header, (2) reset runtime fields, (3) repair linked list, (4) recompute counters, (5) rebuild free-tree. | `verify_repair_mixin.inc:26–41` (header), `allocator_policy.h:340–399` (phases 3–4), `allocator_policy.h:303–329` (phase 5). | `test_issue245_verify_repair.cpp` — "free-tree stale detected after save/load round-trip". `test_block_modernization.cpp` — "save_load_new_format". |
| D2b | After `load()`, `validate_or_bootstrap_forest_registry_unlocked()` restores or creates the forest registry and re-registers system domains. | `forest_domain_mixin.inc:397–451`. | `test_issue241_bootstrap.cpp` — "bootstrap invariants hold after save/load". |

---

## E. Free-Tree Policy

### E1. Ordering

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| E1a | Free-tree sort key: `(block_size_in_granules, block_index)` — strict total ordering. Primary: block size (smaller left). Tie-breaker: block index (lower left). | `AvlFreeTree::compare()` in `free_block_tree.h`. | `test_issue243_free_tree_policy.cpp` — "same-size blocks ordered by block_index". |
| E1b | Block size is a **derived key** computed from linear PAP geometry (`next_offset - block_index`), not stored in `weight`. | `AvlFreeTree::block_size()` computes size from `next_offset` chain. | `test_issue243_free_tree_policy.cpp`. |

### E2. Weight role in free-tree

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| E2a | In the free-tree domain, `weight` is a state discriminator (0 = free, >0 = allocated), not a sort key. | `is_free()` checks `weight == 0 && root_offset == 0`. Free-tree ordering uses derived block size, not `weight`. | `test_block_state.cpp` — "is_free / is_allocated". `test_issue243_free_tree_policy.cpp`. |

### E3. Best-fit search

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| E3a | `find_best_fit()` returns the smallest free block with `block_size >= needed_granules`. O(log n). | `AvlFreeTree::find_best_fit()` in `free_block_tree.h`. | `test_issue243_free_tree_policy.cpp` — "find_best_fit selects minimum fitting block". |

### E4. Free-tree root storage

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| E4a | Free-tree root is stored in `ManagerHeader::free_tree_root`. It is rebuilt from scratch on `load()`. | `rebuild_free_tree()` in `allocator_policy.h:303–329` resets and rebuilds. | `test_issue245_verify_repair.cpp` — "free-tree stale detected after save/load round-trip". |

---

## F. Verify / Repair Contract

### F1. Verify does not modify

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| F1a | `verify()` is a read-only diagnostic pass. It reports violations via `VerifyResult` but never modifies the image. | `verify()` in `verify_repair_mixin.inc` calls `verify_linked_list()`, `verify_counters()`, `verify_block_states()`, `verify_free_tree()`, `verify_forest_registry_unlocked()` — all read-only. | `test_issue245_verify_repair.cpp` — "verify does not modify the image". |
| F1b | `VerifyResult` reports up to 64 diagnostic entries with `ViolationType`, affected block index, expected/actual values, and `DiagnosticAction`. | `diagnostics.h:74–105` — `VerifyResult` struct. | `test_issue245_verify_repair.cpp` — "DiagnosticEntry fields populated". |

### F2. Repair does not masquerade as verify/load

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| F2a | Repair is performed only during `load()` and is explicitly documented in `VerifyResult` with `DiagnosticAction::Repaired` or `DiagnosticAction::Rebuilt`. | `load(VerifyResult&)` in `verify_repair_mixin.inc` runs repair phases and records each action. | `test_issue245_verify_repair.cpp` — "load(VerifyResult&) reports repairs directly", "Repaired vs Rebuilt distinction". |
| F2b | Unrecoverable corruption (invalid magic, wrong granule size) is reported as `DiagnosticAction::Aborted`. Load returns failure. | `verify_repair_mixin.inc:26–41` — header validation. | `test_issue245_verify_repair.cpp` — "Aborted action on header corruption". |

### F3. Corruption is diagnosed, not masked

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| F3a | Every repair action is recorded in `VerifyResult`. No silent fixes. | All repair methods in `allocator_policy.h` and `verify_repair_mixin.inc` add entries to `VerifyResult`. | `test_issue245_verify_repair.cpp` — all repair tests check `VerifyResult` entries. |
| F3b | `ViolationType` enum exhaustively covers: `BlockStateInconsistent`, `PrevOffsetMismatch`, `CounterMismatch`, `FreeTreeStale`, `ForestRegistryMissing`, `ForestDomainMissing`, `ForestDomainFlagsMissing`, `HeaderCorruption`. | `diagnostics.h:35–47`. | `test_issue245_verify_repair.cpp` — tests for each violation type. |

---

## Compile-time structural invariants

These are enforced by `static_assert` and are always checked at build time:

| ID | Invariant | Code checkpoint |
|----|-----------|-----------------|
| S1 | `sizeof(Block<DefaultAddressTraits>) == 32` | `block.h:78`, `types.h:152–158`, `block_state.h:369–372` |
| S2 | `sizeof(ManagerHeader<DefaultAddressTraits>) == 64`, granule-aligned | `types.h:214,216` |
| S3 | `TreeNode<DefaultAddressTraits>` size == 5 × `sizeof(uint32_t)` + 4 | `types.h:163` |
| S4 | `kGranuleSize` is power of 2, equals `DefaultAddressTraits::granule_size` | `types.h:62–63` |
| S5 | `BlockStateBase<AT>` is binary-compatible with `Block<AT>` | `block_state.h:369–372` |
| S6 | `ForestDomainRecord` is trivially copyable; `ForestDomainRegistry` is nothrow-default-constructible | `forest_registry.h:93–96` |
| S7 | Address traits: `IndexT` unsigned, `GranuleSz >= 4`, `GranuleSz` power of 2 | `address_traits.h:63–65` |

---

## Block state machine summary

```
┌───────────────────────┐                    ┌───────────────────────┐
│      FreeBlock        │                    │   AllocatedBlock      │
│  weight == 0          │ ── allocate() ──>  │  weight > 0           │
│  root_offset == 0     │                    │  root_offset == own   │
│  in AVL tree          │ <── deallocate() ─ │  not in AVL tree      │
└───────────────────────┘                    └───────────────────────┘
```

Forbidden states (must never appear in a persisted image):

| `weight` | `root_offset` | Status |
|----------|---------------|--------|
| 0 | != 0 | **Never valid** — `recover_state()` sets `root_offset = 0` |
| > 0 | 0 | **Never valid** — `recover_state()` sets `root_offset = own_idx` |
| > 0 | != own_idx | **Never valid** — `recover_state()` sets `root_offset = own_idx` |

Code: `BlockStateBase::recover_state()` (`block_state.h:162–171`).
Test: `test_block_state.cpp` — "recover_block_state".
