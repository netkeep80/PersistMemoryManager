# Phase 2: Persistence and Reliability (Issue #43)

This document describes the persistence and reliability improvements implemented in Phase 2 of the development plan.

## 2.1 CRC32 Checksum for Persisted Images

**Problem:** `save_manager()` / `load_manager_from_file()` performed raw binary copy without any integrity checks. A corrupted file would be loaded silently, potentially corrupting the heap.

**Solution:** Added CRC32 (ISO 3309, polynomial 0xEDB88320) checksum computation and verification:

- **On save:** `save_manager()` computes CRC32 over the entire managed region (treating the `crc32` field itself as zero) and stores it in `ManagerHeader.crc32`.
- **On load:** `load_manager_from_file()` recomputes the CRC32 and compares it to the stored value. If they don't match, load fails.
- **Backward compatibility:** Images with `crc32 == 0` (saved before Phase 2.1) are accepted without CRC verification.

The CRC32 field uses 4 bytes from the previously reserved `_reserved[8]` space in `ManagerHeader`. The struct size remains unchanged (64 bytes for DefaultAddressTraits).

**Files:** `include/pmm/types.h` (CRC32 utility + header field), `include/pmm/io.h` (save/load integration)

## 2.2 Atomic Save (Write-Then-Rename)

**Problem:** If the process crashed during `fwrite()`, the output file would be corrupted with a partial write, destroying the previous valid image.

**Solution:** `save_manager()` now uses the write-then-rename pattern:
1. Writes the entire image to a temporary file (`filename.tmp`)
2. Flushes the file buffer with `fflush()`
3. Atomically renames the temporary file to the target filename

On POSIX, `rename()` is atomic if source and destination are on the same filesystem. On Windows, `MoveFileExA` with `MOVEFILE_REPLACE_EXISTING` is used.

If any step fails, the temporary file is cleaned up and the original file remains untouched.

**Files:** `include/pmm/io.h`

## 2.3 MMapStorage Expand Support

**Problem:** `MMapStorage::expand()` always returned `false`. Persistent databases backed by mmap files could not grow dynamically.

**Solution:** Implemented `expand()` through remap:

**POSIX:**
1. `munmap()` the current mapping
2. `ftruncate()` the file to the new size
3. `mmap()` a new mapping at the new size

**Windows:**
1. `FlushViewOfFile()` + `UnmapViewOfFile()` the current view
2. Close the file mapping handle
3. `SetFilePointerEx()` + `SetEndOfFile()` to resize the file
4. `CreateFileMappingA()` + `MapViewOfFile()` to create a new mapping

Growth strategy mirrors HeapStorage: grow by 25% plus the requested additional bytes.

**Important:** After `expand()`, `base_ptr()` returns a new address. All previously obtained raw pointers into the mapping are invalidated. Persistent pointers (`pptr<T>`) remain valid since they use granule indices, not raw pointers.

If the remap fails, the implementation attempts to restore the mapping at the old size.

**Files:** `include/pmm/mmap_storage.h`

## Tests

All Phase 2 improvements are covered by `tests/test_issue43_phase2_persistence.cpp`:

- CRC32 known test vectors (2.1)
- CRC32 save/load roundtrip (2.1)
- CRC32 corruption detection (2.1)
- CRC32 backward compatibility with pre-Phase 2 images (2.1)
- `compute_image_crc32` ignores the crc32 field itself (2.1)
- Atomic save leaves no temporary files (2.2)
- Atomic save correctly replaces previous files (2.2)
- MMapStorage expand basic functionality (2.3)
- MMapStorage expand with zero bytes (2.3)
- MMapStorage expand on unmapped storage (2.3)
- MMapStorage expand multiple times (2.3)
