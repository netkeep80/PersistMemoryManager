---
bump: minor
---

### Added
- `PmmError` enum — detailed error codes for `create()`, `load()`, `allocate()` and other operations (Phase 4.1)
- `last_error()` — query the most recent error code per manager specialization
- `clear_error()` — reset error code to `Ok`
- `set_last_error()` — set error code (for utility functions like `io.h`)
- CRC mismatch detection in `load_manager_from_file()` now sets `PmmError::CrcMismatch`
- 18 new tests in `test_issue201_error_codes.cpp`
