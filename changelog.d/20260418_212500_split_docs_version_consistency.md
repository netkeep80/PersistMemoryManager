---
bump: patch
---

### Changed
- Split `docs-consistency` CI into two independent concerns to remove a
  governance deadlock for atomic docs-only PRs:
  - `scripts/check-docs-consistency.sh` now only enforces docs-owned
    invariants (canonical docs existence).
  - `scripts/check-version-consistency.sh` (new) enforces release-owned
    invariants: `CMakeLists.txt` ↔ `README.md` badge ↔ `CHANGELOG.md`.
- The version-consistency check runs only when release-owned paths change,
  so docs-only PRs no longer require a forced `README.md` version bump.
- Documented the docs-owned vs release-owned surface split in
  `CONTRIBUTING.md`.
