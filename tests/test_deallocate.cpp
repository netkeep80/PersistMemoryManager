/**
 * @file test_deallocate.cpp
 * @brief Тесты освобождения памяти (Issue #110 — новый API)
 *
 * Issue #110: использует AbstractPersistMemoryManager через pmm_presets.h.
 *   - Все операции через экземпляр менеджера.
 *   - Выделение через allocate_typed<T>(), освобождение через deallocate_typed().
 */

#include "pmm/pmm_presets.h"

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

using Mgr = pmm::presets::SingleThreadedHeap;

static bool test_deallocate_null()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> null_p;
    Mgr::deallocate_typed( null_p );
    PMM_TEST( Mgr::is_initialized() );

    Mgr::destroy();
    return true;
}

static bool test_deallocate_single()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> ptr = Mgr::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !ptr.is_null() );
    std::size_t used_after_alloc = Mgr::used_size();

    Mgr::deallocate_typed( ptr );
    PMM_TEST( Mgr::is_initialized() );
    PMM_TEST( Mgr::used_size() < used_after_alloc );

    Mgr::destroy();
    return true;
}

static bool test_deallocate_reuse()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> ptr1 = Mgr::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !ptr1.is_null() );

    Mgr::deallocate_typed( ptr1 );

    Mgr::pptr<std::uint8_t> ptr2 = Mgr::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !ptr2.is_null() );

    Mgr::deallocate_typed( ptr2 );
    Mgr::destroy();
    return true;
}

static bool test_deallocate_multiple_fifo()
{
    PMM_TEST( Mgr::create( 256 * 1024 ) );

    const int               num = 5;
    Mgr::pptr<std::uint8_t> ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( 512 );
        PMM_TEST( !ptrs[i].is_null() );
    }

    for ( int i = 0; i < num; i++ )
    {
        Mgr::deallocate_typed( ptrs[i] );
    }

    Mgr::destroy();
    return true;
}

static bool test_deallocate_multiple_lifo()
{
    PMM_TEST( Mgr::create( 256 * 1024 ) );

    const int               num = 5;
    Mgr::pptr<std::uint8_t> ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( 512 );
        PMM_TEST( !ptrs[i].is_null() );
    }

    for ( int i = num - 1; i >= 0; i-- )
    {
        Mgr::deallocate_typed( ptrs[i] );
    }

    Mgr::destroy();
    return true;
}

static bool test_deallocate_random_order()
{
    PMM_TEST( Mgr::create( 256 * 1024 ) );

    Mgr::pptr<std::uint8_t> ptrs[6];
    for ( int i = 0; i < 6; i++ )
    {
        ptrs[i] = Mgr::allocate_typed<std::uint8_t>( static_cast<std::size_t>( ( i + 1 ) * 128 ) );
        PMM_TEST( !ptrs[i].is_null() );
    }

    int order[] = { 2, 5, 0, 3, 1, 4 };
    for ( int idx : order )
    {
        Mgr::deallocate_typed( ptrs[idx] );
    }

    Mgr::destroy();
    return true;
}

static bool test_deallocate_all_then_check_free()
{
    PMM_TEST( Mgr::create( 128 * 1024 ) );

    std::size_t free_before = Mgr::free_size();

    Mgr::pptr<std::uint8_t> ptr = Mgr::allocate_typed<std::uint8_t>( 1024 );
    PMM_TEST( !ptr.is_null() );
    PMM_TEST( Mgr::free_size() < free_before );

    Mgr::deallocate_typed( ptr );
    PMM_TEST( Mgr::is_initialized() );
    PMM_TEST( Mgr::free_size() >= free_before - 128 );

    Mgr::destroy();
    return true;
}

static bool test_deallocate_interleaved()
{
    PMM_TEST( Mgr::create( 512 * 1024 ) );

    Mgr::pptr<std::uint8_t> prev;
    for ( int i = 0; i < 50; i++ )
    {
        Mgr::pptr<std::uint8_t> ptr = Mgr::allocate_typed<std::uint8_t>( static_cast<std::size_t>( 64 + i * 32 ) );
        PMM_TEST( !ptr.is_null() );
        if ( !prev.is_null() )
            Mgr::deallocate_typed( prev );
        prev = ptr;
    }
    if ( !prev.is_null() )
        Mgr::deallocate_typed( prev );
    PMM_TEST( Mgr::is_initialized() );

    Mgr::destroy();
    return true;
}

static bool test_reallocate_grow()
{
    // reallocate is no longer in AbstractPersistMemoryManager; implement manually
    PMM_TEST( Mgr::create( 256 * 1024 ) );

    Mgr::pptr<std::uint8_t> p = Mgr::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );
    std::memset( p.resolve(), 0xCC, 128 );

    // Manual realloc: allocate new, copy, free old
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !p2.is_null() );
    std::memcpy( p2.resolve(), p.resolve(), 128 );
    Mgr::deallocate_typed( p );

    const std::uint8_t* raw = p2.resolve();
    for ( std::size_t i = 0; i < 128; i++ )
        PMM_TEST( raw[i] == 0xCC );

    Mgr::deallocate_typed( p2 );
    Mgr::destroy();
    return true;
}

static bool test_reallocate_from_null()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    // Allocating from a fresh pointer is just allocate_typed
    Mgr::pptr<std::uint8_t> ptr = Mgr::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !ptr.is_null() );

    Mgr::deallocate_typed( ptr );
    Mgr::destroy();
    return true;
}

static bool test_block_data_size_bytes()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> ptr = Mgr::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !ptr.is_null() );

    // Issue #59, #83: only kGranuleSize (16-byte) alignment is guaranteed
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr.resolve() ) % pmm::kGranuleSize == 0 );

    Mgr::deallocate_typed( ptr );
    Mgr::destroy();
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
