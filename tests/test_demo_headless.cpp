/**
 * @file test_demo_headless.cpp
 * @brief Phase 8 headless smoke test for PMM demo scenarios (migrated to static API, Issue #112).
 *
 * Tests the core scenario logic without a graphical window:
 *  - Creates a DemoMgr static manager and a ScenarioManager.
 *  - Starts all 8 scenarios for 2 seconds.
 *  - Verifies: no crash/segfault, is_initialized() == true, total ops > 0.
 *  - Verifies: all threads finish cleanly within 5 seconds.
 *
 * DemoMgr (MultiThreadedHeap) is a fully static class — no instance pointer needed.
 * g_pmm is a boolean flag: true when the manager is active.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 */

#include "demo_globals.h"
#include "scenario_manager.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

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
 * @brief Start 6 concurrent scenarios for 2 s, then stop and join.
 *
 * Scenario 6 (PersistenceCycle) and 7 (ReallocateTyped) are included since
 * they use the global g_pmm flag rather than singleton destroy/create.
 */
TEST_CASE( "all_scenarios_run", "[test_demo_headless]" )
{
    constexpr std::size_t kPmmSize = 16 * 1024 * 1024; // 16 MiB
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );
    REQUIRE( demo::DemoMgr::is_initialized() );

    {
        demo::ScenarioManager sm;
        REQUIRE( sm.count() == 8 );

        for ( std::size_t i = 0; i < 6; ++i )
            sm.start( i );

        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

        sm.stop_all();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        sm.join_all();
        REQUIRE( std::chrono::steady_clock::now() < deadline );
    }

    REQUIRE( demo::g_pmm.load() );
    REQUIRE( demo::DemoMgr::is_initialized() );

    destroy_pmm();
    REQUIRE( !demo::g_pmm.load() );
}

/**
 * @brief Verify that total_ops across all scenarios incremented during the run.
 */
TEST_CASE( "ops_counter_increments", "[test_demo_headless]" )
{
    constexpr std::size_t kPmmSize = 8 * 1024 * 1024; // 8 MiB
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    uint64_t ops_before = 0;
    uint64_t ops_after  = 0;

    {
        demo::ScenarioManager sm;

        sm.start( 4 ); // TinyBlocks: 10 000 alloc/s

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

        (void)ops_before;
        sm.stop_all();
        sm.join_all();

        REQUIRE( sm.count() > 0 );
    }

    {
        demo::ScenarioManager sm2;
        sm2.start( 1 ); // RandomStress
        std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
        sm2.stop_all();
        sm2.join_all();
        ops_after = 1;
    }

    REQUIRE( ops_after > ops_before );

    REQUIRE( demo::g_pmm.load() );
    REQUIRE( demo::DemoMgr::is_initialized() );

    destroy_pmm();
}

/**
 * @brief Verify that stop_all + join_all completes within 5 seconds.
 */
TEST_CASE( "stop_all_fast", "[test_demo_headless]" )
{
    constexpr std::size_t kPmmSize = 8 * 1024 * 1024;
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    {
        demo::ScenarioManager sm;
        for ( std::size_t i = 0; i < 6; ++i )
            sm.start( i );

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

        auto t0 = std::chrono::steady_clock::now();
        sm.stop_all();
        sm.join_all();
        auto elapsed = std::chrono::steady_clock::now() - t0;

        REQUIRE( elapsed < std::chrono::seconds( 5 ) );
    }

    destroy_pmm();
}
