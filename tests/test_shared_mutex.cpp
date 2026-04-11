/**
 * @file test_shared_mutex.cpp
 * @brief Shared lock tests for PersistMemoryManager.
 *
 * Uses MultiThreadedHeap preset (SharedMutexLock + HeapStorage).
 * reallocate_typed() and get_stats() removed from new API.
 * block_count/free_block_count/alloc_block_count used instead of get_stats().
 */

#include "pmm_multi_threaded_heap.h"
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>

#include <thread>
#include <vector>

using Mgr = pmm::presets::MultiThreadedHeap;

/**
 * @brief Concurrent calls to is_initialized() from multiple threads.
 */
TEST_CASE( "test_concurrent_is_initialized", "[test_shared_mutex]" )
{
    constexpr std::size_t kMemSize = 4 * 1024 * 1024;
    constexpr int         kThreads = 8;
    constexpr int         kIter    = 100;

    Mgr pmm;
    INFO( "concurrent_is_initialized: create" );
    REQUIRE( pmm.create( kMemSize ) );

    // Allocate some blocks for a non-trivial state
    std::vector<Mgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 20; ++i )
    {
        auto p = pmm.allocate_typed<std::uint8_t>( 64 );
        if ( !p.is_null() )
            ptrs.push_back( p );
    }

    std::atomic<int>         failures{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [&failures, &pmm, kIter]()
            {
                for ( int i = 0; i < kIter; ++i )
                {
                    if ( !pmm.is_initialized() )
                        failures.fetch_add( 1 );
                }
            } );
    }

    for ( auto& th : threads )
        th.join();

    INFO( "concurrent_is_initialized: all is_initialized() returned true" );

    REQUIRE( failures.load() == 0 );

    for ( auto& p : ptrs )
        pmm.deallocate_typed( p );
    pmm.destroy();
}

/**
 * @brief Concurrent readers (is_initialized) and writers (allocate/deallocate).
 */
TEST_CASE( "test_readers_writers", "[test_shared_mutex]" )
{
    constexpr std::size_t kMemSize      = 32 * 1024 * 1024;
    constexpr int         kReadThreads  = 4;
    constexpr int         kWriteThreads = 2;
    constexpr int         kIter         = 200;

    Mgr pmm;
    INFO( "readers_writers: create" );
    REQUIRE( pmm.create( kMemSize ) );

    std::atomic<bool>        stop{ false };
    std::atomic<int>         invalid_reads{ 0 };
    std::vector<std::thread> threads;

    // Readers: continuously check is_initialized()
    for ( int t = 0; t < kReadThreads; ++t )
    {
        threads.emplace_back(
            [&invalid_reads, &stop, &pmm]()
            {
                while ( !stop.load() )
                {
                    if ( !pmm.is_initialized() )
                        invalid_reads.fetch_add( 1 );
                }
            } );
    }

    // Writers: allocate and free blocks
    for ( int t = 0; t < kWriteThreads; ++t )
    {
        threads.emplace_back(
            [kIter, &pmm]()
            {
                std::vector<Mgr::pptr<std::uint8_t>> ptrs;
                ptrs.reserve( 32 );
                for ( int i = 0; i < kIter; ++i )
                {
                    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 128 );
                    if ( !p.is_null() )
                        ptrs.push_back( p );
                    if ( ptrs.size() > 16 )
                    {
                        pmm.deallocate_typed( ptrs.front() );
                        ptrs.erase( ptrs.begin() );
                    }
                }
                for ( auto& p : ptrs )
                    pmm.deallocate_typed( p );
            } );
    }

    // Wait for writers first
    for ( int t = kReadThreads; t < kReadThreads + kWriteThreads; ++t )
        threads[t].join();
    stop.store( true );

    for ( int t = 0; t < kReadThreads; ++t )
        threads[t].join();

    INFO( "readers_writers: is_initialized() after mixed ops" );

    REQUIRE( pmm.is_initialized() );
    INFO( "readers_writers: readers saw no invalid state" );
    REQUIRE( invalid_reads.load() == 0 );

    pmm.destroy();
}

/**
 * @brief Manual grow (alloc-copy-free) correctness under concurrent access.
 */
TEST_CASE( "test_concurrent_manual_grow_correctness", "[test_shared_mutex]" )
{
    constexpr std::size_t kMemSize  = 16 * 1024 * 1024;
    constexpr int         kThreads  = 4;
    constexpr int         kIter     = 50;
    constexpr std::size_t kInitSize = 64;
    constexpr unsigned    kPattern  = 0xAB;

    Mgr pmm;
    INFO( "concurrent_manual_grow: create" );
    REQUIRE( pmm.create( kMemSize ) );

    std::vector<Mgr::pptr<std::uint8_t>> ptrs( kThreads );
    for ( int t = 0; t < kThreads; ++t )
    {
        ptrs[t] = pmm.allocate_typed<std::uint8_t>( kInitSize );
        INFO( "concurrent_manual_grow: initial alloc" );
        REQUIRE( !ptrs[t].is_null() );
        std::memset( ptrs[t].resolve(), kPattern, kInitSize );
    }

    std::atomic<int>         corrupted{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &ptrs, &pmm, &corrupted, kIter, kInitSize, kPattern]()
            {
                std::size_t cur_size = kInitSize;
                for ( int i = 0; i < kIter; ++i )
                {
                    std::size_t             new_sz = cur_size + 64;
                    Mgr::pptr<std::uint8_t> new_p  = pmm.allocate_typed<std::uint8_t>( new_sz );
                    if ( !new_p.is_null() )
                    {
                        std::memcpy( new_p.resolve(), ptrs[t].resolve(), cur_size );
                        // Verify first byte is still kPattern
                        if ( new_p.resolve()[0] != kPattern )
                            corrupted.fetch_add( 1 );
                        pmm.deallocate_typed( ptrs[t] );
                        ptrs[t]  = new_p;
                        cur_size = new_sz;
                    }
                }
            } );
    }

    for ( auto& th : threads )
        th.join();

    for ( int t = 0; t < kThreads; ++t )
    {
        if ( !ptrs[t].is_null() )
            pmm.deallocate_typed( ptrs[t] );
    }

    INFO( "concurrent_manual_grow: is_initialized() after parallel grows" );

    REQUIRE( pmm.is_initialized() );
    INFO( "concurrent_manual_grow: data not corrupted during grow" );
    REQUIRE( corrupted.load() == 0 );

    pmm.destroy();
}

/**
 * @brief Concurrent calls to block_count/free_block_count/alloc_block_count.
 */
TEST_CASE( "test_concurrent_block_counts", "[test_shared_mutex]" )
{
    constexpr std::size_t kMemSize = 8 * 1024 * 1024;
    constexpr int         kThreads = 6;
    constexpr int         kIter    = 100;

    Mgr pmm;
    INFO( "concurrent_block_counts: create" );
    REQUIRE( pmm.create( kMemSize ) );

    std::vector<Mgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 30; ++i )
    {
        auto p = pmm.allocate_typed<std::uint8_t>( 256 );
        if ( !p.is_null() )
            ptrs.push_back( p );
    }

    std::atomic<int>         inconsistent{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [&inconsistent, &pmm, kIter]()
            {
                for ( int i = 0; i < kIter; ++i )
                {
                    std::uint32_t total = pmm.block_count();
                    std::uint32_t free  = pmm.free_block_count();
                    std::uint32_t alloc = pmm.alloc_block_count();
                    // total should equal free + alloc
                    if ( total != free + alloc )
                        inconsistent.fetch_add( 1 );
                }
            } );
    }

    for ( auto& th : threads )
        th.join();

    INFO( "concurrent_block_counts: counts are consistent under concurrent reads" );

    REQUIRE( inconsistent.load() == 0 );

    for ( auto& p : ptrs )
        pmm.deallocate_typed( p );
    pmm.destroy();
}
