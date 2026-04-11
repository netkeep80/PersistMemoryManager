/**
 * @file test_deallocate.cpp
 * @brief Тесты освобождения памяти
 *
 *   - Все операции через экземпляр менеджера.
 *   - Выделение через allocate_typed<T>(), освобождение через deallocate_typed().
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>

using Mgr = pmm::presets::SingleThreadedHeap;

TEST_CASE( "deallocate_null", "[test_deallocate]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> null_p;
    Mgr::deallocate_typed( null_p );
    REQUIRE( Mgr::is_initialized() );

    Mgr::destroy();
}

TEST_CASE( "deallocate_single", "[test_deallocate]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> ptr = Mgr::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( !ptr.is_null() );
    std::size_t used_after_alloc = Mgr::used_size();

    Mgr::deallocate_typed( ptr );
    REQUIRE( Mgr::is_initialized() );
    REQUIRE( Mgr::used_size() < used_after_alloc );

    Mgr::destroy();
}

TEST_CASE( "deallocate_reuse", "[test_deallocate]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> ptr1 = Mgr::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( !ptr1.is_null() );

    Mgr::deallocate_typed( ptr1 );

    Mgr::pptr<std::uint8_t> ptr2 = Mgr::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( !ptr2.is_null() );

    Mgr::deallocate_typed( ptr2 );
    Mgr::destroy();
}

TEST_CASE( "deallocate_multiple_fifo", "[test_deallocate]" )
{
    REQUIRE( Mgr::create( 256 * 1024 ) );

    const int               num = 5;
    Mgr::pptr<std::uint8_t> ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( 512 );
        REQUIRE( !ptrs[i].is_null() );
    }

    for ( int i = 0; i < num; i++ )
    {
        Mgr::deallocate_typed( ptrs[i] );
    }

    Mgr::destroy();
}

TEST_CASE( "deallocate_multiple_lifo", "[test_deallocate]" )
{
    REQUIRE( Mgr::create( 256 * 1024 ) );

    const int               num = 5;
    Mgr::pptr<std::uint8_t> ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( 512 );
        REQUIRE( !ptrs[i].is_null() );
    }

    for ( int i = num - 1; i >= 0; i-- )
    {
        Mgr::deallocate_typed( ptrs[i] );
    }

    Mgr::destroy();
}

TEST_CASE( "deallocate_random_order", "[test_deallocate]" )
{
    REQUIRE( Mgr::create( 256 * 1024 ) );

    Mgr::pptr<std::uint8_t> ptrs[6];
    for ( int i = 0; i < 6; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( static_cast<std::size_t>( ( i + 1 ) * 128 ) );
        REQUIRE( !ptrs[i].is_null() );
    }

    int order[] = { 2, 5, 0, 3, 1, 4 };
    for ( int idx : order )
    {
        Mgr::deallocate_typed( ptrs[idx] );
    }

    Mgr::destroy();
}

TEST_CASE( "deallocate_all_then_check_free", "[test_deallocate]" )
{
    REQUIRE( Mgr::create( 128 * 1024 ) );

    std::size_t free_before = Mgr::free_size();

    Mgr::pptr<std::uint8_t> ptr = Mgr::allocate_typed<std::uint8_t>( 1024 );
    REQUIRE( !ptr.is_null() );
    REQUIRE( Mgr::free_size() < free_before );

    Mgr::deallocate_typed( ptr );
    REQUIRE( Mgr::is_initialized() );
    REQUIRE( Mgr::free_size() >= free_before - 128 );

    Mgr::destroy();
}

TEST_CASE( "deallocate_interleaved", "[test_deallocate]" )
{
    REQUIRE( Mgr::create( 512 * 1024 ) );

    Mgr::pptr<std::uint8_t> prev;
    for ( int i = 0; i < 50; i++ )
    {
        Mgr::pptr<std::uint8_t> ptr = Mgr::allocate_typed<std::uint8_t>( static_cast<std::size_t>( 64 + i * 32 ) );
        REQUIRE( !ptr.is_null() );
        if ( !prev.is_null() )
            Mgr::deallocate_typed( prev );
        prev = ptr;
    }
    if ( !prev.is_null() )
        Mgr::deallocate_typed( prev );
    REQUIRE( Mgr::is_initialized() );

    Mgr::destroy();
}

TEST_CASE( "reallocate_grow", "[test_deallocate]" )
{
    // reallocate is no longer in AbstractPersistMemoryManager; implement manually
    REQUIRE( Mgr::create( 256 * 1024 ) );

    Mgr::pptr<std::uint8_t> p = Mgr::allocate_typed<std::uint8_t>( 128 );
    REQUIRE( !p.is_null() );
    std::memset( p.resolve(), 0xCC, 128 );

    // Manual realloc: allocate new, copy, free old
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 512 );
    REQUIRE( !p2.is_null() );
    std::memcpy( p2.resolve(), p.resolve(), 128 );
    Mgr::deallocate_typed( p );

    const std::uint8_t* raw = p2.resolve();
    for ( std::size_t i = 0; i < 128; i++ )
        REQUIRE( raw[i] == 0xCC );

    Mgr::deallocate_typed( p2 );
    Mgr::destroy();
}

TEST_CASE( "reallocate_from_null", "[test_deallocate]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    // Allocating from a fresh pointer is just allocate_typed
    Mgr::pptr<std::uint8_t> ptr = Mgr::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( !ptr.is_null() );

    Mgr::deallocate_typed( ptr );
    Mgr::destroy();
}

TEST_CASE( "block_data_size_bytes", "[test_deallocate]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> ptr = Mgr::allocate_typed<std::uint8_t>( 512 );
    REQUIRE( !ptr.is_null() );

    // Only kGranuleSize (16-byte) alignment is guaranteed
    REQUIRE( reinterpret_cast<std::uintptr_t>( ptr.resolve() ) % pmm::kGranuleSize == 0 );

    Mgr::deallocate_typed( ptr );
    Mgr::destroy();
}
