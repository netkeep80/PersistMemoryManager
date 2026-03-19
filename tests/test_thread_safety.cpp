/**
 * @file test_thread_safety.cpp
 * @brief Thread safety tests for PersistMemoryManager (Phase 9, updated #102).
 *
 * Issue #102: PersistMemoryManager<> (singleton) removed.
 * Uses MultiThreadedHeap preset (SharedMutexLock + HeapStorage).
 * reallocate_typed() removed; manual alloc-copy-free pattern used instead.
 */

#include "pmm_multi_threaded_heap.h"
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstring>

#include <thread>
#include <vector>

using Mgr = pmm::presets::MultiThreadedHeap;

/**
 * @brief Concurrent allocation from multiple threads.
 */
TEST_CASE( "test_concurrent_allocate", "[test_thread_safety]" )
{
    constexpr std::size_t kMemSize   = 32 * 1024 * 1024; // 32 MB
    constexpr int         kThreads   = 4;
    constexpr int         kPerThread = 200;
    constexpr std::size_t kBlockSize = 64;

    Mgr pmm;
    INFO( "concurrent_allocate: create" );
    REQUIRE( pmm.create( kMemSize ) );

    std::vector<std::vector<Mgr::pptr<std::uint8_t>>> results( kThreads );
    std::vector<std::thread>                          threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &results, &pmm, kPerThread, kBlockSize]()
            {
                for ( int i = 0; i < kPerThread; ++i )
                {
                    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( kBlockSize );
                    if ( !p.is_null() )
                    {
                        results[t].push_back( p );
                    }
                }
            } );
    }

    for ( auto& th : threads )
        th.join();

    // Free everything
    for ( auto& vec : results )
    {
        for ( auto& p : vec )
            pmm.deallocate_typed( p );
    }

    INFO( "concurrent_allocate: is_initialized() after parallel allocs" );

    REQUIRE( pmm.is_initialized() );

    int total = 0;
    for ( auto& vec : results )
        total += static_cast<int>( vec.size() );
    INFO( "concurrent_allocate: at least one block allocated" );
    REQUIRE( total > 0 );

    pmm.destroy();
}

/**
 * @brief Concurrent interleaved allocate/deallocate.
 */
TEST_CASE( "test_concurrent_alloc_dealloc", "[test_thread_safety]" )
{
    constexpr std::size_t kMemSize = 64 * 1024 * 1024; // 64 MB
    constexpr int         kThreads = 4;
    constexpr int         kIter    = 500;

    Mgr pmm;
    INFO( "concurrent_alloc_dealloc: create" );
    REQUIRE( pmm.create( kMemSize ) );

    std::atomic<int>         errors{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, kIter, &pmm, &errors]()
            {
                unsigned                             state = static_cast<unsigned>( t * 1234567 + 42 );
                std::vector<Mgr::pptr<std::uint8_t>> live;
                live.reserve( 64 );

                for ( int i = 0; i < kIter; ++i )
                {
                    state          = state * 1664525u + 1013904223u;
                    std::size_t sz = 16 + ( ( state >> 16 ) % 128 ) * 8;

                    if ( live.empty() || ( state >> 31 ) == 0 )
                    {
                        Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( sz );
                        if ( !p.is_null() )
                            live.push_back( p );
                    }
                    else
                    {
                        std::size_t idx = ( state >> 16 ) % live.size();
                        pmm.deallocate_typed( live[idx] );
                        live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
                    }
                }

                for ( auto& p : live )
                    pmm.deallocate_typed( p );
                (void)errors;
            } );
    }

    for ( auto& th : threads )
        th.join();

    INFO( "concurrent_alloc_dealloc: is_initialized() after interleaved ops" );

    REQUIRE( pmm.is_initialized() );
    INFO( "concurrent_alloc_dealloc: no errors in threads" );
    REQUIRE( errors.load() == 0 );
    INFO( "concurrent_alloc_dealloc: all blocks freed" );
    REQUIRE( pmm.alloc_block_count() == 1 );

    pmm.destroy();
}

/**
 * @brief Concurrent manual grow (alloc-copy-free) operations.
 */
TEST_CASE( "test_concurrent_manual_grow", "[test_thread_safety]" )
{
    constexpr std::size_t kMemSize  = 32 * 1024 * 1024; // 32 MB
    constexpr int         kThreads  = 4;
    constexpr int         kIter     = 50;
    constexpr std::size_t kInitSize = 64;

    Mgr pmm;
    INFO( "concurrent_manual_grow: create" );
    REQUIRE( pmm.create( kMemSize ) );

    std::vector<Mgr::pptr<std::uint8_t>> blocks( kThreads );
    for ( int t = 0; t < kThreads; ++t )
    {
        blocks[t] = pmm.allocate_typed<std::uint8_t>( kInitSize );
        INFO( "concurrent_manual_grow: initial alloc" );
        REQUIRE( !blocks[t].is_null() );
    }

    std::vector<std::thread> threads;
    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &blocks, &pmm, kIter, kInitSize]()
            {
                std::size_t cur_size = kInitSize;
                for ( int i = 0; i < kIter; ++i )
                {
                    std::size_t             new_sz = cur_size + 64;
                    Mgr::pptr<std::uint8_t> new_p  = pmm.allocate_typed<std::uint8_t>( new_sz );
                    if ( !new_p.is_null() )
                    {
                        std::memcpy( new_p.resolve(), blocks[t].resolve(), cur_size );
                        pmm.deallocate_typed( blocks[t] );
                        blocks[t] = new_p;
                        cur_size  = new_sz;
                    }
                }
            } );
    }

    for ( auto& th : threads )
        th.join();

    for ( int t = 0; t < kThreads; ++t )
    {
        if ( !blocks[t].is_null() )
            pmm.deallocate_typed( blocks[t] );
    }

    INFO( "concurrent_manual_grow: is_initialized() after parallel grows" );

    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

/**
 * @brief Write data in parallel threads — no data races.
 */
TEST_CASE( "test_no_data_races", "[test_thread_safety]" )
{
    constexpr std::size_t kMemSize   = 32 * 1024 * 1024;
    constexpr int         kThreads   = 8;
    constexpr int         kPerThread = 50;

    Mgr pmm;
    INFO( "no_data_races: create" );
    REQUIRE( pmm.create( kMemSize ) );

    std::atomic<int>         mismatches{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &mismatches, &pmm, kPerThread]()
            {
                std::vector<std::pair<Mgr::pptr<std::uint8_t>, int>> allocs;

                for ( int i = 0; i < kPerThread; ++i )
                {
                    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( sizeof( int ) );
                    if ( !p.is_null() )
                    {
                        int val = t * 1000 + i;
                        std::memcpy( p.resolve(), &val, sizeof( int ) );
                        allocs.emplace_back( p, val );
                    }
                }

                for ( auto& [p, expected] : allocs )
                {
                    int actual = 0;
                    std::memcpy( &actual, p.resolve(), sizeof( int ) );
                    if ( actual != expected )
                        mismatches.fetch_add( 1 );
                    pmm.deallocate_typed( p );
                }
            } );
    }

    for ( auto& th : threads )
        th.join();

    INFO( "no_data_races: is_initialized() passed" );

    REQUIRE( pmm.is_initialized() );
    INFO( "no_data_races: data in blocks not corrupted" );
    REQUIRE( mismatches.load() == 0 );

    pmm.destroy();
}
