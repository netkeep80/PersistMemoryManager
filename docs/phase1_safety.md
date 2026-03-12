# Phase 1: Safety and Robustness (Issue #43)

This document describes the safety and robustness improvements implemented in Phase 1 of the development plan.

## 1.1 Exception Safety in `create_typed<T>`

**Problem:** `create_typed<T, Args...>()` and `destroy_typed<T>()` are marked `noexcept`, but placement-new could call throwing constructors. If the constructor throws, the allocated memory would leak.

**Solution:** Added `static_assert` constraints:
- `create_typed<T>(args...)` requires `std::is_nothrow_constructible_v<T, Args...>`
- `destroy_typed<T>(p)` requires `std::is_nothrow_destructible_v<T>`

For types with throwing constructors, use `allocate_typed<T>()` followed by manual placement-new with try/catch.

**Files:** `include/pmm/persist_memory_manager.h`

## 1.2 Bounds Check in `resolve()`

**Problem:** `resolve()` and `resolve_at()` did not verify that the computed offset was within the managed heap. A corrupted `pptr` could lead to out-of-bounds memory access.

**Solution:**
- Added `assert(byte_off + sizeof(T) <= total_size)` in `resolve()` for debug builds
- Added new public method `is_valid_ptr(pptr<T>)` for explicit runtime validation

```cpp
Mgr::pptr<int> p = Mgr::allocate_typed<int>();
if (Mgr::is_valid_ptr(p)) {
    // safe to dereference
    *p = 42;
}
```

**Files:** `include/pmm/persist_memory_manager.h`

## 1.3 Integer Overflow Protection

**Problem:** In `allocate_from_block()`, the computation `needed_gran = kBlkHdrGran + data_gran` could overflow for extremely large allocation requests. Similarly, `needed_gran + min_rem_gran` in the split check could overflow.

**Solution:** Added explicit overflow checks before each addition:
```cpp
if (data_gran > std::numeric_limits<index_type>::max() - kBlkHdrGran)
    return nullptr; // overflow protection
```

The same protection was added in `PersistMemoryManager::allocate()` for the `kBlockHdrGranules + data_gran` computation.

**Files:** `include/pmm/allocator_policy.h`, `include/pmm/persist_memory_manager.h`

## 1.4 Runtime Checks for Critical State Transitions

**Problem:** `assert()` is disabled in Release builds. Critical block state checks in `FreeBlock::cast_from_raw()` and `AllocatedBlock::cast_from_raw()` relied solely on `assert`, meaning corrupted block state would go undetected in production.

**Solution:** Replaced assert-only checks with runtime checks that return `nullptr` in both Debug and Release builds:
- `FreeBlock::cast_from_raw(nullptr)` returns `nullptr` (was: `assert(false)` + UB)
- `FreeBlock::cast_from_raw(non_free_block)` returns `nullptr` (was: `assert(false)` + UB)
- `AllocatedBlock::cast_from_raw(nullptr)` returns `nullptr`
- `AllocatedBlock::cast_from_raw(free_block)` returns `nullptr`

The `assert` is still present for Debug diagnostics, but the code no longer relies on it for correctness.

**Files:** `include/pmm/block_state.h`

## Tests

All Phase 1 improvements are covered by `tests/test_issue43_phase1_safety.cpp`:
- Compile-time noexcept enforcement (1.1)
- `is_valid_ptr()` with null, valid, and uninitialized pointers (1.2)
- Overflow protection with large allocation sizes (1.3)
- `cast_from_raw()` null-safety (1.4)
- Full allocation/deallocation integration test
