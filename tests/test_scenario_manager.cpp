/**
 * @file test_scenario_manager.cpp
 * @brief Phase 8 unit tests for ScenarioManager lifecycle (migrated to static API, Issue #112).
 *
 * DemoMgr (MultiThreadedHeap) is a fully static class — no instance pointer needed.
 * g_pmm is a boolean flag: true when the manager is active.
 *
 * Tests:
 *  - Start 3 scenarios, call stop_all() + join_all(), verify all threads
 *    terminate within 5 seconds.
 *  - start() / stop() individual scenario lifecycle.
 *  - count() returns the expected number of scenarios (8).
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 */

#include "scenario_manager.h"
#include <catch2/catch_test_macros.hpp>

#include "demo_globals.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

// ─── PMM fixture helpers ───────────────────────────────────────────────────────

static constexpr std::size_t kDefaultPmmSize = 16 * 1024 * 1024;

static void pmm_setup( std::size_t size = kDefaultPmmSize )
{
    if ( !demo::DemoMgr::create( size ) )
    {
        std::cerr << "DemoMgr::create() failed\n";
        std::exit( 1 );
    }
    demo::g_pmm.store( true );
}

static void pmm_teardown()
{
    demo::g_pmm.store( false );
    demo::DemoMgr::destroy();
}

// ─── Tests ────────────────────────────────────────────────────────────────────

/**
 * @brief ScenarioManager must expose exactly 8 scenarios.
 */
TEST_CASE( "scenario_count", "[test_scenario_manager]" )
{
    pmm_setup();

    demo::ScenarioManager mgr;
    REQUIRE( mgr.count() == 8 );

    pmm_teardown();
}

/**
 * @brief Start 3 scenarios, then stop_all() + join_all() within 5 s.
 */
TEST_CASE( "stop_all_within_deadline", "[test_scenario_manager]" )
{
    pmm_setup();

    {
        demo::ScenarioManager mgr;

        mgr.start( 0 );
        mgr.start( 1 );
        mgr.start( 4 );

        std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );

        auto t0 = std::chrono::steady_clock::now();
        mgr.stop_all();
        mgr.join_all();
        auto elapsed = std::chrono::steady_clock::now() - t0;

        REQUIRE( elapsed < std::chrono::seconds( 5 ) );
    }

    pmm_teardown();
}

/**
 * @brief Start and stop a single scenario repeatedly (lifecycle idempotency).
 */
TEST_CASE( "start_stop_single", "[test_scenario_manager]" )
{
    pmm_setup();

    {
        demo::ScenarioManager mgr;

        for ( int round = 0; round < 3; ++round )
        {
            mgr.start( 1 );
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            mgr.stop( 1 );
        }

        mgr.stop_all();
        mgr.join_all();
    }

    REQUIRE( demo::g_pmm.load() );
    REQUIRE( demo::DemoMgr::is_initialized() );

    pmm_teardown();
}

/**
 * @brief Destructor must not abort even if stop_all / join_all were not called.
 */
TEST_CASE( "destructor_cleans_up", "[test_scenario_manager]" )
{
    pmm_setup();

    {
        demo::ScenarioManager mgr;
        mgr.start( 0 );
        mgr.start( 2 );
        std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        // Let destructor call stop_all + join_all
    }

    REQUIRE( demo::g_pmm.load() );
    REQUIRE( demo::DemoMgr::is_initialized() );

    pmm_teardown();
}
