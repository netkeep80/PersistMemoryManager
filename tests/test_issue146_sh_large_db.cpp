/**
 * @file test_issue146_sh_large_db.cpp
 * @brief Тест самодостаточности pmm_large_db_heap.h (Issue #146).
 *
 * Проверяет, что single-header файл pmm_large_db_heap.h является
 * полностью автономным: пользователь может скопировать один файл в свой
 * проект и использовать пресет LargeDBHeap без дополнительных зависимостей.
 *
 * Этот файл намеренно не использует никаких других include из include/pmm/.
 *
 * @see single_include/pmm/pmm_large_db_heap.h
 * @version 0.1 (Issue #146 — поддержка 64-bit индекса: LargeDBHeap)
 */

#include "pmm_large_db_heap.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

int main()
{
    std::cout << "=== test_issue146_sh_large_db (pmm_large_db_heap.h) ===\n";

    // LargeDBHeap uses InstanceId=0.
    // We use a custom manager to avoid conflicts with other tests sharing InstanceId=0.
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 1467>;

    // Verify 64-bit index size at compile time
    static_assert( sizeof( LDB::pptr<int> ) == 8, "LargeDBHeap pptr<int> must be 8 bytes (64-bit index)" );

    assert( !LDB::is_initialized() );
    bool created = LDB::create( 4096 );
    assert( created );
    assert( LDB::is_initialized() );
    assert( LDB::total_size() == 4096 );

    void* ptr = LDB::allocate( 128 );
    assert( ptr != nullptr );
    std::memset( ptr, 0xDD, 128 );
    LDB::deallocate( ptr );

    LDB::pptr<double> p = LDB::allocate_typed<double>();
    assert( !p.is_null() );
    *p.resolve() = 1.41421;
    assert( *p.resolve() == 1.41421 );
    LDB::deallocate_typed( p );

    LDB::destroy();
    assert( !LDB::is_initialized() );

    std::cout << "PASSED\n";
    return 0;
}
