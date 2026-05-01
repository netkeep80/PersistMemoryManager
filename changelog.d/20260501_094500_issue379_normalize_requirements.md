---
bump: patch
---

### Added
- Add include source review notes and requirement templates under `req/`.
- Add extended requirements catalog validator (`scripts/check-requirements-catalog.py`).
- Validate Markdown links in supporting catalog files
  (`req/templates/*.md`, `req/README.md`, `req/13_traceability_matrix.md`)
  while ignoring fenced code blocks and inline code spans, which are
  documentation snippets rather than navigable links from the file
  containing them.

### Changed
- Normalize requirements catalog to verbose Russian format.

### Fixed
- Fix broken anchor links in `req/templates/data_requirement.md` and
  `req/templates/quality_attribute.md` so that they reference real PMM
  anchors (`pmm-detail-managerheader` in `include/pmm/types.h` and
  `pmm-verifyresult` in `include/pmm/diagnostics.h`), restoring
  `test_issue354_include_anchors` across all platforms.
