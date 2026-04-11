/**
 * @file test_coalesce.cpp
 * @brief Тесты слияния соседних свободных блоков (: — новый API)
 *
 *   - Все операции через экземпляр менеджера.
 *   - Статистика через block_count(), free_block_count(), alloc_block_count().
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

using Mgr = pmm::presets::SingleThreadedHeap;

TEST_CASE( "coalesce_with_next", "[test_coalesce]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p3 = Mgr::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );

    std::size_t blocks_before = Mgr::block_count();

    Mgr::deallocate_typed( p2 );
    REQUIRE( Mgr::block_count() == blocks_before );

    std::size_t blocks_mid = Mgr::block_count();
    Mgr::deallocate_typed( p1 );
    REQUIRE( Mgr::block_count() < blocks_mid );

    Mgr::pptr<std::uint8_t> big = Mgr::allocate_typed<std::uint8_t>( 400 );
    REQUIRE( !big.is_null() );

    Mgr::deallocate_typed( big );
    Mgr::deallocate_typed( p3 );

    Mgr::destroy();
}

TEST_CASE( "coalesce_with_prev", "[test_coalesce]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p3 = Mgr::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );

    std::size_t blocks_before = Mgr::block_count();

    Mgr::deallocate_typed( p2 );
    REQUIRE( Mgr::block_count() == blocks_before );

    std::size_t blocks_mid = Mgr::block_count();
    Mgr::deallocate_typed( p3 );
    REQUIRE( Mgr::block_count() < blocks_mid );

    Mgr::pptr<std::uint8_t> big = Mgr::allocate_typed<std::uint8_t>( 400 );
    REQUIRE( !big.is_null() );

    Mgr::deallocate_typed( big );
    Mgr::deallocate_typed( p1 );

    Mgr::destroy();
}

TEST_CASE( "coalesce_both_neighbors", "[test_coalesce]" )
{
    REQUIRE( Mgr::create( 128 * 1024 ) );

    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p3 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p4 = Mgr::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() && !p4.is_null() ) );

    Mgr::deallocate_typed( p1 );
    Mgr::deallocate_typed( p3 );

    std::size_t blocks_before = Mgr::block_count();
    std::size_t free_before   = Mgr::free_block_count();

    Mgr::deallocate_typed( p2 );
    REQUIRE( Mgr::block_count() == blocks_before - 2 );
    REQUIRE( Mgr::free_block_count() == free_before - 1 );

    Mgr::pptr<std::uint8_t> big = Mgr::allocate_typed<std::uint8_t>( 600 );
    REQUIRE( !big.is_null() );

    Mgr::deallocate_typed( big );
    Mgr::deallocate_typed( p4 );

    Mgr::destroy();
}

TEST_CASE( "coalesce_no_merge_when_neighbors_used", "[test_coalesce]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 128 );
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 128 );
    Mgr::pptr<std::uint8_t> p3 = Mgr::allocate_typed<std::uint8_t>( 128 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );

    std::size_t blocks_before = Mgr::block_count();
    std::size_t free_before   = Mgr::free_block_count();

    Mgr::deallocate_typed( p2 );
    REQUIRE( Mgr::block_count() == blocks_before );
    REQUIRE( Mgr::free_block_count() == free_before + 1 );

    Mgr::deallocate_typed( p1 );
    Mgr::deallocate_typed( p3 );

    Mgr::destroy();
}

TEST_CASE( "coalesce_first_block_no_next_free", "[test_coalesce]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() ) );

    std::size_t blocks_before = Mgr::block_count();
    std::size_t free_before   = Mgr::free_block_count();

    Mgr::deallocate_typed( p1 );
    REQUIRE( Mgr::block_count() == blocks_before );
    REQUIRE( Mgr::free_block_count() == free_before + 1 );

    Mgr::deallocate_typed( p2 );

    Mgr::destroy();
}

TEST_CASE( "coalesce_zero_fragmentation_after_all_free", "[test_coalesce]" )
{
    REQUIRE( Mgr::create( 256 * 1024 ) );
    const auto baseline_alloc = Mgr::alloc_block_count();

    const int               N = 8;
    Mgr::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( 256 );
        REQUIRE( !ptrs[i].is_null() );
    }

    for ( int i = 0; i < N; i += 2 )
        Mgr::deallocate_typed( ptrs[i] );

    REQUIRE( Mgr::free_block_count() > 1 );

    for ( int i = 1; i < N; i += 2 )
        Mgr::deallocate_typed( ptrs[i] );

    REQUIRE( Mgr::free_block_count() == 1 );
    REQUIRE( Mgr::block_count() == baseline_alloc + 1 );
    REQUIRE( Mgr::alloc_block_count() == baseline_alloc );

    Mgr::destroy();
}

TEST_CASE( "coalesce_lifo_results_in_one_block", "[test_coalesce]" )
{
    REQUIRE( Mgr::create( 128 * 1024 ) );
    const auto baseline_alloc = Mgr::alloc_block_count();

    const int               N = 5;
    Mgr::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( 512 );
        REQUIRE( !ptrs[i].is_null() );
    }

    for ( int i = N - 1; i >= 0; i-- )
        Mgr::deallocate_typed( ptrs[i] );

    REQUIRE( Mgr::block_count() == baseline_alloc + 1 );
    REQUIRE( Mgr::free_block_count() == 1 );

    Mgr::destroy();
}

TEST_CASE( "coalesce_fifo_results_in_one_block", "[test_coalesce]" )
{
    REQUIRE( Mgr::create( 128 * 1024 ) );
    const auto baseline_alloc = Mgr::alloc_block_count();

    const int               N = 5;
    Mgr::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( 512 );
        REQUIRE( !ptrs[i].is_null() );
    }

    for ( int i = 0; i < N; i++ )
        Mgr::deallocate_typed( ptrs[i] );

    REQUIRE( Mgr::block_count() == baseline_alloc + 1 );
    REQUIRE( Mgr::free_block_count() == 1 );

    Mgr::destroy();
}

TEST_CASE( "coalesce_large_allocation_after_merge", "[test_coalesce]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p3 = Mgr::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );

    Mgr::pptr<std::uint8_t> probe = Mgr::allocate_typed<std::uint8_t>( 700 );
    if ( !probe.is_null() )
        Mgr::deallocate_typed( probe );

    Mgr::deallocate_typed( p1 );
    Mgr::deallocate_typed( p2 );
    Mgr::deallocate_typed( p3 );

    Mgr::pptr<std::uint8_t> big = Mgr::allocate_typed<std::uint8_t>( 512 );
    REQUIRE( !big.is_null() );

    std::memset( big.resolve(), 0xAB, 512 );
    const std::uint8_t* p = big.resolve();
    for ( std::size_t i = 0; i < 512; i++ )
        REQUIRE( p[i] == 0xAB );

    Mgr::deallocate_typed( big );

    Mgr::destroy();
}

TEST_CASE( "coalesce_stress_interleaved", "[test_coalesce]" )
{
    REQUIRE( Mgr::create( 512 * 1024 ) );

    static const int        ROUNDS = 200;
    Mgr::pptr<std::uint8_t> ptrs[4];
    std::size_t             sizes[] = { 64, 128, 256, 512 };

    for ( int r = 0; r < ROUNDS; r++ )
    {
        int slot = r % 4;
        if ( !ptrs[slot].is_null() )
        {
            Mgr::deallocate_typed( ptrs[slot] );
            ptrs[slot] = Mgr::pptr<std::uint8_t>();
        }
        ptrs[slot] = Mgr::allocate_typed<std::uint8_t>( sizes[slot] );
        REQUIRE( !ptrs[slot].is_null() );
    }

    for ( int k = 0; k < 4; k++ )
    {
        if ( !ptrs[k].is_null() )
            Mgr::deallocate_typed( ptrs[k] );
    }

    REQUIRE( Mgr::free_block_count() == 1 );

    Mgr::destroy();
}
