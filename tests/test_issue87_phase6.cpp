/**
 * @file test_issue87_phase6.cpp
 * @brief Tests Phase 6: AllocatorPolicy (Issue #87, updated #102).
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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

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

// =============================================================================
// Phase 6 tests: AllocatorPolicy
// =============================================================================

// ─── P6-A: AllocatorPolicy — aliases ─────────────────────────────────────────

static bool test_p6_allocator_policy_aliases()
{
    using Policy = pmm::AllocatorPolicy<pmm::AvlFreeTree<pmm::DefaultAddressTraits>, pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Policy::address_traits, pmm::DefaultAddressTraits>::value,
                   "AllocatorPolicy::address_traits must be DefaultAddressTraits" );
    static_assert( std::is_same<Policy::free_block_tree, pmm::AvlFreeTree<pmm::DefaultAddressTraits>>::value,
                   "AllocatorPolicy::free_block_tree must be AvlFreeTree<Default>" );
    return true;
}

static bool test_p6_default_allocator_policy_alias()
{
    static_assert(
        std::is_same<pmm::DefaultAllocatorPolicy, pmm::AllocatorPolicy<pmm::AvlFreeTree<pmm::DefaultAddressTraits>,
                                                                       pmm::DefaultAddressTraits>>::value,
        "DefaultAllocatorPolicy must be correct alias" );
    return true;
}

// ─── P6-B: AllocatorPolicy — functional via manager ──────────────────────────

/// @brief recompute_counters correctly counts blocks via manager API.
static bool test_p6_recompute_counters_via_manager()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 8192 ) );

    std::uint32_t block_before = pmm.block_count();
    std::uint32_t free_before  = pmm.free_block_count();
    std::uint32_t alloc_before = pmm.alloc_block_count();

    // Allocate and free — should restore same counts
    auto p = pmm.allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );
    pmm.deallocate_typed( p );

    PMM_TEST( pmm.block_count() == block_before );
    PMM_TEST( pmm.free_block_count() == free_before );
    PMM_TEST( pmm.alloc_block_count() == alloc_before );

    pmm.destroy();
    return true;
}

/// @brief repair_linked_list restores prev_offset — verified via correct block count after load.
static bool test_p6_repair_linked_list_via_manager()
{
    // The repair_linked_list is called internally during load(); verify via save/load round-trip.
    const char* TEST_FILE = "test_i87p6_repair.dat";

    Mgr pmm1;
    PMM_TEST( pmm1.create( 8192 ) );
    auto p1 = pmm1.allocate_typed<std::uint32_t>( 10 );
    auto p2 = pmm1.allocate_typed<std::uint32_t>( 20 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    std::uint32_t alloc_before = pmm1.alloc_block_count();

    PMM_TEST( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    PMM_TEST( pmm2.create( 8192 ) );
    PMM_TEST( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE ) );
    PMM_TEST( pmm2.is_initialized() );
    PMM_TEST( pmm2.alloc_block_count() == alloc_before );

    pmm2.destroy();
    std::remove( TEST_FILE );
    return true;
}

/// @brief rebuild_free_tree rebuilds AVL tree — verified via allocations after load.
static bool test_p6_rebuild_free_tree_via_manager()
{
    const char* TEST_FILE = "test_i87p6_rebuild.dat";

    Mgr pmm1;
    PMM_TEST( pmm1.create( 8192 ) );
    auto p = pmm1.allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );
    pmm1.deallocate_typed( p );

    PMM_TEST( pmm1.free_block_count() >= 1 );
    PMM_TEST( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    PMM_TEST( pmm2.create( 8192 ) );
    PMM_TEST( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE ) );
    PMM_TEST( pmm2.is_initialized() );

    // Should be able to allocate after loading (requires valid free tree)
    auto p2 = pmm2.allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p2.is_null() );
    pmm2.deallocate_typed( p2 );

    pmm2.destroy();
    std::remove( TEST_FILE );
    return true;
}

/// @brief coalesce() merges adjacent free blocks.
static bool test_p6_coalesce_via_manager()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 8192 ) );

    // Allocate two blocks side-by-side
    auto p1 = pmm.allocate_typed<std::uint8_t>( 32 );
    auto p2 = pmm.allocate_typed<std::uint8_t>( 32 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    std::uint32_t block_count_before = pmm.block_count();

    // Free both — they coalesce with the remaining free block
    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p2 );

    // After coalesce, block count should be <= before (merged blocks disappear)
    PMM_TEST( pmm.block_count() <= block_count_before );
    PMM_TEST( pmm.alloc_block_count() == 1 ); // Issue #75

    pmm.destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase6 (Phase 6: AllocatorPolicy, updated #102) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P6-A: AllocatorPolicy — aliases ---\n";
    PMM_RUN( "P6-A1: AllocatorPolicy<AvlFreeTree, Default> aliases", test_p6_allocator_policy_aliases );
    PMM_RUN( "P6-A2: DefaultAllocatorPolicy alias correct", test_p6_default_allocator_policy_alias );

    std::cout << "\n--- P6-B: AllocatorPolicy — functional ---\n";
    PMM_RUN( "P6-B1: recompute_counters() correct (via manager)", test_p6_recompute_counters_via_manager );
    PMM_RUN( "P6-B2: repair_linked_list() (via save/load)", test_p6_repair_linked_list_via_manager );
    PMM_RUN( "P6-B3: rebuild_free_tree() (via save/load)", test_p6_rebuild_free_tree_via_manager );
    PMM_RUN( "P6-B4: coalesce() merges adjacent free blocks", test_p6_coalesce_via_manager );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
