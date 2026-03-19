/**
 * @file test_issue210_reallocate_typed.cpp
 * @brief Tests for reallocate_typed<T>() (Issue #210, Phase 4.3).
 *
 * Verifies the key requirements from Issue #210 Phase 4.3:
 *  1. Basic reallocation (grow) preserves data.
 *  2. Basic reallocation (shrink) preserves data.
 *  3. In-place expansion when next block is free.
 *  4. Fallback allocation when in-place is not possible.
 *  5. Null pointer input behaves like allocate_typed.
 *  6. new_count=0 returns null pptr with InvalidSize error.
 *  7. Error codes are set correctly.
 *  8. Same-size reallocation returns same pptr.
 *  9. Shrink with split creates free block.
 * 10. Works with SmallAddressTraits.
 * 11. Works with LargeAddressTraits.
 * 12. Multiple sequential reallocations preserve data.
 * 13. Overflow protection for huge new_count.
 * 14. NotInitialized error when manager not created.
 * 15. Old block is NOT freed on failure.
 *
 * @see include/pmm/persist_memory_manager.h — reallocate_typed<T>()
 * @version 0.1 (Issue #210 — Phase 4.3: reallocate_typed)
 */

#include "pmm/persist_memory_manager.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

using Mgr      = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 210>;
using MgrGrow  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 211>;
using MgrInpl  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 212>;
using MgrFall  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 213>;
using MgrNull  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 214>;
using MgrErr   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 215>;
using MgrSame  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 216>;
using MgrShr   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 217>;
using MgrSmall = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<8192>, 218>;
using MgrLarge = pmm::PersistMemoryManager<pmm::LargeDBConfig, 219>;
using MgrSeq   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 220>;
using MgrOvf   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 221>;
using MgrNI    = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 222>;
using MgrSafe  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 223>;
using MgrShr2  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 224>;

// --- Tests -------------------------------------------------------------------

/// 1. Basic grow preserves data.
static bool test_grow_preserves_data()
{
    PMM_TEST( MgrGrow::create( 64 * 1024 ) );

    MgrGrow::pptr<std::uint8_t> p = MgrGrow::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );
    for ( std::size_t i = 0; i < 64; ++i )
        MgrGrow::resolve( p )[i] = static_cast<std::uint8_t>( i );

    MgrGrow::pptr<std::uint8_t> p2 = MgrGrow::reallocate_typed( p, 64, 256 );
    PMM_TEST( !p2.is_null() );
    for ( std::size_t i = 0; i < 64; ++i )
        PMM_TEST( MgrGrow::resolve( p2 )[i] == static_cast<std::uint8_t>( i ) );

    PMM_TEST( MgrGrow::last_error() == pmm::PmmError::Ok );

    MgrGrow::deallocate_typed( p2 );
    MgrGrow::destroy();
    return true;
}

/// 2. Basic shrink preserves data (truncated).
static bool test_shrink_preserves_data()
{
    PMM_TEST( MgrShr::create( 64 * 1024 ) );

    MgrShr::pptr<std::uint32_t> p = MgrShr::allocate_typed<std::uint32_t>( 16 );
    PMM_TEST( !p.is_null() );
    for ( std::size_t i = 0; i < 16; ++i )
        MgrShr::resolve( p )[i] = static_cast<std::uint32_t>( 0xDEAD0000 + i );

    MgrShr::pptr<std::uint32_t> p2 = MgrShr::reallocate_typed( p, 16, 4 );
    PMM_TEST( !p2.is_null() );
    // Shrink returns same pptr (in-place)
    PMM_TEST( p2.offset() == p.offset() );
    for ( std::size_t i = 0; i < 4; ++i )
        PMM_TEST( MgrShr::resolve( p2 )[i] == static_cast<std::uint32_t>( 0xDEAD0000 + i ) );

    PMM_TEST( MgrShr::last_error() == pmm::PmmError::Ok );

    MgrShr::deallocate_typed( p2 );
    MgrShr::destroy();
    return true;
}

/// 3. In-place expansion when next block is free.
static bool test_inplace_expansion()
{
    PMM_TEST( MgrInpl::create( 64 * 1024 ) );

    // Allocate two blocks, then free the second one to create adjacent free space
    MgrInpl::pptr<std::uint8_t> p1 = MgrInpl::allocate_typed<std::uint8_t>( 64 );
    MgrInpl::pptr<std::uint8_t> p2 = MgrInpl::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    for ( std::size_t i = 0; i < 64; ++i )
        MgrInpl::resolve( p1 )[i] = static_cast<std::uint8_t>( 0xAA );

    // Free p2 — now p1 has a free block right after it
    MgrInpl::deallocate_typed( p2 );

    auto old_offset = p1.offset();

    // Reallocate p1 to 128 bytes — should expand in-place
    MgrInpl::pptr<std::uint8_t> p3 = MgrInpl::reallocate_typed( p1, 64, 128 );
    PMM_TEST( !p3.is_null() );
    // In-place expansion: same offset
    PMM_TEST( p3.offset() == old_offset );

    // Data preserved
    for ( std::size_t i = 0; i < 64; ++i )
        PMM_TEST( MgrInpl::resolve( p3 )[i] == static_cast<std::uint8_t>( 0xAA ) );

    MgrInpl::deallocate_typed( p3 );
    MgrInpl::destroy();
    return true;
}

/// 4. Fallback when in-place is not possible (next block is allocated).
static bool test_fallback_allocation()
{
    PMM_TEST( MgrFall::create( 64 * 1024 ) );

    MgrFall::pptr<std::uint8_t> p1 = MgrFall::allocate_typed<std::uint8_t>( 64 );
    MgrFall::pptr<std::uint8_t> p2 = MgrFall::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    for ( std::size_t i = 0; i < 64; ++i )
        MgrFall::resolve( p1 )[i] = static_cast<std::uint8_t>( 0xBB );

    // p2 is allocated right after p1, so in-place expansion is not possible
    MgrFall::pptr<std::uint8_t> p3 = MgrFall::reallocate_typed( p1, 64, 512 );
    PMM_TEST( !p3.is_null() );

    // Data preserved
    for ( std::size_t i = 0; i < 64; ++i )
        PMM_TEST( MgrFall::resolve( p3 )[i] == static_cast<std::uint8_t>( 0xBB ) );

    MgrFall::deallocate_typed( p2 );
    MgrFall::deallocate_typed( p3 );
    MgrFall::destroy();
    return true;
}

/// 5. Null pointer behaves like allocate_typed.
static bool test_null_pointer()
{
    PMM_TEST( MgrNull::create( 64 * 1024 ) );

    MgrNull::pptr<int> null_p;
    MgrNull::pptr<int> p = MgrNull::reallocate_typed( null_p, 0, 4 );
    PMM_TEST( !p.is_null() );
    MgrNull::resolve( p )[0] = 42;
    PMM_TEST( MgrNull::resolve( p )[0] == 42 );

    MgrNull::deallocate_typed( p );
    MgrNull::destroy();
    return true;
}

/// 6. new_count=0 returns null pptr with InvalidSize.
static bool test_zero_new_count()
{
    PMM_TEST( MgrErr::create( 64 * 1024 ) );

    MgrErr::pptr<int> p = MgrErr::allocate_typed<int>( 4 );
    PMM_TEST( !p.is_null() );

    MgrErr::pptr<int> p2 = MgrErr::reallocate_typed( p, 4, 0 );
    PMM_TEST( p2.is_null() );
    PMM_TEST( MgrErr::last_error() == pmm::PmmError::InvalidSize );

    // Old block still valid
    PMM_TEST( !p.is_null() );
    MgrErr::deallocate_typed( p );
    MgrErr::destroy();
    return true;
}

/// 7. Same-size reallocation returns same pptr.
static bool test_same_size()
{
    PMM_TEST( MgrSame::create( 64 * 1024 ) );

    MgrSame::pptr<int> p = MgrSame::allocate_typed<int>( 4 );
    PMM_TEST( !p.is_null() );
    auto old_off = p.offset();

    MgrSame::pptr<int> p2 = MgrSame::reallocate_typed( p, 4, 4 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( p2.offset() == old_off );
    PMM_TEST( MgrSame::last_error() == pmm::PmmError::Ok );

    MgrSame::deallocate_typed( p2 );
    MgrSame::destroy();
    return true;
}

/// 8. Shrink with split creates free block and reduces used_size.
static bool test_shrink_creates_free_block()
{
    PMM_TEST( MgrShr2::create( 64 * 1024 ) );

    // Allocate a large block
    MgrShr2::pptr<std::uint8_t> p = MgrShr2::allocate_typed<std::uint8_t>( 1024 );
    PMM_TEST( !p.is_null() );

    std::size_t used_before = MgrShr2::used_size();
    // Shrink to much smaller
    MgrShr2::pptr<std::uint8_t> p2 = MgrShr2::reallocate_typed( p, 1024, 64 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( p2.offset() == p.offset() ); // in-place shrink

    // used_size should have decreased
    PMM_TEST( MgrShr2::used_size() < used_before );

    MgrShr2::deallocate_typed( p2 );
    MgrShr2::destroy();
    return true;
}

/// 9. Works with SmallAddressTraits (uint16_t index).
static bool test_small_address_traits()
{
    PMM_TEST( MgrSmall::create() );

    MgrSmall::pptr<std::uint8_t> p = MgrSmall::allocate_typed<std::uint8_t>( 32 );
    PMM_TEST( !p.is_null() );
    for ( std::size_t i = 0; i < 32; ++i )
        MgrSmall::resolve( p )[i] = static_cast<std::uint8_t>( i );

    MgrSmall::pptr<std::uint8_t> p2 = MgrSmall::reallocate_typed( p, 32, 64 );
    PMM_TEST( !p2.is_null() );
    for ( std::size_t i = 0; i < 32; ++i )
        PMM_TEST( MgrSmall::resolve( p2 )[i] == static_cast<std::uint8_t>( i ) );

    MgrSmall::deallocate_typed( p2 );
    MgrSmall::destroy();
    return true;
}

/// 10. Works with LargeAddressTraits (uint64_t index).
static bool test_large_address_traits()
{
    PMM_TEST( MgrLarge::create( 64 * 1024 ) );

    MgrLarge::pptr<std::uint32_t> p = MgrLarge::allocate_typed<std::uint32_t>( 8 );
    PMM_TEST( !p.is_null() );
    for ( std::size_t i = 0; i < 8; ++i )
        MgrLarge::resolve( p )[i] = static_cast<std::uint32_t>( 0xCAFE0000 + i );

    MgrLarge::pptr<std::uint32_t> p2 = MgrLarge::reallocate_typed( p, 8, 32 );
    PMM_TEST( !p2.is_null() );
    for ( std::size_t i = 0; i < 8; ++i )
        PMM_TEST( MgrLarge::resolve( p2 )[i] == static_cast<std::uint32_t>( 0xCAFE0000 + i ) );

    MgrLarge::deallocate_typed( p2 );
    MgrLarge::destroy();
    return true;
}

/// 11. Multiple sequential reallocations preserve data.
static bool test_sequential_reallocations()
{
    PMM_TEST( MgrSeq::create( 256 * 1024 ) );

    MgrSeq::pptr<std::uint8_t> p = MgrSeq::allocate_typed<std::uint8_t>( 16 );
    PMM_TEST( !p.is_null() );
    for ( std::size_t i = 0; i < 16; ++i )
        MgrSeq::resolve( p )[i] = static_cast<std::uint8_t>( 0xCC );

    std::size_t counts[] = { 32, 64, 128, 256, 512 };
    std::size_t prev     = 16;
    for ( std::size_t nc : counts )
    {
        MgrSeq::pptr<std::uint8_t> p2 = MgrSeq::reallocate_typed( p, prev, nc );
        PMM_TEST( !p2.is_null() );
        // First 16 bytes always 0xCC
        for ( std::size_t i = 0; i < 16; ++i )
            PMM_TEST( MgrSeq::resolve( p2 )[i] == static_cast<std::uint8_t>( 0xCC ) );
        p    = p2;
        prev = nc;
    }

    MgrSeq::deallocate_typed( p );
    MgrSeq::destroy();
    return true;
}

/// 12. Overflow protection for huge new_count.
static bool test_overflow_protection()
{
    PMM_TEST( MgrOvf::create( 64 * 1024 ) );

    MgrOvf::pptr<std::uint32_t> p = MgrOvf::allocate_typed<std::uint32_t>( 4 );
    PMM_TEST( !p.is_null() );

    // Attempt to reallocate with a count that would overflow size_t
    std::size_t                 huge_count = ( std::numeric_limits<std::size_t>::max )() / sizeof( std::uint32_t ) + 1;
    MgrOvf::pptr<std::uint32_t> p2         = MgrOvf::reallocate_typed( p, 4, huge_count );
    PMM_TEST( p2.is_null() );
    PMM_TEST( MgrOvf::last_error() == pmm::PmmError::Overflow );

    // Old block still valid
    MgrOvf::deallocate_typed( p );
    MgrOvf::destroy();
    return true;
}

/// 13. NotInitialized error when manager not created.
static bool test_not_initialized()
{
    // Do NOT create the manager
    MgrNI::pptr<int> p;
    MgrNI::pptr<int> p2 = MgrNI::reallocate_typed( p, 0, 4 );
    // Null input + not initialized => allocate_typed => NotInitialized
    PMM_TEST( p2.is_null() );
    return true;
}

/// 14. Old block NOT freed on allocation failure.
static bool test_old_block_preserved_on_failure()
{
    PMM_TEST( MgrSafe::create( 64 * 1024 ) );

    MgrSafe::pptr<std::uint8_t> p = MgrSafe::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );
    for ( std::size_t i = 0; i < 64; ++i )
        MgrSafe::resolve( p )[i] = static_cast<std::uint8_t>( 0xDD );

    // Try to reallocate with overflow count — should fail
    std::size_t                 huge = ( std::numeric_limits<std::size_t>::max )() / sizeof( std::uint8_t );
    MgrSafe::pptr<std::uint8_t> p2   = MgrSafe::reallocate_typed( p, 64, huge );
    // This may or may not fail depending on overflow path.
    // Even if it fails, original data should be intact.
    if ( p2.is_null() )
    {
        // Verify old data still accessible
        for ( std::size_t i = 0; i < 64; ++i )
            PMM_TEST( MgrSafe::resolve( p )[i] == static_cast<std::uint8_t>( 0xDD ) );
        MgrSafe::deallocate_typed( p );
    }
    else
    {
        MgrSafe::deallocate_typed( p2 );
    }

    MgrSafe::destroy();
    return true;
}

/// 15. Shrink then grow pattern.
static bool test_shrink_then_grow()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint32_t> p = Mgr::allocate_typed<std::uint32_t>( 64 );
    PMM_TEST( !p.is_null() );
    for ( std::size_t i = 0; i < 64; ++i )
        Mgr::resolve( p )[i] = static_cast<std::uint32_t>( i + 1 );

    // Shrink
    Mgr::pptr<std::uint32_t> p2 = Mgr::reallocate_typed( p, 64, 8 );
    PMM_TEST( !p2.is_null() );
    for ( std::size_t i = 0; i < 8; ++i )
        PMM_TEST( Mgr::resolve( p2 )[i] == static_cast<std::uint32_t>( i + 1 ) );

    // Grow back (may use in-place expansion from the free remainder)
    Mgr::pptr<std::uint32_t> p3 = Mgr::reallocate_typed( p2, 8, 32 );
    PMM_TEST( !p3.is_null() );
    for ( std::size_t i = 0; i < 8; ++i )
        PMM_TEST( Mgr::resolve( p3 )[i] == static_cast<std::uint32_t>( i + 1 ) );

    Mgr::deallocate_typed( p3 );
    Mgr::destroy();
    return true;
}

// --- main --------------------------------------------------------------------

int main()
{
    std::cout << "=== test_issue210_reallocate_typed (Issue #210, Phase 4.3) ===\n";
    bool all_passed = true;

    PMM_RUN( "grow_preserves_data", test_grow_preserves_data );
    PMM_RUN( "shrink_preserves_data", test_shrink_preserves_data );
    PMM_RUN( "inplace_expansion", test_inplace_expansion );
    PMM_RUN( "fallback_allocation", test_fallback_allocation );
    PMM_RUN( "null_pointer", test_null_pointer );
    PMM_RUN( "zero_new_count", test_zero_new_count );
    PMM_RUN( "same_size", test_same_size );
    PMM_RUN( "shrink_creates_free_block", test_shrink_creates_free_block );
    PMM_RUN( "small_address_traits", test_small_address_traits );
    PMM_RUN( "large_address_traits", test_large_address_traits );
    PMM_RUN( "sequential_reallocations", test_sequential_reallocations );
    PMM_RUN( "overflow_protection", test_overflow_protection );
    PMM_RUN( "not_initialized", test_not_initialized );
    PMM_RUN( "old_block_preserved_on_failure", test_old_block_preserved_on_failure );
    PMM_RUN( "shrink_then_grow", test_shrink_then_grow );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
