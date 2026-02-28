/**
 * @file test_deallocate.cpp
 * @brief Тесты освобождения памяти (Фаза 1, обновлено в Фазе 7)
 *
 * Фаза 7: синглтон, destroy() освобождает буфер — std::free() не нужен.
 */

#include "persist_memory_manager.h"

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

static bool test_deallocate_null()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    mgr->deallocate( nullptr );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_deallocate_single()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 256 );
    PMM_TEST( ptr != nullptr );
    std::size_t used_after_alloc = mgr->used_size();

    mgr->deallocate( ptr );
    PMM_TEST( mgr->validate() );
    PMM_TEST( mgr->used_size() < used_after_alloc );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_deallocate_reuse()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr1 = mgr->allocate( 256 );
    PMM_TEST( ptr1 != nullptr );

    mgr->deallocate( ptr1 );
    PMM_TEST( mgr->validate() );

    void* ptr2 = mgr->allocate( 256 );
    PMM_TEST( ptr2 != nullptr );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_deallocate_multiple_fifo()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    const int num = 5;
    void*     ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = mgr->allocate( 512 );
        PMM_TEST( ptrs[i] != nullptr );
    }
    PMM_TEST( mgr->validate() );

    for ( int i = 0; i < num; i++ )
    {
        mgr->deallocate( ptrs[i] );
        PMM_TEST( mgr->validate() );
    }

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_deallocate_multiple_lifo()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    const int num = 5;
    void*     ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = mgr->allocate( 512 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    for ( int i = num - 1; i >= 0; i-- )
    {
        mgr->deallocate( ptrs[i] );
        PMM_TEST( mgr->validate() );
    }

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_deallocate_random_order()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptrs[6];
    for ( int i = 0; i < 6; i++ )
    {
        ptrs[i] = mgr->allocate( static_cast<std::size_t>( ( i + 1 ) * 128 ) );
        PMM_TEST( ptrs[i] != nullptr );
    }

    int order[] = { 2, 5, 0, 3, 1, 4 };
    for ( int idx : order )
    {
        mgr->deallocate( ptrs[idx] );
        PMM_TEST( mgr->validate() );
    }

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_deallocate_all_then_check_free()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    std::size_t free_before = mgr->free_size();

    void* ptr = mgr->allocate( 1024 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( mgr->free_size() < free_before );

    mgr->deallocate( ptr );
    PMM_TEST( mgr->validate() );
    PMM_TEST( mgr->free_size() >= free_before - 128 );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_deallocate_interleaved()
{
    const std::size_t size = 512 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* prev = nullptr;
    for ( int i = 0; i < 50; i++ )
    {
        void* ptr = mgr->allocate( static_cast<std::size_t>( 64 + i * 32 ) );
        PMM_TEST( ptr != nullptr );
        if ( prev != nullptr )
        {
            mgr->deallocate( prev );
        }
        prev = ptr;
        PMM_TEST( mgr->validate() );
    }
    if ( prev != nullptr )
    {
        mgr->deallocate( prev );
    }
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_reallocate_grow()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr1 = mgr->allocate( 128 );
    PMM_TEST( ptr1 != nullptr );
    std::memset( ptr1, 0xCC, 128 );

    void* ptr2 = mgr->reallocate( ptr1, 512 );
    PMM_TEST( ptr2 != nullptr );
    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    const std::uint8_t* p = static_cast<const std::uint8_t*>( ptr2 );
    for ( std::size_t i = 0; i < 128; i++ )
    {
        PMM_TEST( p[i] == 0xCC );
    }

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_reallocate_from_null()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->reallocate( nullptr, 256 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_get_info()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 512, 32 );
    PMM_TEST( ptr != nullptr );

    pmm::AllocationInfo info = pmm::get_info( mgr, ptr );
    PMM_TEST( info.is_valid );
    PMM_TEST( info.ptr == ptr );
    PMM_TEST( info.size == 512 );
    PMM_TEST( info.alignment == 32 );

    pmm::PersistMemoryManager::destroy();
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
    PMM_RUN( "get_info", test_get_info );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
