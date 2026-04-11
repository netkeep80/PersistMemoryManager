/**
 * @file test_issue123_sh_embedded.cpp
 * @brief Тест самодостаточности pmm_embedded_heap.h.
 *
 * Проверяет, что single-header файл pmm_embedded_heap.h является
 * полностью автономным: пользователь может скопировать один файл в свой
 * проект и использовать пресет EmbeddedHeap без дополнительных зависимостей.
 *
 * Этот файл намеренно не использует никаких других include из include/pmm/.
 *
 * @see include/pmm_embedded_heap.h
 * @version 0.1
 */

#include "pmm_embedded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

TEST_CASE( "test_issue123_sh_embedded", "[test_issue123_sh_embedded]" )
{
    using EMB = pmm::presets::EmbeddedHeap;

    REQUIRE( !EMB::is_initialized() );
    bool created = EMB::create( 16 * 1024 );
    REQUIRE( created );
    REQUIRE( EMB::is_initialized() );
    REQUIRE( EMB::total_size() >= 16 * 1024 );

    void* ptr = EMB::allocate( 128 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xCC, 128 );
    EMB::deallocate( ptr );

    EMB::pptr<int> p = EMB::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 99;
    REQUIRE( *p.resolve() == 99 );
    EMB::deallocate_typed( p );

    EMB::destroy();
    REQUIRE( !EMB::is_initialized() );
}
