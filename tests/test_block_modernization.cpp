/**
 * @file test_block_modernization.cpp
 * @brief Tests for block modernization
 *
 * Tests block format correctness, save/load round-trips, and coalesce behavior.
 * Legacy: magic removed, block_data_size_bytes removed.
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

using Mgr = pmm::presets::SingleThreadedHeap;

// ─── Test 1: Block<A> structural sizes ────────────────────────────────────────

/// Block<DefaultAddressTraits> must be 32 bytes = 2 granules.
TEST_CASE( "block_header_no_magic", "[test_block_modernization]" )
{
    using Block = pmm::Block<pmm::DefaultAddressTraits>;
    static_assert( sizeof( Block ) == 32, "Block<DefaultAddressTraits> must be exactly 32 bytes" );
    static_assert( sizeof( Block ) % pmm::kGranuleSize == 0, "Block<DefaultAddressTraits> must be granule-aligned" );
    // kBlockMagic is gone: compilation success means this test passes
}

// ─── Test 2: Basic alloc/dealloc with block count verification ────────────────

TEST_CASE( "basic_alloc_block_count", "[test_block_modernization]" )
{
    Mgr pmm;
    REQUIRE( pmm.create( 64 * 1024 ) );
    REQUIRE( pmm.is_initialized() );
    const auto baseline_alloc = pmm.alloc_block_count();

    auto p1 = pmm.allocate_typed<std::uint32_t>( 16 );
    REQUIRE( !p1.is_null() );
    REQUIRE( pmm.is_initialized() );

    auto p2 = pmm.allocate_typed<std::uint64_t>( 8 );
    REQUIRE( !p2.is_null() );
    REQUIRE( pmm.is_initialized() );

    pmm.deallocate_typed( p1 );
    REQUIRE( pmm.is_initialized() );

    pmm.deallocate_typed( p2 );
    REQUIRE( pmm.is_initialized() );

    REQUIRE( pmm.alloc_block_count() == baseline_alloc );

    pmm.destroy();
}

// ─── Test 3: Save and load round-trip preserves data ────────────────────────

TEST_CASE( "save_load_new_format", "[test_block_modernization]" )
{
    const char* TEST_FILE = "test_block_mod.dat";

    Mgr pmm1;
    REQUIRE( pmm1.create( 64 * 1024 ) );

    // Allocate a few blocks of different sizes
    auto p1 = pmm1.allocate_typed<std::uint8_t>( 100 );
    auto p2 = pmm1.allocate_typed<std::uint8_t>( 200 );
    auto p3 = pmm1.allocate_typed<std::uint8_t>( 300 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );

    std::memset( p1.resolve(), 0x11, 100 );
    std::memset( p2.resolve(), 0x22, 200 );
    std::memset( p3.resolve(), 0x33, 300 );

    // Free middle block (creates fragmentation)
    pmm1.deallocate_typed( p2 );
    REQUIRE( pmm1.is_initialized() );

    std::uint32_t alloc_before = pmm1.alloc_block_count();
    std::uint32_t free_before  = pmm1.free_block_count();
    std::uint32_t off1         = p1.offset();
    std::uint32_t off3         = p3.offset();

    REQUIRE( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    REQUIRE( pmm2.create( 64 * 1024 ) );
    REQUIRE( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE, pmm::VerifyResult{} ) );
    REQUIRE( pmm2.is_initialized() );

    // Verify block counts are preserved
    REQUIRE( pmm2.alloc_block_count() == alloc_before );
    REQUIRE( pmm2.free_block_count() == free_before );

    // Verify data is preserved
    Mgr::pptr<std::uint8_t> q1( off1 );
    Mgr::pptr<std::uint8_t> q3( off3 );
    for ( std::size_t i = 0; i < 100; i++ )
        REQUIRE( q1.resolve()[i] == 0x11 );
    for ( std::size_t i = 0; i < 300; i++ )
        REQUIRE( q3.resolve()[i] == 0x33 );

    pmm2.deallocate_typed( q1 );
    pmm2.deallocate_typed( q3 );
    REQUIRE( pmm2.alloc_block_count() == alloc_before - 2 );

    pmm2.destroy();
    std::remove( TEST_FILE );
}

// ─── Test 4: Coalesce leaves single free block ─────────────────────────────

/// After freeing adjacent blocks, they should coalesce.
TEST_CASE( "coalesced_leaves_one_free_block", "[test_block_modernization]" )
{
    Mgr pmm;
    REQUIRE( pmm.create( 16 * 1024 ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    // Allocate two adjacent blocks
    auto p1 = pmm.allocate_typed<std::uint8_t>( 64 );
    auto p2 = pmm.allocate_typed<std::uint8_t>( 64 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() ) );

    // Free both — they should coalesce with each other and the original free block
    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p2 );
    REQUIRE( pmm.is_initialized() );

    REQUIRE( pmm.alloc_block_count() == baseline_alloc );
    // Free blocks: after coalesce should be 1 (all merged)
    REQUIRE( pmm.free_block_count() == 1 );

    pmm.destroy();
}

// ─── Test 5: Multiple save/load cycles ───────────────────────────────────────

TEST_CASE( "stress_save_load", "[test_block_modernization]" )
{
    const char* TEST_FILE = "test_stress_mod.dat";

    Mgr pmm1;
    REQUIRE( pmm1.create( 128 * 1024 ) );

    // Allocate many blocks
    const int               N = 50;
    Mgr::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm1.allocate_typed<std::uint8_t>( ( i + 1 ) * 16 );
        REQUIRE( !ptrs[i].is_null() );
        std::memset( ptrs[i].resolve(), static_cast<int>( i + 1 ), ( i + 1 ) * 16 );
    }

    // Free half
    for ( int i = 0; i < N; i += 2 )
        pmm1.deallocate_typed( ptrs[i] );

    REQUIRE( pmm1.is_initialized() );
    std::uint32_t alloc_before = pmm1.alloc_block_count();
    std::uint32_t free_before  = pmm1.free_block_count();

    REQUIRE( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    REQUIRE( pmm2.create( 128 * 1024 ) );
    REQUIRE( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE, pmm::VerifyResult{} ) );
    REQUIRE( pmm2.is_initialized() );

    REQUIRE( pmm2.alloc_block_count() == alloc_before );
    REQUIRE( pmm2.free_block_count() == free_before );

    // Verify data of odd blocks (not freed)
    for ( int i = 1; i < N; i += 2 )
    {
        std::size_t block_size = static_cast<std::size_t>( ( i + 1 ) * 16 );
        const auto* bytes      = ptrs[i].resolve();
        for ( std::size_t j = 0; j < block_size; j++ )
            REQUIRE( bytes[j] == static_cast<std::uint8_t>( i + 1 ) );
    }

    pmm2.destroy();
    std::remove( TEST_FILE );
}

// ─── Test 6: ManagerHeader granule_size check ────────────────────────────────

/// ManagerHeader must record the correct granule_size.
TEST_CASE( "manager_header_granule_size", "[test_block_modernization]" )
{
    // ManagerHeader is now templated; DefaultAddressTraits variant must remain 64 bytes.
    static_assert( sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) == 64,
                   "ManagerHeader<DefaultAddressTraits> must be exactly 64 bytes " );
    // The ManagerHeader.granule_size field is validated on load — tested by save/load round-trip
}
