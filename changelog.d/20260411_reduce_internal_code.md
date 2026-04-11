---
bump: patch
---

### Changed
- Consolidated 15 repetitive `BlockStateBase` static accessor methods into `field_read_idx`/`field_write_idx` helpers with compile-time offsets
- Unified 6 statistics methods via `read_stat()` template helper, eliminating repeated double-check-initialized + shared_lock boilerplate
- Consolidated 6 tree accessor methods via `get_tree_idx_field`/`set_tree_idx_field` generic helpers
- Replaced verbose `static_cast<index_type>(0)` null-sentinel patterns in `parray`, `pstring`, `ppool` with named `detail::kNullIdx_v<AT>` constant

### Added
- `docs/internal_structure.md` — map of internal modules, authoritative files, and namespace organization
- `docs/code_reduction_report.md` — before/after metrics for structural simplification
