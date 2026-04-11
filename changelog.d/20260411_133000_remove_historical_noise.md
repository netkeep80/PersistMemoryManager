---
bump: patch
---

### Changed
- Removed 750+ historical issue references (`Issue #N`, `TODO for issue #N`, etc.) from code comments, Doxygen tags, and canonical documentation
- Added `docs/comment_policy.md` defining the four allowed comment types (invariant, persistence contract, safety note, design note) and prohibited patterns
- Updated `CONTRIBUTING.md` with comment policy rules to prevent re-accumulation of historical noise
