/**
 * @file test_issue146_sh_small_embedded_static.cpp
 * @brief Тест самодостаточности pmm_small_embedded_static_heap.h (Issue #146).
 *
 * Проверяет, что single-header файл pmm_small_embedded_static_heap.h является
 * полностью автономным: пользователь может скопировать один файл в свой
 * проект и использовать пресет SmallEmbeddedStaticHeap без дополнительных зависимостей.
 *
 * Этот файл намеренно не использует никаких других include из include/pmm/.
 *
 * @see single_include/pmm/pmm_small_embedded_static_heap.h
 * @version 0.1 (Issue #146 — поддержка 16-bit индекса: SmallEmbeddedStaticHeap)
 */

#include "pmm_small_embedded_static_heap.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

int main()
{
    std::cout << "=== test_issue146_sh_small_embedded_static (pmm_small_embedded_static_heap.h) ===\n";

    // SmallEmbeddedStaticHeap uses InstanceId=0 with default 1024-byte buffer.
    // We use a custom manager to avoid conflicts with other tests sharing InstanceId=0.
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<1024>, 1466>;

    // Verify 16-bit index size at compile time
    static_assert( sizeof( SESH::pptr<int> ) == 2, "SmallEmbeddedStaticHeap pptr<int> must be 2 bytes (16-bit index)" );

    assert( !SESH::is_initialized() );
    bool created = SESH::create( 1024 );
    assert( created );
    assert( SESH::is_initialized() );
    assert( SESH::total_size() == 1024 );

    void* ptr = SESH::allocate( 32 );
    assert( ptr != nullptr );
    std::memset( ptr, 0xCC, 32 );
    SESH::deallocate( ptr );

    SESH::pptr<int> p = SESH::allocate_typed<int>();
    assert( !p.is_null() );
    *p.resolve() = 7;
    assert( *p.resolve() == 7 );
    SESH::deallocate_typed( p );

    SESH::destroy();
    assert( !SESH::is_initialized() );

    std::cout << "PASSED\n";
    return 0;
}
