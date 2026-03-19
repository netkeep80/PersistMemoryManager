/**
 * @file test_stress_realistic.cpp
 * @brief Реалистичный стресс-тест PersistMemoryManager (Issue #20, обновлено #102)
 *
 * Issue #102: использует AbstractPersistMemoryManager через pmm_presets.h.
 */

#include "pmm_single_threaded_heap.h"
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>
#include <vector>

// Reduce iteration counts in Debug/coverage builds to avoid OOM kills in CI.
#if defined( NDEBUG )
static constexpr int kPhase0Iters = 1000000;
static constexpr int kPhase1Iters = 1000000;
static constexpr int kPhase2Iters = 1000000;
#else
static constexpr int kPhase0Iters = 50000;
static constexpr int kPhase1Iters = 50000;
static constexpr int kPhase2Iters = 50000;
#endif

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

    std::size_t next_block_size() { return static_cast<std::size_t>( ( next_n( 512 ) + 1 ) * 8 ); }
};

} // namespace

TEST_CASE( "stress realistic", "[test_stress_realistic]" )
{
    const std::size_t memory_size = 64UL * 1024 * 1024; // 64 MB

    Mgr pmm;
    REQUIRE( pmm.create( memory_size ) );

    Rng rng( 12345 );

    std::vector<Mgr::pptr<std::uint8_t>> live;
    live.reserve( static_cast<std::size_t>( kPhase0Iters + kPhase1Iters ) );

    auto total_start = now();

    // Phase 0: initial allocations
    {
        std::cout << "  Phase 0: creating " << kPhase0Iters << " initial blocks...\n";
        auto t0     = now();
        int  failed = 0;
        for ( int i = 0; i < kPhase0Iters; i++ )
        {
            std::size_t             sz  = rng.next_block_size();
            Mgr::pptr<std::uint8_t> ptr = pmm.allocate_typed<std::uint8_t>( sz );
            if ( !ptr.is_null() )
                live.push_back( ptr );
            else
                failed++;
        }
        auto   t1 = now();
        double ms = elapsed_ms( t0, t1 );
        std::cout << "    Allocated: " << live.size() << " / " << kPhase0Iters << "  failed: " << failed
                  << "  time: " << ms << " ms\n";

        REQUIRE( pmm.is_initialized() );
    }

    // Phase 1: 66% alloc / 33% dealloc
    {
        std::cout << "  Phase 1: " << kPhase1Iters << " iterations (66% alloc / 33% free)...\n";
        auto   t0          = now();
        int    alloc_ok    = 0;
        int    alloc_fail  = 0;
        int    dealloc_cnt = 0;
        size_t start_live  = live.size();

        for ( int i = 0; i < kPhase1Iters; i++ )
        {
            if ( rng.next_n( 3 ) < 2 )
            {
                std::size_t             sz  = rng.next_block_size();
                Mgr::pptr<std::uint8_t> ptr = pmm.allocate_typed<std::uint8_t>( sz );
                if ( !ptr.is_null() )
                {
                    live.push_back( ptr );
                    alloc_ok++;
                }
                else
                {
                    alloc_fail++;
                }
            }
            else
            {
                if ( !live.empty() )
                {
                    uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
                    pmm.deallocate_typed( live[idx] );
                    live[idx] = live.back();
                    live.pop_back();
                    dealloc_cnt++;
                }
            }
        }

        auto   t1 = now();
        double ms = elapsed_ms( t0, t1 );
        std::cout << "    Allocs: " << alloc_ok << "  failed: " << alloc_fail << "  deallocs: " << dealloc_cnt << "\n"
                  << "    Live blocks: " << start_live << " -> " << live.size() << "  time: " << ms << " ms\n";

        REQUIRE( pmm.is_initialized() );
        REQUIRE( live.size() > start_live );
    }

    // Phase 2: 50% alloc / 50% dealloc
    {
        std::cout << "  Phase 2: " << kPhase2Iters << " iterations (50% alloc / 50% free)...\n";
        auto t0          = now();
        int  alloc_ok    = 0;
        int  alloc_fail  = 0;
        int  dealloc_cnt = 0;

        for ( int i = 0; i < kPhase2Iters; i++ )
        {
            if ( rng.next_n( 2 ) == 0 )
            {
                std::size_t             sz  = rng.next_block_size();
                Mgr::pptr<std::uint8_t> ptr = pmm.allocate_typed<std::uint8_t>( sz );
                if ( !ptr.is_null() )
                {
                    live.push_back( ptr );
                    alloc_ok++;
                }
                else
                {
                    alloc_fail++;
                }
            }
            else
            {
                if ( !live.empty() )
                {
                    uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
                    pmm.deallocate_typed( live[idx] );
                    live[idx] = live.back();
                    live.pop_back();
                    dealloc_cnt++;
                }
            }
        }

        auto   t1 = now();
        double ms = elapsed_ms( t0, t1 );
        std::cout << "    Allocs: " << alloc_ok << "  failed: " << alloc_fail << "  deallocs: " << dealloc_cnt << "\n"
                  << "    Live blocks after phase: " << live.size() << "  time: " << ms << " ms\n";

        REQUIRE( pmm.is_initialized() );
    }

    // Phase 3: 66% dealloc / 33% alloc, until all freed
    {
        std::cout << "  Phase 3: 66% free / 33% alloc, until fully freed...\n";
        auto t0          = now();
        int  alloc_ok    = 0;
        int  alloc_fail  = 0;
        int  dealloc_cnt = 0;
        int  iterations  = 0;

        while ( !live.empty() )
        {
            iterations++;

            if ( rng.next_n( 3 ) < 1 )
            {
                std::size_t             sz  = rng.next_block_size();
                Mgr::pptr<std::uint8_t> ptr = pmm.allocate_typed<std::uint8_t>( sz );
                if ( !ptr.is_null() )
                {
                    live.push_back( ptr );
                    alloc_ok++;
                }
                else
                {
                    alloc_fail++;
                }
            }
            else
            {
                if ( !live.empty() )
                {
                    uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
                    pmm.deallocate_typed( live[idx] );
                    live[idx] = live.back();
                    live.pop_back();
                    dealloc_cnt++;
                }
            }
        }

        auto   t1 = now();
        double ms = elapsed_ms( t0, t1 );
        std::cout << "    Iterations: " << iterations << "  allocs: " << alloc_ok << "  failed: " << alloc_fail
                  << "  deallocs: " << dealloc_cnt << "\n"
                  << "    Live blocks after phase: " << live.size() << "  time: " << ms << " ms\n";

        REQUIRE( live.empty() );
        REQUIRE( pmm.is_initialized() );
        REQUIRE( pmm.alloc_block_count() == 1 ); // Issue #75: BlockHeader_0 always allocated
    }

    double total_ms = elapsed_ms( total_start, now() );
    std::cout << "  Total time: " << total_ms << " ms\n";

    pmm.destroy();
}
