/**
 * @file test_scenario_manager.cpp
 * @brief Phase 8 unit tests for ScenarioManager lifecycle (migrated to new API, Issue #102).
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

#include "demo_globals.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

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

static constexpr std::size_t kDefaultPmmSize = 16 * 1024 * 1024;
static demo::DemoMgr*        g_test_mgr      = nullptr;

static void pmm_setup( std::size_t size = kDefaultPmmSize )
{
    g_test_mgr = new demo::DemoMgr();
    if ( !g_test_mgr->create( size ) )
    {
        delete g_test_mgr;
        g_test_mgr = nullptr;
        std::cerr << "DemoMgr::create() failed\n";
        std::exit( 1 );
    }
    demo::g_pmm.store( g_test_mgr );
}

static void pmm_teardown()
{
    demo::g_pmm.store( nullptr );
    delete g_test_mgr;
    g_test_mgr = nullptr;
}

// ─── Tests ────────────────────────────────────────────────────────────────────

/**
 * @brief ScenarioManager must expose exactly 8 scenarios.
 */
static bool test_scenario_count()
{
    pmm_setup();

    demo::ScenarioManager mgr;
    PMM_TEST( mgr.count() == 8 );

    pmm_teardown();
    return true;
}

/**
 * @brief Start 3 scenarios, then stop_all() + join_all() within 5 s.
 */
static bool test_stop_all_within_deadline()
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

        PMM_TEST( elapsed < std::chrono::seconds( 5 ) );
    }

    pmm_teardown();
    return true;
}

/**
 * @brief Start and stop a single scenario repeatedly (lifecycle idempotency).
 */
static bool test_start_stop_single()
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

    auto* inst = demo::g_pmm.load();
    PMM_TEST( inst != nullptr );
    PMM_TEST( inst->is_initialized() );

    pmm_teardown();
    return true;
}

/**
 * @brief Destructor must not abort even if stop_all / join_all were not called.
 */
static bool test_destructor_cleans_up()
{
    pmm_setup();

    {
        demo::ScenarioManager mgr;
        mgr.start( 0 );
        mgr.start( 2 );
        std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        // Let destructor call stop_all + join_all
    }

    auto* inst = demo::g_pmm.load();
    PMM_TEST( inst != nullptr );
    PMM_TEST( inst->is_initialized() );

    pmm_teardown();
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_scenario_manager ===\n";
    bool all_passed = true;

    PMM_RUN( "scenario_count", test_scenario_count );
    PMM_RUN( "stop_all_within_deadline", test_stop_all_within_deadline );
    PMM_RUN( "start_stop_single", test_start_stop_single );
    PMM_RUN( "destructor_cleans_up", test_destructor_cleans_up );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
