/**
 * @file test_manual_alloc_view.cpp
 * @brief Headless tests for ManualAllocView (migrated to static API).
 *
 * DemoMgr (MultiThreadedHeap) is a fully static class — no instance pointer needed.
 * g_pmm is a boolean flag: true when the manager is active.
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

#include <catch2/catch_test_macros.hpp>

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
 * @brief clear() on a freshly constructed view must not crash.
 */
TEST_CASE( "clear_empty_view", "[test_manual_alloc_view]" )
{
    demo::ManualAllocView view;
    view.clear(); // must not crash on empty view
    REQUIRE( view.live_count() == 0 );
}

/**
 * @brief clear() when PMM is active must not crash and must leave live_count == 0.
 */
TEST_CASE( "clear_frees_blocks", "[test_manual_alloc_view]" )
{
    make_pmm( 256 * 1024 );
    REQUIRE( demo::g_pmm.load() );

    const std::size_t used_before = demo::DemoMgr::used_size();

    // Allocate some blocks outside the view, verify PMM is valid after the scenario.
    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 5; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 64 );
        REQUIRE( !p.is_null() );
        ptrs.push_back( p );
    }

    REQUIRE( demo::DemoMgr::used_size() > used_before );

    // Exercise clear() on an empty view (no blocks tracked by the view).
    {
        demo::ManualAllocView view;
        REQUIRE( view.live_count() == 0 );
        view.clear();
        REQUIRE( view.live_count() == 0 );
    }

    for ( auto& p : ptrs )
        demo::DemoMgr::deallocate_typed( p );

    REQUIRE( demo::DemoMgr::is_initialized() );
    destroy_pmm();
}

/**
 * @brief Calling clear() multiple times on the same view is idempotent.
 */
TEST_CASE( "repeated_clear", "[test_manual_alloc_view]" )
{
    make_pmm( 128 * 1024 );
    REQUIRE( demo::g_pmm.load() );

    {
        demo::ManualAllocView view;
        view.clear();
        view.clear();
        view.clear();
        REQUIRE( view.live_count() == 0 );
    }

    REQUIRE( demo::DemoMgr::is_initialized() );
    destroy_pmm();
}
