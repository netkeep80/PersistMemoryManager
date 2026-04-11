/**
 * @file test_issue123_sh_single_threaded.cpp
 * @brief Тест самодостаточности pmm_single_threaded_heap.h.
 *
 * Проверяет, что single-header файл pmm_single_threaded_heap.h является
 * полностью автономным: пользователь может скопировать один файл в свой
 * проект и использовать пресет SingleThreadedHeap без дополнительных зависимостей.
 *
 * Этот файл намеренно не использует никаких других include из include/pmm/.
 *
 * @see include/pmm_single_threaded_heap.h
 * @version 0.1
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

TEST_CASE( "test_issue123_sh_single_threaded", "[test_issue123_sh_single_threaded]" )
{
    using STH = pmm::presets::SingleThreadedHeap;

    REQUIRE( !STH::is_initialized() );
    bool created = STH::create( 16 * 1024 );
    REQUIRE( created );
    REQUIRE( STH::is_initialized() );
    REQUIRE( STH::total_size() >= 16 * 1024 );

    void* ptr = STH::allocate( 128 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xAA, 128 );
    STH::deallocate( ptr );

    STH::pptr<int> p = STH::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 42;
    REQUIRE( *p.resolve() == 42 );
    STH::deallocate_typed( p );

    STH::destroy();
    REQUIRE( !STH::is_initialized() );
}
