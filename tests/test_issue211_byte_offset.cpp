/**
 * @file test_issue211_byte_offset.cpp
 * @brief Tests for pptr ↔ byte offset conversion.
 *
 * Verifies the key requirements from this feature:
 *  1. pptr::byte_offset() returns offset() * granule_size.
 *  2. Null pptr::byte_offset() returns 0.
 *  3. pptr_from_byte_offset() creates correct pptr from byte offset.
 *  4. pptr_from_byte_offset(0) returns null pptr.
 *  5. pptr_from_byte_offset with non-aligned offset returns null pptr.
 *  6. Round-trip: pptr → byte_offset → pptr_from_byte_offset preserves identity.
 *  7. Round-trip with allocated objects: data accessible via both paths.
 *  8. Works with SmallAddressTraits (uint16_t).
 *  9. Works with LargeAddressTraits (uint64_t).
 * 10. Overflow protection for huge byte offsets.
 * 11. Error codes set correctly for invalid inputs.
 * 12. Multiple allocations round-trip correctly.
 *
 * @see include/pmm/pptr.h — pptr::byte_offset()
 * @see include/pmm/persist_memory_manager.h — pptr_from_byte_offset<T>()
 * @version 0.1
 */

#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include <limits>

// --- Manager aliases ---------------------------------------------------------

using Mgr      = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2110>;
using MgrRT    = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2111>;
using MgrData  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2112>;
using MgrMulti = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2113>;
using MgrErr   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2114>;
using MgrSmall = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<8192>, 2115>;
using MgrLarge = pmm::PersistMemoryManager<pmm::LargeDBConfig, 2116>;

// --- Tests -------------------------------------------------------------------

/// 1. pptr::byte_offset() returns offset() * granule_size.
TEST_CASE( "byte_offset_basic", "[test_issue211_byte_offset]" )
{
    Mgr::create( 64 * 1024 );

    auto p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    std::size_t expected = static_cast<std::size_t>( p.offset() ) * pmm::DefaultAddressTraits::granule_size;
    REQUIRE( p.byte_offset() == expected );
    REQUIRE( p.byte_offset() > 0 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}

/// 2. Null pptr::byte_offset() returns 0.
TEST_CASE( "byte_offset_null", "[test_issue211_byte_offset]" )
{
    Mgr::pptr<int> p;
    REQUIRE( p.is_null() );
    REQUIRE( p.byte_offset() == 0 );

    Mgr::destroy();
}

/// 3. pptr_from_byte_offset() creates correct pptr.
TEST_CASE( "from_byte_offset_basic", "[test_issue211_byte_offset]" )
{
    MgrRT::create( 64 * 1024 );

    auto p = MgrRT::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    std::size_t boff = p.byte_offset();
    auto        p2   = MgrRT::pptr_from_byte_offset<int>( boff );
    REQUIRE( !p2.is_null() );
    REQUIRE( p2.offset() == p.offset() );

    MgrRT::deallocate_typed( p );
    MgrRT::destroy();
}

/// 4. pptr_from_byte_offset(0) returns null pptr.
TEST_CASE( "from_byte_offset_zero", "[test_issue211_byte_offset]" )
{
    MgrRT::create( 64 * 1024 );

    auto p = MgrRT::pptr_from_byte_offset<int>( 0 );
    REQUIRE( p.is_null() );

    MgrRT::destroy();
}

/// 5. pptr_from_byte_offset with non-aligned offset returns null pptr with error.
TEST_CASE( "from_byte_offset_unaligned", "[test_issue211_byte_offset]" )
{
    MgrErr::create( 64 * 1024 );
    MgrErr::clear_error();

    // granule_size for DefaultAddressTraits is 16, so offset 7 is not aligned
    auto p = MgrErr::pptr_from_byte_offset<int>( 7 );
    REQUIRE( p.is_null() );
    REQUIRE( MgrErr::last_error() == pmm::PmmError::InvalidPointer );

    MgrErr::destroy();
}

/// 6. Round-trip: pptr → byte_offset → pptr_from_byte_offset preserves identity.
TEST_CASE( "round_trip", "[test_issue211_byte_offset]" )
{
    MgrRT::create( 64 * 1024 );

    auto p = MgrRT::allocate_typed<double>();
    REQUIRE( !p.is_null() );

    std::size_t boff = p.byte_offset();
    auto        p2   = MgrRT::pptr_from_byte_offset<double>( boff );
    REQUIRE( p == p2 );

    MgrRT::deallocate_typed( p );
    MgrRT::destroy();
}

/// 7. Round-trip with allocated objects: data accessible via both paths.
TEST_CASE( "round_trip_data", "[test_issue211_byte_offset]" )
{
    MgrData::create( 64 * 1024 );

    auto p = MgrData::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    *p = 42;

    std::size_t boff = p.byte_offset();
    auto        p2   = MgrData::pptr_from_byte_offset<int>( boff );
    REQUIRE( !p2.is_null() );
    REQUIRE( *p2 == 42 );

    // Modify through reconstructed pointer
    *p2 = 99;
    REQUIRE( *p == 99 );

    MgrData::deallocate_typed( p );
    MgrData::destroy();
}

/// 8. Works with SmallAddressTraits (uint16_t).
TEST_CASE( "small_address_traits", "[test_issue211_byte_offset]" )
{
    MgrSmall::create( 4096 );

    auto p = MgrSmall::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    *p = 123;

    std::size_t expected = static_cast<std::size_t>( p.offset() ) * pmm::SmallAddressTraits::granule_size;
    REQUIRE( p.byte_offset() == expected );

    auto p2 = MgrSmall::pptr_from_byte_offset<int>( p.byte_offset() );
    REQUIRE( p == p2 );
    REQUIRE( *p2 == 123 );

    MgrSmall::deallocate_typed( p );
    MgrSmall::destroy();
}

/// 9. Works with LargeAddressTraits (uint64_t).
TEST_CASE( "large_address_traits", "[test_issue211_byte_offset]" )
{
    MgrLarge::create( 64 * 1024 );

    auto p = MgrLarge::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    *p = 456;

    std::size_t expected = static_cast<std::size_t>( p.offset() ) * pmm::LargeAddressTraits::granule_size;
    REQUIRE( p.byte_offset() == expected );

    auto p2 = MgrLarge::pptr_from_byte_offset<int>( p.byte_offset() );
    REQUIRE( p == p2 );
    REQUIRE( *p2 == 456 );

    MgrLarge::deallocate_typed( p );
    MgrLarge::destroy();
}

/// 10. Overflow protection for huge byte offsets.
TEST_CASE( "overflow_protection", "[test_issue211_byte_offset]" )
{
    MgrErr::create( 64 * 1024 );
    MgrErr::clear_error();

    // For DefaultAddressTraits (uint32_t, granule=16), max valid byte offset is
    // (2^32 - 2) * 16. Anything beyond should overflow index_type.
    // Use a byte offset that would exceed uint32_t max when divided by granule_size.
    std::size_t huge =
        static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) * pmm::DefaultAddressTraits::granule_size;
    // Add one more granule to overflow
    std::size_t overflow_off = huge + pmm::DefaultAddressTraits::granule_size;

    auto p = MgrErr::pptr_from_byte_offset<int>( overflow_off );
    REQUIRE( p.is_null() );
    REQUIRE( MgrErr::last_error() == pmm::PmmError::Overflow );

    MgrErr::destroy();
}

/// 11. Error codes set correctly for invalid inputs.
TEST_CASE( "error_codes", "[test_issue211_byte_offset]" )
{
    MgrErr::create( 64 * 1024 );

    // Unaligned offset → InvalidPointer
    MgrErr::clear_error();
    auto p1 = MgrErr::pptr_from_byte_offset<int>( 3 );
    REQUIRE( p1.is_null() );
    REQUIRE( MgrErr::last_error() == pmm::PmmError::InvalidPointer );

    // Zero offset → no error (just returns null)
    MgrErr::clear_error();
    auto p2 = MgrErr::pptr_from_byte_offset<int>( 0 );
    REQUIRE( p2.is_null() );
    REQUIRE( MgrErr::last_error() == pmm::PmmError::Ok );

    // Valid aligned offset → no error
    MgrErr::clear_error();
    auto p3 = MgrErr::pptr_from_byte_offset<int>( pmm::DefaultAddressTraits::granule_size * 10 );
    REQUIRE( !p3.is_null() );
    REQUIRE( p3.offset() == 10 );
    REQUIRE( MgrErr::last_error() == pmm::PmmError::Ok );

    MgrErr::destroy();
}

/// 12. Multiple allocations round-trip correctly.
TEST_CASE( "multiple_allocations", "[test_issue211_byte_offset]" )
{
    MgrMulti::create( 64 * 1024 );

    constexpr std::size_t N = 10;
    MgrMulti::pptr<int>   ptrs[N];
    std::size_t           offsets[N];

    for ( std::size_t i = 0; i < N; ++i )
    {
        ptrs[i] = MgrMulti::allocate_typed<int>();
        REQUIRE( !ptrs[i].is_null() );
        *ptrs[i]   = static_cast<int>( i * 100 );
        offsets[i] = ptrs[i].byte_offset();
    }

    // Reconstruct all pointers from byte offsets and verify data
    for ( std::size_t i = 0; i < N; ++i )
    {
        auto p2 = MgrMulti::pptr_from_byte_offset<int>( offsets[i] );
        REQUIRE( p2 == ptrs[i] );
        REQUIRE( *p2 == static_cast<int>( i * 100 ) );
    }

    for ( std::size_t i = 0; i < N; ++i )
        MgrMulti::deallocate_typed( ptrs[i] );

    MgrMulti::destroy();
}
