---
bump: minor
---

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
