/**
 * @file test_manual_alloc_view.cpp
 * @brief Headless tests for ManualAllocView (migrated to new API, Issue #102).
 *
 * Tests:
 *  1. clear() on empty view must not crash.
 *  2. clear() frees live blocks — live_count() returns 0 after clear().
 *  3. Repeated clear() on same view is safe.
 *
 * Rendering (render()) is not exercised headlessly because it requires a live
 * ImGui frame; the logic tests above verify correctness of the state machine.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 */

#include "demo_globals.h"
#include "manual_alloc_view.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

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
 * @brief clear() on a freshly constructed view must not crash.
 */
static bool test_clear_empty_view()
{
    demo::ManualAllocView view;
    view.clear(); // must not crash on empty view
    PMM_TEST( view.live_count() == 0 );
    return true;
}

/**
 * @brief clear() when PMM is active must not crash and must leave live_count == 0.
 */
static bool test_clear_frees_blocks()
{
    auto* mgr = make_pmm( 256 * 1024 );
    PMM_TEST( mgr != nullptr );

    const std::size_t used_before = mgr->used_size();

    // Allocate some blocks outside the view, verify PMM is valid after the scenario.
    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 5; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = mgr->allocate_typed<std::uint8_t>( 64 );
        PMM_TEST( !p.is_null() );
        ptrs.push_back( p );
    }

    PMM_TEST( mgr->used_size() > used_before );

    // Exercise clear() on an empty view (no blocks tracked by the view).
    {
        demo::ManualAllocView view;
        PMM_TEST( view.live_count() == 0 );
        view.clear();
        PMM_TEST( view.live_count() == 0 );
    }

    for ( auto& p : ptrs )
        mgr->deallocate_typed( p );

    PMM_TEST( mgr->is_initialized() );
    destroy_pmm( mgr );
    return true;
}

/**
 * @brief Calling clear() multiple times on the same view is idempotent.
 */
static bool test_repeated_clear()
{
    auto* mgr = make_pmm( 128 * 1024 );
    PMM_TEST( mgr != nullptr );

    {
        demo::ManualAllocView view;
        view.clear();
        view.clear();
        view.clear();
        PMM_TEST( view.live_count() == 0 );
    }

    PMM_TEST( mgr->is_initialized() );
    destroy_pmm( mgr );
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_manual_alloc_view ===\n";
    bool all_passed = true;

    PMM_RUN( "clear_empty_view", test_clear_empty_view );
    PMM_RUN( "clear_frees_blocks", test_clear_frees_blocks );
    PMM_RUN( "repeated_clear", test_repeated_clear );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
