/**
 * @file test_performance.cpp
 * @brief Тесты производительности и корректности оптимизаций (Фаза 6, обновлено в Фазе 7)
 *
 * Фаза 7: синглтон, destroy() освобождает буфер.
 */

#include "persist_memory_manager.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

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

static auto now()
{
    return std::chrono::high_resolution_clock::now();
}

static double elapsed_ms( std::chrono::high_resolution_clock::time_point start,
                          std::chrono::high_resolution_clock::time_point end )
{
    return std::chrono::duration<double, std::milli>( end - start ).count();
}

static bool test_alloc_100k_within_100ms()
{
    const std::size_t MEMORY_SIZE = 32UL * 1024 * 1024;
    const int         N           = 100'000;
    const std::size_t BLOCK_SIZE  = 64;

    void* mem = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );

    std::vector<void*> ptrs( N, nullptr );

    auto t0        = now();
    int  allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( BLOCK_SIZE );
        if ( ptrs[i] == nullptr )
            break;
        allocated++;
    }
    auto   t1       = now();
    double ms_alloc = elapsed_ms( t0, t1 );

    for ( int i = 0; i < allocated; i++ )
    {
        mgr->deallocate( ptrs[i] );
    }

    PMM_TEST( mgr->validate() );
    pmm::PersistMemoryManager::destroy();

    PMM_TEST( allocated == N );
    PMM_TEST( ms_alloc <= 100.0 );

    return true;
}

static bool test_dealloc_100k_within_100ms()
{
    const std::size_t MEMORY_SIZE = 32UL * 1024 * 1024;
    const int         N           = 100'000;
    const std::size_t BLOCK_SIZE  = 64;

    void* mem = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );

    std::vector<void*> ptrs( N, nullptr );

    int allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( BLOCK_SIZE );
        if ( ptrs[i] == nullptr )
            break;
        allocated++;
    }

    PMM_TEST( allocated == N );

    auto t0 = now();
    for ( int i = 0; i < allocated; i++ )
    {
        mgr->deallocate( ptrs[i] );
    }
    auto   t1         = now();
    double ms_dealloc = elapsed_ms( t0, t1 );

    PMM_TEST( mgr->validate() );
    pmm::PersistMemoryManager::destroy();

    PMM_TEST( ms_dealloc <= 100.0 );

    return true;
}

static bool test_alloc_dealloc_validate()
{
    const std::size_t MEMORY_SIZE = 1UL * 1024 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );
    PMM_TEST( mgr->validate() );

    const int          N = 1000;
    std::vector<void*> ptrs( N, nullptr );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 64 );
        PMM_TEST( ptrs[i] != nullptr );
    }
    PMM_TEST( mgr->validate() );

    for ( int i = N - 1; i >= 0; i-- )
    {
        mgr->deallocate( ptrs[i] );
    }
    PMM_TEST( mgr->validate() );

    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.free_blocks == 1 );
    PMM_TEST( stats.allocated_blocks == 0 );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_memory_reuse()
{
    const std::size_t MEMORY_SIZE = 512 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );

    const int          N = 100;
    std::vector<void*> ptrs( N, nullptr );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 128 );
        PMM_TEST( ptrs[i] != nullptr );
        std::memset( ptrs[i], i & 0xFF, 128 );
    }

    for ( int i = 0; i < N; i += 2 )
    {
        mgr->deallocate( ptrs[i] );
        ptrs[i] = nullptr;
    }

    PMM_TEST( mgr->validate() );

    for ( int i = 0; i < N; i += 2 )
    {
        ptrs[i] = mgr->allocate( 64 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    PMM_TEST( mgr->validate() );

    for ( int i = 0; i < N; i++ )
    {
        if ( ptrs[i] != nullptr )
            mgr->deallocate( ptrs[i] );
    }

    PMM_TEST( mgr->validate() );

    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.allocated_blocks == 0 );

    pmm::PersistMemoryManager::destroy();
    return true;
}

/**
 * @brief Список свободных блоков корректно перестраивается после load().
 *
 * Фаза 7: load() устанавливает синглтон — первый менеджер больше не используется.
 */
static bool test_free_list_after_load()
{
    const std::size_t MEMORY_SIZE = 512 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 64 );
    void* p2 = mgr->allocate( 128 );
    void* p3 = mgr->allocate( 64 );
    PMM_TEST( p1 != nullptr && p2 != nullptr && p3 != nullptr );

    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    // Запоминаем смещения p1 и p3 до того как менеджер сменится
    std::ptrdiff_t off1 = static_cast<std::uint8_t*>( p1 ) - static_cast<std::uint8_t*>( static_cast<void*>( mgr ) );
    std::ptrdiff_t off3 = static_cast<std::uint8_t*>( p3 ) - static_cast<std::uint8_t*>( static_cast<void*>( mgr ) );

    // Сохраняем образ и загружаем его в новый буфер
    void* mem_copy = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem_copy != nullptr );
    std::memcpy( mem_copy, mem, MEMORY_SIZE );

    // load() устанавливает синглтон на новый буфер
    pmm::PersistMemoryManager* mgr2 = pmm::PersistMemoryManager::load( mem_copy, MEMORY_SIZE );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );
    PMM_TEST( pmm::PersistMemoryManager::instance() == mgr2 );

    void* p4 = mgr2->allocate( 64 );
    PMM_TEST( p4 != nullptr );
    PMM_TEST( mgr2->validate() );

    // Используем сохранённые смещения для получения указателей в новом буфере
    void* q1 = static_cast<std::uint8_t*>( mem_copy ) + off1;
    void* q3 = static_cast<std::uint8_t*>( mem_copy ) + off3;

    mgr2->deallocate( q1 );
    mgr2->deallocate( q3 );
    mgr2->deallocate( p4 );

    PMM_TEST( mgr2->validate() );

    auto stats = pmm::get_stats( mgr2 );
    PMM_TEST( stats.allocated_blocks == 0 );

    pmm::PersistMemoryManager::destroy(); // освобождает mem_copy

    // Освобождаем исходный буфер вручную (mgr2 не владел им)
    std::free( mem );
    return true;
}

static bool test_data_integrity_with_free_list()
{
    const std::size_t MEMORY_SIZE = 2UL * 1024 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );

    const int          N = 200;
    std::vector<void*> ptrs( N, nullptr );
    const std::size_t  BLOCK = 256;

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( BLOCK );
        PMM_TEST( ptrs[i] != nullptr );
        std::memset( ptrs[i], i & 0xFF, BLOCK );
    }

    for ( int i = 0; i < N; i += 3 )
    {
        mgr->deallocate( ptrs[i] );
        ptrs[i] = nullptr;
    }

    PMM_TEST( mgr->validate() );

    for ( int i = 0; i < N; i++ )
    {
        if ( ptrs[i] == nullptr )
            continue;
        const std::uint8_t* p = static_cast<const std::uint8_t*>( ptrs[i] );
        for ( std::size_t j = 0; j < BLOCK; j++ )
        {
            PMM_TEST( p[j] == static_cast<std::uint8_t>( i & 0xFF ) );
        }
    }

    for ( int i = 0; i < N; i++ )
    {
        if ( ptrs[i] != nullptr )
            mgr->deallocate( ptrs[i] );
    }

    PMM_TEST( mgr->validate() );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_full_coalesce_after_alloc_dealloc()
{
    const std::size_t MEMORY_SIZE = 1UL * 1024 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );

    std::size_t initial_total = mgr->total_size();

    const int          N = 500;
    std::vector<void*> ptrs( N, nullptr );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 256 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    for ( int i = 0; i < N; i += 2 )
    {
        mgr->deallocate( ptrs[i] );
    }
    for ( int i = 1; i < N; i += 2 )
    {
        mgr->deallocate( ptrs[i] );
    }

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
    PMM_TEST( stats.allocated_blocks == 0 );
    PMM_TEST( stats.free_blocks == 1 );

    PMM_TEST( pmm::PersistMemoryManager::instance()->free_size() > 0 );
    PMM_TEST( pmm::PersistMemoryManager::instance()->free_size() + pmm::PersistMemoryManager::instance()->used_size() ==
              pmm::PersistMemoryManager::instance()->total_size() );
    (void)initial_total;

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_minimum_buffer_size()
{
    const std::size_t MEMORY_SIZE = pmm::kMinMemorySize;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );
    PMM_TEST( mgr->validate() );

    void* p = mgr->allocate( 8 );
    if ( p != nullptr )
    {
        PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
        pmm::PersistMemoryManager::instance()->deallocate( p );
        PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
    }

    pmm::PersistMemoryManager::destroy();
    return true;
}

int main()
{
    std::cout << "=== test_performance (Фаза 6) ===\n";
    bool all_passed = true;

    PMM_RUN( "alloc 100K ≤ 100ms", test_alloc_100k_within_100ms );
    PMM_RUN( "dealloc 100K ≤ 100ms", test_dealloc_100k_within_100ms );
    PMM_RUN( "alloc/dealloc validate", test_alloc_dealloc_validate );
    PMM_RUN( "memory reuse", test_memory_reuse );
    PMM_RUN( "free list after load", test_free_list_after_load );
    PMM_RUN( "data integrity with free list", test_data_integrity_with_free_list );
    PMM_RUN( "full coalesce after alloc/dealloc", test_full_coalesce_after_alloc_dealloc );
    PMM_RUN( "minimum buffer size", test_minimum_buffer_size );

    std::cout << ( all_passed ? "ALL PASSED\n" : "SOME TESTS FAILED\n" );
    return all_passed ? 0 : 1;
}
