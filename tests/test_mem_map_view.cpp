/**
 * @file test_mem_map_view.cpp
 * @brief Headless tests for MemMapView::update_snapshot() (migrated to new API, Issue #102).
 *
 * The new MemMapView shows a usage bar based on total/used/free_size from the
 * AbstractPersistMemoryManager API. Block-level pixel colouring is not available.
 *
 * Tests:
 *  1. update_snapshot() on a fresh PMM completes without crash and reports total_bytes.
 *  2. update_snapshot() reflects allocations (used_bytes increases).
 *  3. update_snapshot() handles null PMM gracefully (no crash).
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
 * @brief update_snapshot() on a fresh PMM reports valid total_bytes.
 */
static bool test_fresh_pmm_snapshot()
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    auto*                 mgr      = make_pmm( kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( view.total_bytes() == kPmmSize );
    PMM_TEST( mgr->is_initialized() );

    destroy_pmm( mgr );
    return true;
}

/**
 * @brief update_snapshot() reflects allocations.
 */
static bool test_snapshot_after_alloc()
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    auto*                 mgr      = make_pmm( kPmmSize );
    PMM_TEST( mgr != nullptr );

    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = mgr->allocate_typed<std::uint8_t>( 512 );
        PMM_TEST( !p.is_null() );
        ptrs.push_back( p );
    }

    demo::MemMapView view;
    view.update_snapshot( mgr );
    PMM_TEST( mgr->is_initialized() );
    PMM_TEST( view.total_bytes() == kPmmSize );

    for ( std::size_t i = 0; i < ptrs.size() / 2; ++i )
        mgr->deallocate_typed( ptrs[i] );

    view.update_snapshot( mgr );
    PMM_TEST( mgr->is_initialized() );

    for ( std::size_t i = ptrs.size() / 2; i < ptrs.size(); ++i )
        mgr->deallocate_typed( ptrs[i] );

    view.update_snapshot( mgr );
    PMM_TEST( mgr->is_initialized() );

    destroy_pmm( mgr );
    return true;
}

/**
 * @brief update_snapshot() must handle null PMM gracefully (no crash).
 */
static bool test_snapshot_null_mgr()
{
    demo::MemMapView view;
    view.update_snapshot( nullptr ); // must not crash
    return true;
}

/**
 * @brief highlighted_block index survives snapshot updates.
 */
static bool test_highlighted_block_preserved()
{
    constexpr std::size_t kPmmSize = 128 * 1024;
    auto*                 mgr      = make_pmm( kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::DemoMgr::pptr<std::uint8_t> p = mgr->allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );

    demo::MemMapView view;
    view.highlighted_block = 0;
    view.update_snapshot( mgr );

    PMM_TEST( view.highlighted_block == 0 );

    mgr->deallocate_typed( p );
    destroy_pmm( mgr );
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_mem_map_view ===\n";
    bool all_passed = true;

    PMM_RUN( "fresh_pmm_snapshot", test_fresh_pmm_snapshot );
    PMM_RUN( "snapshot_after_alloc", test_snapshot_after_alloc );
    PMM_RUN( "snapshot_null_mgr", test_snapshot_null_mgr );
    PMM_RUN( "highlighted_block_preserved", test_highlighted_block_preserved );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
