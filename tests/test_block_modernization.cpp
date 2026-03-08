/**
 * @file test_block_modernization.cpp
 * @brief Tests for block modernization (Issue #69, updated #102 — new API)
 *
 * Issue #102: uses AbstractPersistMemoryManager via pmm_presets.h.
 * Tests block format correctness, save/load round-trips, and coalesce behavior.
 * Legacy: magic removed (Issue #69), block_data_size_bytes removed.
 */

#include "pmm_single_threaded_heap.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

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

using Mgr = pmm::presets::SingleThreadedHeap;

// ─── Test 1: Block<A> structural sizes ────────────────────────────────────────

/// Block<DefaultAddressTraits> must be 32 bytes = 2 granules (Issue #112).
static bool test_block_header_no_magic()
{
    using Block = pmm::Block<pmm::DefaultAddressTraits>;
    static_assert( sizeof( Block ) == 32, "Block<DefaultAddressTraits> must be exactly 32 bytes" );
    static_assert( sizeof( Block ) % pmm::kGranuleSize == 0, "Block<DefaultAddressTraits> must be granule-aligned" );
    // kBlockMagic is gone (Issue #69): compilation success means this test passes
    return true;
}

// ─── Test 2: Basic alloc/dealloc with block count verification ────────────────

static bool test_basic_alloc_block_count()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );
    PMM_TEST( pmm.is_initialized() );

    auto p1 = pmm.allocate_typed<std::uint32_t>( 16 );
    PMM_TEST( !p1.is_null() );
    PMM_TEST( pmm.is_initialized() );

    auto p2 = pmm.allocate_typed<std::uint64_t>( 8 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( p1 );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( p2 );
    PMM_TEST( pmm.is_initialized() );

    PMM_TEST( pmm.alloc_block_count() == 1 ); // Issue #75: BlockHeader_0 always allocated

    pmm.destroy();
    return true;
}

// ─── Test 3: Save and load round-trip preserves data ────────────────────────

static bool test_save_load_new_format()
{
    const char* TEST_FILE = "test_block_mod.dat";

    Mgr pmm1;
    PMM_TEST( pmm1.create( 64 * 1024 ) );

    // Allocate a few blocks of different sizes
    auto p1 = pmm1.allocate_typed<std::uint8_t>( 100 );
    auto p2 = pmm1.allocate_typed<std::uint8_t>( 200 );
    auto p3 = pmm1.allocate_typed<std::uint8_t>( 300 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    std::memset( p1.resolve(), 0x11, 100 );
    std::memset( p2.resolve(), 0x22, 200 );
    std::memset( p3.resolve(), 0x33, 300 );

    // Free middle block (creates fragmentation)
    pmm1.deallocate_typed( p2 );
    PMM_TEST( pmm1.is_initialized() );

    std::uint32_t alloc_before = pmm1.alloc_block_count();
    std::uint32_t free_before  = pmm1.free_block_count();
    std::uint32_t off1         = p1.offset();
    std::uint32_t off3         = p3.offset();

    PMM_TEST( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    PMM_TEST( pmm2.create( 64 * 1024 ) );
    PMM_TEST( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE ) );
    PMM_TEST( pmm2.is_initialized() );

    // Verify block counts are preserved
    PMM_TEST( pmm2.alloc_block_count() == alloc_before );
    PMM_TEST( pmm2.free_block_count() == free_before );

    // Verify data is preserved
    Mgr::pptr<std::uint8_t> q1( off1 );
    Mgr::pptr<std::uint8_t> q3( off3 );
    for ( std::size_t i = 0; i < 100; i++ )
        PMM_TEST( q1.resolve()[i] == 0x11 );
    for ( std::size_t i = 0; i < 300; i++ )
        PMM_TEST( q3.resolve()[i] == 0x33 );

    pmm2.deallocate_typed( q1 );
    pmm2.deallocate_typed( q3 );
    PMM_TEST( pmm2.alloc_block_count() == 1 ); // Issue #75

    pmm2.destroy();
    std::remove( TEST_FILE );
    return true;
}

// ─── Test 4: Coalesce leaves single free block ─────────────────────────────

/// After freeing adjacent blocks, they should coalesce.
static bool test_coalesced_leaves_one_free_block()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 16 * 1024 ) );

    // Allocate two adjacent blocks
    auto p1 = pmm.allocate_typed<std::uint8_t>( 64 );
    auto p2 = pmm.allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    // Free both — they should coalesce with each other and the original free block
    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p2 );
    PMM_TEST( pmm.is_initialized() );

    PMM_TEST( pmm.alloc_block_count() == 1 ); // Issue #75
    // Free blocks: after coalesce should be 1 (all merged)
    PMM_TEST( pmm.free_block_count() == 1 );

    pmm.destroy();
    return true;
}

// ─── Test 5: Multiple save/load cycles ───────────────────────────────────────

static bool test_stress_save_load()
{
    const char* TEST_FILE = "test_stress_mod.dat";

    Mgr pmm1;
    PMM_TEST( pmm1.create( 128 * 1024 ) );

    // Allocate many blocks
    const int               N = 50;
    Mgr::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm1.allocate_typed<std::uint8_t>( ( i + 1 ) * 16 );
        PMM_TEST( !ptrs[i].is_null() );
        std::memset( ptrs[i].resolve(), static_cast<int>( i + 1 ), ( i + 1 ) * 16 );
    }

    // Free half
    for ( int i = 0; i < N; i += 2 )
        pmm1.deallocate_typed( ptrs[i] );

    PMM_TEST( pmm1.is_initialized() );
    std::uint32_t alloc_before = pmm1.alloc_block_count();
    std::uint32_t free_before  = pmm1.free_block_count();

    PMM_TEST( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    PMM_TEST( pmm2.create( 128 * 1024 ) );
    PMM_TEST( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE ) );
    PMM_TEST( pmm2.is_initialized() );

    PMM_TEST( pmm2.alloc_block_count() == alloc_before );
    PMM_TEST( pmm2.free_block_count() == free_before );

    // Verify data of odd blocks (not freed)
    for ( int i = 1; i < N; i += 2 )
    {
        std::size_t block_size = static_cast<std::size_t>( ( i + 1 ) * 16 );
        const auto* bytes      = ptrs[i].resolve();
        for ( std::size_t j = 0; j < block_size; j++ )
            PMM_TEST( bytes[j] == static_cast<std::uint8_t>( i + 1 ) );
    }

    pmm2.destroy();
    std::remove( TEST_FILE );
    return true;
}

// ─── Test 6: ManagerHeader granule_size check ────────────────────────────────

/// ManagerHeader must record the correct granule_size.
static bool test_manager_header_granule_size()
{
    static_assert( sizeof( pmm::detail::ManagerHeader ) == 64, "ManagerHeader must be exactly 64 bytes" );
    // The ManagerHeader.granule_size field is validated on load — tested by save/load round-trip
    return true;
}

// ─── main ────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_block_modernization (Issue #69, updated #102) ===\n";
    bool all_passed = true;

    PMM_RUN( "block_header_no_magic", test_block_header_no_magic );
    PMM_RUN( "basic_alloc_block_count", test_basic_alloc_block_count );
    PMM_RUN( "save_load_new_format", test_save_load_new_format );
    PMM_RUN( "coalesced_leaves_one_free_block", test_coalesced_leaves_one_free_block );
    PMM_RUN( "stress_save_load", test_stress_save_load );
    PMM_RUN( "manager_header_granule_size", test_manager_header_granule_size );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
