---
bump: minor
---

### Fixed
- Reject `pmap` key/value types that are not trivially-copyable standard-layout raw persistent representations, including raw and member pointers, so unsupported non-trivial C++ object lifetimes cannot enter the current allocation/deallocation path while persistent `pptr` handles remain supported.
