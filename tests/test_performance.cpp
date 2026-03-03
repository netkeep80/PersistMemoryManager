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

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, MEMORY_SIZE ) );

    std::vector<pmm::pptr<std::uint8_t>> ptrs( N );

    auto t0        = now();
    int  allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( BLOCK_SIZE );
        if ( ptrs[i].is_null() )
            break;
        allocated++;
    }
    auto   t1       = now();
    double ms_alloc = elapsed_ms( t0, t1 );

    for ( int i = 0; i < allocated; i++ )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );

    PMM_TEST( allocated == N );
#ifdef NDEBUG
    PMM_TEST( ms_alloc <= 100.0 );
#else
    (void)ms_alloc; // Skip timing assertion in Debug/coverage builds
#endif

    return true;
}

static bool test_dealloc_100k_within_100ms()
{
    const std::size_t MEMORY_SIZE = 32UL * 1024 * 1024;
    const int         N           = 100'000;
    const std::size_t BLOCK_SIZE  = 64;

    void* mem = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, MEMORY_SIZE ) );

    std::vector<pmm::pptr<std::uint8_t>> ptrs( N );

    int allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( BLOCK_SIZE );
        if ( ptrs[i].is_null() )
            break;
        allocated++;
    }

    PMM_TEST( allocated == N );

    auto t0 = now();
    for ( int i = 0; i < allocated; i++ )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
    }
    auto   t1         = now();
    double ms_dealloc = elapsed_ms( t0, t1 );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );

#ifdef NDEBUG
    PMM_TEST( ms_dealloc <= 100.0 );
#else
    (void)ms_dealloc; // Skip timing assertion in Debug/coverage builds
#endif

    return true;
}

static bool test_alloc_dealloc_validate()
{
    const std::size_t MEMORY_SIZE = 1UL * 1024 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, MEMORY_SIZE ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    const int                            N = 1000;
    std::vector<pmm::pptr<std::uint8_t>> ptrs( N );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
        PMM_TEST( !ptrs[i].is_null() );
    }
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( int i = N - 1; i >= 0; i-- )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
    }
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats = pmm::get_stats();
    PMM_TEST( stats.free_blocks == 1 );
    PMM_TEST( stats.allocated_blocks == 1 ); // Issue #75: BlockHeader_0 always allocated

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_memory_reuse()
{
    const std::size_t MEMORY_SIZE = 512 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, MEMORY_SIZE ) );

    const int                            N = 100;
    std::vector<pmm::pptr<std::uint8_t>> ptrs( N );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
        PMM_TEST( !ptrs[i].is_null() );
        std::memset( ptrs[i].get(), i & 0xFF, 128 );
    }

    for ( int i = 0; i < N; i += 2 )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
        ptrs[i] = pmm::pptr<std::uint8_t>(); // null
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( int i = 0; i < N; i += 2 )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
        PMM_TEST( !ptrs[i].is_null() );
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( int i = 0; i < N; i++ )
    {
        if ( !ptrs[i].is_null() )
            pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats = pmm::get_stats();
    PMM_TEST( stats.allocated_blocks == 1 ); // Issue #75: BlockHeader_0 always allocated

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
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

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, MEMORY_SIZE ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    pmm::pptr<std::uint8_t> p3 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    // Запоминаем гранульные смещения p1 и p3
    std::uint32_t off1 = p1.offset();
    std::uint32_t off3 = p3.offset();

    // Сохраняем образ и загружаем его в новый буфер
    void* mem_copy = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem_copy != nullptr );
    std::memcpy( mem_copy, mem, MEMORY_SIZE );

    // Уничтожаем первый менеджер и освобождаем его память
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );

    // load() устанавливает синглтон на новый буфер
    PMM_TEST( pmm::PersistMemoryManager<>::load( mem_copy, MEMORY_SIZE ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::pptr<std::uint8_t> p4 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p4.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    // Используем сохранённые смещения для получения pptr в новом контексте
    pmm::pptr<std::uint8_t> q1( off1 );
    pmm::pptr<std::uint8_t> q3( off3 );

    pmm::PersistMemoryManager<>::deallocate_typed( q1 );
    pmm::PersistMemoryManager<>::deallocate_typed( q3 );
    pmm::PersistMemoryManager<>::deallocate_typed( p4 );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats = pmm::get_stats();
    PMM_TEST( stats.allocated_blocks == 1 ); // Issue #75: BlockHeader_0 always allocated

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem_copy );
    return true;
}

static bool test_data_integrity_with_free_list()
{
    const std::size_t MEMORY_SIZE = 2UL * 1024 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, MEMORY_SIZE ) );

    const int                            N = 200;
    std::vector<pmm::pptr<std::uint8_t>> ptrs( N );
    const std::size_t                    BLOCK = 256;

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( BLOCK );
        PMM_TEST( !ptrs[i].is_null() );
        std::memset( ptrs[i].get(), i & 0xFF, BLOCK );
    }

    for ( int i = 0; i < N; i += 3 )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
        ptrs[i] = pmm::pptr<std::uint8_t>(); // null
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( int i = 0; i < N; i++ )
    {
        if ( ptrs[i].is_null() )
            continue;
        const std::uint8_t* p = ptrs[i].get();
        for ( std::size_t j = 0; j < BLOCK; j++ )
        {
            PMM_TEST( p[j] == static_cast<std::uint8_t>( i & 0xFF ) );
        }
    }

    for ( int i = 0; i < N; i++ )
    {
        if ( !ptrs[i].is_null() )
            pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_full_coalesce_after_alloc_dealloc()
{
    const std::size_t MEMORY_SIZE = 1UL * 1024 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, MEMORY_SIZE ) );

    std::size_t initial_total = pmm::PersistMemoryManager<>::total_size();

    const int                            N = 500;
    std::vector<pmm::pptr<std::uint8_t>> ptrs( N );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
        PMM_TEST( !ptrs[i].is_null() );
    }

    for ( int i = 0; i < N; i += 2 )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
    }
    for ( int i = 1; i < N; i += 2 )
    {
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats = pmm::get_stats();
    PMM_TEST( stats.allocated_blocks == 1 ); // Issue #75: BlockHeader_0 always allocated
    PMM_TEST( stats.free_blocks == 1 );

    PMM_TEST( pmm::PersistMemoryManager<>::free_size() > 0 );
    PMM_TEST( pmm::PersistMemoryManager<>::free_size() + pmm::PersistMemoryManager<>::used_size() ==
              pmm::PersistMemoryManager<>::total_size() );
    (void)initial_total;

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_minimum_buffer_size()
{
    const std::size_t MEMORY_SIZE = pmm::kMinMemorySize;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, MEMORY_SIZE ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 8 );
    if ( !p.is_null() )
    {
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
        pmm::PersistMemoryManager<>::deallocate_typed( p );
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    }

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
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
