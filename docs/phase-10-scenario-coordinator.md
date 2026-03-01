# Phase 10: Scenario Coordinator

## Overview

Phase 10 introduces `ScenarioCoordinator` — a synchronisation primitive that allows
the `PersistenceCycle` scenario to safely replace the `PersistMemoryManager` singleton
(via `destroy()` + reload) while other scenario threads are running.

This directly resolves **Risk #5** from `plan.md`:

> *Scenario 7 (Persistence Cycle) calls `PersistMemoryManager::destroy()` which changes
> the global singleton — critical if other scenarios are concurrently accessing it.*

## Problem

`PersistMemoryManager` is a process-wide singleton.  When `PersistenceCycle` calls
`PersistMemoryManager::destroy()` to simulate a save/reload lifecycle, all other
scenario threads may be in the middle of `allocate()` / `deallocate()` on the **same**
singleton.  This causes a use-after-free or data race.

The previous implementation only documented the risk in a comment; it did **not**
actually prevent other threads from using the destroyed instance.

## Solution

### ScenarioCoordinator

A new class `ScenarioCoordinator` (declared in `demo/scenarios.h`, implemented in
`demo/scenarios.cpp`) implements a **pause/resume** protocol:

```cpp
class ScenarioCoordinator {
public:
    void pause_others();
    void resume_others();
    void yield_if_paused(const std::atomic<bool>& stop_flag);
    bool is_paused() const noexcept;
private:
    std::atomic<bool>       paused_{ false };
    std::mutex              mutex_;
    std::condition_variable cv_;
};
```

### Protocol

```
PersistenceCycle thread          Other scenario threads
─────────────────────            ──────────────────────
coord.pause_others()             ← sets paused_ = true
sleep_for(50 ms)                 → yield_if_paused() blocks on cv_.wait()
PersistMemoryManager::destroy()  (all other threads are blocked)
load_from_file(...)
coord.resume_others()            → cv_.notify_all(); paused_ = false
                                 ← threads unblock and continue
```

Key safety property: `yield_if_paused()` is also conditioned on `stop_flag`.
If `stop_flag` is set (shutdown requested), the waiting thread unblocks immediately —
preventing deadlock during application exit.

### Changes to Scenario::run() signature

All scenarios now accept a `ScenarioCoordinator&` parameter:

```cpp
// Before (Phases 4-9):
virtual void run(std::atomic<bool>& stop, std::atomic<uint64_t>& ops,
                 const ScenarioParams& params) = 0;

// After (Phase 10):
virtual void run(std::atomic<bool>& stop, std::atomic<uint64_t>& ops,
                 const ScenarioParams& params, ScenarioCoordinator& coordinator) = 0;
```

Non-PersistenceCycle scenarios call `coordinator.yield_if_paused(stop)` at the top of
each iteration loop (after completing one allocation/deallocation, before starting the
next one).

### ScenarioManager owns the coordinator

`ScenarioManager` holds a single `ScenarioCoordinator coordinator_` member.
When starting a thread it passes a reference to the coordinator:

```cpp
scenarios_[i]->run(state.stop_flag, state.total_ops, params_copy, coordinator_);
```

The coordinator is also accessible via `ScenarioManager::coordinator()` for testing.

## Files Changed

| File | Change |
|------|--------|
| `demo/scenarios.h` | Added `ScenarioCoordinator` class; updated `Scenario::run()` signature |
| `demo/scenarios.cpp` | Implemented `ScenarioCoordinator`; updated all 7 scenario `run()` methods; `PersistenceCycle` uses `pause_others()`/`resume_others()`; all scenarios re-fetch `instance()` after potential pause |
| `demo/scenario_manager.h` | Added `ScenarioCoordinator coordinator_` member; added `coordinator()` accessor |
| `demo/scenario_manager.cpp` | Passes `coordinator_` to each scenario thread; added `coordinator()` implementation |
| `demo/CMakeLists.txt` | Added `test_scenario_coordinator` target |
| `.github/workflows/ci.yml` | Added `test_scenario_coordinator` to ctest `-R` filter |
| `tests/test_scenario_coordinator.cpp` | New: 5 unit tests (see below) |
| `plan.md` | Added Phase 10 section; Risk #5 marked ✅ resolved |
| `README.md` | Added `test_scenario_coordinator` to headless test list and repo structure |

## Tests (tests/test_scenario_coordinator.cpp)

| Test | What it verifies |
|------|-----------------|
| `test_pause_blocks_thread` | `yield_if_paused()` blocks until `resume_others()` is called |
| `test_resume_unblocks_all` | 5 blocked threads are all unblocked by one `resume_others()` |
| `test_no_block_when_not_paused` | `yield_if_paused()` returns immediately when no pause is active |
| `test_stop_flag_breaks_pause` | `stop_flag=true` unblocks a waiting thread even while paused |
| `test_persistence_cycle_safety` | PersistenceCycle + LinearFill + RandomStress run for 4 s; `validate()` returns `true` after all threads stop |

## Verification Criteria

- [x] All 5 `test_scenario_coordinator` tests pass.
- [x] `scenarios.cpp` ≤ 1500 lines (CI file-size check).
- [x] `scenarios.h` ≤ 1500 lines.
- [x] CI `build-demo` job passes on ubuntu-latest, windows-latest, macos-latest.
- [x] `plan.md` Risk #5 updated to ✅ Resolved.
- [x] `README.md` and `docs/phase-10-scenario-coordinator.md` updated.
