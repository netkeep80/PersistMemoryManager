---
bump: minor
---

### Added
- Explicit verify mode (`Mgr::verify()`) — read-only structural diagnostics without modifying the image (Issue #245)
- Explicit repair mode (`Mgr::load(VerifyResult&)`) — load with documented repair reporting (Issue #245)
- Structured diagnostics: `RecoveryMode`, `ViolationType`, `DiagnosticAction`, `DiagnosticEntry`, `VerifyResult` types in `diagnostics.h` (Issue #245)
- `BlockStateBase::verify_state()` — read-only counterpart of `recover_state()` (Issue #245)
- `AllocatorPolicy::verify_linked_list()`, `verify_counters()`, `verify_block_states()` — read-only structural checks (Issue #245)
- `verify_repair_mixin.inc` — extracted verify/repair implementation for file-size compliance (Issue #245)
- Regression tests: 9 test cases in `test_issue245_verify_repair.cpp` (Issue #245)

### Changed
- `load()` no longer silently masks corruption; now detects violations before applying repairs (Issue #245)
- Recovery documentation (`docs/recovery.md`) updated with verify/repair API and violation types (Issue #245)
