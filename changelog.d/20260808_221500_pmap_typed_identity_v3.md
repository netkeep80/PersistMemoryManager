---
bump: major
---

### Changed
- Make `pmap` typed-handle domain identity compose stable pointee semantic tags instead of structural `pptr` traits, reject untagged typed-handle map identities, provide the PMM `pstringview` tag, and advance the persistent image version from 2 to 3 without legacy hash/domain fallback or automatic migration.
