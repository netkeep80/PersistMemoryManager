bump: patch

### Added
- `docs/verify_repair_contract.md` — operational contract defining the boundary between verify, repair, and load
- `docs/diagnostics_taxonomy.md` — violation types, severity levels, repair policies, and diagnostic entry format
- `test_issue256_verify_repair_contract.cpp` — 9 tests for verify/repair/load operational contract

### Changed
- Clarified ambiguous "recovery" terminology in code comments to explicitly indicate "repair (within load)"
- Regenerated single-include headers to reflect comment updates
- Updated `docs/index.md` to reference new documents and reading order
