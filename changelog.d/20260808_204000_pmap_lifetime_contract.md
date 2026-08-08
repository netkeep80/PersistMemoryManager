---
bump: minor
---

### Fixed
- Reject `pmap` key/value types that are not trivially-copyable standard-layout direct-storage representations, including raw and member pointers, so unsupported owning/non-trivial C++ object lifetimes cannot enter the raw persistent-node storage path.
