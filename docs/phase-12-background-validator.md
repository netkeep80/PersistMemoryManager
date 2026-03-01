# Phase 12: Background Integrity Validator

## Overview

Phase 12 implements a **periodic integrity check** of the PMM heap by calling
`mgr->validate()` automatically every 5 seconds while the demo app is running.
The result is displayed in the **Metrics** panel alongside a manual
**"Validate now"** button that lets the user trigger an on-demand check.

This fulfils the requirement from `demo.md` section 11:

> `mgr->validate()` — Фоновая проверка каждые 5 с

## Problem

Before Phase 12 `validate()` was only called inside the `PersistenceCycle`
scenario (after a save/reload cycle) and in the headless smoke test.  The
Metrics panel gave no indication of whether the PMM heap was structurally sound
during normal operation.

## Solution

### New Type: `ValidationResult` (`demo/metrics_view.h`)

```cpp
struct ValidationResult {
    enum class State { Unknown, Ok, Failed };
    State                              state     = State::Unknown;
    std::chrono::steady_clock::time_point timestamp{};
};
```

- `Unknown` — initial state (validate has never been called).
- `Ok` — the last `validate()` call returned `true`.
- `Failed` — the last `validate()` call returned `false`.

### Changes to `MetricsView`

Two new methods:

```cpp
void update_validation(const ValidationResult& result);
bool validate_requested() const noexcept;
```

`update_validation()` stores the result and is called by `DemoApp` after every
validate run.  `validate_requested()` returns `true` for one frame after the
user clicks **"Validate now"**; `DemoApp` polls this flag to trigger an
immediate check.

The Metrics panel now shows a status line below the scrolling plots:

```
Integrity check (validate):  OK  (3 s ago)  [Validate now]
```

Colour coding:
- **pending...** (grey) — not yet checked.
- **OK** (green) + elapsed seconds.
- **FAILED** (red) + elapsed seconds.

### Changes to `DemoApp`

New fields:

| Field | Type | Purpose |
|-------|------|---------|
| `kValidateIntervalSec` | `static constexpr long long` (= 5) | Seconds between auto-checks |
| `last_validation_` | `ValidationResult` | Most recent result |
| `last_validate_time_` | `steady_clock::time_point` | When the last check ran |
| `first_validate_` | `bool` | Run validate() on the very first frame |

Logic in `DemoApp::render()` (after snapshot collection):

1. Compute `elapsed = now - last_validate_time_` in seconds.
2. If `first_validate_` or `elapsed >= kValidateIntervalSec` → call `run_validate(mgr)`.
3. Push `last_validation_` to `metrics_view_->update_validation(...)`.
4. After `metrics_view_->render()`: if `validate_requested()` is true → call
   `run_validate(mgr)` immediately.

`apply_pmm_size()` resets `first_validate_ = true` and clears `last_validation_`
so that the freshly created PMM is validated on the very next frame.

## Files Changed

| File | Change |
|------|--------|
| `demo/metrics_view.h` | Added `ValidationResult` struct; added `update_validation()`, `validate_requested()` methods and private fields |
| `demo/metrics_view.cpp` | Implemented `update_validation()`; extended `render()` with status line and "Validate now" button |
| `demo/demo_app.h` | Added `kValidateIntervalSec`, `last_validation_`, `last_validate_time_`, `first_validate_`, `run_validate()` |
| `demo/demo_app.cpp` | Implemented `run_validate()`; added periodic-check logic and button-press handling to `render()`; reset state in `apply_pmm_size()` |
| `demo/CMakeLists.txt` | Added `test_background_validator` target |
| `.github/workflows/ci.yml` | Added `test_background_validator` to ctest `-R` filter |
| `tests/test_background_validator.cpp` | New: 9 unit tests (see below) |
| `plan.md` | Added Phase 12 to overview table and full phase description |
| `README.md` | Added `test_background_validator` to headless test list and docs reference |
| `docs/phase-12-background-validator.md` | This file |

## Tests (`tests/test_background_validator.cpp`)

| Test | What it verifies |
|------|-----------------|
| `validation_result_default_state` | `ValidationResult` default-initialised to `State::Unknown` |
| `metrics_view_initial_state` | `validate_requested_` is `false` on construction |
| `metrics_view_update_validation_ok` | `update_validation(Ok)` does not crash; `MetricsView` remains usable |
| `metrics_view_update_validation_failed` | `update_validation(Failed)` does not crash |
| `validate_fresh_pmm_returns_ok` | `mgr->validate()` returns `true` on a freshly created PMM |
| `validate_after_allocations` | `validate()` returns `true` after alloc/dealloc sequence |
| `validation_timestamp_is_recent` | `timestamp` stored in `ValidationResult` is within 1 s of the call |
| `validate_interval_is_five_seconds` | `DemoApp::kValidateIntervalSec == 5` |
| `update_validation_last_wins` | Successive `update_validation()` calls overwrite the stored result |
| `validation_state_enum_values` | The three `State` enum values (`Unknown`, `Ok`, `Failed`) are distinct |

## Verification Criteria

- [x] All 10 `test_background_validator` tests pass.
- [x] `metrics_view.h` ≤ 1500 lines.
- [x] `metrics_view.cpp` ≤ 1500 lines.
- [x] `demo_app.h` ≤ 1500 lines.
- [x] `demo_app.cpp` ≤ 1500 lines.
- [x] `test_background_validator.cpp` ≤ 1500 lines.
- [x] CI `build-demo` job passes on ubuntu-latest, windows-latest, macos-latest.
- [x] `plan.md` Phase 12 section added to overview table and full description.
- [x] `README.md` and `docs/phase-12-background-validator.md` updated.
