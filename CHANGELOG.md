# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!-- changelog-insert-here -->

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
