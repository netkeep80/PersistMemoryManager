# Code Reduction Report — Issue #254

## Summary

Structural simplification of internal code: consolidated repetitive patterns,
introduced shared helpers, and established one authoritative implementation path
per capability.

**Total LOC change: 9,661 → 9,556 (−105 lines, −1.1%)**

## Changes by File

| File | Before | After | Δ | What changed |
|------|--------|-------|---|--------------|
| `block_state.h` | 907 | 802 | −105 | Replaced 15 identical cast-and-delegate static accessor methods with `field_read_idx`/`field_write_idx` helpers. Compressed free-standing wrapper functions. |
| `persist_memory_manager.h` | 1,419 | 1,388 | −31 | Extracted `get_tree_idx_field`/`set_tree_idx_field` to eliminate 6 near-identical tree accessor methods. Extracted `read_stat()` template to unify 6 statistics methods. |
| `types.h` | 459 | 464 | +5 | Added `detail::kNullIdx_v<AT>` constant for the null granule index sentinel. |
| `parray.h` | 452 | 452 | 0 | Replaced `static_cast<index_type>(0)` with `kNullIdx_v` (no line count change). |
| `pstring.h` | 319 | 319 | 0 | Replaced `static_cast<index_type>(0)` with `kNullIdx_v`. |
| `ppool.h` | 337 | 337 | 0 | Replaced `static_cast<index_type>(0)` with `kNullIdx_v`. |

## Structural Duplication Eliminated

### 1. Block field access (block_state.h)

**Before:** 15 static methods, each with identical structure:
```cpp
static index_type get_X( const void* raw_blk ) noexcept
{
    return reinterpret_cast<const BlockStateBase*>( raw_blk )->X();
}
static void set_X_of( void* raw_blk, index_type v ) noexcept
{
    reinterpret_cast<BlockStateBase*>( raw_blk )->set_X( v );
}
```

**After:** Two shared helpers + one-line delegates:
```cpp
static index_type field_read_idx( const void* raw_blk, std::size_t offset ) noexcept;
static void field_write_idx( void* raw_blk, std::size_t offset, index_type v ) noexcept;

static index_type get_left_offset( const void* b ) noexcept { return field_read_idx( b, kOffsetLeftOffset ); }
```

**Impact:** −105 lines. Single authoritative field access path.

### 2. Tree accessor boilerplate (persist_memory_manager.h)

**Before:** 6 get/set methods with identical guard logic:
```cpp
template <typename T> static index_type get_tree_left_offset( pptr<T> p ) noexcept
{
    if ( p.is_null() || !_initialized ) return 0;
    index_type v = BlockStateBase<address_traits>::get_left_offset( block_raw_ptr_from_pptr( p ) );
    return ( v == address_traits::no_block ) ? static_cast<index_type>( 0 ) : v;
}
// ... repeated for right, parent, and 3 setters
```

**After:** Two generic helpers + one-line delegates:
```cpp
template <typename T> static index_type get_tree_idx_field( pptr<T> p, getter ) noexcept;
template <typename T> static void set_tree_idx_field( pptr<T> p, setter, value ) noexcept;
```

### 3. Statistics double-check pattern (persist_memory_manager.h)

**Before:** 6 methods repeating:
```cpp
if ( !_initialized.load( std::memory_order_acquire ) ) return 0;
typename thread_policy::shared_lock_type lock( _mutex );
if ( !_initialized.load( std::memory_order_relaxed ) ) return 0;
// ... read one field
```

**After:** Single `read_stat(fn)` template:
```cpp
template <typename Fn> static std::size_t read_stat( Fn fn ) noexcept;
```

### 4. Null index sentinel (types.h → parray/pstring/ppool)

**Before:** Verbose `static_cast<index_type>(0)` scattered across 3 files (~20 occurrences).

**After:** Named constant `detail::kNullIdx_v<AT>` with clear semantic intent.

## Layers Removed or Simplified

| Layer | Status | Notes |
|-------|--------|-------|
| Per-field static accessors in BlockStateBase | Consolidated | 15 methods → 2 helpers + 10 one-liners |
| Free-standing `recover_block_state`/`verify_block_state` | Compressed | Multi-line wrappers → 2-line aliases |
| Per-statistic double-check-initialized pattern | Consolidated | 6 copies → 1 template helper |
| Per-tree-field get/set guard logic | Consolidated | 6 methods → 2 generic helpers |

## Verification

- All 74 tests pass after changes.
- No new public API surface added.
- No existing public API removed.
- `single_include/` headers regenerated from modular sources.
