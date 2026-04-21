/**
 * @file bench_allocator.cpp
 * @brief Google Benchmark performance benchmarks for PersistMemoryManager
 *
 * Benchmarks:
 *   - Allocator: allocate, deallocate, reallocate_typed, mixed alloc/dealloc
 *   - pmap<K,V>: insert, find, erase
 *   - parray<T>: push_back, random access
 *   - pstring: assign, append
 *   - pstringview: intern (AVL lookup)
 *   - Multi-threaded allocator scaling
 *   - Comparison: malloc/free baseline
 *
 * Build:
 *   cmake -B build -DPMM_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
 *   cmake --build build --target pmm_benchmarks
 *   ./build/benchmarks/pmm_benchmarks
 */

#include <benchmark/benchmark.h>

#include "pmm/manager_configs.h"
#include "pmm/parray.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmap.h"
#include "pmm/pmm_presets.h"
#include "pmm/pstring.h"
#include "pmm/pstringview.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ─── Manager aliases with unique InstanceIds to avoid static-state conflicts ─

using MgrAlloc   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 100>;
using MgrDealloc = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 101>;
using MgrMixed   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 102>;
using MgrRealloc = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 103>;
using MgrBatch   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 104>;
using MgrPmap    = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 105>;
using MgrParray  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 107>;
using MgrPstring = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 109>;
using MgrMT      = pmm::PersistMemoryManager<pmm::PersistentDataConfig, 112>;

static constexpr std::size_t HEAP_64MB = 64UL * 1024 * 1024;
static constexpr std::size_t HEAP_32MB = 32UL * 1024 * 1024;

// ═════════════════════════════════════════════════════════════════════════════
//  Allocator benchmarks
// ═════════════════════════════════════════════════════════════════════════════

static void BM_Allocate( benchmark::State& state )
{
    const auto block_size = static_cast<std::size_t>( state.range( 0 ) );
    MgrAlloc::create( HEAP_64MB );

    for ( auto _ : state )
    {
        auto p = MgrAlloc::allocate_typed<std::uint8_t>( block_size );
        benchmark::DoNotOptimize( p );
        if ( !p.is_null() )
            MgrAlloc::deallocate_typed( p );
    }

    MgrAlloc::destroy();
}
BENCHMARK( BM_Allocate )->Arg( 16 )->Arg( 64 )->Arg( 256 )->Arg( 1024 )->Arg( 4096 );

static void BM_Deallocate( benchmark::State& state )
{
    const auto block_size = static_cast<std::size_t>( state.range( 0 ) );
    MgrDealloc::create( HEAP_64MB );

    const int                                   POOL_SIZE = 100'000;
    std::vector<MgrDealloc::pptr<std::uint8_t>> pool( POOL_SIZE );
    for ( int i = 0; i < POOL_SIZE; i++ )
        pool[i] = MgrDealloc::allocate_typed<std::uint8_t>( block_size );

    int idx = 0;
    for ( auto _ : state )
    {
        state.PauseTiming();
        if ( idx >= POOL_SIZE || pool[idx].is_null() )
        {
            for ( int i = 0; i < POOL_SIZE; i++ )
                pool[i] = MgrDealloc::allocate_typed<std::uint8_t>( block_size );
            idx = 0;
        }
        auto p    = pool[idx];
        pool[idx] = MgrDealloc::pptr<std::uint8_t>();
        idx++;
        state.ResumeTiming();

        MgrDealloc::deallocate_typed( p );
        benchmark::ClobberMemory();
    }

    for ( int i = idx; i < POOL_SIZE; i++ )
    {
        if ( !pool[i].is_null() )
            MgrDealloc::deallocate_typed( pool[i] );
    }
    MgrDealloc::destroy();
}
BENCHMARK( BM_Deallocate )->Arg( 64 )->Arg( 256 )->Arg( 1024 );

static void BM_AllocDeallocMixed( benchmark::State& state )
{
    MgrMixed::create( HEAP_64MB );

    const std::size_t sizes[] = { 32, 64, 128, 256 };
    int               i       = 0;

    for ( auto _ : state )
    {
        auto p = MgrMixed::allocate_typed<std::uint8_t>( sizes[i & 3] );
        benchmark::DoNotOptimize( p );
        if ( !p.is_null() )
            MgrMixed::deallocate_typed( p );
        i++;
    }

    MgrMixed::destroy();
}
BENCHMARK( BM_AllocDeallocMixed );

static void BM_ReallocateTyped( benchmark::State& state )
{
    MgrRealloc::create( HEAP_64MB );

    auto        p         = MgrRealloc::allocate_typed<std::uint8_t>( 64 );
    std::size_t cur_count = 64;

    for ( auto _ : state )
    {
        std::size_t new_count = ( cur_count == 64 ) ? 128 : 64;
        auto        new_p     = MgrRealloc::reallocate_typed<std::uint8_t>( p, cur_count, new_count );
        benchmark::DoNotOptimize( new_p );
        if ( !new_p.is_null() )
        {
            p         = new_p;
            cur_count = new_count;
        }
    }

    if ( !p.is_null() )
        MgrRealloc::deallocate_typed( p );
    MgrRealloc::destroy();
}
BENCHMARK( BM_ReallocateTyped );

static void BM_AllocateBatch( benchmark::State& state )
{
    const auto N = static_cast<int>( state.range( 0 ) );
    MgrBatch::create( HEAP_64MB );

    std::vector<MgrBatch::pptr<std::uint8_t>> ptrs( N );

    for ( auto _ : state )
    {
        for ( int i = 0; i < N; i++ )
            ptrs[i] = MgrBatch::allocate_typed<std::uint8_t>( 64 );

        state.PauseTiming();
        for ( int i = 0; i < N; i++ )
        {
            if ( !ptrs[i].is_null() )
                MgrBatch::deallocate_typed( ptrs[i] );
        }
        state.ResumeTiming();
    }

    state.SetItemsProcessed( state.iterations() * N );
    MgrBatch::destroy();
}
BENCHMARK( BM_AllocateBatch )->Arg( 1000 )->Arg( 10000 )->Arg( 100000 );

// ═════════════════════════════════════════════════════════════════════════════
//  malloc/free baseline for comparison
// ═════════════════════════════════════════════════════════════════════════════

static void BM_MallocFree( benchmark::State& state )
{
    const auto block_size = static_cast<std::size_t>( state.range( 0 ) );

    for ( auto _ : state )
    {
        void* p = std::malloc( block_size );
        benchmark::DoNotOptimize( p );
        std::free( p );
        benchmark::ClobberMemory();
    }
}
BENCHMARK( BM_MallocFree )->Arg( 16 )->Arg( 64 )->Arg( 256 )->Arg( 1024 )->Arg( 4096 );

static void BM_MallocBatch( benchmark::State& state )
{
    const auto N = static_cast<int>( state.range( 0 ) );

    std::vector<void*> ptrs( N );

    for ( auto _ : state )
    {
        for ( int i = 0; i < N; i++ )
            ptrs[i] = std::malloc( 64 );

        state.PauseTiming();
        for ( int i = 0; i < N; i++ )
            std::free( ptrs[i] );
        state.ResumeTiming();
    }

    state.SetItemsProcessed( state.iterations() * N );
}
BENCHMARK( BM_MallocBatch )->Arg( 1000 )->Arg( 10000 )->Arg( 100000 );

// ═════════════════════════════════════════════════════════════════════════════
//  pmap benchmarks
// ═════════════════════════════════════════════════════════════════════════════

static void BM_PmapInsert( benchmark::State& state )
{
    const auto N = static_cast<int>( state.range( 0 ) );
    MgrPmap::create( HEAP_64MB );

    using MyMap = pmm::pmap<int, int, MgrPmap>;
    MyMap map;

    for ( auto _ : state )
    {
        for ( int i = 0; i < N; i++ )
            map.insert( i, i * 10 );

        state.PauseTiming();
        map.clear();
        state.ResumeTiming();
    }

    state.SetItemsProcessed( state.iterations() * N );
    map.clear();
    MgrPmap::destroy();
}
BENCHMARK( BM_PmapInsert )->Arg( 100 )->Arg( 1000 )->Arg( 10000 );

static void BM_PmapFind( benchmark::State& state )
{
    const auto N = static_cast<int>( state.range( 0 ) );
    MgrPmap::create( HEAP_64MB );

    using MyMap = pmm::pmap<int, int, MgrPmap>;
    MyMap map;

    for ( int i = 0; i < N; i++ )
        map.insert( i, i * 10 );

    int key = 0;
    for ( auto _ : state )
    {
        auto p = map.find( key );
        benchmark::DoNotOptimize( p );
        key = ( key + 1 ) % N;
    }

    map.clear();
    MgrPmap::destroy();
}
BENCHMARK( BM_PmapFind )->Arg( 100 )->Arg( 1000 )->Arg( 10000 );

static void BM_PmapErase( benchmark::State& state )
{
    const auto N = static_cast<int>( state.range( 0 ) );
    MgrPmap::create( HEAP_64MB );

    using MyMap = pmm::pmap<int, int, MgrPmap>;
    MyMap map;

    for ( auto _ : state )
    {
        state.PauseTiming();
        for ( int i = 0; i < N; i++ )
            map.insert( i, i );
        state.ResumeTiming();

        for ( int i = 0; i < N; i++ )
            map.erase( i );
    }

    state.SetItemsProcessed( state.iterations() * N );
    map.clear();
    MgrPmap::destroy();
}
BENCHMARK( BM_PmapErase )->Arg( 100 )->Arg( 1000 );

// ═════════════════════════════════════════════════════════════════════════════
//  parray benchmarks (contiguous storage, O(1) access)
// ═════════════════════════════════════════════════════════════════════════════

static void BM_ParrayPushBack( benchmark::State& state )
{
    const auto N = static_cast<int>( state.range( 0 ) );
    MgrParray::create( HEAP_64MB );

    auto arr = MgrParray::create_typed<pmm::parray<int, MgrParray>>();

    for ( auto _ : state )
    {
        for ( int i = 0; i < N; i++ )
            arr->push_back( i );

        state.PauseTiming();
        arr->clear();
        state.ResumeTiming();
    }

    state.SetItemsProcessed( state.iterations() * N );
    arr->free_data();
    MgrParray::destroy_typed( arr );
    MgrParray::destroy();
}
BENCHMARK( BM_ParrayPushBack )->Arg( 100 )->Arg( 1000 )->Arg( 10000 );

static void BM_ParrayRandomAccess( benchmark::State& state )
{
    const auto N = static_cast<int>( state.range( 0 ) );
    MgrParray::create( HEAP_64MB );

    auto arr = MgrParray::create_typed<pmm::parray<int, MgrParray>>();
    for ( int i = 0; i < N; i++ )
        arr->push_back( i );

    int idx = 0;
    for ( auto _ : state )
    {
        int val = ( *arr )[idx];
        benchmark::DoNotOptimize( val );
        idx = ( idx + 1 ) % N;
    }

    arr->free_data();
    MgrParray::destroy_typed( arr );
    MgrParray::destroy();
}
BENCHMARK( BM_ParrayRandomAccess )->Arg( 100 )->Arg( 1000 )->Arg( 10000 );

// ═════════════════════════════════════════════════════════════════════════════
//  pstring benchmarks (mutable persistent string)
// ═════════════════════════════════════════════════════════════════════════════

static void BM_PstringAssign( benchmark::State& state )
{
    const auto len = static_cast<std::size_t>( state.range( 0 ) );
    MgrPstring::create( HEAP_32MB );

    auto ps = MgrPstring::create_typed<pmm::pstring<MgrPstring>>();

    std::string test_str( len, 'x' );

    for ( auto _ : state )
    {
        ps->assign( test_str.c_str() );
        benchmark::ClobberMemory();
    }

    ps->free_data();
    MgrPstring::destroy_typed( ps );
    MgrPstring::destroy();
}
BENCHMARK( BM_PstringAssign )->Arg( 16 )->Arg( 256 )->Arg( 4096 );

static void BM_PstringAppend( benchmark::State& state )
{
    MgrPstring::create( HEAP_32MB );

    auto ps = MgrPstring::create_typed<pmm::pstring<MgrPstring>>();

    int i = 0;
    for ( auto _ : state )
    {
        ps->append( "x" );
        benchmark::ClobberMemory();
        i++;
        if ( i % 10000 == 0 )
        {
            state.PauseTiming();
            ps->clear();
            state.ResumeTiming();
        }
    }

    ps->free_data();
    MgrPstring::destroy_typed( ps );
    MgrPstring::destroy();
}
BENCHMARK( BM_PstringAppend );

// ═════════════════════════════════════════════════════════════════════════════
//  pstringview benchmarks (interned read-only strings, AVL lookup)
// ═════════════════════════════════════════════════════════════════════════════

// pstringview uses permanently locked blocks; destroy() after intern causes issues.
// We use a single manager per benchmark function, initialized once.

using MgrSviewNew = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 110>;
using MgrSviewHit = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 111>;

static void BM_PstringviewInternExisting( benchmark::State& state )
{
    const auto N = static_cast<int>( state.range( 0 ) );

    std::vector<std::string> strings( N );
    for ( int i = 0; i < N; i++ )
        strings[i] = "sv_" + std::to_string( i );

    if ( !MgrSviewHit::is_initialized() )
        MgrSviewHit::create( HEAP_64MB );

    // Intern all N strings (idempotent — duplicates are deduplicated)
    for ( int i = 0; i < N; i++ )
        pmm::pstringview<MgrSviewHit>::intern( strings[i].c_str() );

    int idx = 0;
    for ( auto _ : state )
    {
        auto p = pmm::pstringview<MgrSviewHit>::intern( strings[idx].c_str() );
        benchmark::DoNotOptimize( p );
        idx = ( idx + 1 ) % N;
    }

    // Do NOT call destroy() — pstringview blocks are permanently locked
}
BENCHMARK( BM_PstringviewInternExisting )->Arg( 100 )->Arg( 1000 )->Arg( 10000 );

// ═════════════════════════════════════════════════════════════════════════════
//  Multi-threaded allocator benchmark
// ═════════════════════════════════════════════════════════════════════════════

static void BM_AllocateMT( benchmark::State& state )
{
    if ( state.thread_index() == 0 )
        MgrMT::create( HEAP_64MB );

    for ( auto _ : state )
    {
        auto p = MgrMT::allocate_typed<std::uint8_t>( 64 );
        benchmark::DoNotOptimize( p );
        if ( !p.is_null() )
            MgrMT::deallocate_typed( p );
    }

    if ( state.thread_index() == 0 )
        MgrMT::destroy();
}
BENCHMARK( BM_AllocateMT )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 );
