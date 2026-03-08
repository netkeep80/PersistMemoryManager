# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!-- changelog-insert-here -->

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
