/**
 * @file test_deallocate.cpp
 * @brief Тесты освобождения памяти (Фаза 1, обновлено в Issue #61)
 *
 * Issue #61: менеджер — полностью статический класс.
 *   - Все операции через статические методы PersistMemoryManager::xxx().
 *   - Выделение через allocate_typed<T>(), освобождение через deallocate_typed().
 *   - reallocate_typed<T>() заменяет void* reallocate(void*, size).
 *   - AllocationInfo и get_info() удалены.
 */

#include "pmm/legacy_manager.h"

#include <cassert>
#include <cstdint>
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

static bool test_deallocate_null()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> null_p;
    pmm::PersistMemoryManager<>::deallocate_typed( null_p );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_deallocate_single()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !ptr.is_null() );
    std::size_t used_after_alloc = pmm::PersistMemoryManager<>::used_size();

    pmm::PersistMemoryManager<>::deallocate_typed( ptr );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    PMM_TEST( pmm::PersistMemoryManager<>::used_size() < used_after_alloc );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_deallocate_reuse()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> ptr1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !ptr1.is_null() );

    pmm::PersistMemoryManager<>::deallocate_typed( ptr1 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::pptr<std::uint8_t> ptr2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !ptr2.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( ptr2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_deallocate_multiple_fifo()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    const int               num = 5;
    pmm::pptr<std::uint8_t> ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
        PMM_TEST( !ptrs[i].is_null() );
    }
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( int i = 0; i < num; i++ )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    }

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_deallocate_multiple_lifo()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    const int               num = 5;
    pmm::pptr<std::uint8_t> ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
        PMM_TEST( !ptrs[i].is_null() );
    }

    for ( int i = num - 1; i >= 0; i-- )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    }

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_deallocate_random_order()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> ptrs[6];
    for ( int i = 0; i < 6; i++ )
    {
        ptrs[i] =
            pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( static_cast<std::size_t>( ( i + 1 ) * 128 ) );
        PMM_TEST( !ptrs[i].is_null() );
    }

    int order[] = { 2, 5, 0, 3, 1, 4 };
    for ( int idx : order )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[idx] );
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    }

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_deallocate_all_then_check_free()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    std::size_t free_before = pmm::PersistMemoryManager<>::free_size();

    pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 1024 );
    PMM_TEST( !ptr.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::free_size() < free_before );

    pmm::PersistMemoryManager<>::deallocate_typed( ptr );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    PMM_TEST( pmm::PersistMemoryManager<>::free_size() >= free_before - 128 );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_deallocate_interleaved()
{
    const std::size_t size = 512 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> prev;
    for ( int i = 0; i < 50; i++ )
    {
        pmm::pptr<std::uint8_t> ptr =
            pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( static_cast<std::size_t>( 64 + i * 32 ) );
        PMM_TEST( !ptr.is_null() );
        if ( !prev.is_null() )
            pmm::PersistMemoryManager<>::deallocate_typed( prev );
        prev = ptr;
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    }
    if ( !prev.is_null() )
        pmm::PersistMemoryManager<>::deallocate_typed( prev );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_reallocate_grow()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );
    std::memset( p.get(), 0xCC, 128 );

    // Reallocate to 512 bytes — data must be preserved
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, 512 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    const std::uint8_t* raw = p2.get();
    for ( std::size_t i = 0; i < 128; i++ )
        PMM_TEST( raw[i] == 0xCC );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_reallocate_from_null()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> null_p;
    pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager<>::reallocate_typed( null_p, 256 );
    PMM_TEST( !ptr.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( ptr );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_block_data_size_bytes()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !ptr.is_null() );

    // Issue #59, #83: only kGranuleSize (16-byte) alignment is guaranteed
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr.get() ) % pmm::kGranuleSize == 0 );

    // block_data_size_bytes reports allocated size in bytes (rounded up to granule)
    std::size_t blk_size = pmm::PersistMemoryManager<>::block_data_size_bytes( ptr.offset() );
    PMM_TEST( blk_size >= 512 );
    PMM_TEST( blk_size % pmm::kGranuleSize == 0 );

    pmm::PersistMemoryManager<>::deallocate_typed( ptr );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

int main()
{
    std::cout << "=== test_deallocate ===\n";
    bool all_passed = true;

    PMM_RUN( "deallocate_null", test_deallocate_null );
    PMM_RUN( "deallocate_single", test_deallocate_single );
    PMM_RUN( "deallocate_reuse", test_deallocate_reuse );
    PMM_RUN( "deallocate_multiple_fifo", test_deallocate_multiple_fifo );
    PMM_RUN( "deallocate_multiple_lifo", test_deallocate_multiple_lifo );
    PMM_RUN( "deallocate_random_order", test_deallocate_random_order );
    PMM_RUN( "deallocate_all_then_check_free", test_deallocate_all_then_check_free );
    PMM_RUN( "deallocate_interleaved", test_deallocate_interleaved );
    PMM_RUN( "reallocate_grow", test_reallocate_grow );
    PMM_RUN( "reallocate_from_null", test_reallocate_from_null );
    PMM_RUN( "block_data_size_bytes", test_block_data_size_bytes );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
