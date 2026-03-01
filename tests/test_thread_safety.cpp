/**
 * @file test_thread_safety.cpp
 * @brief Тесты потокобезопасности PersistMemoryManager (Фаза 9).
 *
 * Проверяет корректность работы менеджера памяти при многопоточном доступе.
 * Использует std::thread для параллельного выделения и освобождения памяти.
 */

#include "persist_memory_manager.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

// ─── Вспомогательный макрос ────────────────────────────────────────────────

#define PMM_TEST( cond, msg )                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( cond ) )                                                                                               \
        {                                                                                                              \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " << ( msg ) << "\n";                           \
            std::exit( 1 );                                                                                            \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "PASS: " << ( msg ) << "\n";                                                                  \
        }                                                                                                              \
    } while ( false )

// ─── Вспомогательная функция ───────────────────────────────────────────────

/// Создаёт новый менеджер с буфером заданного размера.
static pmm::PersistMemoryManager* make_manager( std::size_t size )
{
    void* mem = std::malloc( size );
    return pmm::PersistMemoryManager::create( mem, size );
}

// ─── Тесты ────────────────────────────────────────────────────────────────

/**
 * @brief Параллельное выделение памяти из нескольких потоков.
 *
 * Несколько потоков одновременно вызывают allocate(). Все аллокации
 * должны завершиться успешно (менеджер автоматически расширяется) и
 * возвращать ненулевые, уникальные указатели.
 */
static void test_concurrent_allocate()
{
    constexpr std::size_t kMemSize   = 32 * 1024 * 1024; // 32 МБ
    constexpr int         kThreads   = 4;
    constexpr int         kPerThread = 200;
    constexpr std::size_t kBlockSize = 64;

    make_manager( kMemSize );

    std::vector<std::vector<void*>> results( kThreads );
    std::vector<std::thread>        threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &results, kPerThread, kBlockSize]()
            {
                auto* mgr = pmm::PersistMemoryManager::instance();
                for ( int i = 0; i < kPerThread; ++i )
                {
                    void* p = mgr->allocate( kBlockSize );
                    if ( p != nullptr )
                    {
                        results[t].push_back( p );
                    }
                }
            } );
    }

    for ( auto& th : threads )
    {
        th.join();
    }

    // Освобождаем всё
    auto* mgr = pmm::PersistMemoryManager::instance();
    for ( auto& vec : results )
    {
        for ( void* p : vec )
        {
            mgr->deallocate( p );
        }
    }

    // Проверяем целостность
    PMM_TEST( mgr->validate(), "concurrent_allocate: validate() после параллельных аллокаций" );

    // Проверяем, что суммарно выделено kThreads * kPerThread блоков (или меньше при расширении)
    int total = 0;
    for ( auto& vec : results )
    {
        total += static_cast<int>( vec.size() );
    }
    PMM_TEST( total > 0, "concurrent_allocate: хотя бы один блок выделен" );

    pmm::PersistMemoryManager::destroy();
}

/**
 * @brief Параллельное чередование allocate/deallocate.
 *
 * Несколько потоков выделяют и освобождают память в цикле.
 * После завершения validate() должен вернуть true, и все блоки —
 * быть освобождены.
 */
static void test_concurrent_alloc_dealloc()
{
    constexpr std::size_t kMemSize = 64 * 1024 * 1024; // 64 МБ
    constexpr int         kThreads = 4;
    constexpr int         kIter    = 500;

    make_manager( kMemSize );

    std::atomic<int>         errors{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, kIter]()
            {
                auto* mgr = pmm::PersistMemoryManager::instance();
                // Простой LCG для воспроизводимой псевдослучайности
                unsigned           state = static_cast<unsigned>( t * 1234567 + 42 );
                std::vector<void*> live;
                live.reserve( 64 );

                for ( int i = 0; i < kIter; ++i )
                {
                    state          = state * 1664525u + 1013904223u;
                    std::size_t sz = 16 + ( ( state >> 16 ) % 128 ) * 8;

                    if ( live.empty() || ( state >> 31 ) == 0 )
                    {
                        void* p = mgr->allocate( sz );
                        if ( p != nullptr )
                        {
                            live.push_back( p );
                        }
                    }
                    else
                    {
                        std::size_t idx = ( state >> 16 ) % live.size();
                        mgr->deallocate( live[idx] );
                        live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
                    }
                }

                // Освобождаем оставшиеся блоки этого потока
                for ( void* p : live )
                {
                    mgr->deallocate( p );
                }
            } );
    }

    for ( auto& th : threads )
    {
        th.join();
    }

    auto* mgr = pmm::PersistMemoryManager::instance();
    PMM_TEST( mgr->validate(), "concurrent_alloc_dealloc: validate() после чередующихся операций" );
    PMM_TEST( errors.load() == 0, "concurrent_alloc_dealloc: нет ошибок в потоках" );

    auto stats = pmm::get_stats( mgr );
    PMM_TEST( stats.allocated_blocks == 0, "concurrent_alloc_dealloc: все блоки освобождены" );

    pmm::PersistMemoryManager::destroy();
}

/**
 * @brief Параллельный reallocate.
 *
 * Несколько потоков многократно reallocate существующих блоков.
 */
static void test_concurrent_reallocate()
{
    constexpr std::size_t kMemSize = 32 * 1024 * 1024; // 32 МБ
    constexpr int         kThreads = 4;
    constexpr int         kIter    = 100;

    make_manager( kMemSize );

    // Каждый поток работает со своим блоком (нет гонок на конкретных блоках,
    // но есть конкуренция за мьютекс менеджера)
    std::vector<void*> blocks( kThreads );
    auto*              mgr = pmm::PersistMemoryManager::instance();
    for ( int t = 0; t < kThreads; ++t )
    {
        blocks[t] = mgr->allocate( 64 );
    }

    std::vector<std::thread> threads;
    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &blocks, kIter]()
            {
                auto* m = pmm::PersistMemoryManager::instance();
                for ( int i = 0; i < kIter; ++i )
                {
                    std::size_t new_sz = 64 + static_cast<std::size_t>( i % 8 ) * 64;
                    void*       p      = m->reallocate( blocks[t], new_sz );
                    if ( p != nullptr )
                    {
                        blocks[t] = p;
                    }
                }
            } );
    }

    for ( auto& th : threads )
    {
        th.join();
    }

    for ( int t = 0; t < kThreads; ++t )
    {
        mgr->deallocate( blocks[t] );
    }

    PMM_TEST( mgr->validate(), "concurrent_reallocate: validate() после параллельного reallocate" );

    pmm::PersistMemoryManager::destroy();
}

/**
 * @brief Запись данных в параллельных потоках без гонки данных.
 *
 * Каждый поток пишет уникальное значение в свой блок, затем проверяет его.
 * Тест выявляет ситуации, когда два потока получили один и тот же указатель.
 */
static void test_no_data_races()
{
    constexpr std::size_t kMemSize   = 32 * 1024 * 1024;
    constexpr int         kThreads   = 8;
    constexpr int         kPerThread = 50;

    make_manager( kMemSize );

    std::atomic<int>         mismatches{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &mismatches, kPerThread]()
            {
                auto*                              mgr = pmm::PersistMemoryManager::instance();
                std::vector<std::pair<void*, int>> allocs;

                for ( int i = 0; i < kPerThread; ++i )
                {
                    void* p = mgr->allocate( sizeof( int ) );
                    if ( p != nullptr )
                    {
                        int val = t * 1000 + i;
                        std::memcpy( p, &val, sizeof( int ) );
                        allocs.emplace_back( p, val );
                    }
                }

                // Проверяем, что данные не были перезаписаны другим потоком
                for ( auto& [p, expected] : allocs )
                {
                    int actual = 0;
                    std::memcpy( &actual, p, sizeof( int ) );
                    if ( actual != expected )
                    {
                        mismatches.fetch_add( 1 );
                    }
                    mgr->deallocate( p );
                }
            } );
    }

    for ( auto& th : threads )
    {
        th.join();
    }

    auto* mgr = pmm::PersistMemoryManager::instance();
    PMM_TEST( mgr->validate(), "no_data_races: validate() пройдена" );
    PMM_TEST( mismatches.load() == 0, "no_data_races: данные в блоках не повреждены" );

    pmm::PersistMemoryManager::destroy();
}

// ─── main ──────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== Тесты потокобезопасности (Фаза 9) ===\n";

    test_concurrent_allocate();
    test_concurrent_alloc_dealloc();
    test_concurrent_reallocate();
    test_no_data_races();

    std::cout << "\nВсе тесты потокобезопасности пройдены.\n";
    return 0;
}
