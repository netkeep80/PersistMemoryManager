# Internal Structure Map

Canonical map of internal modules, their responsibilities, and authoritative files.

## Module Responsibilities

### 1. Address Space & Block Layout

| File | Lines | Responsibility |
|------|-------|----------------|
| `address_traits.h` | 144 | `AddressTraits<IndexT, GranuleSz>` template; predefined Small/Default/Large aliases |
| `block.h` | 80 | `Block<AT>` is a typedef for `BlockHeader<AT>` — the single physical block-header layout |
| `block_header.h` | 170 | `BlockHeader<AT>` (AVL slot) intrusive AVL slot (weight, left/right/parent, root_offset, avl_height, node_type) |
| `types.h` | 464 | `ManagerHeader<AT>`, [PmmError](../include/pmm/types.h#pmm-pmmerror), [MemoryStats](../include/pmm/types.h#pmm-memorystats), [BlockView](../include/pmm/types.h#pmm-blockview), [FreeBlockView](../include/pmm/types.h#pmm-freeblockview); CRC32; byte↔granule conversion; `block_at()`, `user_ptr()`, `resolve_granule_ptr()`; `kNullIdx_v<AT>` null sentinel |

**Authoritative path:** `address_traits.h``block.h` / `block_header.h``types.h` (constants & helpers).

### 2. Block State Machine

| File | Lines | Responsibility |
|------|-------|----------------|
| `block_state.h` | 802 | `BlockStateBase<AT>` with 6 state classes; type-safe transitions for allocate/deallocate/split/coalesce; `field_read_idx`/`field_write_idx` unified field access; `recover_state()` / `verify_state()` |

**Authoritative path:** All block state logic goes through `BlockStateBase<AT>` static methods. No direct field writes outside the state machine.

### 3. Allocation & Free Tree

| File | Lines | Responsibility |
|------|-------|----------------|
| `allocator_policy.h` | 632 | `AllocatorPolicy<FreeBlockTreeT, AT>` — allocate, coalesce, split, rebuild, verify |
| `free_block_tree.h` | 255 | `AvlFreeTree<AT>` — AVL forest-policy for free blocks (insert/remove/find_best_fit) |
| `avl_tree_mixin.h` | 648 | Shared AVL operations (rotate, rebalance, insert, remove, find); `BlockPPtr<AT>` adapter; [AvlInorderIterator](../include/pmm/avl_tree_mixin.h#pmm-detail-avlinorderiterator) |

**Authoritative path:** [AvlFreeTree](../include/pmm/free_block_tree.h#pmm-avlfreetree)[AllocatorPolicy](../include/pmm/allocator_policy.h#pmm-allocatorpolicy)[PersistMemoryManager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager). One implementation path for each: allocation, deallocation, coalescing, tree rebuild.

### 4. Storage Backends

| File | Lines | Responsibility |
|------|-------|----------------|
| `storage_backend.h` | 56 | C++20 `StorageBackendConcept` (base_ptr, total_size, expand, owns_memory) |
| `heap_storage.h` | 162 | `HeapStorage<AT>` — malloc/realloc backend |
| `static_storage.h` | 77 | `StaticStorage<Size, AT>` — fixed compile-time buffer |
| `mmap_storage.h` | 389 | `MMapStorage<AT>` — POSIX mmap / Windows MapViewOfFile |

**Authoritative path:** Each backend implements `StorageBackendConcept` independently. No duplication between backends.

### 5. Persistent Data Structures

| File | Lines | Responsibility |
|------|-------|----------------|
| `pptr.h` | 230 | `pptr<T, ManagerT>` — persistent typed pointer (granule index) |
| `pstring.h` | 319 | `pstring<ManagerT>` — mutable persistent string (separate data block) |
| `pstringview.h` | 300 | `pstringview<ManagerT>` — interned read-only string (deduplication via AVL) |
| `pmap.h` | 398 | `pmap<K, V, ManagerT>` — persistent AVL-tree dictionary |
| `parray.h` | 452 | `parray<T, ManagerT>` — persistent dynamic array with O(1) indexed access |
| `pallocator.h` | 155 | `pallocator<T, ManagerT>` — STL-compatible allocator adapter |

**Shared patterns:** All containers use `detail::resolve_granule_ptr()` for data access and `detail::kNullIdx_v<AT>` for null sentinel.

### 6. Configuration & Policies

| File | Lines | Responsibility |
|------|-------|----------------|
| `config.h` | 74 | Lock policies: [SharedMutexLock](../include/pmm/config.h#pmm-config-sharedmutexlock), [NoLock](../include/pmm/config.h#pmm-config-nolock); grow ratio constants |
| `logging_policy.h` | 128 | [NoLogging](../include/pmm/logging_policy.h#pmm-logging-nologging), [StderrLogging](../include/pmm/logging_policy.h#pmm-logging-stderrlogging) with callback hooks |
| `manager_configs.h` | 354 | 9 predefined configs (Cache, Persistent, Embedded, Industrial, LargeDB, Static variants) |
| `manager_concept.h` | 97 | C++20 concept `ManagerConcept` for compile-time validation |
| `forest_registry.h` | 211 | `ForestDomainRegistry<AT>` — persistent domain registry for forest model |

### 7. Main Manager

| File | Lines | Responsibility |
|------|-------|----------------|
| `persist_memory_manager.h` | 1388 | `PersistMemoryManager<ConfigT, InstanceId>` — unified static API; lifecycle, layout, forest registry, and verify/repair orchestration |

**Authoritative path:** All public API goes through [PersistMemoryManager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager). Internal helpers use `read_stat()` for statistics, `get_tree_idx_field()`/`set_tree_idx_field()` for tree accessors.

### 8. I/O & Diagnostics

| File | Lines | Responsibility |
|------|-------|----------------|
| `io.h` | 224 | `save_manager<MgrT>()`, `load_manager_from_file<MgrT>()` with CRC32 |
| `diagnostics.h` | 107 | [RecoveryMode](../include/pmm/diagnostics.h#pmm-recoverymode), [ViolationType](../include/pmm/diagnostics.h#pmm-violationtype), [DiagnosticEntry](../include/pmm/diagnostics.h#pmm-diagnosticentry), [VerifyResult](../include/pmm/diagnostics.h#pmm-verifyresult) |
| `typed_guard.h` | 137 | RAII scope-guards for allocate/deallocate pairs |
| `pmm_presets.h` | 190 | Preset manager aliases (SingleThreadedHeap, MultiThreadedHeap, etc.) |

## Namespace Organization

```
pmm::                           Public API surface
├── PersistMemoryManager<>      Main static manager
├── pptr<T, ManagerT>           Persistent pointer
├── pstring<ManagerT>           Mutable string
├── pstringview<ManagerT>       Interned string
├── pmap<K, V, ManagerT>        Dictionary
├── parray<T, ManagerT>         Dynamic array
├── pallocator<T, ManagerT>     STL allocator
├── Block<AT>                   Block layout
├── BlockHeader<AT>             physical block header (AVL + linked-list)
├── BlockStateBase<AT>          State machine
├── AllocatorPolicy<>           Allocation logic
├── AvlFreeTree<AT>             Free tree policy
├── PmmError                    Error codes
├── MemoryStats, BlockView, ... Public data structures
│
├── detail::                    Implementation-only helpers
│   ├── ManagerHeader<AT>       Header structure
│   ├── ForestDomainRegistry<AT>
│   ├── CRC32, byte↔granule     Conversion utilities
│   ├── block_at(), user_ptr()  Canonical pointer helpers
│   ├── kNoBlock_v<AT>          No-block sentinel
│   ├── kNullIdx_v<AT>          Null-index sentinel
│   ├── avl_* functions         Shared AVL operations
│   └── BlockPPtr<AT>           Free-tree AVL adapter
│
├── config::                    Thread/lock policies
│   ├── SharedMutexLock
│   └── NoLock
│
└── presets::                   Ready-made manager types
```

## Key Design Principles

1. **One capability → one implementation path.** Each operation (allocate, coalesce, tree rotation, etc.) has exactly one authoritative implementation.
2. **Shared AVL mixin.** All AVL trees (free tree, pstringview, pmap) share `avl_tree_mixin.h` via template metaprogramming.
3. **State machine for block transitions.** All block metadata writes go through `BlockStateBase<AT>` to ensure atomicity and recoverability.
4. **Modular headers as source of truth.** `single_include/pmm.h` is generated from modular headers via `scripts/generate-single-headers.sh`.
5. **Unified field access.** [BlockStateBase](../include/pmm/block_state.h#pmm-blockstatebase) uses `field_read_idx`/`field_write_idx` with compile-time offsets for all block field access.
