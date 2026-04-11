/**
 * @file benchmark.cpp
 * @brief PersistMemoryManager performance benchmark
 *
 * Measures allocate/deallocate performance per target benchmarks:
 *   - allocate 100K blocks ≤ 100 ms
 *   - deallocate 100K blocks ≤ 100 ms
 *
 * - All methods are static (Mgr::create(), Mgr::allocate(), etc.)
 * - p.resolve() — no argument needed (uses static manager resolve)
 * - No reallocate_typed() — uses manual alloc-copy-dealloc
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

// ─── Benchmark 1: 100K sequential allocations ─────────────────────────────────

static bool bench_100k_alloc()
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 30>;

    const std::size_t MEMORY_SIZE = 32UL * 1024 * 1024; // 32 MB
    const int         N           = 100'000;
    const std::size_t BLOCK_SIZE  = 64;

    if ( !Mgr::create( MEMORY_SIZE ) )
    {
        std::cerr << "  ERROR: failed to create manager\n";
        return false;
    }

    std::vector<Mgr::pptr<uint8_t>> ptrs( N );

    // Allocation
    auto t0        = now();
    int  allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<uint8_t>( BLOCK_SIZE );
        if ( ptrs[i].is_null() )
            break;
        allocated++;
    }
    auto   t1       = now();
    double ms_alloc = elapsed_ms( t0, t1 );

    // Deallocation
    auto t2 = now();
    for ( int i = 0; i < allocated; i++ )
        Mgr::deallocate_typed( ptrs[i] );
    auto   t3         = now();
    double ms_dealloc = elapsed_ms( t2, t3 );

    bool valid      = Mgr::is_initialized();
    bool alloc_ok   = ( ms_alloc <= 100.0 );
    bool dealloc_ok = ( ms_dealloc <= 100.0 );

    std::cout << "  Allocated          : " << allocated << " / " << N << "\n";
    std::cout << "  Alloc time         : " << ms_alloc << " ms [target ≤ 100 ms: " << ( alloc_ok ? "PASS" : "FAIL" )
              << "]\n";
    std::cout << "  Dealloc time       : " << ms_dealloc << " ms [target ≤ 100 ms: " << ( dealloc_ok ? "PASS" : "FAIL" )
              << "]\n";
    std::cout << "  Manager valid      : " << ( valid ? "OK" : "FAIL" ) << "\n";

    Mgr::destroy();
    return alloc_ok && dealloc_ok && valid && ( allocated == N );
}

// ─── Benchmark 2: 100K blocks of mixed sizes ──────────────────────────────────

static bool bench_100k_mixed_sizes()
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 31>;

    const std::size_t MEMORY_SIZE = 64UL * 1024 * 1024; // 64 MB
    const int         N           = 100'000;

    if ( !Mgr::create( MEMORY_SIZE ) )
    {
        std::cerr << "  ERROR: failed to create manager\n";
        return false;
    }

    const std::size_t SIZES[4] = { 32, 64, 128, 256 };

    std::vector<Mgr::pptr<uint8_t>> ptrs( N );

    auto t0        = now();
    int  allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<uint8_t>( SIZES[i % 4] );
        if ( ptrs[i].is_null() )
            break;
        allocated++;
    }
    auto   t1       = now();
    double ms_alloc = elapsed_ms( t0, t1 );

    auto t2 = now();
    for ( int i = 0; i < allocated; i++ )
        Mgr::deallocate_typed( ptrs[i] );
    auto   t3         = now();
    double ms_dealloc = elapsed_ms( t2, t3 );

    bool valid      = Mgr::is_initialized();
    bool alloc_ok   = ( ms_alloc <= 100.0 );
    bool dealloc_ok = ( ms_dealloc <= 100.0 );

    std::cout << "  Allocated          : " << allocated << " / " << N << "\n";
    std::cout << "  Alloc time         : " << ms_alloc << " ms [target ≤ 100 ms: " << ( alloc_ok ? "PASS" : "FAIL" )
              << "]\n";
    std::cout << "  Dealloc time       : " << ms_dealloc << " ms [target ≤ 100 ms: " << ( dealloc_ok ? "PASS" : "FAIL" )
              << "]\n";
    std::cout << "  Manager valid      : " << ( valid ? "OK" : "FAIL" ) << "\n";

    Mgr::destroy();
    return alloc_ok && dealloc_ok && valid && ( allocated == N );
}

// ─── Benchmark 3: manual realloc (alloc-copy-dealloc) ─────────────────────────

static bool bench_manual_realloc()
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32>;

    const std::size_t MEMORY_SIZE = 16UL * 1024 * 1024; // 16 MB
    const int         N           = 10'000;

    if ( !Mgr::create( MEMORY_SIZE ) )
    {
        std::cerr << "  ERROR: failed to create manager\n";
        return false;
    }

    std::vector<Mgr::pptr<uint8_t>> ptrs( N );

    // Allocate initial 64-byte blocks
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<uint8_t>( 64 );
        if ( !ptrs[i].is_null() )
            std::memset( ptrs[i].resolve(), i & 0xFF, 64 );
    }

    // Manual realloc: alloc-copy-dealloc (128 bytes)
    auto t0               = now();
    int  realloc_ok_count = 0;
    for ( int i = 0; i < N; i++ )
    {
        if ( ptrs[i].is_null() )
            continue;
        Mgr::pptr<uint8_t> new_ptr = Mgr::allocate_typed<uint8_t>( 128 );
        if ( !new_ptr.is_null() )
        {
            std::memcpy( new_ptr.resolve(), ptrs[i].resolve(), 64 );
            Mgr::deallocate_typed( ptrs[i] );
            ptrs[i] = new_ptr;
            realloc_ok_count++;
        }
    }
    auto   t1 = now();
    double ms = elapsed_ms( t0, t1 );

    bool valid = Mgr::is_initialized();

    for ( int i = 0; i < N; i++ )
    {
        if ( !ptrs[i].is_null() )
            Mgr::deallocate_typed( ptrs[i] );
    }

    std::cout << "  Manual reallocs    : " << realloc_ok_count << " / " << N << "\n";
    std::cout << "  Total realloc time : " << ms << " ms\n";
    std::cout << "  Manager valid      : " << ( valid ? "OK" : "FAIL" ) << "\n";

    Mgr::destroy();
    return valid;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== PersistMemoryManager — Benchmark (updated #110) ===\n";
    std::cout << "Targets: allocate/deallocate 100K blocks ≤ 100 ms\n\n";

    bool all_passed = true;

    std::cout << "[Benchmark 1] 100K blocks of 64 bytes (sequential)\n";
    if ( !bench_100k_alloc() )
        all_passed = false;

    std::cout << "\n[Benchmark 2] 100K blocks of mixed sizes (32-256 bytes)\n";
    if ( !bench_100k_mixed_sizes() )
        all_passed = false;

    std::cout << "\n[Benchmark 3] 10K manual realloc operations\n";
    if ( !bench_manual_realloc() )
        all_passed = false;

    std::cout << "\n=== Result: " << ( all_passed ? "ALL TARGETS MET" : "SOME TARGETS MISSED" ) << " ===\n";
    return all_passed ? 0 : 1;
}
