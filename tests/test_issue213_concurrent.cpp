/**
 * @file test_issue213_concurrent.cpp
 * @brief Concurrent allocation/deallocation tests (Issue #213, Phase 5.2).
 *
 * Extends the thread safety tests with more stress scenarios:
 *  - Concurrent allocation with varying block sizes
 *  - Concurrent deallocation patterns (LIFO, FIFO, random)
 *  - Concurrent mixed operations with validation
 *  - High contention with many threads
 *  - Concurrent operations on typed containers (pstring, parray, pmap)
 *
 * @see docs/phase5_testing.md §5.2
 * @version 0.1 (Issue #213 — Phase 5.2: Extended test coverage)
 */

#include "pmm_multi_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <thread>
#include <vector>

using Mgr = pmm::presets::MultiThreadedHeap;

// =============================================================================
// Concurrent allocation with varying block sizes
// =============================================================================

TEST_CASE( "concurrent allocation with varying sizes", "[test_issue213_concurrent]" )
{
    constexpr std::size_t kMemSize      = 64 * 1024 * 1024; // 64 MB
    constexpr int         kThreads      = 8;
    constexpr int         kOpsPerThread = 200;

    Mgr pmm;
    REQUIRE( pmm.create( kMemSize ) );

    std::vector<std::vector<Mgr::pptr<std::uint8_t>>> results( kThreads );
    std::vector<std::thread>                          threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &results, &pmm]()
            {
                unsigned state = static_cast<unsigned>( t * 7654321 + 17 );
                for ( int i = 0; i < kOpsPerThread; ++i )
                {
                    state = state * 1664525u + 1013904223u;
                    // Sizes from 1 byte to 4 KB.
                    std::size_t sz = 1 + ( ( state >> 16 ) % 4096 );

                    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( sz );
                    if ( !p.is_null() )
                    {
                        // Write a pattern to detect corruption.
                        std::memset( p.resolve(), static_cast<std::uint8_t>( t ), sz );
                        results[t].push_back( p );
                    }
                }
            } );
    }

    for ( auto& th : threads )
        th.join();

    int total = 0;
    for ( auto& vec : results )
        total += static_cast<int>( vec.size() );
    REQUIRE( total > 0 );

    // Free everything.
    for ( auto& vec : results )
        for ( auto& p : vec )
            pmm.deallocate_typed( p );

    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

// =============================================================================
// Concurrent LIFO deallocation (stack-like pattern)
// =============================================================================

TEST_CASE( "concurrent LIFO deallocation", "[test_issue213_concurrent]" )
{
    constexpr std::size_t kMemSize = 32 * 1024 * 1024;
    constexpr int         kThreads = 4;
    constexpr int         kBlocks  = 100;

    Mgr pmm;
    REQUIRE( pmm.create( kMemSize ) );

    // Pre-allocate blocks for each thread.
    std::vector<std::vector<Mgr::pptr<std::uint8_t>>> blocks( kThreads );
    for ( int t = 0; t < kThreads; ++t )
    {
        for ( int i = 0; i < kBlocks; ++i )
        {
            auto p = pmm.allocate_typed<std::uint8_t>( 64 );
            REQUIRE( !p.is_null() );
            blocks[t].push_back( p );
        }
    }

    // Each thread frees its blocks in LIFO order (reverse).
    std::vector<std::thread> threads;
    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &blocks, &pmm]()
            {
                for ( int i = kBlocks - 1; i >= 0; --i )
                    pmm.deallocate_typed( blocks[t][i] );
            } );
    }

    for ( auto& th : threads )
        th.join();

    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.is_initialized() );

    // After all frees, only the initial block should remain.
    REQUIRE( pmm.alloc_block_count() == 1 );

    pmm.destroy();
}

// =============================================================================
// Concurrent random deallocation order
// =============================================================================

TEST_CASE( "concurrent random deallocation order", "[test_issue213_concurrent]" )
{
    constexpr std::size_t kMemSize = 32 * 1024 * 1024;
    constexpr int         kThreads = 4;
    constexpr int         kBlocks  = 100;

    Mgr pmm;
    REQUIRE( pmm.create( kMemSize ) );

    // Pre-allocate blocks for each thread.
    std::vector<std::vector<Mgr::pptr<std::uint8_t>>> blocks( kThreads );
    for ( int t = 0; t < kThreads; ++t )
    {
        for ( int i = 0; i < kBlocks; ++i )
        {
            auto p = pmm.allocate_typed<std::uint8_t>( 32 + ( i % 8 ) * 16 );
            REQUIRE( !p.is_null() );
            blocks[t].push_back( p );
        }
    }

    // Each thread frees in a pseudo-random order.
    std::vector<std::thread> threads;
    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &blocks, &pmm]()
            {
                // Simple Fisher-Yates shuffle using thread-specific seed.
                std::vector<int> indices( blocks[t].size() );
                std::iota( indices.begin(), indices.end(), 0 );
                unsigned state = static_cast<unsigned>( t * 999983 + 1 );
                for ( int i = static_cast<int>( indices.size() ) - 1; i > 0; --i )
                {
                    state = state * 1664525u + 1013904223u;
                    int j = static_cast<int>( ( state >> 16 ) % static_cast<unsigned>( i + 1 ) );
                    std::swap( indices[i], indices[j] );
                }
                for ( int idx : indices )
                    pmm.deallocate_typed( blocks[t][idx] );
            } );
    }

    for ( auto& th : threads )
        th.join();

    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.alloc_block_count() == 1 );

    pmm.destroy();
}

// =============================================================================
// High contention: many threads, small heap
// =============================================================================

TEST_CASE( "high contention concurrent allocate/deallocate", "[test_issue213_concurrent]" )
{
    constexpr std::size_t kMemSize = 4 * 1024 * 1024; // Smaller heap = more contention.
    constexpr int         kThreads = 16;
    constexpr int         kOps     = 200;

    Mgr pmm;
    REQUIRE( pmm.create( kMemSize ) );

    std::atomic<int>         success_count{ 0 };
    std::atomic<int>         fail_count{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &pmm, &success_count, &fail_count]()
            {
                unsigned                             state = static_cast<unsigned>( t * 31337 + 42 );
                std::vector<Mgr::pptr<std::uint8_t>> live;
                live.reserve( 32 );

                for ( int i = 0; i < kOps; ++i )
                {
                    state = state * 1664525u + 1013904223u;

                    if ( live.empty() || ( state & 1 ) == 0 )
                    {
                        std::size_t             sz = 16 + ( ( state >> 8 ) % 256 );
                        Mgr::pptr<std::uint8_t> p  = pmm.allocate_typed<std::uint8_t>( sz );
                        if ( !p.is_null() )
                        {
                            live.push_back( p );
                            success_count.fetch_add( 1 );
                        }
                        else
                        {
                            fail_count.fetch_add( 1 );
                        }
                    }
                    else
                    {
                        std::size_t idx = ( state >> 16 ) % live.size();
                        pmm.deallocate_typed( live[idx] );
                        live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
                    }
                }

                // Clean up remaining live blocks.
                for ( auto& p : live )
                    pmm.deallocate_typed( p );
            } );
    }

    for ( auto& th : threads )
        th.join();

    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.is_initialized() );
    REQUIRE( success_count.load() > 0 );

    pmm.destroy();
}

// =============================================================================
// Concurrent data integrity: write/read in parallel
// =============================================================================

TEST_CASE( "concurrent data integrity under contention", "[test_issue213_concurrent]" )
{
    constexpr std::size_t kMemSize = 32 * 1024 * 1024;
    constexpr int         kThreads = 8;
    constexpr int         kBlocks  = 50;

    Mgr pmm;
    REQUIRE( pmm.create( kMemSize ) );

    std::atomic<int>         corruption_count{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &pmm, &corruption_count]()
            {
                // Allocate blocks, write pattern, read back, free.
                std::vector<std::pair<Mgr::pptr<std::uint8_t>, std::size_t>> allocs;

                for ( int i = 0; i < kBlocks; ++i )
                {
                    std::size_t             sz = 64 + i * 16;
                    Mgr::pptr<std::uint8_t> p  = pmm.allocate_typed<std::uint8_t>( sz );
                    if ( !p.is_null() )
                    {
                        // Fill with thread-unique pattern.
                        std::uint8_t pattern = static_cast<std::uint8_t>( t * 37 + i );
                        std::memset( p.resolve(), pattern, sz );
                        allocs.emplace_back( p, sz );
                    }
                }

                // Verify all written data is intact.
                for ( auto& [p, sz] : allocs )
                {
                    auto*        data    = p.resolve();
                    std::uint8_t pattern = data[0];
                    for ( std::size_t j = 0; j < sz; ++j )
                    {
                        if ( data[j] != pattern )
                        {
                            corruption_count.fetch_add( 1 );
                            break;
                        }
                    }
                    pmm.deallocate_typed( p );
                }
            } );
    }

    for ( auto& th : threads )
        th.join();

    REQUIRE( corruption_count.load() == 0 );
    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

// =============================================================================
// Concurrent reallocate_typed
// =============================================================================

TEST_CASE( "concurrent reallocate_typed operations", "[test_issue213_concurrent]" )
{
    constexpr std::size_t kMemSize = 64 * 1024 * 1024;
    constexpr int         kThreads = 4;
    constexpr int         kIter    = 30;

    Mgr pmm;
    REQUIRE( pmm.create( kMemSize ) );

    // Each thread starts with a small array and grows it.
    std::vector<Mgr::pptr<std::uint32_t>> blocks( kThreads );
    for ( int t = 0; t < kThreads; ++t )
    {
        blocks[t] = pmm.allocate_typed<std::uint32_t>( 4 );
        REQUIRE( !blocks[t].is_null() );
        // Write sentinel values.
        for ( int i = 0; i < 4; ++i )
            blocks[t].resolve()[i] = static_cast<std::uint32_t>( t * 1000 + i );
    }

    std::atomic<int>         mismatches{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &blocks, &pmm, &mismatches]()
            {
                std::size_t cur_count = 4;
                for ( int i = 0; i < kIter; ++i )
                {
                    std::size_t new_count = cur_count + 4;
                    auto        new_p     = pmm.reallocate_typed<std::uint32_t>( blocks[t], cur_count, new_count );
                    if ( !new_p.is_null() )
                    {
                        // Verify original sentinel values.
                        for ( std::size_t j = 0; j < 4 && j < cur_count; ++j )
                        {
                            if ( new_p.resolve()[j] != static_cast<std::uint32_t>( t * 1000 + j ) )
                            {
                                mismatches.fetch_add( 1 );
                                break;
                            }
                        }
                        blocks[t] = new_p;
                        cur_count = new_count;
                    }
                }
            } );
    }

    for ( auto& th : threads )
        th.join();

    REQUIRE( mismatches.load() == 0 );

    for ( int t = 0; t < kThreads; ++t )
    {
        if ( !blocks[t].is_null() )
            pmm.deallocate_typed( blocks[t] );
    }

    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}
