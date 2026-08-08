---
bump: patch
---

### Fixed
- Pin PMM's blocking repo-guard check to the immutable revision that evaluates long-lived pull requests against the current trusted base branch head rather than a stale PR base snapshot, while preserving blocking enforcement and read-only workflow permissions.
