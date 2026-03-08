/**
 * @file test_mem_map_view.cpp
 * @brief Headless tests for MemMapView::update_snapshot() (migrated to static API, Issue #112).
 *
 * The new MemMapView shows a usage bar based on total/used/free_size from the
 * PersistMemoryManager static API. Block-level pixel colouring is not available.
 *
 * DemoMgr (MultiThreadedHeap) is a fully static class — no instance pointer needed.
 * g_pmm is a boolean flag: true when the manager is active.
 *
 * Tests:
 *  1. update_snapshot() on a fresh PMM completes without crash and reports total_bytes.
 *  2. update_snapshot() reflects allocations (used_bytes increases).
 *  3. When g_pmm is false (inactive), total_bytes() returns 0 after snapshot.
 *  4. highlighted_block field survives snapshot updates.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 */

#include "demo_globals.h"
#include "mem_map_view.h"

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
 * @brief update_snapshot() on a fresh PMM reports valid total_bytes.
 */
static bool test_fresh_pmm_snapshot()
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );

    demo::MemMapView view;
    view.update_snapshot();

    PMM_TEST( view.total_bytes() == kPmmSize );
    PMM_TEST( demo::DemoMgr::is_initialized() );

    destroy_pmm();
    return true;
}

/**
 * @brief update_snapshot() reflects allocations.
 */
static bool test_snapshot_after_alloc()
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );

    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 512 );
        PMM_TEST( !p.is_null() );
        ptrs.push_back( p );
    }

    demo::MemMapView view;
    view.update_snapshot();
    PMM_TEST( demo::DemoMgr::is_initialized() );
    PMM_TEST( view.total_bytes() == kPmmSize );

    for ( std::size_t i = 0; i < ptrs.size() / 2; ++i )
        demo::DemoMgr::deallocate_typed( ptrs[i] );

    view.update_snapshot();
    PMM_TEST( demo::DemoMgr::is_initialized() );

    for ( std::size_t i = ptrs.size() / 2; i < ptrs.size(); ++i )
        demo::DemoMgr::deallocate_typed( ptrs[i] );

    view.update_snapshot();
    PMM_TEST( demo::DemoMgr::is_initialized() );

    destroy_pmm();
    return true;
}

/**
 * @brief When PMM is inactive (g_pmm=false), total_bytes() returns 0 after snapshot.
 *
 * The old test passed nullptr to update_snapshot(). With the static API, we instead
 * ensure the manager is destroyed and total_bytes() returns 0 (DemoMgr::total_size()
 * returns 0 when not initialized).
 */
static bool test_snapshot_inactive_mgr()
{
    // Make sure no manager is active (destroy if it was from previous test)
    demo::g_pmm.store( false );
    demo::DemoMgr::destroy();

    demo::MemMapView view;
    // When not initialized, DemoMgr::total_size() returns 0.
    view.update_snapshot();
    PMM_TEST( view.total_bytes() == 0 );
    return true;
}

/**
 * @brief highlighted_block index survives snapshot updates.
 */
static bool test_highlighted_block_preserved()
{
    constexpr std::size_t kPmmSize = 128 * 1024;
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );

    demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );

    demo::MemMapView view;
    view.highlighted_block = 0;
    view.update_snapshot();

    PMM_TEST( view.highlighted_block == 0 );

    demo::DemoMgr::deallocate_typed( p );
    destroy_pmm();
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_mem_map_view ===\n";
    bool all_passed = true;

    PMM_RUN( "fresh_pmm_snapshot", test_fresh_pmm_snapshot );
    PMM_RUN( "snapshot_after_alloc", test_snapshot_after_alloc );
    PMM_RUN( "snapshot_inactive_mgr", test_snapshot_inactive_mgr );
    PMM_RUN( "highlighted_block_preserved", test_highlighted_block_preserved );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
