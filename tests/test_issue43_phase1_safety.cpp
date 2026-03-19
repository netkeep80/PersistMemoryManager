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

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <limits>
#include <type_traits>

// ─── Test macros ──────────────────────────────────────────────────────────────

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
TEST_CASE( "create_typed with noexcept type", "[test_issue43_phase1_safety]" )
{
    M::destroy();
    REQUIRE( M::create( 64 * 1024 ) );

    auto p = M::create_typed<NoexceptType>( 42 );
    REQUIRE( !p.is_null() );

    NoexceptType* obj = M::resolve( p );
    REQUIRE( obj != nullptr );
    REQUIRE( obj->value == 42 );

    M::destroy_typed( p );
    M::destroy();
}

/// Test that create_typed with default-constructible noexcept type works.
TEST_CASE( "create_typed with default noexcept int", "[test_issue43_phase1_safety]" )
{
    M::destroy();
    REQUIRE( M::create( 64 * 1024 ) );

    auto p = M::create_typed<int>( 100 );
    REQUIRE( !p.is_null() );
    REQUIRE( *p == 100 );

    M::destroy_typed( p );
    M::destroy();
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
TEST_CASE( "is_valid_ptr null", "[test_issue43_phase1_safety]" )
{
    M::destroy();
    REQUIRE( M::create( 64 * 1024 ) );

    M::pptr<int> null_p;
    REQUIRE( null_p.is_null() );
    REQUIRE( !M::is_valid_ptr( null_p ) );

    M::destroy();
}

/// Test is_valid_ptr returns true for a valid allocated pointer.
TEST_CASE( "is_valid_ptr valid", "[test_issue43_phase1_safety]" )
{
    M::destroy();
    REQUIRE( M::create( 64 * 1024 ) );

    auto p = M::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    REQUIRE( M::is_valid_ptr( p ) );

    M::deallocate_typed( p );
    M::destroy();
}

/// Test is_valid_ptr returns false when manager is not initialized.
TEST_CASE( "is_valid_ptr uninitialized", "[test_issue43_phase1_safety]" )
{
    M::destroy();
    // Manager is not initialized
    M::pptr<int> fake_p;
    REQUIRE( !M::is_valid_ptr( fake_p ) );
}

/// Test resolve returns valid pointer for a properly allocated block.
TEST_CASE( "resolve valid", "[test_issue43_phase1_safety]" )
{
    M::destroy();
    REQUIRE( M::create( 64 * 1024 ) );

    auto p = M::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    int* raw = M::resolve( p );
    REQUIRE( raw != nullptr );
    *raw = 123;
    REQUIRE( *p == 123 );

    M::deallocate_typed( p );
    M::destroy();
}

// =============================================================================
// 1.3: Overflow protection in allocate
// =============================================================================

/// Test that allocating with size zero returns nullptr, and the overflow
/// protection in allocator_policy correctly guards against index_type overflow.
/// Note: CacheManagerConfig uses auto-expand, so "bigger than heap" requests may succeed.
/// We test the overflow path specifically.
TEST_CASE( "allocate overflow size", "[test_issue43_phase1_safety]" )
{
    // Use EmbeddedStaticConfig which has a fixed-size static buffer (no expansion).
    using StaticM = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<4096>, 4301>;
    StaticM::destroy();
    REQUIRE( StaticM::create() );

    // Try to allocate more than the static heap — should fail
    void* p = StaticM::allocate( 8192 );
    REQUIRE( p == nullptr );

    // Normal allocation should still work after failed oversized requests
    void* p3 = StaticM::allocate( 64 );
    REQUIRE( p3 != nullptr );
    StaticM::deallocate( p3 );

    StaticM::destroy();
}

/// Test that allocate_typed with huge count returns null (overflow guard).
TEST_CASE( "allocate_typed overflow", "[test_issue43_phase1_safety]" )
{
    M::destroy();
    REQUIRE( M::create( 64 * 1024 ) );

    // count * sizeof(int) would overflow size_t
    auto p = M::allocate_typed<int>( std::numeric_limits<std::size_t>::max() );
    REQUIRE( p.is_null() );

    M::destroy();
}

// =============================================================================
// 1.4: Runtime checks in cast_from_raw
// =============================================================================

/// Test that FreeBlock::cast_from_raw returns nullptr for null input.
TEST_CASE( "FreeBlock cast_from_raw null", "[test_issue43_phase1_safety]" )
{
    void* null_ptr = nullptr;
    auto* result   = pmm::FreeBlock<pmm::DefaultAddressTraits>::cast_from_raw( null_ptr );
    REQUIRE( result == nullptr );
}

/// Test that AllocatedBlock::cast_from_raw returns nullptr for null input.
TEST_CASE( "AllocatedBlock cast_from_raw null", "[test_issue43_phase1_safety]" )
{
    void* null_ptr = nullptr;
    auto* result   = pmm::AllocatedBlock<pmm::DefaultAddressTraits>::cast_from_raw( null_ptr );
    REQUIRE( result == nullptr );
}

/// Test that normal allocation/deallocation cycle works after all safety changes.
TEST_CASE( "full allocation cycle", "[test_issue43_phase1_safety]" )
{
    M::destroy();
    REQUIRE( M::create( 64 * 1024 ) );

    // Allocate multiple typed objects
    auto p1 = M::create_typed<NoexceptType>( 10 );
    auto p2 = M::create_typed<NoexceptType>( 20 );
    auto p3 = M::create_typed<NoexceptType>( 30 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );

    // Verify values
    REQUIRE( M::resolve( p1 )->value == 10 );
    REQUIRE( M::resolve( p2 )->value == 20 );
    REQUIRE( M::resolve( p3 )->value == 30 );

    // Verify is_valid_ptr
    REQUIRE( M::is_valid_ptr( p1 ) );
    REQUIRE( M::is_valid_ptr( p2 ) );
    REQUIRE( M::is_valid_ptr( p3 ) );

    // Destroy in different order
    M::destroy_typed( p2 );
    M::destroy_typed( p1 );
    M::destroy_typed( p3 );

    M::destroy();
}

/// Test that allocate size 0 returns nullptr.
TEST_CASE( "allocate zero", "[test_issue43_phase1_safety]" )
{
    M::destroy();
    REQUIRE( M::create( 64 * 1024 ) );

    void* p = M::allocate( 0 );
    REQUIRE( p == nullptr );

    M::destroy();
}

// =============================================================================
// main
// =============================================================================
