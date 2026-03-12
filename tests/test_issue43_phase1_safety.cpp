/**
 * @file test_issue43_phase1_safety.cpp
 * @brief Tests for Phase 1 safety improvements (Issue #43).
 *
 * Verifies:
 *  - 1.1 static_assert for noexcept constructibility in create_typed / destroy_typed
 *  - 1.2 Bounds check in resolve() and is_valid_ptr() method
 *  - 1.3 Overflow protection in allocator_policy and allocate()
 *  - 1.4 Runtime checks in cast_from_raw (FreeBlock, AllocatedBlock)
 *
 * @see docs/phase1_safety.md
 * @version 0.1 (Issue #43 — Phase 1: Safety and robustness)
 */

#include "pmm/pmm_presets.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

// ─── Test macros ──────────────────────────────────────────────────────────────

#define PMM_TEST( expr )                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( expr ) )                                                                                               \
        {                                                                                                              \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " << #expr << "\n";                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while ( false )

#define PMM_RUN( name, fn )                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "  " << name << " ... ";                                                                          \
        if ( fn() )                                                                                                    \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

// ─── Manager alias for tests ──────────────────────────────────────────────────

using M = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 43>;

// =============================================================================
// 1.1: static_assert for noexcept constructibility
// =============================================================================

/// A type with a noexcept constructor — should work with create_typed.
struct NoexceptType
{
    int value;
    explicit NoexceptType( int v ) noexcept : value( v ) {}
    ~NoexceptType() noexcept = default;
};

/// Compile-time checks that the static_assert constraints are correct.
static_assert( std::is_nothrow_constructible_v<NoexceptType, int>,
               "NoexceptType must be nothrow-constructible from int" );
static_assert( std::is_nothrow_destructible_v<NoexceptType>, "NoexceptType must be nothrow-destructible" );

/// Test that create_typed works with noexcept types.
static bool test_i43_create_typed_noexcept_type()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    auto p = M::create_typed<NoexceptType>( 42 );
    PMM_TEST( !p.is_null() );

    NoexceptType* obj = M::resolve( p );
    PMM_TEST( obj != nullptr );
    PMM_TEST( obj->value == 42 );

    M::destroy_typed( p );
    M::destroy();
    return true;
}

/// Test that create_typed with default-constructible noexcept type works.
static bool test_i43_create_typed_default_noexcept()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    auto p = M::create_typed<int>( 100 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( *p == 100 );

    M::destroy_typed( p );
    M::destroy();
    return true;
}

// Note: We cannot test that a throwing constructor causes a compile error
// in runtime tests, but the static_assert in create_typed guarantees it.
// The following would fail to compile (as expected):
//   struct ThrowingType { ThrowingType(int) { throw 1; } };
//   M::create_typed<ThrowingType>(42); // COMPILE ERROR: static_assert fails

// =============================================================================
// 1.2: Bounds check in resolve() and is_valid_ptr()
// =============================================================================

/// Test is_valid_ptr returns false for null pptr.
static bool test_i43_is_valid_ptr_null()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    M::pptr<int> null_p;
    PMM_TEST( null_p.is_null() );
    PMM_TEST( !M::is_valid_ptr( null_p ) );

    M::destroy();
    return true;
}

/// Test is_valid_ptr returns true for a valid allocated pointer.
static bool test_i43_is_valid_ptr_valid()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    auto p = M::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( M::is_valid_ptr( p ) );

    M::deallocate_typed( p );
    M::destroy();
    return true;
}

/// Test is_valid_ptr returns false when manager is not initialized.
static bool test_i43_is_valid_ptr_uninitialized()
{
    M::destroy();
    // Manager is not initialized
    M::pptr<int> fake_p;
    PMM_TEST( !M::is_valid_ptr( fake_p ) );
    return true;
}

/// Test resolve returns valid pointer for a properly allocated block.
static bool test_i43_resolve_valid()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    auto p = M::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    int* raw = M::resolve( p );
    PMM_TEST( raw != nullptr );
    *raw = 123;
    PMM_TEST( *p == 123 );

    M::deallocate_typed( p );
    M::destroy();
    return true;
}

// =============================================================================
// 1.3: Overflow protection in allocate
// =============================================================================

/// Test that allocating with size zero returns nullptr, and the overflow
/// protection in allocator_policy correctly guards against index_type overflow.
/// Note: CacheManagerConfig uses auto-expand, so "bigger than heap" requests may succeed.
/// We test the overflow path specifically.
static bool test_i43_allocate_overflow_size()
{
    // Use EmbeddedStaticConfig which has a fixed-size static buffer (no expansion).
    using StaticM = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<4096>, 4301>;
    StaticM::destroy();
    PMM_TEST( StaticM::create() );

    // Try to allocate more than the static heap — should fail
    void* p = StaticM::allocate( 8192 );
    PMM_TEST( p == nullptr );

    // Normal allocation should still work after failed oversized requests
    void* p3 = StaticM::allocate( 64 );
    PMM_TEST( p3 != nullptr );
    StaticM::deallocate( p3 );

    StaticM::destroy();
    return true;
}

/// Test that allocate_typed with huge count returns null (overflow guard).
static bool test_i43_allocate_typed_overflow()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    // count * sizeof(int) would overflow size_t
    auto p = M::allocate_typed<int>( std::numeric_limits<std::size_t>::max() );
    PMM_TEST( p.is_null() );

    M::destroy();
    return true;
}

// =============================================================================
// 1.4: Runtime checks in cast_from_raw
// =============================================================================

/// Test that FreeBlock::cast_from_raw returns nullptr for null input.
static bool test_i43_freeblock_cast_null()
{
    void* null_ptr = nullptr;
    auto* result   = pmm::FreeBlock<pmm::DefaultAddressTraits>::cast_from_raw( null_ptr );
    PMM_TEST( result == nullptr );
    return true;
}

/// Test that AllocatedBlock::cast_from_raw returns nullptr for null input.
static bool test_i43_allocatedblock_cast_null()
{
    void* null_ptr = nullptr;
    auto* result   = pmm::AllocatedBlock<pmm::DefaultAddressTraits>::cast_from_raw( null_ptr );
    PMM_TEST( result == nullptr );
    return true;
}

/// Test that normal allocation/deallocation cycle works after all safety changes.
static bool test_i43_full_cycle()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    // Allocate multiple typed objects
    auto p1 = M::create_typed<NoexceptType>( 10 );
    auto p2 = M::create_typed<NoexceptType>( 20 );
    auto p3 = M::create_typed<NoexceptType>( 30 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    // Verify values
    PMM_TEST( M::resolve( p1 )->value == 10 );
    PMM_TEST( M::resolve( p2 )->value == 20 );
    PMM_TEST( M::resolve( p3 )->value == 30 );

    // Verify is_valid_ptr
    PMM_TEST( M::is_valid_ptr( p1 ) );
    PMM_TEST( M::is_valid_ptr( p2 ) );
    PMM_TEST( M::is_valid_ptr( p3 ) );

    // Destroy in different order
    M::destroy_typed( p2 );
    M::destroy_typed( p1 );
    M::destroy_typed( p3 );

    M::destroy();
    return true;
}

/// Test that allocate size 0 returns nullptr.
static bool test_i43_allocate_zero()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    void* p = M::allocate( 0 );
    PMM_TEST( p == nullptr );

    M::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== Issue #43 Phase 1: Safety and Robustness ===\n";
    bool all_passed = true;

    // 1.1: static_assert for noexcept
    std::cout << "\n--- 1.1: static_assert for noexcept constructibility ---\n";
    PMM_RUN( "create_typed with noexcept type", test_i43_create_typed_noexcept_type );
    PMM_RUN( "create_typed with default noexcept int", test_i43_create_typed_default_noexcept );

    // 1.2: Bounds check in resolve() and is_valid_ptr()
    std::cout << "\n--- 1.2: Bounds check and is_valid_ptr ---\n";
    PMM_RUN( "is_valid_ptr null", test_i43_is_valid_ptr_null );
    PMM_RUN( "is_valid_ptr valid", test_i43_is_valid_ptr_valid );
    PMM_RUN( "is_valid_ptr uninitialized", test_i43_is_valid_ptr_uninitialized );
    PMM_RUN( "resolve valid", test_i43_resolve_valid );

    // 1.3: Overflow protection
    std::cout << "\n--- 1.3: Overflow protection ---\n";
    PMM_RUN( "allocate overflow size", test_i43_allocate_overflow_size );
    PMM_RUN( "allocate_typed overflow", test_i43_allocate_typed_overflow );
    PMM_RUN( "allocate zero", test_i43_allocate_zero );

    // 1.4: Runtime checks in cast_from_raw
    std::cout << "\n--- 1.4: Runtime checks in cast_from_raw ---\n";
    PMM_RUN( "FreeBlock cast_from_raw null", test_i43_freeblock_cast_null );
    PMM_RUN( "AllocatedBlock cast_from_raw null", test_i43_allocatedblock_cast_null );

    // Full cycle integration test
    std::cout << "\n--- Integration ---\n";
    PMM_RUN( "full allocation cycle", test_i43_full_cycle );

    std::cout << "\n" << ( all_passed ? "ALL PASSED" : "SOME TESTS FAILED" ) << "\n";
    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
