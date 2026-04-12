---
bump: minor
---

### Changed
- Narrowed canonical documentation surface: removed governance docs (repository_shape, deletion_policy, comment_policy) and index from canonical set — they remain as supporting documents
- Updated repo-guard pin to latest (7877108e84fc) with draft-aware PR behavior and semantic hardening

### Removed
- Removed Doxygen completely: deleted Doxyfile, docs.yml workflow, and all Doxygen references from README and repository_shape; Markdown docs are now the only documentation surface
