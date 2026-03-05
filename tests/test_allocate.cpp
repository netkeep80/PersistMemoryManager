/**
 * @file test_allocate.cpp
 * @brief Тесты выделения памяти (Фаза 1, обновлено в Issue #61)
 *
 * Issue #61: менеджер — полностью статический класс.
 *   - PersistMemoryManager* не используется в коде (кроме create/load/destroy).
 *   - Все операции через статические методы PersistMemoryManager::xxx().
 *   - Выделение памяти через allocate_typed<T>(), освобождение через deallocate_typed().
 *   - Автоматическое расширение памяти на 25% при нехватке.
 */

#include "pmm/legacy_manager.h"

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

static bool test_create_basic()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::is_initialized() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    PMM_TEST( !pmm::PersistMemoryManager<>::is_initialized() );
    std::free( mem );
    return true;
}

static bool test_create_too_small()
{
    const std::size_t size = 128;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( !pmm::PersistMemoryManager<>::create( mem, size ) );

    std::free( mem );
    return true;
}

static bool test_create_null()
{
    PMM_TEST( !pmm::PersistMemoryManager<>::create( nullptr, 64 * 1024 ) );
    return true;
}

static bool test_allocate_single_small()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( p.get() != nullptr );
    PMM_TEST( reinterpret_cast<std::uintptr_t>( p.get() ) % 16 == 0 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_allocate_multiple()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    const int               num = 10;
    pmm::pptr<std::uint8_t> ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 1024 );
        PMM_TEST( !ptrs[i].is_null() );
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( int i = 0; i < num; i++ )
    {
        for ( int j = i + 1; j < num; j++ )
        {
            PMM_TEST( ptrs[i] != ptrs[j] );
        }
    }

    for ( int i = 0; i < num; i++ )
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_allocate_zero()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 0 );
    PMM_TEST( p.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Автоматическое расширение памяти на 25% при нехватке.
 */
static bool test_allocate_auto_expand()
{
    const std::size_t initial_size = 8 * 1024;
    void*             mem          = std::malloc( initial_size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, initial_size ) );

    std::size_t initial_total = pmm::PersistMemoryManager<>::total_size();

    // Заполняем большую часть памяти
    pmm::pptr<std::uint8_t> block1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !block1.is_null() );

    // Запрашиваем блок, который требует расширения
    pmm::pptr<std::uint8_t> block2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !block2.is_null() );

    // После расширения синглтон указывает на новый (расширенный) буфер
    PMM_TEST( pmm::PersistMemoryManager<>::is_initialized() );
    PMM_TEST( pmm::PersistMemoryManager<>::total_size() > initial_total );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    // Note: mem is freed by destroy() after expand (expand allocates with malloc and owns it)
    return true;
}

static bool test_allocate_write_read()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p1.is_null() );
    PMM_TEST( !p2.is_null() );

    std::memset( p1.get(), 0xAA, 128 );
    std::memset( p2.get(), 0xBB, 256 );

    const std::uint8_t* r1 = p1.get();
    const std::uint8_t* r2 = p2.get();
    for ( std::size_t i = 0; i < 128; i++ )
        PMM_TEST( r1[i] == 0xAA );
    for ( std::size_t i = 0; i < 256; i++ )
        PMM_TEST( r2[i] == 0xBB );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p1 );
    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_allocate_metrics()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    PMM_TEST( pmm::PersistMemoryManager<>::total_size() == size );
    PMM_TEST( pmm::PersistMemoryManager<>::used_size() > 0 );
    PMM_TEST( pmm::PersistMemoryManager<>::free_size() < size );
    PMM_TEST( pmm::PersistMemoryManager<>::used_size() + pmm::PersistMemoryManager<>::free_size() <= size );

    std::size_t used_before = pmm::PersistMemoryManager<>::used_size();

    pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !ptr.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::used_size() > used_before );

    pmm::PersistMemoryManager<>::deallocate_typed( ptr );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Fragmented free blocks must be reused before the tail expansion space
 *        (Issue #53 fix verification).
 *
 * Strategy:
 *   1. Create a PMM and allocate N blocks, staying within the initial buffer.
 *   2. Free every other block to create N/2 non-adjacent fragmented holes.
 *   3. Re-allocate N/2 blocks of the same size.
 *   4. Verify total_size did NOT grow — all allocations fit in the freed holes.
 */
static bool test_fragmented_gaps_reused_before_expand_space()
{
    const std::size_t block_size   = 256;
    const std::size_t initial_size = 8 * 1024;

    void* mem = std::malloc( initial_size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, initial_size ) );

    // Allocate blocks until most of the space is used, but stop before auto-grow.
    pmm::pptr<std::uint8_t> ptrs[20];
    int                     n = 0;
    for ( ; n < 20; ++n )
    {
        ptrs[n] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( block_size );
        if ( ptrs[n].is_null() )
            break;
        // Stop before we accidentally trigger auto-grow
        if ( pmm::PersistMemoryManager<>::total_size() != initial_size )
            break;
    }
    PMM_TEST( n >= 4 );

    // Free every other block — creates n/2 non-adjacent holes
    int holes = 0;
    for ( int i = 0; i < n; i += 2 )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
        ++holes;
    }
    PMM_TEST( holes >= 2 );

    // Record state before re-allocation
    std::size_t size_before = pmm::PersistMemoryManager<>::total_size();

    // Re-allocate the freed blocks; they must fit in the fragmented holes.
    for ( int i = 0; i < holes; ++i )
    {
        pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( block_size );
        PMM_TEST( !p.is_null() );
        ptrs[i] = p; // track for cleanup
    }

    // No expansion must have occurred — all allocations fit inside the freed holes.
    PMM_TEST( pmm::PersistMemoryManager<>::total_size() == size_before );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

int main()
{
    std::cout << "=== test_allocate ===\n";
    bool all_passed = true;

    PMM_RUN( "create_basic", test_create_basic );
    PMM_RUN( "create_too_small", test_create_too_small );
    PMM_RUN( "create_null", test_create_null );
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
