/**
 * @file test_avl_allocator.cpp
 * @brief Тесты AVL-дерева свободных блоков
 *
 * Проверяет корректность best-fit выбора и слияния через публичный API.
 */

#include "pmm_single_threaded_heap.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include <vector>

using Mgr = pmm::presets::SingleThreadedHeap;

/// After fresh create(), there should be exactly 1 free block and 1 alloc block
TEST_CASE( "free_block_has_zero_size", "[test_avl_allocator]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    // Immediately after creation: 1 free block
    REQUIRE( pmm.free_block_count() == 1 );
    REQUIRE( pmm.alloc_block_count() == baseline_alloc );

    pmm.destroy();
}

/// best-fit: allocate/deallocate multiple blocks; after coalesce, one free block remains
TEST_CASE( "best_fit_selection", "[test_avl_allocator]" )
{
    const std::size_t size = 256 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    // Create 4 blocks of different sizes: 512, 1024, 2048, 4096
    Mgr::pptr<std::uint8_t> p[4];
    p[0] = pmm.allocate_typed<std::uint8_t>( 512 );
    p[1] = pmm.allocate_typed<std::uint8_t>( 1024 );
    p[2] = pmm.allocate_typed<std::uint8_t>( 2048 );
    p[3] = pmm.allocate_typed<std::uint8_t>( 4096 );
    REQUIRE( ( !p[0].is_null() && !p[1].is_null() && !p[2].is_null() && !p[3].is_null() ) );
    REQUIRE( pmm.is_initialized() );

    // Free all — coalesce should merge adjacent blocks
    pmm.deallocate_typed( p[0] );
    pmm.deallocate_typed( p[1] );
    pmm.deallocate_typed( p[2] );
    pmm.deallocate_typed( p[3] );
    REQUIRE( pmm.is_initialized() );

    // Only one free block after full coalesce (all were adjacent)
    REQUIRE( pmm.free_block_count() == 1 );

    // Should be able to allocate a large block in the merged free space
    Mgr::pptr<std::uint8_t> big = pmm.allocate_typed<std::uint8_t>( 1500 );
    REQUIRE( !big.is_null() );
    REQUIRE( pmm.is_initialized() );

    pmm.deallocate_typed( big );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

/// AVL tree integrity stress: alloc many blocks, free every other, then rest
TEST_CASE( "avl_integrity_stress", "[test_avl_allocator]" )
{
    const std::size_t size = 512 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    static const int        N = 50;
    Mgr::pptr<std::uint8_t> ptrs[N];
    std::size_t             sizes[] = { 64, 128, 256, 512, 1024, 2048 };
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( sizes[i % 6] );
        REQUIRE( !ptrs[i].is_null() );
        REQUIRE( pmm.is_initialized() );
    }

    // Free every other
    for ( int i = 0; i < N; i += 2 )
    {
        pmm.deallocate_typed( ptrs[i] );
        REQUIRE( pmm.is_initialized() );
    }
    // Free the rest
    for ( int i = 1; i < N; i += 2 )
    {
        pmm.deallocate_typed( ptrs[i] );
        REQUIRE( pmm.is_initialized() );
    }

    // After full release: should have 1 free block and system blocks allocated
    REQUIRE( pmm.free_block_count() == 1 );
    REQUIRE( pmm.alloc_block_count() == baseline_alloc );

    pmm.destroy();
}

/// Test three-way coalesce: prev + current + next -> one block
TEST_CASE( "coalesce_three_way", "[test_avl_allocator]" )
{
    const std::size_t size = 128 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> p1 = pmm.allocate_typed<std::uint8_t>( 512 );
    Mgr::pptr<std::uint8_t> p2 = pmm.allocate_typed<std::uint8_t>( 512 );
    Mgr::pptr<std::uint8_t> p3 = pmm.allocate_typed<std::uint8_t>( 512 );
    Mgr::pptr<std::uint8_t> p4 = pmm.allocate_typed<std::uint8_t>( 512 ); // barrier
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() && !p4.is_null() ) );

    // Free p1 and p3 (non-adjacent free blocks)
    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p3 );
    REQUIRE( pmm.is_initialized() );

    std::size_t blocks_before = pmm.block_count();
    std::size_t free_before   = pmm.free_block_count();

    // Free p2 — should coalesce with p1 (prev) and p3 (next)
    pmm.deallocate_typed( p2 );
    REQUIRE( pmm.is_initialized() );

    // 2 merges = block_count decreased by 2
    REQUIRE( pmm.block_count() == blocks_before - 2 );
    // free_blocks decreased by 1 (3 became 1)
    REQUIRE( pmm.free_block_count() == free_before - 1 );

    pmm.deallocate_typed( p4 );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

/// Test: allocation/deallocation maintains consistent block counts
TEST_CASE( "block_count_consistency", "[test_avl_allocator]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    Mgr::pptr<std::uint8_t> p1 = pmm.allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = pmm.allocate_typed<std::uint8_t>( 512 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() ) );

    // block_count = alloc_count + free_count
    REQUIRE( pmm.block_count() == pmm.alloc_block_count() + pmm.free_block_count() );

    // 2 user blocks + system blocks
    REQUIRE( pmm.alloc_block_count() == baseline_alloc + 2 );
    REQUIRE( pmm.is_initialized() );

    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p2 );
    REQUIRE( pmm.is_initialized() );
    // After freeing both: consistency should still hold
    REQUIRE( pmm.block_count() == pmm.alloc_block_count() + pmm.free_block_count() );

    pmm.destroy();
}

/// Test: save/load preserves AVL tree structure and block counts
TEST_CASE( "avl_survives_save_load", "[test_avl_allocator]" )
{
    const std::size_t size      = 64 * 1024;
    const char*       TEST_FILE = "avl_test.dat";

    Mgr pmm1;
    REQUIRE( pmm1.create( size ) );

    Mgr::pptr<std::uint8_t> p1 = pmm1.allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = pmm1.allocate_typed<std::uint8_t>( 512 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() ) );
    pmm1.deallocate_typed( p1 ); // create fragmentation
    REQUIRE( pmm1.is_initialized() );

    std::size_t blocks_before = pmm1.block_count();
    std::size_t free_before   = pmm1.free_block_count();
    std::size_t alloc_before  = pmm1.alloc_block_count();

    REQUIRE( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    REQUIRE( pmm2.create( size ) );
    REQUIRE( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE, pmm::VerifyResult{} ) );
    REQUIRE( pmm2.is_initialized() );

    REQUIRE( pmm2.block_count() == blocks_before );
    REQUIRE( pmm2.free_block_count() == free_before );
    REQUIRE( pmm2.alloc_block_count() == alloc_before );

    // Should be able to allocate memory after load
    Mgr::pptr<std::uint8_t> p3 = pmm2.allocate_typed<std::uint8_t>( 128 );
    REQUIRE( !p3.is_null() );
    REQUIRE( pmm2.is_initialized() );

    pmm2.deallocate_typed( p3 );
    pmm2.destroy();
    std::remove( TEST_FILE );
}

/// Test: best-fit chooses the smallest fitting block
TEST_CASE( "best_fit_chooses_smallest_fitting", "[test_avl_allocator]" )
{
    const std::size_t size = 512 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    // Create blocks to get gaps of different sizes after freeing
    Mgr::pptr<std::uint8_t> barrier[5];
    Mgr::pptr<std::uint8_t> gap[4];
    gap[0]     = pmm.allocate_typed<std::uint8_t>( 64 );
    barrier[0] = pmm.allocate_typed<std::uint8_t>( 64 );
    gap[1]     = pmm.allocate_typed<std::uint8_t>( 256 );
    barrier[1] = pmm.allocate_typed<std::uint8_t>( 64 );
    gap[2]     = pmm.allocate_typed<std::uint8_t>( 512 );
    barrier[2] = pmm.allocate_typed<std::uint8_t>( 64 );
    gap[3]     = pmm.allocate_typed<std::uint8_t>( 1024 );
    barrier[3] = pmm.allocate_typed<std::uint8_t>( 64 );
    barrier[4] = pmm.allocate_typed<std::uint8_t>( 128 ); // keep allocated at end
    REQUIRE( ( !gap[0].is_null() && !barrier[0].is_null() && !gap[1].is_null() && !barrier[1].is_null() ) );
    REQUIRE( ( !gap[2].is_null() && !barrier[2].is_null() && !gap[3].is_null() && !barrier[3].is_null() ) );
    REQUIRE( !barrier[4].is_null() );

    // Free gaps — create fragmentation
    pmm.deallocate_typed( gap[0] );
    pmm.deallocate_typed( gap[1] );
    pmm.deallocate_typed( gap[2] );
    pmm.deallocate_typed( gap[3] );
    REQUIRE( pmm.is_initialized() );

    // Request 200 bytes: best-fit should choose the gap[1] (256 bytes — smallest fitting)
    Mgr::pptr<std::uint8_t> result = pmm.allocate_typed<std::uint8_t>( 200 );
    REQUIRE( !result.is_null() );
    REQUIRE( pmm.is_initialized() );

    // Cleanup
    pmm.deallocate_typed( result );
    for ( int i = 0; i < 4; i++ )
        pmm.deallocate_typed( barrier[i] );
    pmm.deallocate_typed( barrier[4] );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

/// Test: allocate/deallocate works correctly
TEST_CASE( "alloc_dealloc_works", "[test_avl_allocator]" )
{
    const std::size_t size = 128 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> ptr = pmm.allocate_typed<std::uint8_t>( 256 );
    REQUIRE( !ptr.is_null() );

    // Write data
    std::memset( ptr.resolve(), 0xAB, 256 );

    // Allocate a bigger block (no reallocate in new API)
    Mgr::pptr<std::uint8_t> new_ptr = pmm.allocate_typed<std::uint8_t>( 512 );
    REQUIRE( !new_ptr.is_null() );

    // Copy data
    std::memcpy( new_ptr.resolve(), ptr.resolve(), 256 );
    pmm.deallocate_typed( ptr );

    REQUIRE( pmm.is_initialized() );

    // Verify data preserved
    const std::uint8_t* p = new_ptr.resolve();
    for ( std::size_t i = 0; i < 256; i++ )
        REQUIRE( p[i] == 0xAB );

    pmm.deallocate_typed( new_ptr );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

/// Test: free block count after dealloc
TEST_CASE( "block_count_after_dealloc", "[test_avl_allocator]" )
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( size ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    // Fresh: system blocks alloc + 1 free
    REQUIRE( pmm.alloc_block_count() == baseline_alloc );
    REQUIRE( pmm.free_block_count() == 1 );

    Mgr::pptr<std::uint32_t> p = pmm.allocate_typed<std::uint32_t>( 4 );
    REQUIRE( !p.is_null() );
    REQUIRE( pmm.alloc_block_count() == baseline_alloc + 1 ); // system + p

    pmm.deallocate_typed( p );
    REQUIRE( pmm.is_initialized() );

    // After dealloc: back to system blocks + 1 free (coalesced)
    REQUIRE( pmm.alloc_block_count() == baseline_alloc ); // only system blocks
    REQUIRE( pmm.free_block_count() == 1 );

    pmm.destroy();
}
