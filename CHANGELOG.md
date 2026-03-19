# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!-- changelog-insert-here -->

## [0.33.0] - 2026-03-19

### Added
- `set_root<T>(pptr<T>)` / `get_root<T>()` — root object API in ManagerHeader (Phase 3.7, #200)
- `root_offset` field in `ManagerHeader` replaces `_reserved[4]` — stores a single persistent root pointer
- Root object survives save/load cycles and enables object discovery after heap restoration
- 13 new tests covering root set/get, persistence, all address traits, pmap registry pattern


## [0.32.0] - 2026-03-19

### Added
- `ppool<T, ManagerT>` — persistent object pool with O(1) allocate/deallocate via embedded free-list (Issue #199, Phase 3.6)
- `Mgr::ppool<T>` type alias in `PersistMemoryManager`
- 18 tests for ppool in `tests/test_issue199_ppool.cpp`


## [0.31.0] - 2026-03-19

### Added
- `pallocator<T, ManagerT>` — STL-compatible allocator for persistent address space (Issue #198, Phase 3.5). Allows using STL containers like `std::vector<T, Mgr::pallocator<T>>` with persistent memory managed by PersistMemoryManager.


## [0.30.0] - 2026-03-19

### Added
- `pvector::erase(index)` — O(log n) removal by index with AVL tree rebalancing (#197, Phase 3.4)


## [0.29.0] - 2026-03-19

### Added
- `pmap::erase(key)` — remove a node by key with O(log n) AVL removal and memory deallocation (#196)
- `pmap::size()` — return the number of elements in the dictionary (#196)
- `pmap::begin()`/`end()` — iterator for in-order (sorted key) traversal (#196)
- `pmap::clear()` — remove all elements with memory deallocation (#196)
- 22 new tests for pmap erase, size, iterator, and clear functionality


## [0.28.0] - 2026-03-19

### Added
- `parray<T, ManagerT>` — persistent dynamic array with O(1) random access (Issue #195, Phase 3.2)
  - `push_back()` / `pop_back()` — add/remove elements (amortized O(1))
  - `at(i)` / `operator[]` — O(1) indexed access
  - `set(i, value)` — modify element at index
  - `reserve(n)` / `resize(n)` — capacity management
  - `front()` / `back()` / `data()` — element accessors
  - `clear()` / `free_data()` — reset and deallocation
  - `operator==` / `operator!=` — equality comparison
  - POD-structure (`std::is_trivially_copyable_v == true`) for direct serialization in PAP
  - Manager alias: `Mgr::parray<T>` for concise usage
- 23 tests for parray covering all API operations


## [0.27.0] - 2026-03-19

### Added
- `pstring<ManagerT>` — mutable persistent string type for persistent address space (Issue #45, Phase 3.1)
  - API: `assign()`, `append()`, `c_str()`, `size()`, `clear()`, `free_data()`, `operator[]`
  - Data stored in a separate block with amortized O(1) reallocation (doubling strategy)
  - POD-structure (trivially copyable) for direct serialization in PAP
  - Comparison operators: `==`, `!=`, `<` with C-strings and other pstrings
  - Manager alias: `Mgr::pstring` for concise usage


## [0.26.0] - 2026-03-18

### Added
- `is_valid_ptr(pptr<T>)` method for explicit runtime pointer validation (Phase 1.2)
- Integer overflow protection in `allocate()` and `allocate_from_block()` (Phase 1.3)

### Changed
- `create_typed<T>()` now enforces `std::is_nothrow_constructible_v<T, Args...>` via `static_assert` (Phase 1.1)
- `destroy_typed<T>()` now enforces `std::is_nothrow_destructible_v<T>` via `static_assert` (Phase 1.1)
- `resolve()` now includes a debug-mode bounds check via `assert` (Phase 1.2)
- `FreeBlock::cast_from_raw()` and `AllocatedBlock::cast_from_raw()` now return `nullptr` in Release builds for invalid input instead of relying on `assert` only (Phase 1.4)

### Added
- CRC32 checksum for persisted images — `save_manager()` computes and stores CRC32, `load_manager_from_file()` verifies it (Phase 2.1)
- Atomic save via write-then-rename pattern — `save_manager()` writes to temporary file, then atomically renames (Phase 2.2)
- `MMapStorage::expand()` — dynamic file growth with remap on POSIX and Windows (Phase 2.3)
- `compute_crc32()` and `compute_image_crc32()` utility functions in `pmm::detail` (Phase 2.1)

### Changed
- `ManagerHeader._reserved[8]` split into `crc32` (4 bytes) + `_reserved[4]` — struct size unchanged (Phase 2.1)
- `save_manager()` now computes CRC32 before writing and uses atomic rename (Phase 2.1, 2.2)
- `load_manager_from_file()` now verifies CRC32 before calling `load()` (Phase 2.1)


## [0.24.0] - 2026-03-12

### Changed
- **avl_tree_mixin.h**: Added `NodeUpdateFn` hook parameter to `avl_rotate_right`, `avl_rotate_left`, `avl_rebalance_up`, and `avl_insert` — enables custom node-attribute updates (e.g. order-statistic weight) without duplicating rotation/rebalance code (Issue #188).
- **avl_tree_mixin.h**: Added shared `avl_remove`, `avl_min_node`, `avl_max_node` functions for reuse across persistent containers (Issue #188).
- **pvector.h**: Refactored to delegate AVL rotation, rebalancing, insertion and removal to `avl_tree_mixin.h` via `_WeightUpdateFn`, eliminating ~280 lines of duplicated AVL code (Issue #188).


## [0.22.0] - 2026-03-12

### Added
- New persistent container `pvector<T>` — a sequential container (vector) for the persistent address space (Issue #186)
  - O(1) `push_back()`, `front()`, `back()`, `pop_back()` operations with tail pointer
  - O(n) `at(i)` access via linear traversal from head
  - Iterator support via `begin()`/`end()` for range-based for loops
  - `clear()` method to remove all elements and free memory
  - Nodes are NOT permanently locked — can be freed via `pop_back()` or `clear()`
- `Mgr::pvector<T>` type alias in PersistMemoryManager for convenient access
- Comprehensive test suite for pvector (test_issue186_pvector.cpp)
- Documentation for pvector in README.md and docs/api_reference.md


## [0.21.0] - 2026-03-11

### Changed
- **pstringview**: Optimized memory layout — string length and data are now stored in a single PAP block instead of two (Issue #184)
  - Reduces memory usage by eliminating the separate char[] block allocation
  - Improves performance for `pmap<pptr<pstringview>, _Tvalue>` operations
  - Uses flexible array member pattern: `struct { uint32_t length; char str[1]; }`
  - `c_str()` now returns pointer to embedded string data directly

### Added
- **pptr**: Added `operator<` for persistent pointers (Issue #184)
  - Enables using `pptr<pstringview>` as keys in `pmap`
  - Compares by dereferencing pointers and comparing pointed-to values
  - Null pptr is considered less than any non-null pptr

### Fixed
- Updated `pmap<pptr<pstringview>, int>` test to use the correct key type pattern (Issue #184)
  - `pstringview` objects should be referenced by `pptr`, not copied by value
  - The new embedded string layout requires using `pptr<pstringview>` as map keys


## [0.20.0] - 2026-03-11

### Added
- `SmallEmbeddedStaticConfig<N>` — new configuration for small embedded systems with 16-bit index (`SmallAddressTraits`, `StaticStorage`, `NoLock`) supporting pools up to ~1 MB (Issue #146).
- `LargeDBConfig` — new configuration for large databases with 64-bit index (`LargeAddressTraits`, `HeapStorage`, `SharedMutexLock`, 100% growth) (Issue #146).
- `SmallEmbeddedStaticHeap<N>` preset — ready-to-use manager alias for 16-bit embedded systems with static storage (Issue #146).
- `LargeDBHeap` preset — ready-to-use manager alias for large databases with 64-bit index (Issue #146).
- `kMinGranuleSize = 4` constant in `manager_configs.h` — explicit minimum granule size (architecture word size) (Issue #146).
- `ValidPmmAddressTraits` C++20 concept in `manager_configs.h` — validates that `AddressTraitsT` has `granule_size >= kMinGranuleSize` and `granule_size` is a power of two (Issue #146).
- New tests: `test_issue146_configs`, `test_issue146_index_sizes`, and single-header tests for embedded static, small embedded static, and large DB presets (Issue #146).
- Single-header files for new presets: `pmm_embedded_static_heap.h`, `pmm_small_embedded_static_heap.h`, `pmm_large_db_heap.h` (Issue #146).

### Changed
- `AddressTraits<IndexT, GranuleSz>` static assertion strengthened: `GranuleSz >= 4` (was `>= 1`) — enforces minimum architecture word size (Issue #146).
- `address_traits.h` documentation updated to list valid index types (uint16_t, uint32_t, uint64_t) and explain granule selection rules (Issue #146).
- `manager_configs.h` documentation updated with configuration selection rules, granule waste analysis, and architecture scenarios (Issue #146).
- `pmm_presets.h` documentation updated: "tiny embedded" renamed to "small embedded" to align with `SmallAddressTraits` terminology (Issue #146).
- Tests `test_issue87_phase1/2/3/5/abstraction` updated: references to removed `TinyAddressTraits` alias replaced with explicit `AddressTraits<uint8_t, 8>` (Issue #146).

### Removed
- `TinyAddressTraits` alias (`AddressTraits<uint8_t, 8>`) removed from `address_traits.h` — a uint8_t index allows only 255 granules (~2 KB), which is insufficient for any real PMM use case (Issue #146).


## [0.19.3] - 2026-03-10

### Changed
- Removed the non-templated `header_from_ptr()` overload hardcoded for `DefaultAddressTraits` from `types.h` (Issue #179). The templated `header_from_ptr_t<AddressTraitsT>()` already covers all address-trait configurations generically, eliminating the duplicate code.


## [0.19.2] - 2026-03-10

### Changed
- Extracted repeated `raw → pptr` conversion in `allocate_typed<T>()`, `allocate_typed<T>(count)`,
  and `create_typed<T>(args...)` into a private helper `make_pptr_from_raw<T>()`, eliminating three
  identical copies of the same formula (Issue #179)
- Extracted the repeated block-header lookup prologue shared by `deallocate()`,
  `lock_block_permanent()`, and `is_permanently_locked()` into two private overloaded helpers
  `find_block_from_user_ptr(void*)` / `find_block_from_user_ptr(const void*)`, reducing the risk
  of behavioural divergence between these methods (Issue #179)


## [0.19.1] - 2026-03-10

### Changed
- Extracted repeated `blk_raw` pointer computation in `PersistMemoryManager` into two private
  helper methods (`block_raw_ptr_from_pptr` and `block_raw_mut_ptr_from_pptr`), eliminating
  ten copies of the same formula across `get_tree_left_offset`, `get_tree_right_offset`,
  `get_tree_parent_offset`, `set_tree_left_offset`, `set_tree_right_offset`,
  `set_tree_parent_offset`, `get_tree_weight`, `set_tree_weight`, `get_tree_height`,
  `set_tree_height`, and `tree_node` (Issue #179)


## [0.19.0] - 2026-03-10

### Added
- `ManagerHeader<AddressTraitsT>` is now a template parameterized on `AddressTraitsT` (Issue #175). All 7 index/counter fields (`block_count`, `free_count`, `alloc_count`, `first_block_offset`, `last_block_offset`, `free_tree_root`, `used_size`) now use `AT::index_type` instead of hardcoded `std::uint32_t`, enabling correct 16-bit and 64-bit index support.
- `FreeBlockTreePolicyForTraitsConcept<Policy, AT>` — new C++20 concept that validates a free block tree policy against a specific `AddressTraitsT` (Issue #175). Allows `AllocatorPolicy<SmallAddressTraits, AvlFreeTree<SmallAddressTraits>, ...>` and `AllocatorPolicy<LargeAddressTraits, AvlFreeTree<LargeAddressTraits>, ...>` to compile and work correctly.

### Changed
- `kManagerHeaderGranules_t<AT>` now returns `AT::index_type` instead of `std::uint32_t` (Issue #175).
- `kBlockHeaderGranules_t<AT>` now returns `AT::index_type` instead of `std::uint32_t` (Issue #175).
- `required_block_granules_t<AT>()` now returns `AT::index_type` instead of `std::uint32_t` (Issue #175).
- All internal index variables in `AllocatorPolicy`, `AvlFreeTree`, and `PersistMemoryManager` now use `index_type` (i.e. `AT::index_type`) instead of hardcoded `std::uint32_t`, enabling correct operation with 16-bit and 64-bit address traits (Issue #175).
- `AllocatorPolicy` now uses `FreeBlockTreePolicyForTraitsConcept<FreeBlockTreeT, AddressTraitsT>` instead of `is_free_block_tree_policy_v<FreeBlockTreeT>` for the static_assert, enabling non-DefaultAddressTraits specializations (Issue #175).
- All `detail::kNoBlock` sentinel comparisons with `index_type` fields replaced with `AddressTraitsT::no_block` or `address_traits::no_block` to avoid integer promotion issues between differently-sized sentinels (Issue #175).

### Fixed
- SEGFAULT when using `SmallAddressTraits` (uint16_t index) heaps: `detail::kNoBlock` (`uint32_t 0xFFFFFFFF`) was being compared against `uint16_t` index fields. Due to integer promotion, `0xFFFF != 0xFFFFFFFF`, so the sentinel check failed silently, causing invalid block accesses. Fixed by replacing all `detail::kNoBlock` comparisons with `address_traits::no_block` throughout `persist_memory_manager.h` (Issue #175).


## [0.18.1] - 2026-03-10

### Removed
- `prev_owns_memory` (bool) and `prev_base_ptr` (void*) fields removed from `ManagerHeader`
  (Issue #176). These runtime-only fields were obsolete and unused. Their bytes are now
  occupied by reserved padding (`_pad` and `_reserved[8]`) to maintain the 64-byte struct layout.


## [0.18.0] - 2026-03-10

### Added
- `create_typed<T>(args...)` — new API to allocate memory and construct an object via placement new, replacing the misleading `allocate_typed<T>()` for non-trivial types (Issue #172)
- `destroy_typed<T>(p)` — new API to explicitly call the destructor and free memory, replacing the misleading `deallocate_typed<T>(p)` for non-trivial types (Issue #172)

### Fixed
- `tree_node(p)` now asserts that `p` is not null and manager is initialized before dereferencing (prevents UB under ASan/UBSan, Issue #172)
- `is_initialized()` now uses `std::atomic<bool>` for `_initialized` — lock-free safe read without data race (Issue #172)
- `total_size()`, `used_size()`, `free_size()`, `block_count()`, `free_block_count()`, `alloc_block_count()` now take a `shared_lock` to prevent data races in multi-threaded configurations (Issue #172)
- `create(initial_size)` now guards against integer overflow when `initial_size` is close to `SIZE_MAX` — returns `false` instead of undefined behavior (Issue #172)
- Added `#error` guard: `pmm.h` now emits a compile error if compiled without C++20 (Issue #172)

### Changed
- `LargeDBConfig` documentation clarified: ManagerHeader index fields are still `uint32_t` (limiting practical address space to ~256 GiB); full 64-bit support requires a future templated ManagerHeader refactoring (Issue #172)


## [0.17.0] - 2026-03-10

### Added
- `scripts/strip-comments.py`: Python helper that strips C/C++ comments from a
  source file while preserving string literals and line structure.
- `--strip-comments` flag for `scripts/generate-single-headers.sh`: when passed,
  also generates `single_include/pmm/pmm_no_comments.h` — a comment-free variant
  of `pmm.h` that is ~42 % smaller in line count and ~56 % smaller in byte size,
  suitable for embedded or size-critical environments.
- `tests/test_issue170_sh_no_comments.cpp`: self-sufficiency test confirming that
  `pmm_no_comments.h` compiles and runs correctly without any other PMM headers.
- CI (`single-headers` job) now validates `pmm_no_comments.h` freshness alongside
  the other single-header files.


## [0.16.4] - 2026-03-10

### Changed
- Removed duplicate wrapper functions `reset_block_avl_fields()`, `repair_block_prev_offset()`, `read_block_next_offset()`, `read_block_weight()` from `block_state.h` (Issue #168). `AllocatorPolicy` now calls `BlockStateBase<AT>::*` static methods directly, eliminating ~50 lines of one-liner delegation.
- Deleted `detail::kBlockHeaderGranules` from `types.h`; all call sites updated to use `detail::kBlockHeaderGranules_t<DefaultAddressTraits>` directly (Issue #168).
- Added `using BlockState = BlockStateBase<AddressTraitsT>` alias in `AllocatorPolicy` for consistent, readable access to `BlockStateBase` static methods (Issue #168).
- Regenerated `single_include/pmm/pmm.h` after deduplication (5739 lines, -47 lines from removed wrappers).


## [0.16.2] - 2026-03-10

### Changed
- Replaced deprecated `detail::granules_to_bytes()` calls with `address_traits::granules_to_bytes()` in `persist_memory_manager.h` `used_size()` and `free_size()` methods, ensuring correct granule size is used for any `address_traits` (#166)
- Removed redundant `static_assert(ValidPmmAddressTraits<X>)` from `SmallEmbeddedStaticConfig` and `EmbeddedStaticConfig`; these are already verified at namespace scope (#166)

### Added
- `detail::kNoBlock_v<AT>` — template variable alias for `AT::no_block` sentinel, enabling type-safe sentinel comparisons in generic (templated) code across all address traits (#166)
- `detail::required_block_granules_t<AT>()` — templated variant of `required_block_granules()` that uses `AddressTraitsT::granule_size` and `kBlockHeaderGranules_t<AT>`, eliminating the non-templated `DefaultAddressTraits`-specific version as the only option (#166)


## [0.16.1] - 2026-03-10

### Removed
- Removed 10 redundant `pptr` instance methods that duplicated `tree_node()` functionality (Issue #164):
  `get_tree_left()`, `get_tree_right()`, `get_tree_parent()`,
  `set_tree_left()`, `set_tree_right()`, `set_tree_parent()`,
  `get_tree_weight()`, `set_tree_weight()`,
  `get_tree_height()`, `set_tree_height()`.
  Use `p.tree_node()` to access `TreeNode` fields directly via `get_left()`,
  `set_left()`, `get_right()`, `set_right()`, `get_parent()`, `set_parent()`,
  `get_weight()`, `set_weight()`, `get_height()`, `set_height()`.
- Updated `avl_tree_mixin.h` to use `tree_node()` API instead of removed `pptr` methods.
- Rewrote `test_pptr.cpp` tree-related tests to use `tree_node()` API.
- Updated `README.md` and `docs/api_reference.md` to reflect the new API.
- `pptr::resolve()` retained for low-level/array use; prefer `*p` and `p->field` for scalar access.


## [0.16.0] - 2026-03-10

### Changed
- Extracted generic `detail::avl_find()` template into `avl_tree_mixin.h` (Issue #162), eliminating duplicate AVL traversal loops in `pstringview` and `pmap`. Both classes now share the single `detail::avl_find()` implementation from `avl_tree_mixin.h`, consistent with how `detail::avl_insert()` is already shared.


## [0.15.0] - 2026-03-10

### Changed
- **`BasicConfig<>` template** added to `manager_configs.h` (Issue #160): eliminates ~250 lines of
  duplicated code across 5 heap-based config structs. `CacheManagerConfig`, `PersistentDataConfig`,
  `EmbeddedManagerConfig`, `IndustrialDBConfig`, and `LargeDBConfig` are now type aliases of
  `BasicConfig<AddressTraitsT, LockPolicyT, GrowNum, GrowDen, MaxMemoryGB>`, preserving full
  backward compatibility.
- **Byte/granule conversion deduplication** in `types.h` (Issue #160): non-templated
  `detail::bytes_to_granules()`, `detail::granules_to_bytes()`, `detail::idx_to_byte_off()`, and
  `detail::byte_off_to_idx()` now delegate to their `_t<DefaultAddressTraits>` counterparts,
  eliminating ~60 lines of duplicated arithmetic logic. Behavior is unchanged.
- **`block_total_granules` unified** to a single templated implementation (Issue #160):
  the non-templated `DefaultAddressTraits` overload is removed; the templated variant covers all
  address traits including `DefaultAddressTraits`.


## [0.14.0] - 2026-03-10

### Changed
- `docs/api_reference.md`: rewritten from scratch to reflect library as of v0.13.0 — added `pstringview`, `pmap`, `avl_tree_mixin`, all new configurations (`SmallEmbeddedStaticConfig`, `EmbeddedStaticConfig`, `LargeDBConfig`), all new presets (`EmbeddedStaticHeap`, `SmallEmbeddedStaticHeap`, `LargeDBHeap`), new single-header files (`pmm.h`), address traits table, `tree_node()` method, updated pptr AVL access methods (Issue #134)
- `docs/architecture.md`: rewritten from scratch to reflect library as of v0.13.0 — added `pstringview`/`pmap` persistent data structures section, `avl_tree_mixin` layer diagram, address traits table, updated block layout order, storage backends and lock policies (Issue #134)
- `docs/atomic_writes.md`: updated to reflect current block layout (`TreeNode` before `LinkedListNode`), added section on user-data AVL trees (`pstringview`, `pmap`) persistence and crash consistency, added `granule_size` validation note (Issue #134)

### Changed
- Replaced 14 repeated `static_assert` pairs in `manager_configs.h` with a single `ValidPmmAddressTraits<AT>` C++20 concept (Issue #155)


## [0.13.0] - 2026-03-10

### Added
- `include/pmm/avl_tree_mixin.h`: shared AVL tree helper functions (`avl_height`, `avl_update_height`, `avl_balance_factor`, `avl_set_child`, `avl_rotate_right`, `avl_rotate_left`, `avl_rebalance_up`, `avl_insert`) used by both `pmap` and `pstringview` (Issue #155).

### Changed
- `pmap<_K,_V,ManagerT>`: refactored to use shared `detail::avl_*` helpers from `avl_tree_mixin.h`, eliminating ~130 lines of duplicated AVL tree code (Issue #155).
- `pstringview<ManagerT>`: refactored to use shared `detail::avl_*` helpers from `avl_tree_mixin.h`, eliminating ~130 lines of duplicated AVL tree code (Issue #155).
- `pmap<_K,_V,ManagerT>`: nodes are no longer permanently locked via `lock_block_permanent()` — permanent locking is only needed for `pstringview` interning semantics (Issue #155).


## [0.12.0] - 2026-03-10

### Added
- `pmap<_K,_V>` — persistent AVL tree dictionary in PAP (Issue #153). Stores key-value pairs
  in PAP blocks using built-in TreeNode fields for the AVL tree structure, analogous to
  `pstringview` (Issue #151). Features: O(log n) insert/find/contains, automatic
  AVL self-balancing, permanent block locking (Issue #126), value update on duplicate key.
- `Mgr::pmap<_K,_V>` — concise manager-bound type alias in `PersistMemoryManager`.
  Example: `Mgr::pmap<Mgr::pstringview, int>` for named persistent object dictionaries.


## [0.11.0] - 2026-03-09

### Added
- `pstringview<ManagerT>` — interned read-only persistent string type (Issue #151):
  - Concise API via manager type alias: `Mgr::pptr<Mgr::pstringview> p = Mgr::pstringview("hello");`
    — `Mgr::pstringview` is a nested type alias inside `PersistMemoryManager`, so no template
    parameter is needed at the call site. Equal strings always return the same `pptr` (deduplication guaranteed).
  - `PersistMemoryManager<ConfigT>::pstringview` — nested type alias for `pmm::pstringview<manager_type>`,
    enabling the `Mgr::pstringview(...)` syntax analogous to `Mgr::pptr<T>`.
  - `pstringview<ManagerT>::intern(s)` — static method for explicit interning; same semantics.
  - `pstringview<ManagerT>` — stores `chars_idx` (granule index of char[] in PAP) and `length`.
    Supports `c_str()`, `size()`, `empty()`, `operator==`, `operator!=`, `operator<`.
  - Deduplication dictionary uses the **built-in AVL tree** (forest of AVL trees concept):
    each `pstringview` block's `TreeNode` fields (left, right, parent, height) serve as AVL links.
    Only one `static inline index_type _root_idx` is stored — no separate PAP allocation needed.
  - All char[] and pstringview blocks are permanently locked via `lock_block_permanent()`
    (Issue #126), ensuring they cannot be freed through `deallocate()`.
  - `pstringview<ManagerT>::reset()` — clears the singleton for test isolation.
  - New header: `include/pmm/pstringview.h` (auto-included via `persist_memory_manager.h`)


## [0.10.0] - 2026-03-09

### Added
- Add `single_include/pmm/pmm.h` — a new single-header file bundling the full PMM library
  without any specific preset specialization, allowing users to drop a single file into their
  project and use any configuration or define their own `PersistMemoryManager<MyConfig>`

### Changed
- Reorganize single-header preset files (`pmm_embedded_heap.h`, `pmm_single_threaded_heap.h`,
  `pmm_multi_threaded_heap.h`, `pmm_industrial_db_heap.h`, `pmm_embedded_static_heap.h`,
  `pmm_small_embedded_static_heap.h`, `pmm_large_db_heap.h`) to be thin wrappers that
  include `pmm.h` and define only their specific preset alias — eliminating ~36k lines of
  duplicated code from the release and making preset files serve as clear usage examples
- Update `scripts/generate-single-headers.sh` to generate `pmm.h` and all thin preset files
- Update CI `single-headers` check to verify `pmm.h` is up to date alongside preset files
- Update tests that use multiple presets to include all required preset headers explicitly
  instead of relying on one preset file containing all definitions


## [0.9.0] - 2026-03-09

### Added
- `SmallEmbeddedStaticConfig<N>` — конфигурация с 16-bit индексом (SmallAddressTraits, uint16_t), StaticStorage, NoLock; pptr<T> занимает 2 байта (вместо 4), максимальный пул ~1 МБ; для ARM Cortex-M, AVR, ESP32 и других ресурсоограниченных систем
- `LargeDBConfig` — конфигурация с 64-bit индексом (LargeAddressTraits, uint64_t), HeapStorage, SharedMutexLock, 64B гранула; pptr<T> занимает 8 байт, адресует петабайтный масштаб; для крупных баз данных и облачных хранилищ
- `SmallEmbeddedStaticHeap<N>` — пресет в `pmm::presets` на базе SmallEmbeddedStaticConfig (16-bit)
- `LargeDBHeap` — пресет в `pmm::presets` на базе LargeDBConfig (64-bit)
- Single-header файлы `pmm_small_embedded_static_heap.h` и `pmm_large_db_heap.h` для удобного использования без системы сборки
- Тесты `test_issue146_index_sizes`, `test_issue146_sh_small_embedded_static`, `test_issue146_sh_large_db`


## [0.8.0] - 2026-03-08

### Added
- `EmbeddedStaticConfig<N>` — new configuration for embedded systems without heap: `StaticStorage<N, DefaultAddressTraits>` + `NoLock`, 16B granule, no dynamic expansion (Issue #146)
- `EmbeddedStaticHeap<N>` — new preset alias for `PersistMemoryManager<EmbeddedStaticConfig<N>, 0>`, ready for bare-metal / RTOS use (Issue #146)
- `kMinGranuleSize = 4` constant — minimum supported granule size (architecture word size), enforced by `static_assert` in all configs (Issue #146)
- `static_assert` rules to all existing configs: `granule_size >= kMinGranuleSize` and `granule_size` is a power of 2 (Issue #146)
- `single_include/pmm/pmm_embedded_static_heap.h` — self-contained single-header preset file for `EmbeddedStaticHeap` (Issue #146)
- `tests/test_issue146_configs.cpp` — comprehensive tests for all config rules, `EmbeddedStaticHeap` lifecycle, and preset properties (Issue #146)
- `tests/test_issue146_sh_embedded_static.cpp` — single-header self-containment test for `pmm_embedded_static_heap.h` (Issue #146)

### Changed
- `include/pmm/manager_configs.h` — updated documentation to describe architecture rules and constraints; added `static_assert` guards to all existing configs (Issue #146)
- `include/pmm/pmm_presets.h` — updated documentation and added `EmbeddedStaticHeap` preset (Issue #146)
- `scripts/generate-single-headers.sh` — regenerates all 5 single-header files including the new `pmm_embedded_static_heap.h` (Issue #146)
- All single-header files in `single_include/pmm/` regenerated to include updated `manager_configs.h` with new configs and `static_assert` rules (Issue #146)


## [0.7.1] - 2026-03-08

### Fixed
- Added debug-mode assertions (`assert`) in `FreeBlock::cast_from_raw` and `AllocatedBlock::cast_from_raw` to validate block state invariants at the reinterpret-cast boundaries. Violations are now detected early in debug builds (Issue #144).

### Added
- Added test suite `test_issue144_code_review` covering: debug-mode cast validation, `bytes_to_granules` overflow handling, `block state` consistency checks, `recover_block_state` transitional states, `for_each_block` read-only callback safety, `lock_block_permanent` immutability, and `reset_block_avl_fields` field clearing (Issue #144).


## [0.7.0] - 2026-03-08

### Changed
- Rewrote README.md from scratch in English (Issue #124): updated from outdated C++17 content to accurate C++20 documentation, added single-header quick start, full API reference, configuration guide, architecture overview, performance table, and repository structure

### Changed
- Merged `LinkedListNode` into `Block` (Issue #138): `prev_offset` and `next_offset` fields moved directly into `Block<AddressTraitsT>` as protected member variables; `Block` now inherits only `TreeNode<AddressTraitsT>`; file `include/pmm/linked_list_node.h` deleted
- Updated binary block layout (Issue #138): `TreeNode` fields now occupy bytes 0–23, `prev_offset`/`next_offset` occupy bytes 24–31 (total 32 bytes unchanged)
- Updated `kMagic` format version from `"PMM_V083"` to `"PMM_V098"` to reflect layout change; old persisted files are incompatible
- Moved single-header preset files from `include/` root to `single_include/pmm/` (Issue #138), following the nlohmann::json single_include pattern
- Updated `scripts/generate-single-headers.sh` to output to `single_include/pmm/` by default
- Regenerated all four single-header preset files in `single_include/pmm/` with new block layout
- Updated `CMakeLists.txt` to expose both `include` and `single_include` paths for the `pmm` interface target

### Changed
- Consolidated `blk_at()` duplication (Issue #141): removed per-file private `blk_at()` helpers from `AllocatorPolicy` and `AvlFreeTree`; all call sites now use the single canonical `detail::block_at<AddressTraitsT>(base, idx)` template from `types.h`
- Unified `user_ptr()` duplication (Issue #141): `detail::user_ptr()` in `types.h` is now a template `detail::user_ptr<AddressTraitsT>(block)`, and `AllocatorPolicy::allocate_from_block()` now delegates to it instead of repeating the inline pointer arithmetic
- Documented 12 `get_tree_*/set_tree_*` wrapper methods in `persist_memory_manager.h` as intentional safe-adapters over `BlockStateBase` (Issue #141)
- Added explanatory comment for byte/granule conversion functions in `detail` namespace of `types.h` clarifying why they coexist with `AddressTraits` methods (Issue #141)

### Fixed
- Fixed release pipeline (Issue #138): updated `git add` paths in `.github/workflows/release.yml` to use `single_include/pmm/*.h` instead of the old `include/pmm_*.h` location, so generated single-header presets are correctly committed during auto and manual releases


## [0.6.1] - 2026-03-08

### Changed (Issue #138)
- **Merged `LinkedListNode` into `Block`**: `prev_offset` and `next_offset` fields moved directly into `Block<AddressTraitsT>` as protected member variables. `Block` now inherits only `TreeNode<AddressTraitsT>` (no longer `LinkedListNode`). The file `include/pmm/linked_list_node.h` has been deleted.
- **Updated memory layout**: With `Block : TreeNode`, `TreeNode` fields (`weight`, `left_offset`, `right_offset`, `parent_offset`, `root_offset`, `avl_height`, `node_type`) now occupy bytes 0–23, and `prev_offset`/`next_offset` occupy bytes 24–31. Total block size remains 32 bytes for `DefaultAddressTraits`.
- **Updated `BlockStateBase` offset constants**: `kOffsetWeight=0`, `kOffsetLeftOffset=4`, ..., `kOffsetAvlHeight=20`, `kOffsetNodeType=22`, `kOffsetPrevOffset=24`, `kOffsetNextOffset=28` (Issue #138).
- **Moved single-header preset files** from `include/` root to `single_include/pmm/` directory (Issue #138).
- **Updated `kMagic`** from `"PMM_V083"` to `"PMM_V098"` to reflect the binary layout change (Issue #138, #83). Old persisted files in the previous block format are no longer compatible and will fail to load.
- **Updated `scripts/generate-single-headers.sh`** to output to `single_include/pmm/` by default.
- **Updated `CMakeLists.txt`** to add `single_include` and `single_include/pmm` to `pmm` interface include directories for backward compatibility.
- **Updated `tests/CMakeLists.txt`** for single-header self-containedness tests to use `single_include` include path.
- **Updated all four single-header preset files** in `single_include/pmm/` to reflect the new layout.
- **Updated test files** (`test_issue87_phase2.cpp`, `test_issue87_phase3.cpp`, `test_issue87_abstraction.cpp`) to remove `LinkedListNode` base class checks and update layout offset assertions.
- **Updated `types.h`**: removed `#include "pmm/linked_list_node.h"`, replaced `LinkedListNode`-based `static_assert` with `Block`-based size check.


## [0.6.0] - 2026-03-08

### Added
- Single-header preset files generated by `scripts/generate-single-headers.sh` using `quom` (Issue #123): `include/pmm_single_threaded_heap.h`, `include/pmm_multi_threaded_heap.h`, `include/pmm_embedded_heap.h`, `include/pmm_industrial_db_heap.h` — each bundles the full PMM library for that configuration so users can download one file and start using the chosen preset
- Two new preset aliases in `pmm_presets.h`: `pmm::presets::EmbeddedHeap` (NoLock + HeapStorage, grow 50%, for embedded systems) and `pmm::presets::IndustrialDBHeap` (SharedMutexLock + HeapStorage, grow 100%, for industrial databases)
- CI job `single-headers` that verifies committed single-header files match what `generate-single-headers.sh` would produce, so they are never stale
- Release workflow regenerates single-header files automatically on every release
- Tests for `EmbeddedHeap` and `IndustrialDBHeap` presets (`test_issue123_presets`)
- Self-contained smoke tests for each generated single-header file (`test_issue123_sh_*`), each compiled without `include/pmm/` in the include path


## [0.5.0] - 2026-03-08

### Changed
- Migrated the library from C++17 to C++20 (Issue #129)
- Replaced SFINAE-based type traits (`std::void_t`, `std::enable_if`) with native C++20 concepts (`concept`, `requires`) in `manager_concept.h`, `storage_backend.h`, `free_block_tree.h`
- Added `PersistMemoryManagerConcept<T>`, `StorageBackendConcept<Backend>`, and `FreeBlockTreePolicyConcept<Policy>` as first-class C++20 concepts
- Replaced `static_assert(!std::is_void<ManagerT>::value, ...)` with a `requires(!std::is_void_v<ManagerT>)` constraint on `pptr<T, ManagerT>` class template
- Replaced SFINAE `std::void_t` specialization in `pptr.h` helper trait with C++20 `requires` clause
- Updated `CMakeLists.txt`: `CMAKE_CXX_STANDARD` changed from `17` to `20`
- Updated CI workflow: `cppcheck --std=c++17` changed to `--std=c++20`
- Updated `CONTRIBUTING.md`: compiler prerequisites updated to C++20-capable versions


## [0.4.0] - 2026-03-08

### Added
- AVL tree node methods to `pptr<T, ManagerT>` (Issue #125): `get_tree_left()`, `set_tree_left()`,
  `get_tree_right()`, `set_tree_right()`, `get_tree_parent()`, `set_tree_parent()`,
  `get_tree_height()`, `set_tree_height()`, `get_tree_weight()`, `set_tree_weight()`
- Corresponding static methods to `PersistMemoryManager`: `get_tree_left_offset()`,
  `set_tree_left_offset()`, `get_tree_right_offset()`, `set_tree_right_offset()`,
  `get_tree_parent_offset()`, `set_tree_parent_offset()`, `get_tree_weight()`,
  `set_tree_weight()`, `get_tree_height()`, `set_tree_height()`
- Users can now build AVL trees on top of `pptr` nodes or include a `pptr` in another AVL tree;
  all tree link methods accept only `pptr` of the same manager type enforced at compile time
- Tests for new `pptr` AVL tree methods in `test_pptr.cpp`


## [0.3.0] - 2026-03-08

### Added
- `lock_block_permanent()` method to `PersistMemoryManager` for marking blocks as permanently locked (read-only), preventing them from being freed via `deallocate()` (Issue #126)
- `is_permanently_locked()` query method to `PersistMemoryManager` (Issue #126)
- `node_type` field to `TreeNode` (renamed from `_pad`): `kNodeReadWrite` (0) and `kNodeReadOnly` (1) (Issue #126)
- New test file `test_issue108_static_model.cpp` covering `lock_block_permanent` and `is_permanently_locked` functionality (Issue #126)

### Changed
- Reordered `TreeNode` fields: `weight` moved to first position for faster cache access (Issue #126)
- `avl_height` and `node_type` (was `_pad`) moved to end of `TreeNode` layout (Issue #126)
- New `TreeNode` field order: `weight`, `left_offset`, `right_offset`, `parent_offset`, `root_offset`, `avl_height`, `node_type` (Issue #126)
- `deallocate()` now skips permanently locked blocks (`node_type == kNodeReadOnly`) (Issue #126)
- Updated `BlockStateBase` accessors to expose `node_type` field via `get_node_type()` and `set_node_type_of()` (Issue #126)


## [0.2.0] - 2026-03-08

### Added
- Pre-commit hooks configuration (`.pre-commit-config.yaml`) with clang-format, cppcheck, file size validation, and secrets detection
- Changeset-based versioning system with `changelog.d/` fragment directory
- `CHANGELOG.md` for tracking notable changes
- Release automation workflow (`.github/workflows/release.yml`) with automatic GitHub releases on version changes
- Changelog fragment validation in CI for pull requests
- `scripts/collect-changelog.sh` for collecting fragments into `CHANGELOG.md`
- Version declared in `CMakeLists.txt` following semantic versioning
- `CONTRIBUTING.md` documenting development workflow and changelog fragment process


## [0.1.0] - 2026-03-08

### Added
- Initial project structure with header-only C++17 persistent memory manager library
- Block state machine with free/used/coalescing transitions
- Best-fit allocation algorithm with AVL-tree backed free block management
- Multiple storage backends: HeapStorage, StaticStorage, MmapStorage
- Thread-safety support with configurable lock policies (NoLock, SharedMutexLock)
- Multi-instance support via InstanceId template parameter
- Persistent memory I/O (save/load) utilities
- Ready-made configuration presets (SingleThreadedHeap, MultiThreadedHeap, etc.)
- Comprehensive test suite (40+ tests covering allocation, coalescing, persistence, threading)
- Visual demo application with ImGui-based memory map visualization
- Multi-platform CI/CD pipeline (Ubuntu, macOS, Windows with GCC, Clang, MSVC)
- Code formatting enforcement with clang-format
- Static analysis with cppcheck
- File size limits (max 1500 lines per file)
- Coverage reporting with lcov and Codecov integration
- Doxygen documentation generation and GitHub Pages deployment
- Pre-commit hooks for local quality gates
- Changeset-based versioning for conflict-free changelog management
- Release automation workflow
