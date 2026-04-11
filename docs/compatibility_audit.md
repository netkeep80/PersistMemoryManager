# Compatibility Audit — Issue #253

Audit of all compatibility/legacy paths in the PersistMemoryManager codebase.

## Preservation Criteria

A compatibility layer is kept **only** if at least one condition holds:

1. Promised public API that is worth maintaining.
2. Platform-specific path confirmed by tests.
3. Migration seam required by a current supported workflow.
4. Low-cost shim with high practical value.

If none apply, the path is **deleted**.

## Audit Table

| # | Item | Location | Type | Decision | Rationale |
|---|------|----------|------|----------|-----------|
| 1 | `bytes_to_granules()` | `types.h:316-320` | `[[deprecated]]` function | **Delete** | Delegates to `bytes_to_granules_t<DefaultAddressTraits>()`. No callers outside `single_include/`. No external users — marked for v1.0 removal. |
| 2 | `granules_to_bytes()` | `types.h:324-328` | `[[deprecated]]` function | **Delete** | Delegates to `DefaultAddressTraits::granules_to_bytes()`. No callers. |
| 3 | `idx_to_byte_off()` | `types.h:332-336` | `[[deprecated]]` function | **Delete** | Delegates to `DefaultAddressTraits::idx_to_byte_off()`. No callers. |
| 4 | `byte_off_to_idx()` | `types.h:340-344` | `[[deprecated]]` function | **Delete** | Delegates to `byte_off_to_idx_t<DefaultAddressTraits>()`. No callers. |
| 5 | `required_block_granules()` | `types.h:553-560` | `[[deprecated]]` function | **Delete** | Delegates to `required_block_granules_t<DefaultAddressTraits>()`. No callers. |
| 6 | `to_u32_idx<AT>()` | `types.h:414-418` | Identity function | **Delete** | No-op pass-through after ManagerHeader fields were upgraded to `index_type`. No callers. |
| 7 | `from_u32_idx<AT>()` | `types.h:422-426` | Identity function | **Delete** | Same as above — identity function with no callers. |
| 8 | `is_valid_block()` (non-templated) | `types.h:451-489` | Compat overload | **Delete** | DefaultAddressTraits-specific overload. No callers in the codebase. Templated code uses `is_valid_block_t<AT>()` or inline structural checks. |
| 9 | `block_idx()` (non-templated) | `types.h:375-381` | Compat overload | **Delete** | DefaultAddressTraits-specific overload. No callers — all code uses `block_idx_t<AT>()`. |
| 10 | `load()` (no-arg overload) | `persist_memory_manager.h:293-303` | `[[deprecated]]` method | **Delete** | Wrapper around `load(VerifyResult&)`. 6 callers in tests — updated to use `load(VerifyResult&)` directly. |
| 11 | `load_manager_from_file<MgrT>(filename)` (no-arg) | `io.h:238-242` | `[[deprecated]]` function | **Delete** | Wrapper around `load_manager_from_file<MgrT>(filename, result)`. Multiple callers in tests/examples — updated to pass `VerifyResult`. |
| 12 | `FreeBlockTreePolicyConcept` (non-templated) | `free_block_tree.h:75-76` | Compat concept alias | **Delete** | Defaults to `DefaultAddressTraits`. Replaced by `FreeBlockTreePolicyForTraitsConcept<P, AT>`. |
| 13 | `is_free_block_tree_policy_v` | `free_block_tree.h:83` | Compat trait variable | **Delete** | Uses `FreeBlockTreePolicyConcept` which is being removed. Replace usage with `FreeBlockTreePolicyForTraitsConcept<P, AT>` directly. |
| 14 | `PersistentAvlTree` type alias | `free_block_tree.h:275` | Compat alias | **Delete** | Alias for `AvlFreeTree<DefaultAddressTraits>`. Used in 2 test files — updated to use `AvlFreeTree<DefaultAddressTraits>`. |
| 15 | CRC32 zero-check (stored_crc==0 accepted) | `io.h:213,219-220` | Load behavior | **Delete** | Accepts CRC32=0 for pre-CRC32 images. Migration period has ended — all current images have CRC32 stored. |
| 16 | `kGranuleSize` (non-templated) | `types.h:61-64` | Constant | **Keep** | Used as a fundamental constant and assertion anchor. Low cost, matches `DefaultAddressTraits::granule_size`. |
| 17 | `kNoBlock` (non-templated) | `types.h:168-169` | Sentinel constant | **Keep** | Still referenced in comments; paired with `kNoBlock_v<AT>` for generic code. Low cost. |
| 18 | `kMinMemorySize` (non-templated) | `types.h:249-252` | Constant | **Keep** | Used in `persist_memory_manager.h` and `io.h` for validation. Low cost. |
| 19 | `kManagerHeaderGranules` (non-templated) | `types.h:243` | Constant | **Keep** | Low-cost constant alongside `kManagerHeaderGranules_t<AT>`. |
| 20 | `kMinBlockSize` (non-templated) | `types.h:246` | Constant | **Keep** | Low-cost constant. |
| 21 | `legacy_root_offset` in ForestDomainRegistry | `forest_registry.h:57` | Field + `set_root`/`get_root` API | **Keep** | Active public API (`set_root`/`get_root`) backed by forest registry. Single-root use is a supported workflow, not a dead path. |
| 22 | MSVC `_MSVC_LANG` check | `persist_memory_manager.h:15-21` | Platform detection | **Keep** | Platform-specific path required for MSVC (confirmed by CI). |
| 23 | Windows file operations (`atomic_rename`, MMapStorage) | `io.h`, `mmap_storage.h` | Platform-specific code | **Keep** | Required for Windows support, gated by `_WIN32`/`_WIN64`. |
| 24 | Logging policy SFINAE detection | `persist_memory_manager.h:74-85` | Config detection | **Keep** | Low-cost, non-breaking default (`NoLogging`). Part of the configuration API contract. |

## Allowed Compatibility Seams (Remaining)

| Seam | Justification |
|------|---------------|
| `legacy_root_offset` + `set_root`/`get_root` | Active public API for single-root usage. Forest registry stores it. |
| MSVC `_MSVC_LANG` | Platform-specific, confirmed by CI. |
| Windows `_WIN32` / `_WIN64` paths | Platform-specific, mmap and atomic rename. |
| Logging policy SFINAE | Low-cost config detection with safe default. |
| Non-templated constants (`kGranuleSize`, `kNoBlock`, etc.) | Low-cost, used for static assertions and validation. |
