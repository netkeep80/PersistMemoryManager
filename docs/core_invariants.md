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
- block-header field semantics: [block_and_treenode_semantics.md](block_and_treenode_semantics.md)
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
| A3 | AVL-forest is a first-class abstraction. The free-tree is the primary system domain; [pstringview](../include/pmm/pstringview.h#pmm-pstringview), [pmap](../include/pmm/pmap.h#pmm-pmap), and user domains are additional forest domains sharing the same AVL substrate. | `avl_tree_mixin.h` — shared AVL operations parameterized by `PPtr`/`IndexType`. [BlockPPtr](../include/pmm/avl_tree_mixin.h#pmm-detail-blockpptr) adapter enables free-tree to reuse the same substrate. | `test_issue243_free_tree_policy.cpp`, `test_avl_tree_view.cpp`, `test_issue153_pmap.cpp`, `test_issue151_pstringview.cpp`. |

---

## B. Block Header Semantics

Each block is an atom of the linear PAP **and** an atom of the intrusive forest.
`BlockHeader<A>` is the only physical block-header layout; its prefix
(fields `weight`, `left_offset`, `right_offset`, `parent_offset`, `avl_height`)
is the universal intrusive AVL slot. `Block<A>` is a type alias for `BlockHeader<A>`.
`BlockStateBase<A>` is **not** storage; it is a static typed access/helper layer
over `BlockHeader<A>`. The state classes (`FreeBlock`, `AllocatedBlock`,
`FreeBlockRemovedAVL`, `FreeBlockNotInAVL`, `SplittingBlock`, `CoalescingBlock`)
are non-owning typed views over `BlockHeader<A>&`.

### B1. Linear neighbors

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B1a | `prev_offset` is the granule index of the previous physical block in PAP. It must not be overloaded with forest/meta/type semantics. | `repair_linked_list()` in `allocator_policy.h:340–355` — repairs `prev_offset` by walking `next_offset` chain. `verify_linked_list()` in `allocator_policy.h:413–434` detects mismatches. | `test_issue245_verify_repair.cpp` — "verify detects prev_offset mismatch". |
| B1b | `next_offset` is the granule index of the next physical block. It defines the block's physical extent (`block_size = next_offset - block_index`). | Used by `rebuild_free_tree()` (`allocator_policy.h:303–329`), coalescing, and splitting logic. | `test_coalesce.cpp` — all coalescing tests rely on correct `next_offset` chain. |

### B2. Intrusive tree links

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B2a | `left_offset`, `right_offset`, `parent_offset` are AVL structural links within the current tree-slot. They have no relation to physical block position. | `avl_tree_mixin.h` — all rotations and rebalancing operate exclusively through these fields. `avl_init_node()` sets them to `no_block`. | `test_avl_tree_view.cpp`, `test_issue243_free_tree_policy.cpp`. |
| B2b | `avl_height` is the structural AVL subtree height. `avl_height == 0` means the slot is not participating in any tree. | `avl_update_height()` in `avl_tree_mixin.h`. `rebuild_free_tree()` resets all AVL fields before reinsertion. | `test_block_header.cpp` — "AllocatedBlock -> FreeBlockNotInAVL -> FreeBlock transitions" verifies `avl_height=1` after `insert_to_avl()`. |

### B3. Weight (cached block size)

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B3a | `weight` is the cached size of the block in granules. For a free block (`NodeType::Free`) — total block size **including** the header. For an allocated block — payload size in granules. The free-tree keys nodes by this cached value and never recomputes it from `next_offset - own_idx` in the normal path. | [AllocatorPolicy](../include/pmm/allocator_policy.h#pmm-allocatorpolicy) keeps `weight` synchronized in every split/coalesce/extend/shrink/free/allocate path. [AvlFreeTree](../include/pmm/free_block_tree.h#pmm-avlfreetree) reads `weight` directly via `BlockState::get_weight`. | `test_block_header.cpp` — `AllocatedBlock -> FreeBlockNotInAVL -> FreeBlock transitions`, `CoalescingBlock merges with a free next neighbour`. |
| B3b | State of a block is determined exclusively by `node_type` — never by `weight == 0` / `weight > 0`. The allocator and the free-tree consult only the `is_free` / `is_allocated` / `is_mutable` / `can_be_deleted_from_pap` / `participates_in_free_tree` helpers. | [BlockStateBase::is_free_raw](../include/pmm/block_state.h#pmm-blockstatebase) reads `pmm::is_free(node_type)`. [FreeBlock::cast_from_raw](../include/pmm/block_state.h#pmm-freeblock-cast_from_raw) asserts `is_free(node_type)`. | `test_block_header.cpp` — `detect_block_state and recover_block_state behave consistently`. |
| B3c | Verify-mode cross-checks every free block's cached `weight` against the physical span derived from neighbours. A divergence is reported as `BlockStateInconsistent` so a corrupted cache cannot stay invisible behind the AVL ordering invariant. The split helpers `physical_block_total_granules<AT>` (computed) and `cached_block_total_granules<AT>` (read) make this distinction explicit. | [verify_block_states](../include/pmm/allocator_policy.h#pmm-allocatorpolicy) compares `cached` vs `physical_block_total_granules`. | `test_issue369_review_fixes.cpp` — `verify reports a free block whose cached weight does not match the physical span`. |

### B4. Root offset

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B4a | `root_offset` is the owner-domain / owner-tree marker of the intrusive tree-slot. | [BlockStateBase::verify_state()](../include/pmm/block_state.h#pmm-blockstatebase-verify_state) checks consistency with `weight`. | `test_issue245_verify_repair.cpp` — "verify detects block state inconsistency". |
| B4b | Free block: `root_offset == 0`. Allocated block: `root_offset == own_granule_index`. | [FreeBlock::verify_invariants()](../include/pmm/block_state.h#pmm-freeblock-verify_invariants), [AllocatedBlock::verify_invariants()](../include/pmm/block_state.h#pmm-allocatedblock-verify_invariants). [BlockStateBase::recover_state()](../include/pmm/block_state.h#pmm-blockstatebase-recover_state) fixes violations. | `test_block_header.cpp` — `FreeBlock -> FreeBlockRemovedAVL -> AllocatedBlock transitions`, `AllocatedBlock -> FreeBlockNotInAVL -> FreeBlock transitions`, `detect_block_state and recover_block_state behave consistently`. |

### B5. Node type

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B5a | `node_type` is a strongly typed `enum class NodeType : std::uint8_t` and the single source of truth for a block's high-level state and mutability properties. The current enumerators are `Free`, `ManagerHeader`, `Generic`, `ReadOnlyLocked`, `PStringView`, `PString`, `PArray`, `PMap`, `PPtr`. The list is extensible — adding a new persistent object type only requires registering its properties in the `is_*` helpers and providing a [node_type_for\<T\>](../include/pmm/block_header.h#pmm-nodetype-for) specialization. | [NodeType](../include/pmm/block_header.h#pmm-nodetype) enum and [helpers](../include/pmm/block_header.h#pmm-nodetype-helpers) in `block_header.h`. `lock_block_permanent()` sets `NodeType::ReadOnlyLocked`. Deallocation goes through `pmm::can_be_deleted_from_pap(node_type)`. | `test_issue241_bootstrap.cpp` — bootstrap symbols are permanently locked. `test_issue151_pstringview.cpp` — interned strings are locked. |
| B5b | `is_allocated(NodeType)` is a closed-world `switch` over every known enum value. A corrupted byte in `node_type` is neither `is_free` nor `is_allocated` and cannot pass any block-state guard. `NodeType::Free` is **not** user/PAP-deletable; `deallocate()` requires `is_allocated(nt) && can_be_deleted_from_pap(nt)`. | [is_allocated](../include/pmm/block_header.h#pmm-nodetype-helpers), [can_be_deleted_from_pap](../include/pmm/block_header.h#pmm-nodetype-helpers), and the guard in [deallocate_unlocked](../include/pmm/persist_memory_manager.h). | `test_issue369_review_fixes.cpp` — `is_allocated rejects an unknown NodeType enum value`, `deallocate() of a Free-typed block is rejected even if it leaks past helpers`. |
| B5c | Typed allocation paths (`allocate_typed<T>`, `create_typed<T>`, `reallocate_typed<T>`) stamp every block with the logical kind from [node_type_for\<T\>](../include/pmm/block_header.h#pmm-nodetype-for). A `pstring`/`pstringview`/`parray`/`pmap`/`pptr` block carries its own `NodeType`, not the generic `NodeType::Generic`. | [PersistMemoryTypedApi](../include/pmm/typed_manager_api.h) calls `assign_node_type_for<T>` after every typed allocation. | `test_issue369_review_fixes.cpp` — `allocate_typed<T> tags the block with node_type_for_v<T>`, `create_typed<T> tags the block with node_type_for_v<T>`. |

### B6. One intrusive tree-slot per block

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B6a | Each block has exactly one set of tree fields (`left_offset`, `right_offset`, `parent_offset`, `root_offset`, `avl_height`, `weight`, `node_type`). A block cannot simultaneously belong to two different AVL trees. `avl_height` is `std::uint8_t` and `node_type` is `NodeType` (`std::uint8_t`); both live at the very end of `BlockHeader<AT>` so future single-byte fields can be packed next to them without moving existing offsets. | Enforced by `BlockHeader<A>` layout: `sizeof(BlockHeader<DefaultAddressTraits>) == 32`, `offsetof(H, avl_height) == sizeof(H) - 2`, `offsetof(H, node_type) == sizeof(H) - 1`. | `test_block_header.cpp` — "BlockHeader\<DefaultAddressTraits\> is standard-layout and trivially-copyable" / "field offsets match the binary contract" / "avl_height/node_type are compact and at the very end". |

### B7. Block header size

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| B7a | `BlockHeader<DefaultAddressTraits>` = 32 bytes = 2 granules. This size must not increase. | `static_assert(sizeof(BlockHeader<DefaultAddressTraits>) == 32)` in `block_header.h`. `Block<AT>` is a type alias for `BlockHeader<AT>`. | `test_block_header.cpp` — "BlockHeader\<DefaultAddressTraits\> is standard-layout and trivially-copyable", "BlockHeader\<DefaultAddressTraits\> field offsets match the binary contract". |

---

## C. Forest / Domain Model

### C1. System domains

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| C1a | After `create()`, three system domains must exist: `system/free_tree`, `system/symbols`, `system/domain_registry`. | `validate_bootstrap_invariants_unlocked()` checks required system domains and `kForestDomainFlagSystem`. | `test_issue241_bootstrap.cpp` — "bootstrap invariants hold after create(size)". `test_forest_registry.cpp` — "forest registry bootstraps system domains". |
| C1b | All system domains must have the `kForestDomainFlagSystem` flag set. | `validate_bootstrap_invariants_unlocked()` and `verify_forest_registry_unlocked()` check system-domain flags. | `test_issue245_verify_repair.cpp` — "verify detects missing system domain flags". |

### C2. Named roots and persistent registry

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| C2a | The [ForestDomainRegistry](../include/pmm/forest_registry.h#pmm-detail-forestdomainregistry) is a persistent locked block containing up to 32 domain slots. Its granule index is stored in `ManagerHeader::root_offset`. | `bootstrap_forest_registry_unlocked()` allocates and locks the registry. `validate_bootstrap_invariants_unlocked()` checks `hdr->root_offset` matches the registry domain root. | `test_issue241_bootstrap.cpp` — "bootstrap invariants hold after save/load". `test_forest_registry.cpp` — "forest registry persists user domains and root". |
| C2b | [ForestDomainRegistry](../include/pmm/forest_registry.h#pmm-detail-forestdomainregistry) has magic `0x50465247` ("PFRG") and version 1. | `validate_or_bootstrap_forest_registry_unlocked()` validates magic/version; `verify_forest_registry_unlocked()` reports registry corruption. | `test_issue245_verify_repair.cpp` — "verify detects forest registry corruption". |

### C3. Symbol dictionary

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| C3a | The symbol dictionary is a [pstringview](../include/pmm/pstringview.h#pmm-pstringview) AVL tree registered as the `system/symbols` domain. All system domain names are interned as permanently locked blocks (`kNodeReadOnly`). | `bootstrap_system_symbols_unlocked()` interns system names. `validate_bootstrap_invariants_unlocked()` checks non-zero symbol dictionary root and non-zero `symbol_offset` for each system domain. | `test_issue241_bootstrap.cpp` — checks system symbol names are discoverable. `test_forest_registry.cpp` — symbols survive save/load. |

---

## D. Bootstrap Model

### D1. What must exist after `create()`

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| D1a | [ManagerHeader](../include/pmm/types.h#pmm-detail-managerheader) (Block_0) with valid magic, `total_size`, `granule_size`, and `root_offset` pointing to the forest registry. | `init_layout()` writes header fields; `validate_bootstrap_invariants_unlocked()` verifies them. | `test_issue241_bootstrap.cpp` — "bootstrap invariants hold after create(size)". |
| D1b | At least one free block (Block_1) spanning remaining space, inserted into the free-tree. | After `init_layout()`, Block_1 is the free-tree root. | `test_allocate.cpp` — "create_basic". |
| D1c | Forest domain registry, three system domains, and symbol dictionary — all as permanently locked blocks. | `bootstrap_forest_registry_unlocked()` + `bootstrap_system_symbols_unlocked()` + `validate_bootstrap_invariants_unlocked()`. | `test_issue241_bootstrap.cpp`, `test_forest_registry.cpp`. |
| D1d | Bootstrap is deterministic: identical `create(N)` calls produce identical block layouts, binding IDs, and symbol offsets. | Enforced by sequential bootstrap order. | `test_issue241_bootstrap.cpp` — "bootstrap is deterministic". |

### D2. How state is restored after `load()`

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| D2a | `load()` performs a 5-phase recovery: (1) validate header, (2) reset runtime fields, (3) repair linked list, (4) recompute counters, (5) rebuild free-tree. | `load(VerifyResult&)` validates the header; `repair_linked_list()`, `recompute_counters()`, and `rebuild_free_tree()` run phases 3-5. | `test_issue245_verify_repair.cpp` — "free-tree stale detected after save/load round-trip". `test_block_modernization.cpp` — "save_load_new_format". |
| D2b | After `load()`, `validate_or_bootstrap_forest_registry_unlocked()` restores or creates the forest registry and re-registers system domains. | `validate_or_bootstrap_forest_registry_unlocked()`. | `test_issue241_bootstrap.cpp` — "bootstrap invariants hold after save/load". |

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
| E2a | In the free-tree domain, `weight` is a state discriminator (0 = free, >0 = allocated), not a sort key. | `is_free()` checks `weight == 0 && root_offset == 0`. Free-tree ordering uses derived block size, not `weight`. | `test_block_header.cpp` — `detect_block_state and recover_block_state behave consistently`. `test_issue243_free_tree_policy.cpp`. |

### E3. Best-fit search

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| E3a | `find_best_fit()` returns the smallest free block with `block_size >= needed_granules`. O(log n). | [AvlFreeTree::find_best_fit()](../include/pmm/free_block_tree.h#pmm-avlfreetree-find_best_fit) in `free_block_tree.h`. | `test_issue243_free_tree_policy.cpp` — "find_best_fit selects minimum fitting block". |

### E4. Free-tree root storage

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| E4a | Free-tree root is stored in `ManagerHeader::free_tree_root`. It is rebuilt from scratch on `load()`. | `rebuild_free_tree()` in `allocator_policy.h:303–329` resets and rebuilds. | `test_issue245_verify_repair.cpp` — "free-tree stale detected after save/load round-trip". |

---

## F. Verify / Repair Contract

### F1. Verify does not modify

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| F1a | `verify()` is a read-only diagnostic pass. It reports violations via [VerifyResult](../include/pmm/diagnostics.h#pmm-verifyresult) but never modifies the image. | `verify()` calls `verify_image_unlocked()`, including linked-list, counter, block-state, free-tree, and forest-registry checks — all read-only. | `test_issue245_verify_repair.cpp` — "verify does not modify the image". |
| F1b | [VerifyResult](../include/pmm/diagnostics.h#pmm-verifyresult) reports up to 64 diagnostic entries with [ViolationType](../include/pmm/diagnostics.h#pmm-violationtype), affected block index, expected/actual values, and [DiagnosticAction](../include/pmm/diagnostics.h#pmm-diagnosticaction). | `diagnostics.h:74–105` — [VerifyResult](../include/pmm/diagnostics.h#pmm-verifyresult) struct. | `test_issue245_verify_repair.cpp` — "DiagnosticEntry fields populated". |

### F2. Repair does not masquerade as verify/load

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| F2a | Repair is performed only during `load()` and is explicitly documented in [VerifyResult](../include/pmm/diagnostics.h#pmm-verifyresult) with `DiagnosticAction::Repaired` or `DiagnosticAction::Rebuilt`. | `load(VerifyResult&)` runs repair phases and records each action. | `test_issue245_verify_repair.cpp` — "load(VerifyResult&) reports repairs directly", "Repaired vs Rebuilt distinction". |
| F2b | Unrecoverable corruption (invalid magic, wrong granule size) is reported as `DiagnosticAction::Aborted`. Load returns failure. | `load(VerifyResult&)` header validation records `DiagnosticAction::Aborted`. | `test_issue245_verify_repair.cpp` — "Aborted action on header corruption". |

### F3. Corruption is diagnosed, not masked

| ID | Invariant | Code checkpoint | Test |
|----|-----------|-----------------|------|
| F3a | Every repair action is recorded in [VerifyResult](../include/pmm/diagnostics.h#pmm-verifyresult). No silent fixes. | Repair methods (`repair_linked_list()`, `recompute_counters()`, `rebuild_free_tree()`, forest-registry validation) add entries to [VerifyResult](../include/pmm/diagnostics.h#pmm-verifyresult). | `test_issue245_verify_repair.cpp` — all repair tests check [VerifyResult](../include/pmm/diagnostics.h#pmm-verifyresult) entries. |
| F3b | [ViolationType](../include/pmm/diagnostics.h#pmm-violationtype) enum exhaustively covers: `BlockStateInconsistent`, `PrevOffsetMismatch`, `CounterMismatch`, `FreeTreeStale`, `ForestRegistryMissing`, `ForestDomainMissing`, `ForestDomainFlagsMissing`, `HeaderCorruption`. | `diagnostics.h:35–47`. | `test_issue245_verify_repair.cpp` — tests for each violation type. |

---

## Compile-time structural invariants

These are enforced by `static_assert` and are always checked at build time:

| ID | Invariant | Code checkpoint |
|----|-----------|-----------------|
| S1 | `sizeof(BlockHeader<DefaultAddressTraits>) == 32` | `block_header.h` (`static_assert(sizeof(BlockHeader<DefaultAddressTraits>) == 32)`); `block.h` re-asserts the size on the `Block<AT>` alias |
| S2 | `sizeof(ManagerHeader<DefaultAddressTraits>) == 64`, granule-aligned | `types.h` (`ManagerHeader` `static_assert`s) |
| S3 | `BlockHeader<DefaultAddressTraits>` is the only physical block-header layout; it is standard-layout, trivially-copyable, and 32 bytes; the AVL slot is its prefix (`weight`, `left_offset`, `right_offset`, `parent_offset`, `avl_height`). | `block_header.h` (`BlockLayoutContract<AT>` and `static_assert`s on size/offsets) |
| S4 | `kGranuleSize` is power of 2, equals `DefaultAddressTraits::granule_size` | `types.h` |
| S5 | `BlockStateBase<AT>` is **not** storage; it is a static typed access/helper layer over `BlockHeader<AT>`. State classes (`FreeBlock`, `AllocatedBlock`, …) are non-owning typed views over `BlockHeader<AT>&`. | `block_state.h` (`BlockStateBase` only declares `static` members and a deleted constructor; `FreeBlock`/`AllocatedBlock`/… store a single `BlockHeader<AT>*`) |
| S6 | [ForestDomainRecord](../include/pmm/forest_registry.h#pmm-detail-forestdomainrecord) is trivially copyable; [ForestDomainRegistry](../include/pmm/forest_registry.h#pmm-detail-forestdomainregistry) is nothrow-default-constructible | `forest_registry.h` |
| S7 | Address traits: `IndexT` unsigned, `GranuleSz >= 4`, `GranuleSz` power of 2 | `address_traits.h` |

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

Code: [BlockStateBase::recover_state()](../include/pmm/block_state.h#pmm-blockstatebase-recover_state).
Test: `test_block_header.cpp` — `detect_block_state and recover_block_state behave consistently`.
