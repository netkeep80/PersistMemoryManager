/**
 * @file stress_test.cpp
 * @brief PersistMemoryManager stress test
 *
 * Tests correctness and performance under:
 * - 100 000 sequential allocations
 * - 1 000 000 alternating allocate/deallocate operations
 *
 * - All methods are static (Mgr::create(), Mgr::allocate(), etc.)
 * - p.resolve() — no argument needed (uses static manager resolve)
 */

#include "pmm_single_threaded_heap.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

// ─── Helpers ──────────────────────────────────────────────────────────────────

static auto now()
{
    return std::chrono::high_resolution_clock::now();
}

static double elapsed_ms( std::chrono::high_resolution_clock::time_point start,
                          std::chrono::high_resolution_clock::time_point end )
{
    return std::chrono::duration<double, std::milli>( end - start ).count();
}

// ─── Test 1: 100 000 sequential allocations ───────────────────────────────────

static bool test_100k_allocations()
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 20>;

    std::cout << "\n[Test 1] 100 000 sequential allocations\n";

    const std::size_t memory_size = 32UL * 1024 * 1024; // 32 MB

    if ( !Mgr::create( memory_size ) )
    {
        std::cerr << "  ERROR: failed to create manager\n";
        return false;
    }

    const int                       N    = 100'000;
    const std::size_t               BSIZ = 64;
    std::vector<Mgr::pptr<uint8_t>> ptrs( N );

    // Allocation phase
    auto t0        = now();
    int  allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<uint8_t>( BSIZ );
        if ( ptrs[i].is_null() )
        {
            std::cout << "  Limit reached at i=" << i << " (not enough memory)\n";
            break;
        }
        std::memset( ptrs[i].resolve(), static_cast<int>( i & 0xFF ), BSIZ );
        allocated++;
    }
    auto   t1       = now();
    double ms_alloc = elapsed_ms( t0, t1 );

    std::cout << "  Allocated: " << allocated << " / " << N << "\n";
    std::cout << "  Alloc time: " << ms_alloc << " ms\n";

    // Data integrity check on first 1000 blocks
    bool data_ok = true;
    for ( int i = 0; i < allocated && i < 1000; i++ )
    {
        const std::uint8_t* p       = ptrs[i].resolve();
        const std::uint8_t  pattern = static_cast<std::uint8_t>( i & 0xFF );
        for ( std::size_t j = 0; j < BSIZ; j++ )
        {
            if ( p[j] != pattern )
            {
                data_ok = false;
                std::cerr << "  DATA ERROR in block " << i << " offset " << j << "\n";
                break;
            }
        }
        if ( !data_ok )
            break;
    }

    if ( !Mgr::is_initialized() )
    {
        std::cerr << "  ERROR: manager not initialized after allocations\n";
        Mgr::destroy();
        return false;
    }

    // Deallocation phase
    auto t2 = now();
    for ( int i = 0; i < allocated; i++ )
        Mgr::deallocate_typed( ptrs[i] );
    auto   t3         = now();
    double ms_dealloc = elapsed_ms( t2, t3 );

    std::cout << "  Dealloc time: " << ms_dealloc << " ms\n";
    std::cout << "  Free after dealloc: " << Mgr::free_size() << " bytes\n";
    std::cout << "  Used after dealloc: " << Mgr::used_size() << " bytes\n";

    Mgr::destroy();

    bool passed = data_ok && ( allocated > 0 );
    std::cout << "  Result: " << ( passed ? "PASS" : "FAIL" ) << "\n";
    return passed;
}

// ─── Test 2: 1 000 000 alternating allocations/deallocations ──────────────────

static bool test_1m_alternating()
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 21>;

    std::cout << "\n[Test 2] 1 000 000 alternating allocate/deallocate\n";

    const std::size_t memory_size = 8UL * 1024 * 1024; // 8 MB

    if ( !Mgr::create( memory_size ) )
    {
        std::cerr << "  ERROR: failed to create manager\n";
        return false;
    }

    const int                       POOL     = 64;
    const std::size_t               SIZES[8] = { 32, 64, 128, 256, 512, 64, 128, 256 };
    std::vector<Mgr::pptr<uint8_t>> pool( POOL );
    std::vector<std::size_t>        pool_sizes( POOL, 0 );

    const int TOTAL_OPS     = 1'000'000;
    int       alloc_ops     = 0;
    int       dealloc_ops   = 0;
    int       failed_allocs = 0;

    uint32_t rng      = 42;
    auto     next_rng = [&]() -> uint32_t
    {
        rng = rng * 1664525u + 1013904223u;
        return rng;
    };

    auto t0 = now();
    for ( int op = 0; op < TOTAL_OPS; op++ )
    {
        int slot = static_cast<int>( next_rng() % POOL );
        if ( pool[slot].is_null() )
        {
            std::size_t sz   = SIZES[next_rng() % 8];
            pool[slot]       = Mgr::allocate_typed<uint8_t>( sz );
            pool_sizes[slot] = sz;
            if ( !pool[slot].is_null() )
            {
                std::memset( pool[slot].resolve(), static_cast<int>( slot & 0xFF ), sz );
                alloc_ops++;
            }
            else
            {
                pool_sizes[slot] = 0;
                failed_allocs++;
            }
        }
        else
        {
            Mgr::deallocate_typed( pool[slot] );
            pool_sizes[slot] = 0;
            dealloc_ops++;
        }
    }
    auto   t1       = now();
    double ms_total = elapsed_ms( t0, t1 );

    std::cout << "  Allocations done   : " << alloc_ops << "\n";
    std::cout << "  Deallocations done : " << dealloc_ops << "\n";
    std::cout << "  Failed allocations : " << failed_allocs << "\n";
    std::cout << "  Total time         : " << ms_total << " ms\n";
    std::cout << "  Avg per op         : " << ( ms_total / TOTAL_OPS * 1000.0 ) << " us\n";

    // Data integrity check on remaining allocated blocks
    bool data_ok = true;
    for ( int i = 0; i < POOL; i++ )
    {
        if ( !pool[i].is_null() && pool_sizes[i] > 0 )
        {
            const std::uint8_t* p       = pool[i].resolve();
            const std::uint8_t  pattern = static_cast<std::uint8_t>( i & 0xFF );
            for ( std::size_t j = 0; j < ( std::min )( pool_sizes[i], std::size_t( 8 ) ); j++ )
            {
                if ( p[j] != pattern )
                {
                    data_ok = false;
                    std::cerr << "  DATA ERROR in slot " << i << "\n";
                    break;
                }
            }
        }
    }

    // Free remaining blocks
    for ( int i = 0; i < POOL; i++ )
    {
        if ( !pool[i].is_null() )
            Mgr::deallocate_typed( pool[i] );
    }

    bool valid = Mgr::is_initialized();

    Mgr::destroy();

    bool passed = data_ok && valid;
    std::cout << "  Result: " << ( passed ? "PASS" : "FAIL" ) << "\n";
    return passed;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== PersistMemoryManager — Stress Test ===\n";

    bool all_passed = true;

    if ( !test_100k_allocations() )
        all_passed = false;

    if ( !test_1m_alternating() )
        all_passed = false;

    std::cout << "\n=== Result: " << ( all_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED" ) << " ===\n";
    return all_passed ? 0 : 1;
}
