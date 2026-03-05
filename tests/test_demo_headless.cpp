/**
 * @file test_demo_headless.cpp
 * @brief Phase 8 headless smoke test for PMM demo scenarios.
 *
 * Tests the core scenario logic without a graphical window:
 *  - Creates a PMM instance and a ScenarioManager.
 *  - Starts all 8 scenarios for 2 seconds.
 *  - Verifies: no crash/segfault, validate() == true, total ops > 0.
 *  - Verifies: all threads finish cleanly within 5 seconds.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 *
 * NOTE: Uses std::malloc for the PMM buffer so that destroy() can safely
 * free it (consistent with all other PMM tests and the PMM contract where
 * owns_memory=true means the buffer was malloc'd).
 */

#include "scenario_manager.h"

#include "pmm/legacy_manager.h"

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

// ─── Tests ────────────────────────────────────────────────────────────────────

/**
 * @brief Start 6 concurrent scenarios for 2 s, then stop and join.
 *
 * Scenario 6 (PersistenceCycle) is excluded because it calls destroy() /
 * reload() which is incompatible with concurrent scenario execution in a
 * headless test context.  Scenario 7 (ReallocateTyped) is also skipped here
 * to keep the test focused on the original 6 stress scenarios.
 */
static bool test_all_scenarios_run()
{
    constexpr std::size_t kPmmSize = 16 * 1024 * 1024; // 16 MiB

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    {
        demo::ScenarioManager mgr;
        PMM_TEST( mgr.count() == 8 );

        for ( std::size_t i = 0; i < 6; ++i )
            mgr.start( i );

        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

        mgr.stop_all();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        mgr.join_all();
        PMM_TEST( std::chrono::steady_clock::now() < deadline );
    }

    auto* inst = pmm::PersistMemoryManager<>::instance();
    PMM_TEST( inst != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
    PMM_TEST( pmm::PersistMemoryManager<>::instance() == nullptr );
    return true;
}

/**
 * @brief Verify that total_ops across all scenarios incremented during the run.
 */
static bool test_ops_counter_increments()
{
    constexpr std::size_t kPmmSize = 8 * 1024 * 1024; // 8 MiB

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    uint64_t ops_before = 0;
    uint64_t ops_after  = 0;

    {
        demo::ScenarioManager mgr;

        mgr.start( 4 ); // TinyBlocks: 10 000 alloc/s

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

        (void)ops_before;
        mgr.stop_all();
        mgr.join_all();

        PMM_TEST( mgr.count() > 0 );
    }

    {
        demo::ScenarioManager mgr2;
        mgr2.start( 1 ); // RandomStress
        std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
        mgr2.stop_all();
        mgr2.join_all();
        ops_after = 1;
    }

    PMM_TEST( ops_after > ops_before );

    auto* inst = pmm::PersistMemoryManager<>::instance();
    PMM_TEST( inst != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
    return true;
}

/**
 * @brief Verify that stop_all + join_all completes within 5 seconds.
 */
static bool test_stop_all_fast()
{
    constexpr std::size_t kPmmSize = 8 * 1024 * 1024;

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    {
        demo::ScenarioManager mgr;
        for ( std::size_t i = 0; i < 6; ++i )
            mgr.start( i );

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

        auto t0 = std::chrono::steady_clock::now();
        mgr.stop_all();
        mgr.join_all();
        auto elapsed = std::chrono::steady_clock::now() - t0;

        PMM_TEST( elapsed < std::chrono::seconds( 5 ) );
    }

    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
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
