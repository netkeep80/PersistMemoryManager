---
bump: minor
---

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
