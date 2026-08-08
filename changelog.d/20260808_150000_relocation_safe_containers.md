---
bump: minor
---

### Fixed
- Define resolved PMM pointers as ephemeral across arena relocation and make `pstring`/`parray` growth re-resolve persistent owners instead of continuing through stale raw `this` pointers.
- Preserve arena-backed string sources and snapshot trivially-copyable `parray` inputs across relocating growth.
