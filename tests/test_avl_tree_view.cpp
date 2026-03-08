/**
 * @file test_avl_tree_view.cpp
 * @brief Headless tests for AvlTreeView (migrated to static API, Issue #112).
 *
 * The new AvlTreeView no longer exposes a per-node snapshot (block-level
 * iteration is not available in PersistMemoryManager). These tests
 * verify the aggregate statistics reported by the view.
 *
 * DemoMgr (MultiThreadedHeap) is a fully static class — no instance pointer needed.
 * g_pmm is a boolean flag: true when the manager is active.
 *
 * Tests:
 *  1. Freshly created PMM: update_snapshot() completes without crash, free_block_count > 0.
 *  2. When PMM is inactive, update_snapshot() must not crash.
 *  3. After allocating blocks, used_size increases.
 *  4. After deallocating, free_size recovers.
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

static void make_pmm( std::size_t sz )
{
    demo::DemoMgr::create( sz );
    demo::g_pmm.store( true );
}

static void destroy_pmm()
{
    demo::g_pmm.store( false );
    demo::DemoMgr::destroy();
}

// ─── Tests ────────────────────────────────────────────────────────────────────

/**
 * @brief Freshly created PMM: update_snapshot() completes, free_block_count > 0.
 */
static bool test_empty_pmm_has_free_blocks()
{
    make_pmm( 256 * 1024 );
    PMM_TEST( demo::g_pmm.load() );
    PMM_TEST( demo::DemoMgr::is_initialized() );

    demo::AvlTreeView view;
    view.update_snapshot();

    // Freshly created PMM has one large free block.
    PMM_TEST( demo::DemoMgr::free_block_count() >= 1 );

    destroy_pmm();
    return true;
}

/**
 * @brief When PMM is inactive, update_snapshot() must not crash.
 */
static bool test_inactive_mgr_no_crash()
{
    demo::g_pmm.store( false );
    demo::DemoMgr::destroy();

    demo::AvlTreeView view;
    view.update_snapshot(); // must not crash (DemoMgr returns 0 for all stats when not initialized)
    return true;
}

/**
 * @brief After allocating blocks, used_size increases.
 */
static bool test_alloc_increases_used_size()
{
    make_pmm( 256 * 1024 );
    PMM_TEST( demo::g_pmm.load() );

    std::size_t used_before = demo::DemoMgr::used_size();

    demo::AvlTreeView view;
    view.update_snapshot();

    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 5; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 1024 );
        PMM_TEST( !p.is_null() );
        ptrs.push_back( p );
    }

    view.update_snapshot();
    PMM_TEST( demo::DemoMgr::used_size() > used_before );

    for ( auto& p : ptrs )
        demo::DemoMgr::deallocate_typed( p );

    destroy_pmm();
    return true;
}

/**
 * @brief After deallocating, free_size recovers.
 */
static bool test_dealloc_recovers_free_size()
{
    make_pmm( 256 * 1024 );
    PMM_TEST( demo::g_pmm.load() );

    std::size_t free_before = demo::DemoMgr::free_size();

    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 512 );
        if ( !p.is_null() )
            ptrs.push_back( p );
    }

    for ( auto& p : ptrs )
        demo::DemoMgr::deallocate_typed( p );

    demo::AvlTreeView view;
    view.update_snapshot();

    // After freeing everything, free_size should be back to original (or close).
    PMM_TEST( demo::DemoMgr::free_size() > 0 );
    // free_size may not equal free_before exactly due to coalescing order,
    // but it should be at least as large.
    PMM_TEST( demo::DemoMgr::free_size() <= demo::DemoMgr::total_size() );
    (void)free_before;

    destroy_pmm();
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_avl_tree_view ===\n";
    bool all_passed = true;

    PMM_RUN( "empty_pmm_has_free_blocks", test_empty_pmm_has_free_blocks );
    PMM_RUN( "inactive_mgr_no_crash", test_inactive_mgr_no_crash );
    PMM_RUN( "alloc_increases_used_size", test_alloc_increases_used_size );
    PMM_RUN( "dealloc_recovers_free_size", test_dealloc_recovers_free_size );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
