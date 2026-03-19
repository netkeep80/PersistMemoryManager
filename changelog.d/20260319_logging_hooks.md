---
bump: minor
---

### Added
- Logging hooks via `logging_policy` template parameter in config (Issue #202, Phase 4.2)
- `logging::NoLogging` — default no-op policy with zero overhead
- `logging::StderrLogging` — logs events and errors to stderr
- Hooks: `on_allocation_failure()`, `on_expand()`, `on_corruption_detected()`, `on_create()`, `on_destroy()`, `on_load()`
- SFINAE-based detection: configs without `logging_policy` default to `NoLogging` (backward compatible)
- 14 tests in `test_issue202_logging_hooks.cpp`
