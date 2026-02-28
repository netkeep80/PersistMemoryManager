/**
 * @file test_coalesce.cpp
 * @brief Тесты слияния соседних свободных блоков — Фаза 2
 *
 * Проверяет корректность алгоритма coalescing в PersistMemoryManager::deallocate():
 * - слияние со следующим свободным блоком;
 * - слияние с предыдущим свободным блоком;
 * - слияние сразу с обоими соседями;
 * - отсутствие слияния при занятых соседях;
 * - фрагментация снижается до нуля после освобождения всех блоков;
 * - после слияния можно выделить блок, размером больше любого из исходных;
 * - корректность validate() на каждом шаге.
 */

#include "persist_memory_manager.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

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

// ─── Тестовые функции ─────────────────────────────────────────────────────────

/**
 * @brief Слияние с СЛЕДУЮЩИМ блоком: free(p1) при p2 свободен → p1+p2 объединяются.
 *
 * Топология до:  [p1_alloc | p2_free | p3_alloc | tail_free]
 * Топология после free(p1): [p1p2_free | p3_alloc | tail_free]
 */
static bool test_coalesce_with_next()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 256 );
    void* p3 = mgr->allocate( 256 );
    PMM_TEST( p1 && p2 && p3 );

    auto before = pmm::get_stats( mgr );

    // Сначала освобождаем p2 (следующий за p1)
    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    // p2 теперь свободен, p1 ещё занят — слияния нет
    auto mid = pmm::get_stats( mgr );
    PMM_TEST( mid.total_blocks == before.total_blocks );

    // Теперь освобождаем p1 — он должен слиться с p2 (справа)
    mgr->deallocate( p1 );
    PMM_TEST( mgr->validate() );

    auto after = pmm::get_stats( mgr );
    // Блоков должно стать меньше (p1+p2 объединились)
    PMM_TEST( after.total_blocks < mid.total_blocks );

    // Проверяем, что можно выделить блок размером > 256 (использует объединённое пространство)
    void* big = mgr->allocate( 400 );
    PMM_TEST( big != nullptr );
    PMM_TEST( mgr->validate() );

    mgr->deallocate( big );
    mgr->deallocate( p3 );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Слияние с ПРЕДЫДУЩИМ блоком: free(p3) при p2 свободен → p2+p3 объединяются.
 *
 * Топология до:  [p1_alloc | p2_free | p3_alloc | tail_free]
 * Топология после free(p3): [p1_alloc | p2p3_free | tail_free]
 */
static bool test_coalesce_with_prev()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 256 );
    void* p3 = mgr->allocate( 256 );
    PMM_TEST( p1 && p2 && p3 );

    auto before = pmm::get_stats( mgr );

    // Освобождаем p2 (предшественник p3)
    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    auto mid = pmm::get_stats( mgr );
    PMM_TEST( mid.total_blocks == before.total_blocks ); // слияния не было

    // Освобождаем p3 — должен слиться с p2 (слева)
    mgr->deallocate( p3 );
    PMM_TEST( mgr->validate() );

    auto after = pmm::get_stats( mgr );
    // Блоков должно стать меньше (p2+p3 объединились)
    PMM_TEST( after.total_blocks < mid.total_blocks );

    // Проверяем, что можно выделить блок > 256 из объединённой области
    void* big = mgr->allocate( 400 );
    PMM_TEST( big != nullptr );
    PMM_TEST( mgr->validate() );

    mgr->deallocate( big );
    mgr->deallocate( p1 );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Слияние СРАЗУ С ДВУМЯ соседями: free(p2) при p1 и p3 свободны → p1+p2+p3.
 *
 * Топология до:  [p1_free | p2_alloc | p3_free | p4_alloc | tail_free]
 * Топология после free(p2): [p1p2p3_free | p4_alloc | tail_free]
 */
static bool test_coalesce_both_neighbors()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 256 );
    void* p3 = mgr->allocate( 256 );
    void* p4 = mgr->allocate( 256 );
    PMM_TEST( p1 && p2 && p3 && p4 );

    // Освобождаем p1 и p3 (соседи p2 с обеих сторон)
    mgr->deallocate( p1 );
    mgr->deallocate( p3 );
    PMM_TEST( mgr->validate() );

    auto        before      = pmm::get_stats( mgr );
    std::size_t free_before = before.free_blocks;

    // Теперь освобождаем p2 — должно слиться с p1 (слева) и p3 (справа)
    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    auto after = pmm::get_stats( mgr );
    // Количество блоков должно уменьшиться на 2 (p1+p2+p3 → один блок)
    PMM_TEST( after.total_blocks == before.total_blocks - 2 );
    // Количество свободных блоков тоже уменьшилось (три → один)
    PMM_TEST( after.free_blocks == free_before - 1 );

    // Можно выделить блок размером > 512 из объединённой области
    void* big = mgr->allocate( 600 );
    PMM_TEST( big != nullptr );
    PMM_TEST( mgr->validate() );

    mgr->deallocate( big );
    mgr->deallocate( p4 );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Нет слияния, когда оба соседа заняты.
 */
static bool test_coalesce_no_merge_when_neighbors_used()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 128 );
    void* p2 = mgr->allocate( 128 );
    void* p3 = mgr->allocate( 128 );
    PMM_TEST( p1 && p2 && p3 );

    auto before = pmm::get_stats( mgr );

    // Освобождаем средний блок — оба соседа заняты → слияния нет
    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    auto after = pmm::get_stats( mgr );
    PMM_TEST( after.total_blocks == before.total_blocks );
    PMM_TEST( after.free_blocks == before.free_blocks + 1 );

    mgr->deallocate( p1 );
    mgr->deallocate( p3 );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Нет слияния при освобождении первого блока без свободного соседа справа.
 */
static bool test_coalesce_first_block_no_next_free()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 256 );
    PMM_TEST( p1 && p2 );

    auto before = pmm::get_stats( mgr );

    // p1 — первый блок, p2 занят → слияния нет
    mgr->deallocate( p1 );
    PMM_TEST( mgr->validate() );

    auto after = pmm::get_stats( mgr );
    PMM_TEST( after.total_blocks == before.total_blocks );
    PMM_TEST( after.free_blocks == before.free_blocks + 1 );

    mgr->deallocate( p2 );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief После освобождения всех блоков фрагментация == 0.
 */
static bool test_coalesce_zero_fragmentation_after_all_free()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    const int N = 8;
    void*     ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 256 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    // Освобождаем все чётные — чередующиеся свободные/занятые
    for ( int i = 0; i < N; i += 2 )
    {
        mgr->deallocate( ptrs[i] );
    }
    PMM_TEST( mgr->validate() );
    PMM_TEST( mgr->fragmentation() > 0 ); // должна быть фрагментация

    // Освобождаем нечётные — должно произойти слияние
    for ( int i = 1; i < N; i += 2 )
    {
        mgr->deallocate( ptrs[i] );
        PMM_TEST( mgr->validate() );
    }

    PMM_TEST( mgr->fragmentation() == 0 );
    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.total_blocks == 1 );
    PMM_TEST( stats.free_blocks == 1 );
    PMM_TEST( stats.allocated_blocks == 0 );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Последовательное LIFO-освобождение приводит к одному свободному блоку.
 */
static bool test_coalesce_lifo_results_in_one_block()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    const int N = 5;
    void*     ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 512 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    for ( int i = N - 1; i >= 0; i-- )
    {
        mgr->deallocate( ptrs[i] );
        PMM_TEST( mgr->validate() );
    }

    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.total_blocks == 1 );
    PMM_TEST( stats.free_blocks == 1 );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Последовательное FIFO-освобождение приводит к одному свободному блоку.
 */
static bool test_coalesce_fifo_results_in_one_block()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    const int N = 5;
    void*     ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 512 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    for ( int i = 0; i < N; i++ )
    {
        mgr->deallocate( ptrs[i] );
        PMM_TEST( mgr->validate() );
    }

    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.total_blocks == 1 );
    PMM_TEST( stats.free_blocks == 1 );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief После слияния можно выделить блок суммарного размера.
 *
 * Три блока по 256 байт → освобождаем все → выделяем 512 байт.
 * Без coalescing это было бы невозможно (каждый блок содержит 256 байт данных,
 * но ещё заголовок; с coalescing — пространство объединяется).
 */
static bool test_coalesce_large_allocation_after_merge()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 256 );
    void* p3 = mgr->allocate( 256 );
    PMM_TEST( p1 && p2 && p3 );

    // Убеждаемся, что без освобождения нет места на 700 байт
    void* probe = mgr->allocate( 700 );
    // Здесь может быть место в хвосте, поэтому просто освободим если выделилось
    if ( probe )
    {
        mgr->deallocate( probe );
    }

    // Освобождаем все три блока (с coalescing они объединяются)
    mgr->deallocate( p1 );
    mgr->deallocate( p2 );
    mgr->deallocate( p3 );
    PMM_TEST( mgr->validate() );

    // Теперь должно хватить места на большой блок
    void* big = mgr->allocate( 512 );
    PMM_TEST( big != nullptr );
    PMM_TEST( mgr->validate() );

    // Записываем данные — проверяем, что память корректна
    std::memset( big, 0xAB, 512 );
    const std::uint8_t* p = static_cast<const std::uint8_t*>( big );
    for ( std::size_t i = 0; i < 512; i++ )
    {
        PMM_TEST( p[i] == 0xAB );
    }

    mgr->deallocate( big );
    PMM_TEST( mgr->validate() );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief Интенсивное чередование allocate/deallocate не нарушает структуры.
 */
static bool test_coalesce_stress_interleaved()
{
    const std::size_t size = 512 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    static const int ROUNDS  = 200;
    void*            ptrs[4] = { nullptr, nullptr, nullptr, nullptr };
    std::size_t      sizes[] = { 64, 128, 256, 512 };

    for ( int r = 0; r < ROUNDS; r++ )
    {
        int slot = r % 4;
        if ( ptrs[slot] )
        {
            mgr->deallocate( ptrs[slot] );
            ptrs[slot] = nullptr;
            PMM_TEST( mgr->validate() );
        }
        ptrs[slot] = mgr->allocate( sizes[slot] );
        if ( ptrs[slot] == nullptr )
        {
            // Память могла кончиться, освобождаем всё и пробуем снова
            for ( int k = 0; k < 4; k++ )
            {
                if ( ptrs[k] )
                {
                    mgr->deallocate( ptrs[k] );
                    ptrs[k] = nullptr;
                }
            }
            PMM_TEST( mgr->validate() );
            ptrs[slot] = mgr->allocate( sizes[slot] );
            PMM_TEST( ptrs[slot] != nullptr );
        }
        PMM_TEST( mgr->validate() );
    }

    // Освобождаем всё оставшееся
    for ( int k = 0; k < 4; k++ )
    {
        if ( ptrs[k] )
        {
            mgr->deallocate( ptrs[k] );
        }
    }
    PMM_TEST( mgr->validate() );

    // После всего — должен быть один свободный блок
    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.free_blocks == 1 );

    mgr->destroy();
    std::free( mem );
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_coalesce ===\n";
    bool all_passed = true;

    PMM_RUN( "coalesce_with_next", test_coalesce_with_next );
    PMM_RUN( "coalesce_with_prev", test_coalesce_with_prev );
    PMM_RUN( "coalesce_both_neighbors", test_coalesce_both_neighbors );
    PMM_RUN( "coalesce_no_merge_when_neighbors_used", test_coalesce_no_merge_when_neighbors_used );
    PMM_RUN( "coalesce_first_block_no_next_free", test_coalesce_first_block_no_next_free );
    PMM_RUN( "coalesce_zero_fragmentation_after_all_free", test_coalesce_zero_fragmentation_after_all_free );
    PMM_RUN( "coalesce_lifo_results_in_one_block", test_coalesce_lifo_results_in_one_block );
    PMM_RUN( "coalesce_fifo_results_in_one_block", test_coalesce_fifo_results_in_one_block );
    PMM_RUN( "coalesce_large_allocation_after_merge", test_coalesce_large_allocation_after_merge );
    PMM_RUN( "coalesce_stress_interleaved", test_coalesce_stress_interleaved );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
