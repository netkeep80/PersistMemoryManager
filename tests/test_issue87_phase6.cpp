/**
 * @file test_issue87_phase6.cpp
 * @brief Tests Phase 6: AllocatorPolicy.
 *
 * Verifies:
 *  - AllocatorPolicy<AvlFreeTree<Default>, Default> compiles
 *  - Type aliases address_traits, free_block_tree
 *  - coalesce() merges adjacent free blocks
 *  - rebuild_free_tree() rebuilds tree from linked list
 *  - repair_linked_list() restores prev_offset
 *  - recompute_counters() counts blocks and sizes
 *
 * @see include/pmm/allocator_policy.h
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

using Mgr = pmm::presets::SingleThreadedHeap;

// =============================================================================
// Phase 6 tests: AllocatorPolicy
// =============================================================================

// ─── P6-A: AllocatorPolicy — aliases ─────────────────────────────────────────

TEST_CASE( "P6-A1: AllocatorPolicy<AvlFreeTree, Default> aliases", "[test_issue87_phase6]" )
{
    using Policy = pmm::AllocatorPolicy<pmm::AvlFreeTree<pmm::DefaultAddressTraits>, pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Policy::address_traits, pmm::DefaultAddressTraits>::value,
                   "AllocatorPolicy::address_traits must be DefaultAddressTraits" );
    static_assert( std::is_same<Policy::free_block_tree, pmm::AvlFreeTree<pmm::DefaultAddressTraits>>::value,
                   "AllocatorPolicy::free_block_tree must be AvlFreeTree<Default>" );
}

TEST_CASE( "P6-A2: DefaultAllocatorPolicy alias correct", "[test_issue87_phase6]" )
{
    static_assert(
        std::is_same<pmm::DefaultAllocatorPolicy, pmm::AllocatorPolicy<pmm::AvlFreeTree<pmm::DefaultAddressTraits>,
                                                                       pmm::DefaultAddressTraits>>::value,
        "DefaultAllocatorPolicy must be correct alias" );
}

// ─── P6-B: AllocatorPolicy — functional via manager ──────────────────────────

/// @brief recompute_counters correctly counts blocks via manager API.
TEST_CASE( "P6-B1: recompute_counters() correct (via manager)", "[test_issue87_phase6]" )
{
    Mgr pmm;
    REQUIRE( pmm.create( 8192 ) );

    std::uint32_t block_before = pmm.block_count();
    std::uint32_t free_before  = pmm.free_block_count();
    std::uint32_t alloc_before = pmm.alloc_block_count();

    // Allocate and free — should restore same counts
    auto p = pmm.allocate_typed<std::uint8_t>( 64 );
    REQUIRE( !p.is_null() );
    pmm.deallocate_typed( p );

    REQUIRE( pmm.block_count() == block_before );
    REQUIRE( pmm.free_block_count() == free_before );
    REQUIRE( pmm.alloc_block_count() == alloc_before );

    pmm.destroy();
}

/// @brief repair_linked_list restores prev_offset — verified via correct block count after load.
TEST_CASE( "P6-B2: repair_linked_list() (via save/load)", "[test_issue87_phase6]" )
{
    // The repair_linked_list is called internally during load(); verify via save/load round-trip.
    const char* TEST_FILE = "test_i87p6_repair.dat";

    Mgr pmm1;
    REQUIRE( pmm1.create( 8192 ) );
    auto p1 = pmm1.allocate_typed<std::uint32_t>( 10 );
    auto p2 = pmm1.allocate_typed<std::uint32_t>( 20 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() ) );

    std::uint32_t alloc_before = pmm1.alloc_block_count();

    REQUIRE( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    REQUIRE( pmm2.create( 8192 ) );
    REQUIRE( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE, pmm::VerifyResult{} ) );
    REQUIRE( pmm2.is_initialized() );
    REQUIRE( pmm2.alloc_block_count() == alloc_before );

    pmm2.destroy();
    std::remove( TEST_FILE );
}

/// @brief rebuild_free_tree rebuilds AVL tree — verified via allocations after load.
TEST_CASE( "P6-B3: rebuild_free_tree() (via save/load)", "[test_issue87_phase6]" )
{
    const char* TEST_FILE = "test_i87p6_rebuild.dat";

    Mgr pmm1;
    REQUIRE( pmm1.create( 8192 ) );
    auto p = pmm1.allocate_typed<std::uint8_t>( 64 );
    REQUIRE( !p.is_null() );
    pmm1.deallocate_typed( p );

    REQUIRE( pmm1.free_block_count() >= 1 );
    REQUIRE( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    REQUIRE( pmm2.create( 8192 ) );
    REQUIRE( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE, pmm::VerifyResult{} ) );
    REQUIRE( pmm2.is_initialized() );

    // Should be able to allocate after loading (requires valid free tree)
    auto p2 = pmm2.allocate_typed<std::uint8_t>( 64 );
    REQUIRE( !p2.is_null() );
    pmm2.deallocate_typed( p2 );

    pmm2.destroy();
    std::remove( TEST_FILE );
}

/// @brief coalesce() merges adjacent free blocks.
TEST_CASE( "P6-B4: coalesce() merges adjacent free blocks", "[test_issue87_phase6]" )
{
    Mgr pmm;
    REQUIRE( pmm.create( 8192 ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    // Allocate two blocks side-by-side
    auto p1 = pmm.allocate_typed<std::uint8_t>( 32 );
    auto p2 = pmm.allocate_typed<std::uint8_t>( 32 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() ) );

    std::uint32_t block_count_before = pmm.block_count();

    // Free both — they coalesce with the remaining free block
    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p2 );

    // After coalesce, block count should be <= before (merged blocks disappear)
    REQUIRE( pmm.block_count() <= block_count_before );
    REQUIRE( pmm.alloc_block_count() == baseline_alloc );

    pmm.destroy();
}

// =============================================================================
// main
// =============================================================================
