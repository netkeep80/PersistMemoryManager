# Phase 1: Safety and Robustness

This document describes the safety and robustness improvements implemented as part of Phase 1 of the PersistMemoryManager development plan.

## Overview

Phase 1 focuses on hardening the library against common safety issues:
- Exception safety in typed allocation APIs
- Bounds checking for pointer resolution
- Integer overflow protection in size calculations
- Runtime checks for critical invariants

---

## 1.1 Exception Safety in `create_typed<T>` ✅ Completed

**Issue:** Methods `create_typed<T, Args...>()` and `destroy_typed<T>()` are marked `noexcept`, but placement-new can call throwing constructors. If a constructor throws an exception, the allocated memory would leak.

### Solution

Added compile-time enforcement via `static_assert`:

```cpp
// In create_typed<T>(args...)
static_assert( std::is_nothrow_constructible_v<T, Args...>,
               "create_typed<T>(args...) requires T(args...) to be noexcept. "
               "A throwing constructor would cause a memory leak because create_typed is noexcept." );

// In destroy_typed<T>(p)
static_assert( std::is_nothrow_destructible_v<T>,
               "destroy_typed<T>(p) requires ~T() to be noexcept. "
               "A throwing destructor would cause std::terminate because destroy_typed is noexcept." );
```

### Impact

- **Compile-time safety:** Users get a clear error message if they try to use `create_typed` with a type that has a throwing constructor.
- **No runtime overhead:** The check happens entirely at compile time.
- **Documentation:** Doxygen comments clearly state the `noexcept` requirements.

### Example: Types that work with create_typed

```cpp
// Good: noexcept constructor and destructor
struct SafeType {
    int value;
    SafeType(int v) noexcept : value(v) {}
    ~SafeType() noexcept = default;
};

Mgr::create_typed<SafeType>(42);  // OK - compiles and works
```

### Example: Types that would fail compilation

```cpp
// Bad: throwing constructor
struct UnsafeType {
    int value;
    UnsafeType(int v) : value(v) {  // NOT noexcept!
        if (v < 0) throw std::runtime_error("Negative value");
    }
};

Mgr::create_typed<UnsafeType>(42);  // ERROR - static_assert fails at compile time
```

### Tests

See `tests/test_plan_phase1_1.cpp` for comprehensive tests verifying:
- Default constructor works correctly
- Constructor with arguments works correctly
- Copy constructor works correctly
- Move constructor works correctly
- Destructor is called correctly by `destroy_typed`
- Primitive types (int, double) work correctly
- POD types work correctly

---

## 1.2 Bounds Checking in `resolve()` ⏳ Pending

**Issue:** `resolve()` and `resolve_at()` do not verify that the computed offset is within heap bounds. A corrupted `pptr` could lead to out-of-bounds access.

**Planned solution:**
- Add `assert(offset < total_granules)` check in debug builds
- Add `is_valid_ptr(pptr<T>)` method for explicit validation

---

## 1.3 Integer Overflow Protection ⏳ Pending

**Issue:** `needed_gran = kBlkHdrGran + data_gran` can overflow for large allocation requests.

**Planned solution:**
```cpp
if ( data_gran > std::numeric_limits<index_type>::max() - kBlkHdrGran )
    return nullptr; // overflow protection
```

---

## 1.4 Runtime Checks for Critical Invariants ⏳ Pending

**Issue:** `assert()` is disabled in Release builds. Critical state machine transitions should work in Release mode too.

**Planned solution:**
- Separate critical invariants into runtime checks with error returns
- Keep `assert()` only for internal invariants that indicate bugs in the library itself

---

*Document created 2026-03-12 as part of Plan Phase 1 implementation.*
