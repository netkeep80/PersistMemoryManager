/**
 * @file test_issue146_sh_small_embedded_static.cpp
 * @brief Тест самодостаточности pmm_small_embedded_static_heap.h.
 *
 * Проверяет, что single-header файл pmm_small_embedded_static_heap.h является
 * полностью автономным: пользователь может скопировать один файл в свой
 * проект и использовать пресет SmallEmbeddedStaticHeap без дополнительных зависимостей.
 *
 * Этот файл намеренно не использует никаких других include из include/pmm/.
 *
 * @see single_include/pmm/pmm_small_embedded_static_heap.h
 * @version 0.1
 */

#include "pmm_small_embedded_static_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>

TEST_CASE( "test_issue146_sh_small_embedded_static", "[test_issue146_sh_small_embedded_static]" )
{
    // SmallEmbeddedStaticHeap uses InstanceId=0 with default 1024-byte buffer.
    // We use a custom manager to avoid conflicts with other tests sharing InstanceId=0.
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 1466>;

    // Verify 16-bit index size at compile time
    static_assert( sizeof( SESH::pptr<int> ) == 2, "SmallEmbeddedStaticHeap pptr<int> must be 2 bytes (16-bit index)" );

    REQUIRE( !SESH::is_initialized() );
    bool created = SESH::create( 4096 );
    REQUIRE( created );
    REQUIRE( SESH::is_initialized() );
    REQUIRE( SESH::total_size() == 4096 );

    void* ptr = SESH::allocate( 32 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xCC, 32 );
    SESH::deallocate( ptr );

    SESH::pptr<int> p = SESH::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 7;
    REQUIRE( *p.resolve() == 7 );
    SESH::deallocate_typed( p );

    SESH::destroy();
    REQUIRE( !SESH::is_initialized() );
}
