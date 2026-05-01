/*
## test-allocate
req: ac-001, ac-002
*/

/**
 * @file test_allocate.cpp
 * @brief Тесты выделения памяти
 *
 *   - pmm::presets::SingleThreadedHeap — однопоточный менеджер на базе HeapStorage.
 *   - Все операции через статический интерфейс менеджера.
 *   - Выделение через allocate_typed<T>(), освобождение через deallocate_typed().
 *   - Автоматическое расширение памяти при нехватке.
 *   - Каждый тест, требующий изоляции бэкенда, использует уникальный InstanceId.
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <cstring>

using Mgr = pmm::presets::SingleThreadedHeap;

/*
### test-allocate
req: feat-002, fr-004, fr-022, fr-026, ur-001
*/

TEST_CASE( "create_basic", "[test_allocate]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );
    REQUIRE( Mgr::is_initialized() );

    Mgr::destroy();
    REQUIRE( !Mgr::is_initialized() );
}

TEST_CASE( "create_too_small", "[test_allocate]" )
{
    REQUIRE( !Mgr::create( 128 ) );
}

TEST_CASE( "allocate_single_small", "[test_allocate]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p = Mgr::allocate_typed<std::uint8_t>( 64 );
    REQUIRE( !p.is_null() );
    REQUIRE( p.resolve() != nullptr );
    REQUIRE( reinterpret_cast<std::uintptr_t>( p.resolve() ) % 16 == 0 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}

TEST_CASE( "allocate_multiple", "[test_allocate]" )
{
    REQUIRE( Mgr::create( 256 * 1024 ) );

    const int               num = 10;
    Mgr::pptr<std::uint8_t> ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( 1024 );
        REQUIRE( !ptrs[i].is_null() );
    }

    for ( int i = 0; i < num; i++ )
    {
        for ( int j = i + 1; j < num; j++ )
        {
            REQUIRE( ptrs[i] != ptrs[j] );
        }
    }

    for ( int i = 0; i < num; i++ )
        Mgr::deallocate_typed( ptrs[i] );

    Mgr::destroy();
}

TEST_CASE( "allocate_zero", "[test_allocate]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p = Mgr::allocate_typed<std::uint8_t>( 0 );
    REQUIRE( p.is_null() );

    Mgr::destroy();
}

/**
 * @brief Автоматическое расширение памяти при нехватке.
 *
 * Uses unique InstanceId (500) to start with a fresh backend of exactly 8K.
 */
TEST_CASE( "allocate_auto_expand", "[test_allocate]" )
{
    using MgrExpand = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 500>;

    REQUIRE( MgrExpand::create( 8 * 1024 ) );

    std::size_t initial_total = MgrExpand::total_size();

    // Fill most of the memory
    MgrExpand::pptr<std::uint8_t> block1 = MgrExpand::allocate_typed<std::uint8_t>( 4 * 1024 );
    REQUIRE( !block1.is_null() );

    // Request a block that requires expansion
    MgrExpand::pptr<std::uint8_t> block2 = MgrExpand::allocate_typed<std::uint8_t>( 4 * 1024 );
    REQUIRE( !block2.is_null() );

    REQUIRE( MgrExpand::is_initialized() );
    REQUIRE( MgrExpand::total_size() > initial_total );

    MgrExpand::destroy();
}

TEST_CASE( "allocate_write_read", "[test_allocate]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 128 );
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );

    std::memset( p1.resolve(), 0xAA, 128 );
    std::memset( p2.resolve(), 0xBB, 256 );

    const std::uint8_t* r1 = p1.resolve();
    const std::uint8_t* r2 = p2.resolve();
    for ( std::size_t i = 0; i < 128; i++ )
        REQUIRE( r1[i] == 0xAA );
    for ( std::size_t i = 0; i < 256; i++ )
        REQUIRE( r2[i] == 0xBB );

    Mgr::deallocate_typed( p1 );
    Mgr::deallocate_typed( p2 );
    Mgr::destroy();
}

/**
 * Uses unique InstanceId (501) so backend starts fresh at exactly 64K.
 */
TEST_CASE( "allocate_metrics", "[test_allocate]" )
{
    using MgrMetrics = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 501>;

    REQUIRE( MgrMetrics::create( 64 * 1024 ) );

    REQUIRE( MgrMetrics::total_size() == 64 * 1024 );
    REQUIRE( MgrMetrics::used_size() > 0 );
    REQUIRE( MgrMetrics::free_size() < 64 * 1024 );
    REQUIRE( MgrMetrics::used_size() + MgrMetrics::free_size() <= 64 * 1024 );

    std::size_t used_before = MgrMetrics::used_size();

    MgrMetrics::pptr<std::uint8_t> ptr = MgrMetrics::allocate_typed<std::uint8_t>( 512 );
    REQUIRE( !ptr.is_null() );
    REQUIRE( MgrMetrics::used_size() > used_before );

    MgrMetrics::deallocate_typed( ptr );
    MgrMetrics::destroy();
}

/**
 * @brief Fragmented free blocks must be reused before the tail expansion space
 *.
 *
 * Uses unique InstanceId (502) to start with a fresh 8K backend.
 *
 * Strategy:
 *   1. Create a PMM and allocate N blocks, staying within the initial buffer.
 *   2. Free every other block to create N/2 non-adjacent fragmented holes.
 *   3. Re-allocate N/2 blocks of the same size.
 *   4. Verify total_size did NOT grow — all allocations fit in the freed holes.
 */
TEST_CASE( "fragmented_gaps_reused_before_expand_space", "[test_allocate]" )
{
    using MgrFrag = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 502>;

    const std::size_t block_size   = 256;
    const std::size_t initial_size = 8 * 1024;

    REQUIRE( MgrFrag::create( initial_size ) );

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
    REQUIRE( n >= 4 );

    // Free every other block — creates n/2 non-adjacent holes
    int holes = 0;
    for ( int i = 0; i < n; i += 2 )
    {
        MgrFrag::deallocate_typed( ptrs[i] );
        ++holes;
    }
    REQUIRE( holes >= 2 );

    // Record state before re-allocation
    std::size_t size_before = MgrFrag::total_size();

    // Re-allocate the freed blocks; they must fit in the fragmented holes.
    for ( int i = 0; i < holes; ++i )
    {
        MgrFrag::pptr<std::uint8_t> p = MgrFrag::allocate_typed<std::uint8_t>( block_size );
        REQUIRE( !p.is_null() );
        ptrs[i] = p; // track for cleanup
    }

    // No expansion must have occurred — all allocations fit inside the freed holes.
    REQUIRE( MgrFrag::total_size() == size_before );

    MgrFrag::destroy();
}
