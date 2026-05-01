/*
## test-issue123-sh-multi-threaded
req: ac-008, ac-009
*/

/**
 * @file test_issue123_sh_multi_threaded.cpp
 * @brief Тест самодостаточности pmm_multi_threaded_heap.h.
 *
 * Проверяет, что single-header файл pmm_multi_threaded_heap.h является
 * полностью автономным: пользователь может скопировать один файл в свой
 * проект и использовать пресет MultiThreadedHeap без дополнительных зависимостей.
 *
 * Этот файл намеренно не использует никаких других include из include/pmm/.
 *
 * @see include/pmm_multi_threaded_heap.h
 * @version 0.1
 */

#include "pmm_multi_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

TEST_CASE( "test_issue123_sh_multi_threaded", "[test_issue123_sh_multi_threaded]" )
{
    using MTH = pmm::presets::MultiThreadedHeap;

    REQUIRE( !MTH::is_initialized() );
    bool created = MTH::create( 16 * 1024 );
    REQUIRE( created );
    REQUIRE( MTH::is_initialized() );
    REQUIRE( MTH::total_size() >= 16 * 1024 );

    void* ptr = MTH::allocate( 128 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xBB, 128 );
    MTH::deallocate( ptr );

    MTH::pptr<double> p = MTH::allocate_typed<double>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 3.14;
    REQUIRE( *p.resolve() == 3.14 );
    MTH::deallocate_typed( p );

    MTH::destroy();
    REQUIRE( !MTH::is_initialized() );
}
