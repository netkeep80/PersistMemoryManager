---
bump: minor
---

### Changed
- Make `pstringview` a standard-layout persistent node with explicit length-aware interning and binary-safe `(length, bytes)` identity; remove the transient converting-constructor facade, preserve the existing persisted `length + bytes + NUL` layout, and migrate repository consumers to `pstringview::intern(...)`.
