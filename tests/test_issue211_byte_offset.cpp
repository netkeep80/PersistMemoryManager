/**
 * @file test_issue211_byte_offset.cpp
 * @brief Tests for pptr ↔ byte offset conversion (Issue #211, Phase 4.4).
 *
 * Verifies the key requirements from Issue #211 Phase 4.4:
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
 * @version 0.1 (Issue #211 — Phase 4.4: pptr byte offset conversion)
 */

#include "pmm/persist_memory_manager.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

// --- Test macros -------------------------------------------------------------

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
static bool test_byte_offset_basic()
{
    Mgr::create( 64 * 1024 );

    auto p = Mgr::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    std::size_t expected = static_cast<std::size_t>( p.offset() ) * pmm::DefaultAddressTraits::granule_size;
    PMM_TEST( p.byte_offset() == expected );
    PMM_TEST( p.byte_offset() > 0 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
    return true;
}

/// 2. Null pptr::byte_offset() returns 0.
static bool test_byte_offset_null()
{
    Mgr::pptr<int> p;
    PMM_TEST( p.is_null() );
    PMM_TEST( p.byte_offset() == 0 );

    Mgr::destroy();
    return true;
}

/// 3. pptr_from_byte_offset() creates correct pptr.
static bool test_from_byte_offset_basic()
{
    MgrRT::create( 64 * 1024 );

    auto p = MgrRT::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    std::size_t boff = p.byte_offset();
    auto        p2   = MgrRT::pptr_from_byte_offset<int>( boff );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( p2.offset() == p.offset() );

    MgrRT::deallocate_typed( p );
    MgrRT::destroy();
    return true;
}

/// 4. pptr_from_byte_offset(0) returns null pptr.
static bool test_from_byte_offset_zero()
{
    MgrRT::create( 64 * 1024 );

    auto p = MgrRT::pptr_from_byte_offset<int>( 0 );
    PMM_TEST( p.is_null() );

    MgrRT::destroy();
    return true;
}

/// 5. pptr_from_byte_offset with non-aligned offset returns null pptr with error.
static bool test_from_byte_offset_unaligned()
{
    MgrErr::create( 64 * 1024 );
    MgrErr::clear_error();

    // granule_size for DefaultAddressTraits is 16, so offset 7 is not aligned
    auto p = MgrErr::pptr_from_byte_offset<int>( 7 );
    PMM_TEST( p.is_null() );
    PMM_TEST( MgrErr::last_error() == pmm::PmmError::InvalidPointer );

    MgrErr::destroy();
    return true;
}

/// 6. Round-trip: pptr → byte_offset → pptr_from_byte_offset preserves identity.
static bool test_round_trip()
{
    MgrRT::create( 64 * 1024 );

    auto p = MgrRT::allocate_typed<double>();
    PMM_TEST( !p.is_null() );

    std::size_t boff = p.byte_offset();
    auto        p2   = MgrRT::pptr_from_byte_offset<double>( boff );
    PMM_TEST( p == p2 );

    MgrRT::deallocate_typed( p );
    MgrRT::destroy();
    return true;
}

/// 7. Round-trip with allocated objects: data accessible via both paths.
static bool test_round_trip_data()
{
    MgrData::create( 64 * 1024 );

    auto p = MgrData::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    *p = 42;

    std::size_t boff = p.byte_offset();
    auto        p2   = MgrData::pptr_from_byte_offset<int>( boff );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( *p2 == 42 );

    // Modify through reconstructed pointer
    *p2 = 99;
    PMM_TEST( *p == 99 );

    MgrData::deallocate_typed( p );
    MgrData::destroy();
    return true;
}

/// 8. Works with SmallAddressTraits (uint16_t).
static bool test_small_address_traits()
{
    MgrSmall::create( 4096 );

    auto p = MgrSmall::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    *p = 123;

    std::size_t expected = static_cast<std::size_t>( p.offset() ) * pmm::SmallAddressTraits::granule_size;
    PMM_TEST( p.byte_offset() == expected );

    auto p2 = MgrSmall::pptr_from_byte_offset<int>( p.byte_offset() );
    PMM_TEST( p == p2 );
    PMM_TEST( *p2 == 123 );

    MgrSmall::deallocate_typed( p );
    MgrSmall::destroy();
    return true;
}

/// 9. Works with LargeAddressTraits (uint64_t).
static bool test_large_address_traits()
{
    MgrLarge::create( 64 * 1024 );

    auto p = MgrLarge::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    *p = 456;

    std::size_t expected = static_cast<std::size_t>( p.offset() ) * pmm::LargeAddressTraits::granule_size;
    PMM_TEST( p.byte_offset() == expected );

    auto p2 = MgrLarge::pptr_from_byte_offset<int>( p.byte_offset() );
    PMM_TEST( p == p2 );
    PMM_TEST( *p2 == 456 );

    MgrLarge::deallocate_typed( p );
    MgrLarge::destroy();
    return true;
}

/// 10. Overflow protection for huge byte offsets.
static bool test_overflow_protection()
{
    MgrErr::create( 64 * 1024 );
    MgrErr::clear_error();

    // For DefaultAddressTraits (uint32_t, granule=16), max valid byte offset is
    // (2^32 - 2) * 16. Anything beyond should overflow index_type.
    // Use a byte offset that would exceed uint32_t max when divided by granule_size.
    std::size_t huge = static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) *
                       pmm::DefaultAddressTraits::granule_size;
    // Add one more granule to overflow
    std::size_t overflow_off = huge + pmm::DefaultAddressTraits::granule_size;

    auto p = MgrErr::pptr_from_byte_offset<int>( overflow_off );
    PMM_TEST( p.is_null() );
    PMM_TEST( MgrErr::last_error() == pmm::PmmError::Overflow );

    MgrErr::destroy();
    return true;
}

/// 11. Error codes set correctly for invalid inputs.
static bool test_error_codes()
{
    MgrErr::create( 64 * 1024 );

    // Unaligned offset → InvalidPointer
    MgrErr::clear_error();
    auto p1 = MgrErr::pptr_from_byte_offset<int>( 3 );
    PMM_TEST( p1.is_null() );
    PMM_TEST( MgrErr::last_error() == pmm::PmmError::InvalidPointer );

    // Zero offset → no error (just returns null)
    MgrErr::clear_error();
    auto p2 = MgrErr::pptr_from_byte_offset<int>( 0 );
    PMM_TEST( p2.is_null() );
    PMM_TEST( MgrErr::last_error() == pmm::PmmError::Ok );

    // Valid aligned offset → no error
    MgrErr::clear_error();
    auto p3 = MgrErr::pptr_from_byte_offset<int>( pmm::DefaultAddressTraits::granule_size * 10 );
    PMM_TEST( !p3.is_null() );
    PMM_TEST( p3.offset() == 10 );
    PMM_TEST( MgrErr::last_error() == pmm::PmmError::Ok );

    MgrErr::destroy();
    return true;
}

/// 12. Multiple allocations round-trip correctly.
static bool test_multiple_allocations()
{
    MgrMulti::create( 64 * 1024 );

    constexpr std::size_t N = 10;
    MgrMulti::pptr<int>   ptrs[N];
    std::size_t           offsets[N];

    for ( std::size_t i = 0; i < N; ++i )
    {
        ptrs[i] = MgrMulti::allocate_typed<int>();
        PMM_TEST( !ptrs[i].is_null() );
        *ptrs[i]   = static_cast<int>( i * 100 );
        offsets[i] = ptrs[i].byte_offset();
    }

    // Reconstruct all pointers from byte offsets and verify data
    for ( std::size_t i = 0; i < N; ++i )
    {
        auto p2 = MgrMulti::pptr_from_byte_offset<int>( offsets[i] );
        PMM_TEST( p2 == ptrs[i] );
        PMM_TEST( *p2 == static_cast<int>( i * 100 ) );
    }

    for ( std::size_t i = 0; i < N; ++i )
        MgrMulti::deallocate_typed( ptrs[i] );

    MgrMulti::destroy();
    return true;
}

// --- Main --------------------------------------------------------------------

int main()
{
    std::cout << "=== test_issue211_byte_offset ===\n";
    bool all_passed = true;

    PMM_RUN( "byte_offset_basic", test_byte_offset_basic );
    PMM_RUN( "byte_offset_null", test_byte_offset_null );
    PMM_RUN( "from_byte_offset_basic", test_from_byte_offset_basic );
    PMM_RUN( "from_byte_offset_zero", test_from_byte_offset_zero );
    PMM_RUN( "from_byte_offset_unaligned", test_from_byte_offset_unaligned );
    PMM_RUN( "round_trip", test_round_trip );
    PMM_RUN( "round_trip_data", test_round_trip_data );
    PMM_RUN( "small_address_traits", test_small_address_traits );
    PMM_RUN( "large_address_traits", test_large_address_traits );
    PMM_RUN( "overflow_protection", test_overflow_protection );
    PMM_RUN( "error_codes", test_error_codes );
    PMM_RUN( "multiple_allocations", test_multiple_allocations );

    std::cout << ( all_passed ? "ALL PASSED\n" : "SOME TESTS FAILED\n" );
    return all_passed ? 0 : 1;
}
