/**
 * @file test_issue243_free_tree_policy.cpp
 * @brief Regression tests for the free-tree forest-policy ordering contract (Issue #243).
 *
 * Verifies the canonized free-tree policy:
 *   - Sort key: (block_size_in_granules, block_index) — strict total ordering.
 *   - Tie-breaker: when two free blocks have the same size, they are ordered by block_index.
 *   - find_best_fit() returns the minimum free block >= requested size.
 *
 * These tests lock down the behavioural contract documented in
 * docs/free_tree_forest_policy.md to prevent docs-vs-code divergence
 * on future refactors.
 *
 * @see include/pmm/free_block_tree.h — AvlFreeTree implementation
 * @see docs/free_tree_forest_policy.md — canonical policy document
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using Mgr = pmm::presets::SingleThreadedHeap;

// ─── Helper: collect free blocks via in-order traversal ─────────────────────

static std::vector<pmm::FreeBlockView> collect_free_blocks()
{
    std::vector<pmm::FreeBlockView> blocks;
    Mgr::for_each_free_block( [&]( const pmm::FreeBlockView& v ) { blocks.push_back( v ); } );
    return blocks;
}

// ─── Test 1: same-size free blocks ordered by block_index (tie-breaker) ─────

/**
 * @brief Two free blocks of the same size are ordered by block_index in the AVL tree.
 *
 * Strategy:
 *   1. Allocate three equal-sized blocks and barriers between them.
 *   2. Free the equal-sized blocks (non-adjacent due to barriers).
 *   3. Collect free blocks via in-order traversal.
 *   4. Among free blocks with the same total_size, verify they appear
 *      in ascending offset order (= ascending block_index).
 *
 * This locks down the tie-breaker part of the policy:
 *   sort key = (block_size_in_granules, block_index).
 */
TEST_CASE( "free_tree_policy: same-size blocks ordered by block_index", "[test_issue243]" )
{
    const std::size_t arena_size = 256 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( arena_size ) );

    // Allocate equal-sized blocks interleaved with barriers to prevent coalescing.
    // Pattern: [gap0] [barrier0] [gap1] [barrier1] [gap2] [barrier2] [tail_barrier]
    // All gaps are the same requested size (512 bytes).
    const std::size_t gap_size     = 512;
    const std::size_t barrier_size = 64;
    const int         num_gaps     = 3;

    Mgr::pptr<std::uint8_t> gaps[3];
    Mgr::pptr<std::uint8_t> barriers[4];

    for ( int i = 0; i < num_gaps; ++i )
    {
        gaps[i]     = pmm.allocate_typed<std::uint8_t>( gap_size );
        barriers[i] = pmm.allocate_typed<std::uint8_t>( barrier_size );
        REQUIRE( !gaps[i].is_null() );
        REQUIRE( !barriers[i].is_null() );
    }
    // Tail barrier to prevent coalescing with the trailing free space.
    barriers[num_gaps] = pmm.allocate_typed<std::uint8_t>( barrier_size );
    REQUIRE( !barriers[num_gaps].is_null() );

    // Free the gap blocks — creates three non-adjacent free blocks of equal size.
    for ( int i = 0; i < num_gaps; ++i )
        pmm.deallocate_typed( gaps[i] );

    REQUIRE( pmm.is_initialized() );

    // Collect free blocks via in-order traversal.
    auto blocks = collect_free_blocks();
    REQUIRE( blocks.size() >= static_cast<std::size_t>( num_gaps ) );

    // Find the gap-sized free blocks (they should all have the same total_size).
    // The first freed gap determines the expected total_size.
    // Filter: find blocks whose total_size equals the most common size among small blocks.
    // Since all gaps were allocated with the same byte size, their granule sizes match.
    std::vector<pmm::FreeBlockView> same_size_blocks;
    if ( blocks.size() >= 2 )
    {
        // The gap blocks should all have the same total_size.
        // Identify it: it's the total_size that appears exactly num_gaps times.
        std::vector<std::size_t> sizes;
        for ( const auto& b : blocks )
            sizes.push_back( b.total_size );
        std::sort( sizes.begin(), sizes.end() );

        for ( const auto& b : blocks )
        {
            std::size_t count = std::count( sizes.begin(), sizes.end(), b.total_size );
            if ( count >= static_cast<std::size_t>( num_gaps ) && b.total_size < arena_size / 2 )
            {
                same_size_blocks.push_back( b );
            }
        }
    }

    // We expect at least num_gaps blocks with the same size.
    REQUIRE( same_size_blocks.size() >= static_cast<std::size_t>( num_gaps ) );

    // Verify: among blocks with the same total_size, the in-order traversal
    // visits them in ascending offset order (= ascending block_index).
    // Since for_each_free_block does in-order traversal and the sort key is
    // (block_size, block_index), blocks with the same size must appear
    // in ascending block_index (offset) order.
    for ( std::size_t i = 1; i < same_size_blocks.size(); ++i )
    {
        if ( same_size_blocks[i].total_size == same_size_blocks[i - 1].total_size )
        {
            INFO( "Block at offset " << same_size_blocks[i - 1].offset << " (size "
                                     << same_size_blocks[i - 1].total_size << ") must come before block at offset "
                                     << same_size_blocks[i].offset );
            REQUIRE( same_size_blocks[i].offset > same_size_blocks[i - 1].offset );
        }
    }

    // Cleanup.
    for ( int i = 0; i <= num_gaps; ++i )
        pmm.deallocate_typed( barriers[i] );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

// ─── Test 2: find_best_fit() selects the minimum fitting block ──────────────

/**
 * @brief find_best_fit() chooses the smallest free block >= requested size.
 *
 * Strategy:
 *   1. Create free gaps of different sizes by allocating and selectively freeing.
 *   2. Allocate a size that should exactly fit in the second-smallest gap.
 *   3. Verify the allocation lands inside that gap (by checking the pointer offset).
 *
 * This locks down the best-fit semantics of the forest-policy:
 *   find_best_fit traverses left (smaller) first, recording the first node >= needed.
 */
TEST_CASE( "free_tree_policy: find_best_fit selects minimum fitting block", "[test_issue243]" )
{
    const std::size_t arena_size = 512 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( arena_size ) );

    // Create gaps of 4 different sizes by allocating then selectively freeing.
    // Layout: [small_gap] [barrier] [medium_gap] [barrier] [large_gap] [barrier] [xlarge_gap] [barrier]
    const std::size_t small_bytes  = 128;
    const std::size_t medium_bytes = 512;
    const std::size_t large_bytes  = 2048;
    const std::size_t xlarge_bytes = 4096;

    Mgr::pptr<std::uint8_t> gap_small  = pmm.allocate_typed<std::uint8_t>( small_bytes );
    Mgr::pptr<std::uint8_t> bar0       = pmm.allocate_typed<std::uint8_t>( 64 );
    Mgr::pptr<std::uint8_t> gap_medium = pmm.allocate_typed<std::uint8_t>( medium_bytes );
    Mgr::pptr<std::uint8_t> bar1       = pmm.allocate_typed<std::uint8_t>( 64 );
    Mgr::pptr<std::uint8_t> gap_large  = pmm.allocate_typed<std::uint8_t>( large_bytes );
    Mgr::pptr<std::uint8_t> bar2       = pmm.allocate_typed<std::uint8_t>( 64 );
    Mgr::pptr<std::uint8_t> gap_xlarge = pmm.allocate_typed<std::uint8_t>( xlarge_bytes );
    Mgr::pptr<std::uint8_t> bar3       = pmm.allocate_typed<std::uint8_t>( 64 );

    REQUIRE( !gap_small.is_null() );
    REQUIRE( !gap_medium.is_null() );
    REQUIRE( !gap_large.is_null() );
    REQUIRE( !gap_xlarge.is_null() );
    REQUIRE( !bar0.is_null() );
    REQUIRE( !bar1.is_null() );
    REQUIRE( !bar2.is_null() );
    REQUIRE( !bar3.is_null() );

    // Record raw pointers of gaps before freeing (for range checks).
    std::uint8_t* medium_raw = gap_medium.resolve();

    // Free all gaps to create 4 free blocks of different sizes.
    pmm.deallocate_typed( gap_small );
    pmm.deallocate_typed( gap_medium );
    pmm.deallocate_typed( gap_large );
    pmm.deallocate_typed( gap_xlarge );
    REQUIRE( pmm.is_initialized() );

    // Collect free blocks to understand what sizes we actually have.
    auto blocks = collect_free_blocks();
    REQUIRE( blocks.size() >= 4 );

    // Verify the in-order traversal is sorted by (total_size, offset) — the policy.
    for ( std::size_t i = 1; i < blocks.size(); ++i )
    {
        bool correctly_ordered =
            ( blocks[i].total_size > blocks[i - 1].total_size ) ||
            ( blocks[i].total_size == blocks[i - 1].total_size && blocks[i].offset > blocks[i - 1].offset );
        INFO( "In-order block [" << ( i - 1 ) << "] size=" << blocks[i - 1].total_size
                                 << " offset=" << blocks[i - 1].offset << " -> block [" << i
                                 << "] size=" << blocks[i].total_size << " offset=" << blocks[i].offset );
        REQUIRE( correctly_ordered );
    }

    // Now allocate a size that is too big for the small gap but fits in the medium gap.
    // The best-fit policy should select the medium gap (smallest fitting block).
    // Request size > small_bytes but <= medium_bytes.
    const std::size_t request_bytes = small_bytes + 64; // 192 bytes — won't fit in small (128), fits in medium (512)
    Mgr::pptr<std::uint8_t> result  = pmm.allocate_typed<std::uint8_t>( request_bytes );
    REQUIRE( !result.is_null() );

    // The result pointer should land inside the medium gap's former region.
    // The block header occupies the beginning of the block, so the user pointer
    // is at some fixed offset after the block header.
    // Since the medium gap was at medium_raw, the allocated pointer should
    // be at the same address (best-fit reuses the exact block).
    std::uint8_t* result_raw = result.resolve();
    REQUIRE( result_raw == medium_raw );

    // Cleanup.
    pmm.deallocate_typed( result );
    pmm.deallocate_typed( bar0 );
    pmm.deallocate_typed( bar1 );
    pmm.deallocate_typed( bar2 );
    pmm.deallocate_typed( bar3 );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}
