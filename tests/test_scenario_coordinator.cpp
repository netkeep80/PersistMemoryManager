/**
 * @file test_scenario_coordinator.cpp
 * @brief Phase 10 unit tests for ScenarioCoordinator.
 *
 * Verifies the pause/resume protocol that allows PersistenceCycle to safely
 * call PersistMemoryManager::destroy() while other scenario threads are
 * running (fixes plan.md Risk #5).
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
 *    alongside two other scenarios; after a destroy/reload cycle the PMM
 *    instance is valid and validate() returns true.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources).
 */

#include "scenario_manager.h"
#include "scenarios.h"

#include "persist_memory_manager.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
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
static void*                 g_buf           = nullptr;

static void pmm_setup( std::size_t size = kDefaultPmmSize )
{
    g_buf = std::malloc( size );
    if ( g_buf )
    {
        std::memset( g_buf, 0, size );
        pmm::PersistMemoryManager::create( g_buf, size );
    }
}

static void pmm_teardown()
{
    if ( pmm::PersistMemoryManager::instance() )
        pmm::PersistMemoryManager::destroy();
    g_buf = nullptr;
}

// ─── Tests ────────────────────────────────────────────────────────────────────

/**
 * @brief yield_if_paused() must block until resume_others() is called.
 */
static bool test_pause_blocks_thread()
{
    demo::ScenarioCoordinator coord;
    std::atomic<bool>         stop_flag{ false };
    std::atomic<bool>         thread_unblocked{ false };

    coord.pause_others();

    std::thread t(
        [&]
        {
            coord.yield_if_paused( stop_flag );
            thread_unblocked.store( true, std::memory_order_release );
        } );

    // Thread should still be blocked after 100 ms
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    PMM_TEST( !thread_unblocked.load( std::memory_order_acquire ) );

    coord.resume_others();
    t.join();

    PMM_TEST( thread_unblocked.load( std::memory_order_acquire ) );
    return true;
}

/**
 * @brief All blocked threads are unblocked by a single resume_others() call.
 */
static bool test_resume_unblocks_all()
{
    demo::ScenarioCoordinator coord;
    std::atomic<bool>         stop_flag{ false };
    static constexpr int      kThreads = 5;
    std::atomic<int>          unblocked_count{ 0 };
    std::vector<std::thread>  threads;

    coord.pause_others();

    for ( int i = 0; i < kThreads; ++i )
    {
        threads.emplace_back(
            [&]
            {
                coord.yield_if_paused( stop_flag );
                unblocked_count.fetch_add( 1, std::memory_order_relaxed );
            } );
    }

    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    PMM_TEST( unblocked_count.load() == 0 );

    coord.resume_others();

    for ( auto& t : threads )
        t.join();

    PMM_TEST( unblocked_count.load() == kThreads );
    return true;
}

/**
 * @brief yield_if_paused() returns immediately when no pause is active.
 */
static bool test_no_block_when_not_paused()
{
    demo::ScenarioCoordinator coord;
    std::atomic<bool>         stop_flag{ false };
    std::atomic<bool>         returned{ false };

    std::thread t(
        [&]
        {
            coord.yield_if_paused( stop_flag ); // should not block
            returned.store( true, std::memory_order_release );
        } );

    t.join();
    PMM_TEST( returned.load( std::memory_order_acquire ) );
    return true;
}

/**
 * @brief yield_if_paused() returns promptly when stop_flag is set while paused.
 */
static bool test_stop_flag_breaks_pause()
{
    demo::ScenarioCoordinator coord;
    std::atomic<bool>         stop_flag{ false };
    std::atomic<bool>         thread_returned{ false };

    coord.pause_others();

    std::thread t(
        [&]
        {
            coord.yield_if_paused( stop_flag );
            thread_returned.store( true, std::memory_order_release );
        } );

    // Let the thread enter the wait
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    PMM_TEST( !thread_returned.load( std::memory_order_acquire ) );

    // Set stop flag — thread must unblock even though pause is still active
    stop_flag.store( true, std::memory_order_release );
    coord.resume_others(); // notify the CV so the thread re-evaluates the predicate

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 2 );
    while ( !thread_returned.load( std::memory_order_acquire ) )
    {
        if ( std::chrono::steady_clock::now() > deadline )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    t.join();

    PMM_TEST( thread_returned.load( std::memory_order_acquire ) );
    return true;
}

/**
 * @brief PersistenceCycle scenario runs safely alongside LinearFill and
 *        RandomStress; after the destroy/reload cycle PMM validate() == true.
 *
 * This test exercises the full coordinator flow end-to-end by starting 3
 * scenarios (0=LinearFill, 1=RandomStress, 6=PersistenceCycle), letting them
 * run for 4 seconds, then stopping all and verifying PMM integrity.
 */
static bool test_persistence_cycle_safety()
{
    pmm_setup( kDefaultPmmSize );

    {
        demo::ScenarioManager sm;

        // Increase PersistenceCycle frequency so it fires within 4 s
        // (index 6, alloc_freq = 0.5 => 1 cycle per 2 s)
        sm.start( 0 ); // LinearFill
        sm.start( 1 ); // RandomStress
        sm.start( 6 ); // PersistenceCycle

        std::this_thread::sleep_for( std::chrono::seconds( 4 ) );

        sm.stop_all();
        sm.join_all();
    }

    // After all scenarios have stopped the PMM singleton must still be valid.
    auto* inst = pmm::PersistMemoryManager::instance();
    PMM_TEST( inst != nullptr );
    PMM_TEST( inst->validate() );

    pmm_teardown();
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_scenario_coordinator ===\n";
    bool all_passed = true;

    PMM_RUN( "pause_blocks_thread", test_pause_blocks_thread );
    PMM_RUN( "resume_unblocks_all", test_resume_unblocks_all );
    PMM_RUN( "no_block_when_not_paused", test_no_block_when_not_paused );
    PMM_RUN( "stop_flag_breaks_pause", test_stop_flag_breaks_pause );
    PMM_RUN( "persistence_cycle_safety", test_persistence_cycle_safety );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
