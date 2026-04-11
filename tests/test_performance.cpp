/**
 * @file test_performance.cpp
 * @brief Тесты производительности и корректности (Issue #102 — новый API)
 *
 * Issue #102: использует AbstractPersistMemoryManager через pmm_presets.h.
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>

#include <vector>

using Mgr = pmm::presets::SingleThreadedHeap;

static auto now()
{
    return std::chrono::high_resolution_clock::now();
}

static double elapsed_ms( std::chrono::high_resolution_clock::time_point start,
                          std::chrono::high_resolution_clock::time_point end )
{
    return std::chrono::duration<double, std::milli>( end - start ).count();
}

TEST_CASE( "alloc 100K ≤ 100ms", "[test_performance]" )
{
    const std::size_t MEMORY_SIZE = 32UL * 1024 * 1024;
    const int         N           = 100'000;
    const std::size_t BLOCK_SIZE  = 64;

    Mgr pmm;
    REQUIRE( pmm.create( MEMORY_SIZE ) );

    std::vector<Mgr::pptr<std::uint8_t>> ptrs( N );

    auto t0        = now();
    int  allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( BLOCK_SIZE );
        if ( ptrs[i].is_null() )
            break;
        allocated++;
    }
    auto   t1       = now();
    double ms_alloc = elapsed_ms( t0, t1 );

    for ( int i = 0; i < allocated; i++ )
        pmm.deallocate_typed( ptrs[i] );

    REQUIRE( pmm.is_initialized() );
    pmm.destroy();

    REQUIRE( allocated == N );
#ifdef NDEBUG
    REQUIRE( ms_alloc <= 100.0 );
#else
    (void)ms_alloc; // Skip timing assertion in Debug/coverage builds
#endif
}

TEST_CASE( "dealloc 100K ≤ 100ms", "[test_performance]" )
{
    const std::size_t MEMORY_SIZE = 32UL * 1024 * 1024;
    const int         N           = 100'000;
    const std::size_t BLOCK_SIZE  = 64;

    Mgr pmm;
    REQUIRE( pmm.create( MEMORY_SIZE ) );

    std::vector<Mgr::pptr<std::uint8_t>> ptrs( N );

    int allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( BLOCK_SIZE );
        if ( ptrs[i].is_null() )
            break;
        allocated++;
    }

    REQUIRE( allocated == N );

    auto t0 = now();
    for ( int i = 0; i < allocated; i++ )
        pmm.deallocate_typed( ptrs[i] );
    auto   t1         = now();
    double ms_dealloc = elapsed_ms( t0, t1 );

    REQUIRE( pmm.is_initialized() );
    pmm.destroy();

#ifdef NDEBUG
    REQUIRE( ms_dealloc <= 100.0 );
#else
    (void)ms_dealloc; // Skip timing assertion in Debug/coverage builds
#endif
}

TEST_CASE( "alloc/dealloc validate", "[test_performance]" )
{
    const std::size_t MEMORY_SIZE = 1UL * 1024 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( MEMORY_SIZE ) );
    REQUIRE( pmm.is_initialized() );
    const auto baseline_alloc = pmm.alloc_block_count();

    const int                            N = 1000;
    std::vector<Mgr::pptr<std::uint8_t>> ptrs( N );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( 64 );
        REQUIRE( !ptrs[i].is_null() );
    }
    REQUIRE( pmm.is_initialized() );

    for ( int i = N - 1; i >= 0; i-- )
        pmm.deallocate_typed( ptrs[i] );

    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.free_block_count() == 1 );
    REQUIRE( pmm.alloc_block_count() == baseline_alloc );

    pmm.destroy();
}

TEST_CASE( "memory reuse", "[test_performance]" )
{
    const std::size_t MEMORY_SIZE = 512 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( MEMORY_SIZE ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    const int                            N = 100;
    std::vector<Mgr::pptr<std::uint8_t>> ptrs( N );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( 128 );
        REQUIRE( !ptrs[i].is_null() );
        std::memset( ptrs[i].resolve(), i & 0xFF, 128 );
    }

    for ( int i = 0; i < N; i += 2 )
    {
        pmm.deallocate_typed( ptrs[i] );
        ptrs[i] = Mgr::pptr<std::uint8_t>(); // null
    }

    REQUIRE( pmm.is_initialized() );

    for ( int i = 0; i < N; i += 2 )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( 64 );
        REQUIRE( !ptrs[i].is_null() );
    }

    REQUIRE( pmm.is_initialized() );

    for ( int i = 0; i < N; i++ )
    {
        if ( !ptrs[i].is_null() )
            pmm.deallocate_typed( ptrs[i] );
    }

    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.alloc_block_count() == baseline_alloc );

    pmm.destroy();
}

TEST_CASE( "free list after load", "[test_performance]" )
{
    const std::size_t MEMORY_SIZE = 512 * 1024;
    const char*       TEST_FILE   = "perf_test.dat";

    Mgr pmm1;
    REQUIRE( pmm1.create( MEMORY_SIZE ) );
    const auto baseline_alloc = pmm1.alloc_block_count();

    Mgr::pptr<std::uint8_t> p1 = pmm1.allocate_typed<std::uint8_t>( 64 );
    Mgr::pptr<std::uint8_t> p2 = pmm1.allocate_typed<std::uint8_t>( 128 );
    Mgr::pptr<std::uint8_t> p3 = pmm1.allocate_typed<std::uint8_t>( 64 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );

    pmm1.deallocate_typed( p2 );
    REQUIRE( pmm1.is_initialized() );

    std::uint32_t off1 = p1.offset();
    std::uint32_t off3 = p3.offset();

    REQUIRE( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    REQUIRE( pmm2.create( MEMORY_SIZE ) );
    REQUIRE( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE ) );
    REQUIRE( pmm2.is_initialized() );

    Mgr::pptr<std::uint8_t> p4 = pmm2.allocate_typed<std::uint8_t>( 64 );
    REQUIRE( !p4.is_null() );
    REQUIRE( pmm2.is_initialized() );

    Mgr::pptr<std::uint8_t> q1( off1 );
    Mgr::pptr<std::uint8_t> q3( off3 );

    pmm2.deallocate_typed( q1 );
    pmm2.deallocate_typed( q3 );
    pmm2.deallocate_typed( p4 );

    REQUIRE( pmm2.is_initialized() );
    REQUIRE( pmm2.alloc_block_count() == baseline_alloc );

    pmm2.destroy();
    std::remove( TEST_FILE );
}

TEST_CASE( "data integrity with free list", "[test_performance]" )
{
    const std::size_t MEMORY_SIZE = 2UL * 1024 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( MEMORY_SIZE ) );

    const int                            N = 200;
    std::vector<Mgr::pptr<std::uint8_t>> ptrs( N );
    const std::size_t                    BLOCK = 256;

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( BLOCK );
        REQUIRE( !ptrs[i].is_null() );
        std::memset( ptrs[i].resolve(), i & 0xFF, BLOCK );
    }

    for ( int i = 0; i < N; i += 3 )
    {
        pmm.deallocate_typed( ptrs[i] );
        ptrs[i] = Mgr::pptr<std::uint8_t>(); // null
    }

    REQUIRE( pmm.is_initialized() );

    for ( int i = 0; i < N; i++ )
    {
        if ( ptrs[i].is_null() )
            continue;
        const std::uint8_t* p = ptrs[i].resolve();
        for ( std::size_t j = 0; j < BLOCK; j++ )
            REQUIRE( p[j] == static_cast<std::uint8_t>( i & 0xFF ) );
    }

    for ( int i = 0; i < N; i++ )
    {
        if ( !ptrs[i].is_null() )
            pmm.deallocate_typed( ptrs[i] );
    }

    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

TEST_CASE( "full coalesce after alloc/dealloc", "[test_performance]" )
{
    const std::size_t MEMORY_SIZE = 1UL * 1024 * 1024;

    Mgr pmm;
    REQUIRE( pmm.create( MEMORY_SIZE ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    const int                            N = 500;
    std::vector<Mgr::pptr<std::uint8_t>> ptrs( N );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( 256 );
        REQUIRE( !ptrs[i].is_null() );
    }

    for ( int i = 0; i < N; i += 2 )
        pmm.deallocate_typed( ptrs[i] );
    for ( int i = 1; i < N; i += 2 )
        pmm.deallocate_typed( ptrs[i] );

    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.alloc_block_count() == baseline_alloc );
    REQUIRE( pmm.free_block_count() == 1 );

    REQUIRE( pmm.free_size() > 0 );
    REQUIRE( pmm.free_size() + pmm.used_size() == pmm.total_size() );

    pmm.destroy();
}

TEST_CASE( "minimum buffer size", "[test_performance]" )
{
    const std::size_t MEMORY_SIZE = 16 * 1024; // min viable for HeapStorage

    Mgr pmm;
    REQUIRE( pmm.create( MEMORY_SIZE ) );
    REQUIRE( pmm.is_initialized() );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 8 );
    if ( !p.is_null() )
    {
        REQUIRE( pmm.is_initialized() );
        pmm.deallocate_typed( p );
        REQUIRE( pmm.is_initialized() );
    }

    pmm.destroy();
}
