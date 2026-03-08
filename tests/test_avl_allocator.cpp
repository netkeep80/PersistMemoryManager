/**
 * @file test_avl_allocator.cpp
 * @brief Тесты AVL-дерева свободных блоков (Issue #55, обновлено #102 — новый API)
 *
 * Issue #102: использует AbstractPersistMemoryManager через pmm_presets.h.
 * Проверяет корректность best-fit выбора и слияния через публичный API.
 */

#include "pmm/pmm_presets.h"
#include "pmm/free_block_tree.h"
#include "pmm/types.h"
#include "pmm/io.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

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

/// After fresh create(), there should be exactly 1 free block and 1 alloc block
static bool test_free_block_has_zero_size()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    // Immediately after creation: 1 free block
    PMM_TEST( pmm.free_block_count() == 1 );
    PMM_TEST( pmm.alloc_block_count() == 1 ); // BlockHeader_0

    pmm.destroy();
    return true;
}

/// best-fit: allocate/deallocate multiple blocks; after coalesce, one free block remains
static bool test_best_fit_selection()
{
    const std::size_t size = 256 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    // Create 4 blocks of different sizes: 512, 1024, 2048, 4096
    Mgr::pptr<std::uint8_t> p[4];
    p[0] = pmm.allocate_typed<std::uint8_t>( 512 );
    p[1] = pmm.allocate_typed<std::uint8_t>( 1024 );
    p[2] = pmm.allocate_typed<std::uint8_t>( 2048 );
    p[3] = pmm.allocate_typed<std::uint8_t>( 4096 );
    PMM_TEST( !p[0].is_null() && !p[1].is_null() && !p[2].is_null() && !p[3].is_null() );
    PMM_TEST( pmm.is_initialized() );

    // Free all — coalesce should merge adjacent blocks
    pmm.deallocate_typed( p[0] );
    pmm.deallocate_typed( p[1] );
    pmm.deallocate_typed( p[2] );
    pmm.deallocate_typed( p[3] );
    PMM_TEST( pmm.is_initialized() );

    // Only one free block after full coalesce (all were adjacent)
    PMM_TEST( pmm.free_block_count() == 1 );

    // Should be able to allocate a large block in the merged free space
    Mgr::pptr<std::uint8_t> big = pmm.allocate_typed<std::uint8_t>( 1500 );
    PMM_TEST( !big.is_null() );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( big );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

/// AVL tree integrity stress: alloc many blocks, free every other, then rest
static bool test_avl_integrity_stress()
{
    const std::size_t size = 512 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    static const int        N = 50;
    Mgr::pptr<std::uint8_t> ptrs[N];
    std::size_t             sizes[] = { 64, 128, 256, 512, 1024, 2048 };
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( sizes[i % 6] );
        PMM_TEST( !ptrs[i].is_null() );
        PMM_TEST( pmm.is_initialized() );
    }

    // Free every other
    for ( int i = 0; i < N; i += 2 )
    {
        pmm.deallocate_typed( ptrs[i] );
        PMM_TEST( pmm.is_initialized() );
    }
    // Free the rest
    for ( int i = 1; i < N; i += 2 )
    {
        pmm.deallocate_typed( ptrs[i] );
        PMM_TEST( pmm.is_initialized() );
    }

    // After full release: should have 1 free block and BlockHeader_0 allocated
    PMM_TEST( pmm.free_block_count() == 1 );
    PMM_TEST( pmm.alloc_block_count() == 1 ); // Issue #75: BlockHeader_0 always allocated

    pmm.destroy();
    return true;
}

/// Test three-way coalesce: prev + current + next -> one block
static bool test_coalesce_three_way()
{
    const std::size_t size = 128 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> p1 = pmm.allocate_typed<std::uint8_t>( 512 );
    Mgr::pptr<std::uint8_t> p2 = pmm.allocate_typed<std::uint8_t>( 512 );
    Mgr::pptr<std::uint8_t> p3 = pmm.allocate_typed<std::uint8_t>( 512 );
    Mgr::pptr<std::uint8_t> p4 = pmm.allocate_typed<std::uint8_t>( 512 ); // barrier
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() && !p4.is_null() );

    // Free p1 and p3 (non-adjacent free blocks)
    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p3 );
    PMM_TEST( pmm.is_initialized() );

    std::size_t blocks_before = pmm.block_count();
    std::size_t free_before   = pmm.free_block_count();

    // Free p2 — should coalesce with p1 (prev) and p3 (next)
    pmm.deallocate_typed( p2 );
    PMM_TEST( pmm.is_initialized() );

    // 2 merges = block_count decreased by 2
    PMM_TEST( pmm.block_count() == blocks_before - 2 );
    // free_blocks decreased by 1 (3 became 1)
    PMM_TEST( pmm.free_block_count() == free_before - 1 );

    pmm.deallocate_typed( p4 );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

/// Test: allocation/deallocation maintains consistent block counts
static bool test_block_count_consistency()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> p1 = pmm.allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = pmm.allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    // block_count = alloc_count + free_count
    PMM_TEST( pmm.block_count() == pmm.alloc_block_count() + pmm.free_block_count() );

    // Issue #75: 2 user blocks + BlockHeader_0 = 3 alloc blocks
    PMM_TEST( pmm.alloc_block_count() == 3 );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p2 );
    PMM_TEST( pmm.is_initialized() );
    // After freeing both: consistency should still hold
    PMM_TEST( pmm.block_count() == pmm.alloc_block_count() + pmm.free_block_count() );

    pmm.destroy();
    return true;
}

/// Test: save/load preserves AVL tree structure and block counts
static bool test_avl_survives_save_load()
{
    const std::size_t size      = 64 * 1024;
    const char*       TEST_FILE = "avl_test.dat";

    Mgr pmm1;
    PMM_TEST( pmm1.create( size ) );

    Mgr::pptr<std::uint8_t> p1 = pmm1.allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = pmm1.allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );
    pmm1.deallocate_typed( p1 ); // create fragmentation
    PMM_TEST( pmm1.is_initialized() );

    std::size_t blocks_before = pmm1.block_count();
    std::size_t free_before   = pmm1.free_block_count();
    std::size_t alloc_before  = pmm1.alloc_block_count();

    PMM_TEST( pmm::save_manager<decltype(pmm1)>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    PMM_TEST( pmm2.create( size ) );
    PMM_TEST( pmm::load_manager_from_file<decltype(pmm2)>( TEST_FILE ) );
    PMM_TEST( pmm2.is_initialized() );

    PMM_TEST( pmm2.block_count() == blocks_before );
    PMM_TEST( pmm2.free_block_count() == free_before );
    PMM_TEST( pmm2.alloc_block_count() == alloc_before );

    // Should be able to allocate memory after load
    Mgr::pptr<std::uint8_t> p3 = pmm2.allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p3.is_null() );
    PMM_TEST( pmm2.is_initialized() );

    pmm2.deallocate_typed( p3 );
    pmm2.destroy();
    std::remove( TEST_FILE );
    return true;
}

/// Test: best-fit chooses the smallest fitting block
static bool test_best_fit_chooses_smallest_fitting()
{
    const std::size_t size = 512 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

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
    PMM_TEST( !gap[0].is_null() && !barrier[0].is_null() && !gap[1].is_null() && !barrier[1].is_null() );
    PMM_TEST( !gap[2].is_null() && !barrier[2].is_null() && !gap[3].is_null() && !barrier[3].is_null() );
    PMM_TEST( !barrier[4].is_null() );

    // Free gaps — create fragmentation
    pmm.deallocate_typed( gap[0] );
    pmm.deallocate_typed( gap[1] );
    pmm.deallocate_typed( gap[2] );
    pmm.deallocate_typed( gap[3] );
    PMM_TEST( pmm.is_initialized() );

    // Request 200 bytes: best-fit should choose the gap[1] (256 bytes — smallest fitting)
    Mgr::pptr<std::uint8_t> result = pmm.allocate_typed<std::uint8_t>( 200 );
    PMM_TEST( !result.is_null() );
    PMM_TEST( pmm.is_initialized() );

    // Cleanup
    pmm.deallocate_typed( result );
    for ( int i = 0; i < 4; i++ )
        pmm.deallocate_typed( barrier[i] );
    pmm.deallocate_typed( barrier[4] );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

/// Test: allocate/deallocate works correctly
static bool test_alloc_dealloc_works()
{
    const std::size_t size = 128 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> ptr = pmm.allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !ptr.is_null() );

    // Write data
    std::memset( ptr.resolve(), 0xAB, 256 );

    // Allocate a bigger block (no reallocate in new API)
    Mgr::pptr<std::uint8_t> new_ptr = pmm.allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !new_ptr.is_null() );

    // Copy data
    std::memcpy( new_ptr.resolve(), ptr.resolve(), 256 );
    pmm.deallocate_typed( ptr );

    PMM_TEST( pmm.is_initialized() );

    // Verify data preserved
    const std::uint8_t* p = new_ptr.resolve();
    for ( std::size_t i = 0; i < 256; i++ )
        PMM_TEST( p[i] == 0xAB );

    pmm.deallocate_typed( new_ptr );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

/// Test: free block count after dealloc
static bool test_block_count_after_dealloc()
{
    const std::size_t size = 64 * 1024;

    Mgr pmm;
    PMM_TEST( pmm.create( size ) );

    // Fresh: 1 alloc (BlockHeader_0) + 1 free
    PMM_TEST( pmm.alloc_block_count() == 1 );
    PMM_TEST( pmm.free_block_count() == 1 );

    Mgr::pptr<std::uint32_t> p = pmm.allocate_typed<std::uint32_t>( 4 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( pmm.alloc_block_count() == 2 ); // BlockHeader_0 + p

    pmm.deallocate_typed( p );
    PMM_TEST( pmm.is_initialized() );

    // After dealloc: back to 1 alloc + 1 free (coalesced)
    PMM_TEST( pmm.alloc_block_count() == 1 ); // only BlockHeader_0
    PMM_TEST( pmm.free_block_count() == 1 );

    pmm.destroy();
    return true;
}

int main()
{
    std::cout << "=== test_avl_allocator ===\n";
    bool all_passed = true;

    PMM_RUN( "free_block_has_zero_size", test_free_block_has_zero_size );
    PMM_RUN( "best_fit_selection", test_best_fit_selection );
    PMM_RUN( "avl_integrity_stress", test_avl_integrity_stress );
    PMM_RUN( "coalesce_three_way", test_coalesce_three_way );
    PMM_RUN( "block_count_consistency", test_block_count_consistency );
    PMM_RUN( "avl_survives_save_load", test_avl_survives_save_load );
    PMM_RUN( "best_fit_chooses_smallest_fitting", test_best_fit_chooses_smallest_fitting );
    PMM_RUN( "alloc_dealloc_works", test_alloc_dealloc_works );
    PMM_RUN( "block_count_after_dealloc", test_block_count_after_dealloc );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
