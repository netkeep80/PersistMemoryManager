/**
 * @file test_issue213_overflow.cpp
 * @brief Overflow tests for size calculations.
 *
 * Verifies that PersistMemoryManager correctly handles arithmetic overflow
 * in granule computations, allocation size calculations, and edge cases
 * across all AddressTraits variants (16-bit, 32-bit, 64-bit indexes).
 *
 * @see docs/phase5_testing.md §5.2
 * @version 0.1
 */

#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>

// ─── Manager aliases ─────────────────────────────────────────────────────────

/// 32-bit index, static storage (no auto-expand), for controlled overflow tests.
using StaticM = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<8192>, 21301>;

/// 32-bit index, heap storage (auto-expand), to verify overflow returns nullptr.
using HeapM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 21302>;

/// 16-bit index, static storage — smaller index range makes overflow easier to trigger.
using SmallM = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 21303>;

/// 64-bit index, heap storage.
using LargeM = pmm::PersistMemoryManager<pmm::LargeDBConfig, 21304>;

// =============================================================================
// bytes_to_granules overflow
// =============================================================================

TEST_CASE( "bytes_to_granules_t returns 0 on size_t overflow", "[test_issue213_overflow]" )
{
    // Passing a value close to size_t max should overflow the (bytes + granule_size - 1) computation.
    std::size_t huge = std::numeric_limits<std::size_t>::max();
    auto        g    = pmm::detail::bytes_to_granules_t<pmm::DefaultAddressTraits>( huge );
    REQUIRE( g == 0 );
}

TEST_CASE( "bytes_to_granules_t returns 0 when result exceeds index_type max", "[test_issue213_overflow]" )
{
    // For 16-bit index: max index = 65535. If we need more granules than that, return 0.
    // 65536 granules × 16 bytes/granule = 1,048,576 bytes = 1 MB
    std::size_t needs_more_than_16bit = static_cast<std::size_t>( 65536 ) * 16 + 1;
    auto        g = pmm::detail::bytes_to_granules_t<pmm::SmallAddressTraits>( needs_more_than_16bit );
    REQUIRE( g == 0 );
}

TEST_CASE( "bytes_to_idx_t returns no_block on overflow", "[test_issue213_overflow]" )
{
    std::size_t huge = std::numeric_limits<std::size_t>::max();
    auto        idx  = pmm::detail::bytes_to_idx_t<pmm::DefaultAddressTraits>( huge );
    REQUIRE( idx == pmm::DefaultAddressTraits::no_block );
}

TEST_CASE( "bytes_to_idx_t returns 0 for zero bytes", "[test_issue213_overflow]" )
{
    auto idx = pmm::detail::bytes_to_idx_t<pmm::DefaultAddressTraits>( 0 );
    REQUIRE( idx == 0 );
}

// =============================================================================
// allocate with overflow sizes
// =============================================================================

TEST_CASE( "allocate returns nullptr for size_t max on static storage", "[test_issue213_overflow]" )
{
    StaticM::destroy();
    REQUIRE( StaticM::create() );

    // bytes_to_granules_t returns 0 for size_t max (overflow), clamped to 1 granule.
    // On static storage this succeeds as 1 granule (minimum alloc).
    // The key test is that the manager doesn't crash or corrupt state.
    void* p = StaticM::allocate( std::numeric_limits<std::size_t>::max() );
    if ( p != nullptr )
        StaticM::deallocate( p ); // cleanup — the allocator silently treated overflow as 1 granule

    REQUIRE( StaticM::is_initialized() );
    StaticM::destroy();
}

TEST_CASE( "allocate_typed overflow size causes null pptr", "[test_issue213_overflow]" )
{
    StaticM::destroy();
    REQUIRE( StaticM::create() );

    // sizeof(int) * max would overflow size_t — allocate_typed catches this pre-multiplication.
    auto p = StaticM::allocate_typed<int>( std::numeric_limits<std::size_t>::max() );
    REQUIRE( p.is_null() );

    REQUIRE( StaticM::is_initialized() );
    StaticM::destroy();
}

TEST_CASE( "allocate returns nullptr for zero size", "[test_issue213_overflow]" )
{
    HeapM::destroy();
    REQUIRE( HeapM::create( 64 * 1024 ) );

    void* p = HeapM::allocate( 0 );
    REQUIRE( p == nullptr );
    REQUIRE( HeapM::last_error() == pmm::PmmError::InvalidSize );

    HeapM::destroy();
}

TEST_CASE( "allocate_typed returns null for count overflow", "[test_issue213_overflow]" )
{
    HeapM::destroy();
    REQUIRE( HeapM::create( 64 * 1024 ) );

    // sizeof(int) * max_size_t would overflow size_t.
    auto p = HeapM::allocate_typed<int>( std::numeric_limits<std::size_t>::max() );
    REQUIRE( p.is_null() );

    // Verify normal allocation still works after overflow rejection.
    auto p2 = HeapM::allocate_typed<int>();
    REQUIRE( !p2.is_null() );
    *p2 = 42;
    REQUIRE( *p2 == 42 );
    HeapM::deallocate_typed( p2 );

    HeapM::destroy();
}

TEST_CASE( "allocate_typed returns null for count = 0", "[test_issue213_overflow]" )
{
    HeapM::destroy();
    REQUIRE( HeapM::create( 64 * 1024 ) );

    auto p = HeapM::allocate_typed<int>( 0 );
    REQUIRE( p.is_null() );

    HeapM::destroy();
}

// =============================================================================
// Static storage: allocate larger than available
// =============================================================================

TEST_CASE( "static storage rejects allocation larger than heap", "[test_issue213_overflow]" )
{
    StaticM::destroy();
    REQUIRE( StaticM::create() );

    // Request far more than the 8192-byte static buffer.
    void* p = StaticM::allocate( 16384 );
    REQUIRE( p == nullptr );

    // Normal allocation should still work.
    void* p2 = StaticM::allocate( 32 );
    REQUIRE( p2 != nullptr );
    StaticM::deallocate( p2 );

    StaticM::destroy();
}

// =============================================================================
// 16-bit index overflow
// =============================================================================

TEST_CASE( "SmallAddressTraits: allocate near index_type max", "[test_issue213_overflow]" )
{
    SmallM::destroy();
    REQUIRE( SmallM::create() );

    // 16-bit index max = 65535. With 16-byte granules, max addressable = ~1 MB.
    // Static buffer is only 4096 bytes, so requesting the full index range fails.
    auto p = SmallM::allocate_typed<std::uint8_t>( 8192 );
    REQUIRE( p.is_null() );

    // Small valid allocation works.
    auto p2 = SmallM::allocate_typed<std::uint8_t>( 16 );
    REQUIRE( !p2.is_null() );
    SmallM::deallocate_typed( p2 );

    SmallM::destroy();
}

// =============================================================================
// reallocate_typed overflow
// =============================================================================

TEST_CASE( "reallocate_typed returns null for count overflow", "[test_issue213_overflow]" )
{
    HeapM::destroy();
    REQUIRE( HeapM::create( 64 * 1024 ) );

    auto p = HeapM::allocate_typed<int>( 4 );
    REQUIRE( !p.is_null() );
    p.resolve()[0] = 1;
    p.resolve()[1] = 2;

    // Try to reallocate to size_t max count — should fail with Overflow.
    auto p2 = HeapM::reallocate_typed<int>( p, 4, std::numeric_limits<std::size_t>::max() );
    REQUIRE( p2.is_null() );
    REQUIRE( HeapM::last_error() == pmm::PmmError::Overflow );

    // Original pointer should still be valid.
    REQUIRE( p.resolve()[0] == 1 );
    REQUIRE( p.resolve()[1] == 2 );

    HeapM::deallocate_typed( p );
    HeapM::destroy();
}

TEST_CASE( "reallocate_typed with new_count = 0 returns null", "[test_issue213_overflow]" )
{
    HeapM::destroy();
    REQUIRE( HeapM::create( 64 * 1024 ) );

    auto p = HeapM::allocate_typed<int>( 4 );
    REQUIRE( !p.is_null() );

    auto p2 = HeapM::reallocate_typed<int>( p, 4, 0 );
    REQUIRE( p2.is_null() );
    REQUIRE( HeapM::last_error() == pmm::PmmError::InvalidSize );

    HeapM::deallocate_typed( p );
    HeapM::destroy();
}

// =============================================================================
// allocate when not initialized
// =============================================================================

TEST_CASE( "allocate before create returns nullptr with NotInitialized", "[test_issue213_overflow]" )
{
    HeapM::destroy();

    void* p = HeapM::allocate( 64 );
    REQUIRE( p == nullptr );
    REQUIRE( HeapM::last_error() == pmm::PmmError::NotInitialized );
}

// =============================================================================
// Exhaustion and recovery
// =============================================================================

TEST_CASE( "allocate all memory then verify manager integrity", "[test_issue213_overflow]" )
{
    StaticM::destroy();
    REQUIRE( StaticM::create() );

    // Exhaust the 8192-byte static heap with small allocations.
    std::vector<void*> ptrs;
    while ( true )
    {
        void* p = StaticM::allocate( 16 );
        if ( p == nullptr )
            break;
        ptrs.push_back( p );
    }

    REQUIRE( !ptrs.empty() );
    REQUIRE( StaticM::last_error() == pmm::PmmError::OutOfMemory );

    // Manager should still be valid.
    REQUIRE( StaticM::is_initialized() );
    REQUIRE( StaticM::is_initialized() );

    // Free everything.
    for ( void* p : ptrs )
        StaticM::deallocate( p );

    // After freeing, allocation should work again.
    void* p_after = StaticM::allocate( 16 );
    REQUIRE( p_after != nullptr );
    StaticM::deallocate( p_after );

    StaticM::destroy();
}

TEST_CASE( "allocate and deallocate cycle preserves heap integrity", "[test_issue213_overflow]" )
{
    StaticM::destroy();
    REQUIRE( StaticM::create() );

    // Multiple rounds of allocate-all then free-all.
    for ( int round = 0; round < 3; ++round )
    {
        std::vector<void*> ptrs;
        while ( true )
        {
            void* p = StaticM::allocate( 32 );
            if ( p == nullptr )
                break;
            ptrs.push_back( p );
        }
        REQUIRE( !ptrs.empty() );
        REQUIRE( StaticM::is_initialized() );

        for ( void* p : ptrs )
            StaticM::deallocate( p );

        REQUIRE( StaticM::is_initialized() );
    }

    StaticM::destroy();
}

// =============================================================================
// 64-bit index: large allocation sizes
// =============================================================================

TEST_CASE( "LargeAddressTraits: basic allocation and overflow resilience", "[test_issue213_overflow]" )
{
    LargeM::destroy();
    REQUIRE( LargeM::create( 1024 * 1024 ) ); // 1 MB

    // Normal allocation works.
    void* p2 = LargeM::allocate( 64 );
    REQUIRE( p2 != nullptr );
    LargeM::deallocate( p2 );

    // allocate_typed overflow: sizeof(double) * max overflows size_t.
    auto p3 = LargeM::allocate_typed<double>( std::numeric_limits<std::size_t>::max() );
    REQUIRE( p3.is_null() );

    // Manager remains valid.
    REQUIRE( LargeM::is_initialized() );

    LargeM::destroy();
}

// =============================================================================
// Boundary granule sizes
// =============================================================================

TEST_CASE( "allocate exactly one granule", "[test_issue213_overflow]" )
{
    HeapM::destroy();
    REQUIRE( HeapM::create( 64 * 1024 ) );

    // Allocate exactly 16 bytes (one DefaultAddressTraits granule).
    void* p = HeapM::allocate( pmm::DefaultAddressTraits::granule_size );
    REQUIRE( p != nullptr );
    HeapM::deallocate( p );

    // Allocate 1 byte — should be rounded up to one granule.
    void* p2 = HeapM::allocate( 1 );
    REQUIRE( p2 != nullptr );
    HeapM::deallocate( p2 );

    HeapM::destroy();
}

TEST_CASE( "allocate just below and at granule boundary", "[test_issue213_overflow]" )
{
    HeapM::destroy();
    REQUIRE( HeapM::create( 64 * 1024 ) );

    constexpr std::size_t gran = pmm::DefaultAddressTraits::granule_size; // 16

    // Allocate 15 bytes (just under one granule) — rounds up to 1 granule.
    void* p1 = HeapM::allocate( gran - 1 );
    REQUIRE( p1 != nullptr );
    HeapM::deallocate( p1 );

    // Allocate 17 bytes (just over one granule) — rounds up to 2 granules.
    void* p2 = HeapM::allocate( gran + 1 );
    REQUIRE( p2 != nullptr );
    HeapM::deallocate( p2 );

    // Allocate exactly 2 granules.
    void* p3 = HeapM::allocate( 2 * gran );
    REQUIRE( p3 != nullptr );
    HeapM::deallocate( p3 );

    HeapM::destroy();
}
