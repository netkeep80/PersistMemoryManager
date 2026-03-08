/**
 * @file test_allocate.cpp
 * @brief Тесты выделения памяти (Issue #102, #110 — статический API)
 *
 * Issue #110: использует PersistMemoryManager через pmm_presets.h.
 *   - pmm::presets::SingleThreadedHeap — однопоточный менеджер на базе HeapStorage.
 *   - Все операции через статический интерфейс менеджера.
 *   - Выделение через allocate_typed<T>(), освобождение через deallocate_typed().
 *   - Автоматическое расширение памяти при нехватке.
 *   - Каждый тест, требующий изоляции бэкенда, использует уникальный InstanceId.
 */

#include "pmm_single_threaded_heap.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

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
        std::cout << "  " << name << " ... ";                                                                          \
        if ( fn() )                                                                                                    \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

using Mgr = pmm::presets::SingleThreadedHeap;

static bool test_create_basic()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );
    PMM_TEST( Mgr::is_initialized() );

    Mgr::destroy();
    PMM_TEST( !Mgr::is_initialized() );
    return true;
}

static bool test_create_too_small()
{
    PMM_TEST( !Mgr::create( 128 ) );
    return true;
}

static bool test_allocate_single_small()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p = Mgr::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( p.resolve() != nullptr );
    PMM_TEST( reinterpret_cast<std::uintptr_t>( p.resolve() ) % 16 == 0 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
    return true;
}

static bool test_allocate_multiple()
{
    PMM_TEST( Mgr::create( 256 * 1024 ) );

    const int               num = 10;
    Mgr::pptr<std::uint8_t> ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( 1024 );
        PMM_TEST( !ptrs[i].is_null() );
    }

    for ( int i = 0; i < num; i++ )
    {
        for ( int j = i + 1; j < num; j++ )
        {
            PMM_TEST( ptrs[i] != ptrs[j] );
        }
    }

    for ( int i = 0; i < num; i++ )
        Mgr::deallocate_typed( ptrs[i] );

    Mgr::destroy();
    return true;
}

static bool test_allocate_zero()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p = Mgr::allocate_typed<std::uint8_t>( 0 );
    PMM_TEST( p.is_null() );

    Mgr::destroy();
    return true;
}

/**
 * @brief Автоматическое расширение памяти при нехватке.
 *
 * Uses unique InstanceId (500) to start with a fresh backend of exactly 8K.
 */
static bool test_allocate_auto_expand()
{
    using MgrExpand = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 500>;

    PMM_TEST( MgrExpand::create( 8 * 1024 ) );

    std::size_t initial_total = MgrExpand::total_size();

    // Fill most of the memory
    MgrExpand::pptr<std::uint8_t> block1 = MgrExpand::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !block1.is_null() );

    // Request a block that requires expansion
    MgrExpand::pptr<std::uint8_t> block2 = MgrExpand::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !block2.is_null() );

    PMM_TEST( MgrExpand::is_initialized() );
    PMM_TEST( MgrExpand::total_size() > initial_total );

    MgrExpand::destroy();
    return true;
}

static bool test_allocate_write_read()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 128 );
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p1.is_null() );
    PMM_TEST( !p2.is_null() );

    std::memset( p1.resolve(), 0xAA, 128 );
    std::memset( p2.resolve(), 0xBB, 256 );

    const std::uint8_t* r1 = p1.resolve();
    const std::uint8_t* r2 = p2.resolve();
    for ( std::size_t i = 0; i < 128; i++ )
        PMM_TEST( r1[i] == 0xAA );
    for ( std::size_t i = 0; i < 256; i++ )
        PMM_TEST( r2[i] == 0xBB );

    Mgr::deallocate_typed( p1 );
    Mgr::deallocate_typed( p2 );
    Mgr::destroy();
    return true;
}

/**
 * Uses unique InstanceId (501) so backend starts fresh at exactly 64K.
 */
static bool test_allocate_metrics()
{
    using MgrMetrics = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 501>;

    PMM_TEST( MgrMetrics::create( 64 * 1024 ) );

    PMM_TEST( MgrMetrics::total_size() == 64 * 1024 );
    PMM_TEST( MgrMetrics::used_size() > 0 );
    PMM_TEST( MgrMetrics::free_size() < 64 * 1024 );
    PMM_TEST( MgrMetrics::used_size() + MgrMetrics::free_size() <= 64 * 1024 );

    std::size_t used_before = MgrMetrics::used_size();

    MgrMetrics::pptr<std::uint8_t> ptr = MgrMetrics::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !ptr.is_null() );
    PMM_TEST( MgrMetrics::used_size() > used_before );

    MgrMetrics::deallocate_typed( ptr );
    MgrMetrics::destroy();
    return true;
}

/**
 * @brief Fragmented free blocks must be reused before the tail expansion space
 *        (Issue #53 fix verification).
 *
 * Uses unique InstanceId (502) to start with a fresh 8K backend.
 *
 * Strategy:
 *   1. Create a PMM and allocate N blocks, staying within the initial buffer.
 *   2. Free every other block to create N/2 non-adjacent fragmented holes.
 *   3. Re-allocate N/2 blocks of the same size.
 *   4. Verify total_size did NOT grow — all allocations fit in the freed holes.
 */
static bool test_fragmented_gaps_reused_before_expand_space()
{
    using MgrFrag = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 502>;

    const std::size_t block_size   = 256;
    const std::size_t initial_size = 8 * 1024;

    PMM_TEST( MgrFrag::create( initial_size ) );

    // Allocate blocks until most of the space is used, but stop before auto-grow.
    MgrFrag::pptr<std::uint8_t> ptrs[20];
    int                         n = 0;
    for ( ; n < 20; ++n )
    {
        ptrs[n] = MgrFrag::allocate_typed<std::uint8_t>( block_size );
        if ( ptrs[n].is_null() )
            break;
        // Stop before we accidentally trigger auto-grow
        if ( MgrFrag::total_size() != initial_size )
            break;
    }
    PMM_TEST( n >= 4 );

    // Free every other block — creates n/2 non-adjacent holes
    int holes = 0;
    for ( int i = 0; i < n; i += 2 )
    {
        MgrFrag::deallocate_typed( ptrs[i] );
        ++holes;
    }
    PMM_TEST( holes >= 2 );

    // Record state before re-allocation
    std::size_t size_before = MgrFrag::total_size();

    // Re-allocate the freed blocks; they must fit in the fragmented holes.
    for ( int i = 0; i < holes; ++i )
    {
        MgrFrag::pptr<std::uint8_t> p = MgrFrag::allocate_typed<std::uint8_t>( block_size );
        PMM_TEST( !p.is_null() );
        ptrs[i] = p; // track for cleanup
    }

    // No expansion must have occurred — all allocations fit inside the freed holes.
    PMM_TEST( MgrFrag::total_size() == size_before );

    MgrFrag::destroy();
    return true;
}

int main()
{
    std::cout << "=== test_allocate ===\n";
    bool all_passed = true;

    PMM_RUN( "create_basic", test_create_basic );
    PMM_RUN( "create_too_small", test_create_too_small );
    PMM_RUN( "allocate_single_small", test_allocate_single_small );
    PMM_RUN( "allocate_multiple", test_allocate_multiple );
    PMM_RUN( "allocate_zero", test_allocate_zero );
    PMM_RUN( "allocate_auto_expand", test_allocate_auto_expand );
    PMM_RUN( "allocate_write_read", test_allocate_write_read );
    PMM_RUN( "allocate_metrics", test_allocate_metrics );
    PMM_RUN( "fragmented_gaps_reused_before_expand_space", test_fragmented_gaps_reused_before_expand_space );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
