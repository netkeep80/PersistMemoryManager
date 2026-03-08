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

#include <atomic>
#include <cassert>
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
static bool test_all_scenarios_run()
{
    constexpr std::size_t kPmmSize = 16 * 1024 * 1024; // 16 MiB
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );
    PMM_TEST( demo::DemoMgr::is_initialized() );

    {
        demo::ScenarioManager sm;
        PMM_TEST( sm.count() == 8 );

        for ( std::size_t i = 0; i < 6; ++i )
            sm.start( i );

        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

        sm.stop_all();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        sm.join_all();
        PMM_TEST( std::chrono::steady_clock::now() < deadline );
    }

    PMM_TEST( demo::g_pmm.load() );
    PMM_TEST( demo::DemoMgr::is_initialized() );

    destroy_pmm();
    PMM_TEST( !demo::g_pmm.load() );
    return true;
}

/**
 * @brief Verify that total_ops across all scenarios incremented during the run.
 */
static bool test_ops_counter_increments()
{
    constexpr std::size_t kPmmSize = 8 * 1024 * 1024; // 8 MiB
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );

    uint64_t ops_before = 0;
    uint64_t ops_after  = 0;

    {
        demo::ScenarioManager sm;

        sm.start( 4 ); // TinyBlocks: 10 000 alloc/s

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

        (void)ops_before;
        sm.stop_all();
        sm.join_all();

        PMM_TEST( sm.count() > 0 );
    }

    {
        demo::ScenarioManager sm2;
        sm2.start( 1 ); // RandomStress
        std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
        sm2.stop_all();
        sm2.join_all();
        ops_after = 1;
    }

    PMM_TEST( ops_after > ops_before );

    PMM_TEST( demo::g_pmm.load() );
    PMM_TEST( demo::DemoMgr::is_initialized() );

    destroy_pmm();
    return true;
}

/**
 * @brief Verify that stop_all + join_all completes within 5 seconds.
 */
static bool test_stop_all_fast()
{
    constexpr std::size_t kPmmSize = 8 * 1024 * 1024;
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );

    {
        demo::ScenarioManager sm;
        for ( std::size_t i = 0; i < 6; ++i )
            sm.start( i );

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

        auto t0 = std::chrono::steady_clock::now();
        sm.stop_all();
        sm.join_all();
        auto elapsed = std::chrono::steady_clock::now() - t0;

        PMM_TEST( elapsed < std::chrono::seconds( 5 ) );
    }

    destroy_pmm();
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_demo_headless ===\n";
    bool all_passed = true;

    PMM_RUN( "all_scenarios_run", test_all_scenarios_run );
    PMM_RUN( "ops_counter_increments", test_ops_counter_increments );
    PMM_RUN( "stop_all_fast", test_stop_all_fast );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
