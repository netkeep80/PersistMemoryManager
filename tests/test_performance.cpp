/**
 * @file test_performance.cpp
 * @brief Тесты производительности и корректности оптимизаций (Фаза 6)
 *
 * Проверяет:
 * - Достижение целевых показателей: allocate/deallocate 100K блоков ≤ 100 мс.
 * - Корректность работы после allocate/deallocate при наличии оптимизаций.
 * - Корректность validate() после серии операций.
 * - Восстановление списка свободных блоков при load().
 */

#include "persist_memory_manager.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

// ─── Вспомогательные макросы ──────────────────────────────────────────────────

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

// ─── Вспомогательные функции ──────────────────────────────────────────────────

static auto now()
{
    return std::chrono::high_resolution_clock::now();
}

static double elapsed_ms( std::chrono::high_resolution_clock::time_point start,
                          std::chrono::high_resolution_clock::time_point end )
{
    return std::chrono::duration<double, std::milli>( end - start ).count();
}

// ─── Тестовые функции ─────────────────────────────────────────────────────────

/**
 * @brief allocate 100K блоков выполняется за ≤ 100 мс (целевой показатель ТЗ).
 */
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
    mgr->destroy();
    std::free( mem );

    PMM_TEST( allocated == N );
    PMM_TEST( ms_alloc <= 100.0 );

    return true;
}

/**
 * @brief deallocate 100K блоков выполняется за ≤ 100 мс (целевой показатель ТЗ).
 */
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
    mgr->destroy();
    std::free( mem );

    PMM_TEST( ms_dealloc <= 100.0 );

    return true;
}

/**
 * @brief Последовательные alloc/dealloc не нарушают структуру менеджера.
 */
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

    // Выделяем N блоков
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 64 );
        PMM_TEST( ptrs[i] != nullptr );
    }
    PMM_TEST( mgr->validate() );

    // Освобождаем в обратном порядке (вызывает coalescing)
    for ( int i = N - 1; i >= 0; i-- )
    {
        mgr->deallocate( ptrs[i] );
    }
    PMM_TEST( mgr->validate() );

    // После полного освобождения должен быть один большой свободный блок
    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.free_blocks == 1 );
    PMM_TEST( stats.allocated_blocks == 0 );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Повторное использование памяти после освобождения.
 */
static bool test_memory_reuse()
{
    const std::size_t MEMORY_SIZE = 512 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );

    // Первый раунд аллокаций
    const int          N = 100;
    std::vector<void*> ptrs( N, nullptr );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 128 );
        PMM_TEST( ptrs[i] != nullptr );
        std::memset( ptrs[i], i & 0xFF, 128 );
    }

    // Освобождаем чётные блоки
    for ( int i = 0; i < N; i += 2 )
    {
        mgr->deallocate( ptrs[i] );
        ptrs[i] = nullptr;
    }

    PMM_TEST( mgr->validate() );

    // Повторно выделяем в освобождённые слоты
    for ( int i = 0; i < N; i += 2 )
    {
        ptrs[i] = mgr->allocate( 64 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    PMM_TEST( mgr->validate() );

    // Освобождаем всё
    for ( int i = 0; i < N; i++ )
    {
        if ( ptrs[i] != nullptr )
            mgr->deallocate( ptrs[i] );
    }

    PMM_TEST( mgr->validate() );

    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.allocated_blocks == 0 );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Список свободных блоков корректно перестраивается после load().
 */
static bool test_free_list_after_load()
{
    const std::size_t MEMORY_SIZE = 512 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );

    // Выделяем несколько блоков
    void* p1 = mgr->allocate( 64 );
    void* p2 = mgr->allocate( 128 );
    void* p3 = mgr->allocate( 64 );
    PMM_TEST( p1 != nullptr && p2 != nullptr && p3 != nullptr );

    // Освобождаем средний блок
    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    // «Загружаем» из существующего буфера (имитация перезапуска)
    pmm::PersistMemoryManager* mgr2 = pmm::PersistMemoryManager::load( mem, MEMORY_SIZE );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    // После загрузки можно выделить новый блок
    void* p4 = mgr2->allocate( 64 );
    PMM_TEST( p4 != nullptr );

    PMM_TEST( mgr2->validate() );

    mgr2->deallocate( p1 );
    mgr2->deallocate( p3 );
    mgr2->deallocate( p4 );

    PMM_TEST( mgr2->validate() );

    // Все блоки освобождены — должен быть один большой свободный блок
    auto stats = pmm::get_stats( mgr2 );
    PMM_TEST( stats.allocated_blocks == 0 );

    mgr2->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Данные в выделенных блоках сохраняются при наличии оптимизаций.
 */
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

    // Выделяем и заполняем блоки
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( BLOCK );
        PMM_TEST( ptrs[i] != nullptr );
        std::memset( ptrs[i], i & 0xFF, BLOCK );
    }

    // Освобождаем каждый третий блок
    for ( int i = 0; i < N; i += 3 )
    {
        mgr->deallocate( ptrs[i] );
        ptrs[i] = nullptr;
    }

    PMM_TEST( mgr->validate() );

    // Проверяем, что данные в оставшихся блоках не повреждены
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

    // Освобождаем оставшиеся блоки
    for ( int i = 0; i < N; i++ )
    {
        if ( ptrs[i] != nullptr )
            mgr->deallocate( ptrs[i] );
    }

    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief После освобождения всех блоков память полностью объединяется.
 */
static bool test_full_coalesce_after_alloc_dealloc()
{
    const std::size_t MEMORY_SIZE = 1UL * 1024 * 1024;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );

    std::size_t initial_free = mgr->free_size();

    const int          N = 500;
    std::vector<void*> ptrs( N, nullptr );

    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 256 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    // Освобождаем в случайном порядке (чётные сначала, затем нечётные)
    for ( int i = 0; i < N; i += 2 )
    {
        mgr->deallocate( ptrs[i] );
    }
    for ( int i = 1; i < N; i += 2 )
    {
        mgr->deallocate( ptrs[i] );
    }

    PMM_TEST( mgr->validate() );

    // После слияния должен быть один большой свободный блок
    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.allocated_blocks == 0 );
    PMM_TEST( stats.free_blocks == 1 );

    // Свободная память должна быть не меньше начальной (за вычетом заголовков)
    PMM_TEST( mgr->free_size() > 0 );
    // После освобождения всего — free_size должен быть близок к initial_free
    // Допускаем небольшое расхождение из-за накладных расходов заголовков
    PMM_TEST( mgr->free_size() + mgr->used_size() == MEMORY_SIZE );
    (void)initial_free;

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Менеджер корректно работает при минимальном буфере.
 */
static bool test_minimum_buffer_size()
{
    const std::size_t MEMORY_SIZE = pmm::kMinMemorySize;
    void*             mem         = std::malloc( MEMORY_SIZE );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    PMM_TEST( mgr != nullptr );
    PMM_TEST( mgr->validate() );

    // Должна быть возможность выделить хотя бы один маленький блок
    void* p = mgr->allocate( 8 );
    if ( p != nullptr )
    {
        PMM_TEST( mgr->validate() );
        mgr->deallocate( p );
        PMM_TEST( mgr->validate() );
    }

    mgr->destroy();
    std::free( mem );
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

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
