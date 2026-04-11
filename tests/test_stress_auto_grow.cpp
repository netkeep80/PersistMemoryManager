/**
 * @file test_stress_auto_grow.cpp
 * @brief Стресс-тест автоматического роста
 *
 * Каждый тест использует уникальный InstanceId для изоляции бэкенда.
 * Примечание: reallocate_typed() удалён из нового API.
 *   Тест 4 заменён на "large alloc triggers expand".
 */

#include "pmm_single_threaded_heap.h"
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

static auto now()
{
    return std::chrono::high_resolution_clock::now();
}

static double elapsed_ms( std::chrono::high_resolution_clock::time_point start,
                          std::chrono::high_resolution_clock::time_point end )
{
    return std::chrono::duration<double, std::milli>( end - start ).count();
}

namespace
{

struct Rng
{
    uint32_t state;

    explicit Rng( uint32_t seed = 42 ) : state( seed ) {}

    uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    uint32_t next_n( uint32_t n ) { return ( next() >> 16 ) % n; }

    std::size_t next_block_size_small() { return static_cast<std::size_t>( ( next_n( 32 ) + 1 ) * 8 ); }
};

} // namespace

// Each test uses a unique InstanceId to ensure a fresh backend (no carryover from expand).

TEST_CASE( "single expand", "[test_stress_auto_grow]" )
{
    using MgrT = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 700>;

    const std::size_t initial_size = 64UL * 1024;

    REQUIRE( MgrT::create( initial_size ) );
    const auto baseline_alloc = MgrT::alloc_block_count();

    const std::size_t                     block_size = 512;
    std::vector<MgrT::pptr<std::uint8_t>> ptrs;
    ptrs.reserve( 300 );

    const uint8_t pattern = 0xAB;
    auto          t0      = now();

    std::size_t total_before = MgrT::total_size();
    int         expand_count = 0;

    for ( int i = 0; i < 300 && expand_count < 2; ++i )
    {
        MgrT::pptr<std::uint8_t> p = MgrT::allocate_typed<std::uint8_t>( block_size );
        if ( p.is_null() )
            break;

        std::memset( p.resolve(), static_cast<int>( pattern ), block_size );
        ptrs.push_back( p );

        std::size_t cur = MgrT::total_size();
        if ( cur > total_before )
        {
            expand_count++;
            total_before = cur;
            std::cout << "    expand #" << expand_count << ": buffer " << cur / 1024 << " KB, "
                      << "live blocks: " << ptrs.size() << "\n";
        }
    }

    REQUIRE( expand_count >= 1 );
    REQUIRE( MgrT::is_initialized() );

    bool data_ok = true;
    for ( auto& p : ptrs )
    {
        const auto* bytes = p.resolve();
        for ( std::size_t i = 0; i < block_size; ++i )
        {
            if ( bytes[i] != pattern )
            {
                data_ok = false;
                std::cerr << "  DATA ERROR at offset " << i << "\n";
                break;
            }
        }
        if ( !data_ok )
            break;
    }
    REQUIRE( data_ok );

    for ( auto& p : ptrs )
        MgrT::deallocate_typed( p );
    ptrs.clear();

    REQUIRE( MgrT::is_initialized() );
    REQUIRE( MgrT::alloc_block_count() == baseline_alloc );

    double ms = elapsed_ms( t0, now() );
    std::cout << "    Time: " << ms << " ms\n";

    MgrT::destroy();
}

TEST_CASE( "multi expand", "[test_stress_auto_grow]" )
{
    using MgrT = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 701>;

    const std::size_t initial_size = 16 * 1024; // start small

    REQUIRE( MgrT::create( initial_size ) );
    const auto baseline_alloc = MgrT::alloc_block_count();

    Rng rng( 7777 );

    std::vector<MgrT::pptr<std::uint8_t>> ptrs;
    std::vector<std::size_t>              sizes;
    ptrs.reserve( 500 );
    sizes.reserve( 500 );

    std::size_t prev_total    = MgrT::total_size();
    int         expand_count  = 0;
    const int   max_expands   = 5;
    const int   max_alloc_cnt = 500;

    auto t0 = now();

    for ( int i = 0; i < max_alloc_cnt && expand_count < max_expands; ++i )
    {
        std::size_t              sz = rng.next_block_size_small();
        MgrT::pptr<std::uint8_t> p  = MgrT::allocate_typed<std::uint8_t>( sz );
        if ( p.is_null() )
        {
            std::cerr << "  ERROR: allocate returned null at i=" << i << "\n";
            MgrT::destroy();
            FAIL( "unexpected failure" );
        }

        std::memset( p.resolve(), static_cast<int>( i & 0xFF ), sz );
        ptrs.push_back( p );
        sizes.push_back( sz );

        std::size_t cur = MgrT::total_size();
        if ( cur > prev_total )
        {
            expand_count++;
            prev_total = cur;
            std::cout << "    expand #" << expand_count << ": buffer " << MgrT::total_size() / 1024 << " KB, "
                      << "live blocks: " << ptrs.size() << "\n";
        }
    }

    std::cout << "    Allocated: " << ptrs.size() << " blocks, expand() called: " << expand_count << " times\n";

    REQUIRE( expand_count >= max_expands );
    REQUIRE( MgrT::is_initialized() );

    bool data_ok = true;
    for ( int i = 0; i < static_cast<int>( ptrs.size() ); ++i )
    {
        const auto*   bytes   = ptrs[i].resolve();
        const uint8_t pattern = static_cast<uint8_t>( i & 0xFF );
        for ( std::size_t j = 0; j < sizes[i]; ++j )
        {
            if ( bytes[j] != pattern )
            {
                data_ok = false;
                std::cerr << "  DATA ERROR in block " << i << " at offset " << j << "\n";
                break;
            }
        }
        if ( !data_ok )
            break;
    }
    REQUIRE( data_ok );

    for ( auto& p : ptrs )
        MgrT::deallocate_typed( p );
    ptrs.clear();

    REQUIRE( MgrT::is_initialized() );
    REQUIRE( MgrT::alloc_block_count() == baseline_alloc );

    double ms = elapsed_ms( t0, now() );
    std::cout << "    Time: " << ms << " ms\n";

    MgrT::destroy();
}

TEST_CASE( "expand with mixed ops", "[test_stress_auto_grow]" )
{
    using MgrT = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 702>;

    const std::size_t initial_size = 32UL * 1024;

    REQUIRE( MgrT::create( initial_size ) );
    const auto baseline_alloc = MgrT::alloc_block_count();

    Rng rng( 31415 );

    std::vector<MgrT::pptr<std::uint8_t>> live;
    std::vector<std::size_t>              live_sizes;
    live.reserve( 100000 );
    live_sizes.reserve( 100000 );

    std::size_t prev_total   = MgrT::total_size();
    int         expand_count = 0;
    int         alloc_ok     = 0;
    int         dealloc_cnt  = 0;
    const int   max_expands  = 50;
    const int   max_iter     = 200000;

    auto t0 = now();

    for ( int i = 0; i < max_iter && expand_count < max_expands; ++i )
    {
        if ( rng.next_n( 10 ) < 7 || live.empty() )
        {
            std::size_t              sz = rng.next_block_size_small();
            MgrT::pptr<std::uint8_t> p  = MgrT::allocate_typed<std::uint8_t>( sz );
            if ( !p.is_null() )
            {
                std::memset( p.resolve(), static_cast<int>( alloc_ok & 0xFF ), sz );
                live.push_back( p );
                live_sizes.push_back( sz );
                alloc_ok++;
            }
        }
        else
        {
            uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
            MgrT::deallocate_typed( live[idx] );
            live[idx]       = live.back();
            live_sizes[idx] = live_sizes.back();
            live.pop_back();
            live_sizes.pop_back();
            dealloc_cnt++;
        }

        std::size_t cur = MgrT::total_size();
        if ( cur > prev_total )
        {
            expand_count++;
            prev_total = cur;
            std::cout << "    expand #" << expand_count << ": buffer " << cur / 1024 << " KB, "
                      << "live blocks: " << live.size() << "\n";
        }
    }

    std::cout << "    Allocs: " << alloc_ok << "  deallocs: " << dealloc_cnt << "\n";
    std::cout << "    Live blocks: " << live.size() << "  expand() called: " << expand_count << " times\n";

    REQUIRE( expand_count >= 1 );
    REQUIRE( MgrT::is_initialized() );

    for ( auto& p : live )
        MgrT::deallocate_typed( p );
    live.clear();

    REQUIRE( MgrT::is_initialized() );
    REQUIRE( MgrT::alloc_block_count() == baseline_alloc );

    double ms = elapsed_ms( t0, now() );
    std::cout << "    Time: " << ms << " ms\n";

    MgrT::destroy();
}

TEST_CASE( "large alloc triggers expand", "[test_stress_auto_grow]" )
{
    using MgrT = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 703>;

    const std::size_t initial_size = 16UL * 1024;

    REQUIRE( MgrT::create( initial_size ) );

    auto t0 = now();

    // Allocate a few small blocks
    const std::size_t                     block_sz = 64;
    const int                             n_blocks = 5;
    std::vector<MgrT::pptr<std::uint8_t>> ptrs;
    ptrs.reserve( n_blocks );

    for ( int i = 0; i < n_blocks; ++i )
    {
        MgrT::pptr<std::uint8_t> p = MgrT::allocate_typed<std::uint8_t>( block_sz );
        REQUIRE( !p.is_null() );
        std::memset( p.resolve(), i + 1, block_sz );
        ptrs.push_back( p );
    }

    std::cout << "    Allocated " << n_blocks << " blocks before large alloc\n";
    std::size_t size_before = MgrT::total_size();

    // Allocate a large block that won't fit in the initial buffer
    const std::size_t        big_sz = initial_size * 2;
    MgrT::pptr<std::uint8_t> big    = MgrT::allocate_typed<std::uint8_t>( big_sz );
    REQUIRE( !big.is_null() );

    std::size_t size_after = MgrT::total_size();
    bool        did_expand = size_after > size_before;
    std::cout << "    large alloc expand: " << ( did_expand ? "yes" : "no" ) << "\n";
    std::cout << "    Buffer: " << size_before / 1024 << " KB -> " << size_after / 1024 << " KB\n";

    REQUIRE( did_expand );
    REQUIRE( MgrT::is_initialized() );

    MgrT::deallocate_typed( big );
    for ( int i = 0; i < n_blocks; ++i )
        MgrT::deallocate_typed( ptrs[i] );

    REQUIRE( MgrT::is_initialized() );

    double ms = elapsed_ms( t0, now() );
    std::cout << "    Time: " << ms << " ms\n";

    MgrT::destroy();
}

TEST_CASE( "grow factor >= 25%", "[test_stress_auto_grow]" )
{
    using MgrT = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 704>;

    const std::size_t initial_size = 8UL * 1024;

    REQUIRE( MgrT::create( initial_size ) );
    const auto baseline_alloc = MgrT::alloc_block_count();

    auto t0 = now();

    std::size_t last_size     = MgrT::total_size();
    int         expand_count  = 0;
    bool        grow_ok       = true;
    const int   max_expands   = 5;
    const int   max_alloc_cnt = 1000;

    std::vector<MgrT::pptr<std::uint8_t>> ptrs;

    for ( int i = 0; i < max_alloc_cnt && expand_count < max_expands; ++i )
    {
        MgrT::pptr<std::uint8_t> p = MgrT::allocate_typed<std::uint8_t>( 64 );
        if ( p.is_null() )
        {
            std::cerr << "  ERROR: allocate returned null at i=" << i << "\n";
            MgrT::destroy();
            FAIL( "unexpected failure" );
        }
        ptrs.push_back( p );

        std::size_t cur = MgrT::total_size();
        if ( cur > last_size )
        {
            expand_count++;
            std::size_t min_expected =
                last_size * pmm::config::kDefaultGrowNumerator / pmm::config::kDefaultGrowDenominator;
            bool grew_enough = ( cur >= min_expected );
            std::cout << "    expand #" << expand_count << ": " << last_size / 1024 << " KB -> " << cur / 1024
                      << " KB (min=" << min_expected / 1024 << " KB, " << ( grew_enough ? "OK" : "ERROR" ) << ")\n";
            if ( !grew_enough )
                grow_ok = false;
            last_size = cur;
        }
    }

    REQUIRE( grow_ok );
    REQUIRE( expand_count >= max_expands );
    REQUIRE( MgrT::is_initialized() );

    for ( auto& p : ptrs )
        MgrT::deallocate_typed( p );

    REQUIRE( MgrT::is_initialized() );
    REQUIRE( MgrT::alloc_block_count() == baseline_alloc );

    double ms = elapsed_ms( t0, now() );
    std::cout << "    Time: " << ms << " ms\n";

    MgrT::destroy();
}
