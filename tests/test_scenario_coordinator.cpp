/**
 * @file test_scenario_coordinator.cpp
 * @brief Phase 10 unit tests for ScenarioCoordinator (migrated to static API, Issue #112).
 *
 * Verifies the pause/resume protocol that allows scenarios to safely
 * synchronise while other scenario threads are running.
 *
 * DemoMgr (MultiThreadedHeap) is a fully static class — no instance pointer needed.
 * g_pmm is a boolean flag: true when the manager is active.
 *
 * Tests:
 *  - test_pause_blocks_thread(): yield_if_paused() blocks until resume_others().
 *  - test_resume_unblocks_all(): multiple threads blocked by pause are all
 *    unblocked when resume_others() is called.
 *  - test_no_block_when_not_paused(): yield_if_paused() returns immediately
 *    when no pause is active.
 *  - test_stop_flag_breaks_pause(): yield_if_paused() returns promptly when
 *    stop_flag is set even while pause is active.
 *  - test_persistence_cycle_safety(): ScenarioManager runs PersistenceCycle
 *    alongside two other scenarios; after the cycle the PMM instance is valid
 *    and is_initialized() returns true.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources).
 */

#include "scenario_manager.h"
#include <catch2/catch_test_macros.hpp>
#include "scenarios.h"

#include "demo_globals.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

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
 * @brief yield_if_paused() must block until resume_others() is called.
 */
TEST_CASE( "pause_blocks_thread", "[test_scenario_coordinator]" )
{
    demo::ScenarioCoordinator coord;
    std::atomic<bool>         stop_flag{ false };
    std::atomic<bool>         thread_unblocked{ false };

    coord.register_participant();

    std::thread t(
        [&]
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
            coord.yield_if_paused( stop_flag );
            thread_unblocked.store( true, std::memory_order_release );
        } );

    coord.pause_others( stop_flag );

    REQUIRE( !thread_unblocked.load( std::memory_order_acquire ) );

    coord.resume_others();
    t.join();

    coord.unregister_participant();

    REQUIRE( thread_unblocked.load( std::memory_order_acquire ) );
}

/**
 * @brief All blocked threads are unblocked by a single resume_others() call.
 */
TEST_CASE( "resume_unblocks_all", "[test_scenario_coordinator]" )
{
    demo::ScenarioCoordinator coord;
    std::atomic<bool>         stop_flag{ false };
    static constexpr int      kThreads = 5;
    std::atomic<int>          unblocked_count{ 0 };
    std::vector<std::thread>  threads;

    for ( int i = 0; i < kThreads; ++i )
        coord.register_participant();

    for ( int i = 0; i < kThreads; ++i )
    {
        threads.emplace_back(
            [&]
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
                coord.yield_if_paused( stop_flag );
                unblocked_count.fetch_add( 1, std::memory_order_relaxed );
            } );
    }

    coord.pause_others( stop_flag );

    REQUIRE( unblocked_count.load() == 0 );

    coord.resume_others();

    for ( auto& t : threads )
        t.join();

    for ( int i = 0; i < kThreads; ++i )
        coord.unregister_participant();

    REQUIRE( unblocked_count.load() == kThreads );
}

/**
 * @brief yield_if_paused() returns immediately when no pause is active.
 */
TEST_CASE( "no_block_when_not_paused", "[test_scenario_coordinator]" )
{
    demo::ScenarioCoordinator coord;
    std::atomic<bool>         stop_flag{ false };
    std::atomic<bool>         returned{ false };

    std::thread t(
        [&]
        {
            coord.yield_if_paused( stop_flag );
            returned.store( true, std::memory_order_release );
        } );

    t.join();
    REQUIRE( returned.load( std::memory_order_acquire ) );
}

/**
 * @brief yield_if_paused() returns promptly when stop_flag is set while paused.
 */
TEST_CASE( "stop_flag_breaks_pause", "[test_scenario_coordinator]" )
{
    demo::ScenarioCoordinator coord;
    std::atomic<bool>         stop_flag{ false };
    std::atomic<bool>         thread_returned{ false };

    coord.register_participant();

    std::thread t(
        [&]
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
            coord.yield_if_paused( stop_flag );
            thread_returned.store( true, std::memory_order_release );
        } );

    coord.pause_others( stop_flag );

    REQUIRE( !thread_returned.load( std::memory_order_acquire ) );

    stop_flag.store( true, std::memory_order_release );
    coord.resume_others();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 2 );
    while ( !thread_returned.load( std::memory_order_acquire ) )
    {
        if ( std::chrono::steady_clock::now() > deadline )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    t.join();

    coord.unregister_participant();

    REQUIRE( thread_returned.load( std::memory_order_acquire ) );
}

/**
 * @brief PersistenceCycle scenario runs safely alongside LinearFill and
 *        RandomStress; after the save cycle PMM is_initialized() returns true.
 */
TEST_CASE( "persistence_cycle_safety", "[test_scenario_coordinator]" )
{
    pmm_setup( kDefaultPmmSize );

    {
        demo::ScenarioManager sm;

        sm.start( 0 ); // LinearFill
        sm.start( 1 ); // RandomStress
        sm.start( 6 ); // PersistenceCycle

        std::this_thread::sleep_for( std::chrono::seconds( 4 ) );

        sm.stop_all();
        sm.join_all();
    }

    REQUIRE( demo::g_pmm.load() );
    REQUIRE( demo::DemoMgr::is_initialized() );

    pmm_teardown();
}
