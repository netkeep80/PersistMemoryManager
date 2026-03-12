---
bump: minor
---

### Added
- CRC32 checksum for persisted images — `save_manager()` computes and stores CRC32, `load_manager_from_file()` verifies it (Phase 2.1)
- Atomic save via write-then-rename pattern — `save_manager()` writes to temporary file, then atomically renames (Phase 2.2)
- `MMapStorage::expand()` — dynamic file growth with remap on POSIX and Windows (Phase 2.3)
- `compute_crc32()` and `compute_image_crc32()` utility functions in `pmm::detail` (Phase 2.1)

### Changed
- `ManagerHeader._reserved[8]` split into `crc32` (4 bytes) + `_reserved[4]` — struct size unchanged (Phase 2.1)
- `save_manager()` now computes CRC32 before writing and uses atomic rename (Phase 2.1, 2.2)
- `load_manager_from_file()` now verifies CRC32 before calling `load()` (Phase 2.1)
