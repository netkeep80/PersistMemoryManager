/**
 * @file test_allocate.cpp
 * @brief Тесты выделения памяти (Фаза 1, обновлено в Фазе 7)
 *
 * Фаза 7: менеджер — синглтон, управляет памятью самостоятельно.
 * Вызов PersistMemoryManager::destroy() освобождает буфер — std::free() не нужен.
 * Автоматическое расширение памяти на 25% при нехватке.
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

static bool test_create_basic()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );
    PMM_TEST( mgr == pmm::PersistMemoryManager::instance() );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    PMM_TEST( pmm::PersistMemoryManager::instance() == nullptr );
    return true;
}

static bool test_create_too_small()
{
    const std::size_t size = 128;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr == nullptr );

    std::free( mem ); // create() failed — free manually
    return true;
}

static bool test_create_null()
{
    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( nullptr, 64 * 1024 );
    PMM_TEST( mgr == nullptr );
    return true;
}

static bool test_allocate_single_small()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 64 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr ) % 16 == 0 );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_allocate_alignment_32()
{
    // Issue #59: alignment parameter is accepted but only 16-byte alignment is guaranteed.
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 128, 32 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr ) % pmm::kMinAlignment == 0 );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_allocate_alignment_64()
{
    // Issue #59: alignment parameter is accepted but only 16-byte alignment is guaranteed.
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 256, 64 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr ) % pmm::kMinAlignment == 0 );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_allocate_multiple()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    const int num = 10;
    void*     ptrs[num];
    for ( int i = 0; i < num; i++ )
    {
        ptrs[i] = mgr->allocate( 1024 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    PMM_TEST( mgr->validate() );

    for ( int i = 0; i < num; i++ )
    {
        for ( int j = i + 1; j < num; j++ )
        {
            PMM_TEST( ptrs[i] != ptrs[j] );
        }
    }

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_allocate_zero()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 0 );
    PMM_TEST( ptr == nullptr );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

/**
 * @brief Автоматическое расширение памяти на 25% при нехватке (Фаза 7).
 */
static bool test_allocate_auto_expand()
{
    const std::size_t initial_size = 8 * 1024;
    void*             mem          = std::malloc( initial_size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, initial_size );
    PMM_TEST( mgr != nullptr );

    std::size_t initial_total = mgr->total_size();

    // Заполняем большую часть памяти
    void* block1 = mgr->allocate( 4 * 1024 );
    PMM_TEST( block1 != nullptr );

    // Запрашиваем блок, который требует расширения
    void* block2 = mgr->allocate( 4 * 1024 );
    PMM_TEST( block2 != nullptr );

    // Синглтон указывает на новый (расширенный) буфер
    pmm::PersistMemoryManager* mgr2 = pmm::PersistMemoryManager::instance();
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->total_size() > initial_total );
    PMM_TEST( mgr2->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_allocate_invalid_alignment()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr = mgr->allocate( 64, 17 );
    PMM_TEST( ptr == nullptr );
    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_allocate_write_read()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* ptr1 = mgr->allocate( 128 );
    void* ptr2 = mgr->allocate( 256 );
    PMM_TEST( ptr1 != nullptr );
    PMM_TEST( ptr2 != nullptr );

    std::memset( ptr1, 0xAA, 128 );
    std::memset( ptr2, 0xBB, 256 );

    const std::uint8_t* p1 = static_cast<const std::uint8_t*>( ptr1 );
    const std::uint8_t* p2 = static_cast<const std::uint8_t*>( ptr2 );
    for ( std::size_t i = 0; i < 128; i++ )
    {
        PMM_TEST( p1[i] == 0xAA );
    }
    for ( std::size_t i = 0; i < 256; i++ )
    {
        PMM_TEST( p2[i] == 0xBB );
    }

    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_allocate_metrics()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    PMM_TEST( mgr->total_size() == size );
    PMM_TEST( mgr->used_size() > 0 );
    PMM_TEST( mgr->free_size() < size );
    PMM_TEST( mgr->used_size() + mgr->free_size() <= size );

    std::size_t used_before = mgr->used_size();

    void* ptr = mgr->allocate( 512 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( mgr->used_size() > used_before );

    pmm::PersistMemoryManager::destroy();
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

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, initial_size );
    PMM_TEST( mgr != nullptr );

    // Allocate blocks until most of the space is used, but stop before auto-grow.
    void* ptrs[20];
    int   n = 0;
    for ( ; n < 20; ++n )
    {
        ptrs[n] = pmm::PersistMemoryManager::instance()->allocate( block_size );
        if ( ptrs[n] == nullptr )
            break;
        // Stop before we accidentally trigger auto-grow
        if ( pmm::PersistMemoryManager::instance()->total_size() != initial_size )
            break;
    }
    PMM_TEST( n >= 4 );

    // Free every other block — creates n/2 non-adjacent holes
    int holes = 0;
    for ( int i = 0; i < n; i += 2 )
    {
        pmm::PersistMemoryManager::instance()->deallocate( ptrs[i] );
        ++holes;
    }
    PMM_TEST( holes >= 2 );

    // Record state before re-allocation
    std::size_t size_before = pmm::PersistMemoryManager::instance()->total_size();

    // Re-allocate the freed blocks; they must fit in the fragmented holes.
    for ( int i = 0; i < holes; ++i )
    {
        void* p = pmm::PersistMemoryManager::instance()->allocate( block_size );
        PMM_TEST( p != nullptr );
    }

    // No expansion must have occurred — all allocations fit inside the freed holes.
    PMM_TEST( pmm::PersistMemoryManager::instance()->total_size() == size_before );
    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    pmm::PersistMemoryManager::destroy();
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
    PMM_RUN( "allocate_alignment_32", test_allocate_alignment_32 );
    PMM_RUN( "allocate_alignment_64", test_allocate_alignment_64 );
    PMM_RUN( "allocate_multiple", test_allocate_multiple );
    PMM_RUN( "allocate_zero", test_allocate_zero );
    PMM_RUN( "allocate_auto_expand", test_allocate_auto_expand );
    PMM_RUN( "allocate_invalid_alignment", test_allocate_invalid_alignment );
    PMM_RUN( "allocate_write_read", test_allocate_write_read );
    PMM_RUN( "allocate_metrics", test_allocate_metrics );
    PMM_RUN( "fragmented_gaps_reused_before_expand_space", test_fragmented_gaps_reused_before_expand_space );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
