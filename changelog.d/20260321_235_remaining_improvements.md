---
bump: minor
---

### Added
- RAII `typed_guard<T, ManagerT>` scope-guard for persistent containers (`pstring`, `parray`, `ppool`) — auto-calls `free_data()`/`free_all()` + `destroy_typed()` on scope exit (Issue #235)
- `PersistMemoryManager::make_guard<T>()` convenience factory method (Issue #235)
- `HasFreeData` and `HasFreeAll` C++20 concepts for cleanup method detection (Issue #235)
- ASan+UBSan and TSan CI jobs for GCC and Clang (Issue #235)

### Changed
- `HeapStorage::expand()` uses 4096-byte minimum initial allocation to avoid inefficient tiny allocations from zero (Issue #235)
- Deprecated functions in `types.h` now emit compiler warnings via `[[deprecated]]` attribute with v1.0 removal notice (Issue #235)
- Internal code migrated from deprecated `detail::idx_to_byte_off()` / `detail::byte_off_to_idx()` to non-deprecated `_t` variants (Issue #235)
- cppcheck CI suppressions documented with rationale comments (Issue #235)

### Fixed
- `detail::required_block_granules()` no longer internally calls deprecated `bytes_to_granules()` (Issue #235)
