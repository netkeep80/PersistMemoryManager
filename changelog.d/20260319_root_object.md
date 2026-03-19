---
bump: minor
---

### Added
- `set_root<T>(pptr<T>)` / `get_root<T>()` — root object API in ManagerHeader (Phase 3.7, #200)
- `root_offset` field in `ManagerHeader` replaces `_reserved[4]` — stores a single persistent root pointer
- Root object survives save/load cycles and enables object discovery after heap restoration
- 13 new tests covering root set/get, persistence, all address traits, pmap registry pattern
