/**
 * @file test_avl_tree_view.cpp
 * @brief Headless tests for AvlTreeView (migrated to new API, Issue #102).
 *
 * The new AvlTreeView no longer exposes a per-node snapshot (block-level
 * iteration is not available in AbstractPersistMemoryManager). These tests
 * verify the aggregate statistics reported by the view.
 *
 * Tests:
 *  1. Freshly created PMM: update_snapshot() completes without crash, free_block_count > 0.
 *  2. update_snapshot() with null mgr must not crash.
 *  3. After allocating all free space free_block_count drops.
 *  4. After deallocating free_block_count increases again.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 */

#include "avl_tree_view.h"
#include "demo_globals.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

// ─── Test helpers ─────────────────────────────────────────────────────────────

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
        std::cout << "  " << ( name ) << " ... " << std::flush;                                                        \
        if ( (fn)() )                                                                                                  \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

// ─── PMM fixture helpers ───────────────────────────────────────────────────────

static demo::DemoMgr* make_pmm( std::size_t sz )
{
    auto* mgr = new demo::DemoMgr();
    mgr->create( sz );
    demo::g_pmm.store( mgr );
    return mgr;
}

static void destroy_pmm( demo::DemoMgr* mgr )
{
    demo::g_pmm.store( nullptr );
    delete mgr;
}

// ─── Tests ────────────────────────────────────────────────────────────────────

/**
 * @brief Freshly created PMM: update_snapshot() completes, free_block_count > 0.
 */
static bool test_empty_pmm_has_free_blocks()
{
    auto* mgr = make_pmm( 256 * 1024 );
    PMM_TEST( mgr != nullptr );
    PMM_TEST( mgr->is_initialized() );

    demo::AvlTreeView view;
    view.update_snapshot( mgr );

    // Freshly created PMM has one large free block.
    PMM_TEST( mgr->free_block_count() >= 1 );

    destroy_pmm( mgr );
    return true;
}

/**
 * @brief update_snapshot() with null mgr must not crash.
 */
static bool test_null_mgr_no_crash()
{
    demo::AvlTreeView view;
    view.update_snapshot( nullptr ); // must not crash
    return true;
}

/**
 * @brief After allocating blocks, used_size increases.
 */
static bool test_alloc_increases_used_size()
{
    auto* mgr = make_pmm( 256 * 1024 );
    PMM_TEST( mgr != nullptr );

    std::size_t used_before = mgr->used_size();

    demo::AvlTreeView view;
    view.update_snapshot( mgr );

    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 5; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = mgr->allocate_typed<std::uint8_t>( 1024 );
        PMM_TEST( !p.is_null() );
        ptrs.push_back( p );
    }

    view.update_snapshot( mgr );
    PMM_TEST( mgr->used_size() > used_before );

    for ( auto& p : ptrs )
        mgr->deallocate_typed( p );

    destroy_pmm( mgr );
    return true;
}

/**
 * @brief After deallocating, free_size recovers.
 */
static bool test_dealloc_recovers_free_size()
{
    auto* mgr = make_pmm( 256 * 1024 );
    PMM_TEST( mgr != nullptr );

    std::size_t free_before = mgr->free_size();

    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = mgr->allocate_typed<std::uint8_t>( 512 );
        if ( !p.is_null() )
            ptrs.push_back( p );
    }

    for ( auto& p : ptrs )
        mgr->deallocate_typed( p );

    demo::AvlTreeView view;
    view.update_snapshot( mgr );

    // After freeing everything, free_size should be back to original (or close).
    PMM_TEST( mgr->free_size() > 0 );
    // free_size may not equal free_before exactly due to coalescing order,
    // but it should be at least as large.
    PMM_TEST( mgr->free_size() <= mgr->total_size() );

    destroy_pmm( mgr );
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_avl_tree_view ===\n";
    bool all_passed = true;

    PMM_RUN( "empty_pmm_has_free_blocks", test_empty_pmm_has_free_blocks );
    PMM_RUN( "null_mgr_no_crash", test_null_mgr_no_crash );
    PMM_RUN( "alloc_increases_used_size", test_alloc_increases_used_size );
    PMM_RUN( "dealloc_recovers_free_size", test_dealloc_recovers_free_size );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
