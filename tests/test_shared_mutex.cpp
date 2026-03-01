/**
 * @file test_shared_mutex.cpp
 * @brief Тесты разделённых блокировок PersistMemoryManager (Фаза 10).
 *
 * Проверяет корректность shared_mutex: параллельные читатели не блокируют
 * друг друга, а запись блокирует чтение. Дополнительно проверяется
 * корректность refactored reallocate() с unlock-перед-subvызовом.
 */

#include "persist_memory_manager.h"

#include <atomic>
#include <chrono>
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

static pmm::PersistMemoryManager* make_manager( std::size_t size )
{
    void* mem = std::malloc( size );
    return pmm::PersistMemoryManager::create( mem, size );
}

// ─── Тесты ─────────────────────────────────────────────────────────────────

/**
 * @brief Параллельный вызов validate() из нескольких потоков.
 *
 * Все потоки одновременно вызывают validate() (shared_lock). Ни один поток
 * не должен заблокироваться другим. Все вызовы должны вернуть true.
 */
static void test_concurrent_validate()
{
    constexpr std::size_t kMemSize = 4 * 1024 * 1024;
    constexpr int         kThreads = 8;
    constexpr int         kIter    = 100;

    make_manager( kMemSize );
    auto* mgr = pmm::PersistMemoryManager::instance();

    // Выделяем несколько блоков для нетривиального состояния
    for ( int i = 0; i < 20; ++i )
    {
        mgr->allocate( 64 );
    }

    std::atomic<int>         failures{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [&failures, kIter]()
            {
                auto* m = pmm::PersistMemoryManager::instance();
                for ( int i = 0; i < kIter; ++i )
                {
                    if ( !m->validate() )
                    {
                        failures.fetch_add( 1 );
                    }
                }
            } );
    }

    for ( auto& th : threads )
    {
        th.join();
    }

    PMM_TEST( failures.load() == 0, "concurrent_validate: все validate() вернули true" );

    pmm::PersistMemoryManager::destroy();
}

/**
 * @brief Параллельный вызов validate() и allocate().
 *
 * Часть потоков читает (validate), часть пишет (allocate/deallocate).
 * Проверяем целостность после завершения.
 */
static void test_readers_writers()
{
    constexpr std::size_t kMemSize      = 32 * 1024 * 1024;
    constexpr int         kReadThreads  = 4;
    constexpr int         kWriteThreads = 2;
    constexpr int         kIter         = 200;

    make_manager( kMemSize );

    std::atomic<int>         invalid_reads{ 0 };
    std::atomic<bool>        stop{ false };
    std::vector<std::thread> threads;

    // Читатели: непрерывно вызывают validate()
    for ( int t = 0; t < kReadThreads; ++t )
    {
        threads.emplace_back(
            [&invalid_reads, &stop]()
            {
                auto* m = pmm::PersistMemoryManager::instance();
                while ( !stop.load() )
                {
                    if ( !m->validate() )
                    {
                        invalid_reads.fetch_add( 1 );
                    }
                }
            } );
    }

    // Писатели: выделяют и освобождают блоки
    for ( int t = 0; t < kWriteThreads; ++t )
    {
        threads.emplace_back(
            [kIter]()
            {
                auto*              m = pmm::PersistMemoryManager::instance();
                std::vector<void*> ptrs;
                ptrs.reserve( 32 );
                for ( int i = 0; i < kIter; ++i )
                {
                    void* p = m->allocate( 128 );
                    if ( p != nullptr )
                    {
                        ptrs.push_back( p );
                    }
                    if ( ptrs.size() > 16 )
                    {
                        m->deallocate( ptrs.front() );
                        ptrs.erase( ptrs.begin() );
                    }
                }
                for ( void* p : ptrs )
                {
                    m->deallocate( p );
                }
            } );
    }

    // Ждём завершения писателей (последние kWriteThreads потоков)
    for ( int t = kReadThreads; t < kReadThreads + kWriteThreads; ++t )
    {
        threads[t].join();
    }
    stop.store( true );

    // Ждём завершения читателей
    for ( int t = 0; t < kReadThreads; ++t )
    {
        threads[t].join();
    }

    auto* mgr = pmm::PersistMemoryManager::instance();
    PMM_TEST( mgr->validate(), "readers_writers: validate() после смешанных операций" );
    PMM_TEST( invalid_reads.load() == 0, "readers_writers: читатели не видели инвалидного состояния" );

    pmm::PersistMemoryManager::destroy();
}

/**
 * @brief Корректность reallocate() после перехода к unlock-before-subvызов.
 *
 * Многократный reallocate в нескольких потоках. Проверяем, что данные
 * не портятся и validate() проходит.
 */
static void test_reallocate_correctness()
{
    constexpr std::size_t kMemSize = 16 * 1024 * 1024;
    constexpr int         kThreads = 4;
    constexpr int         kIter    = 200;

    make_manager( kMemSize );
    auto*              mgr = pmm::PersistMemoryManager::instance();
    std::vector<void*> ptrs( kThreads );
    for ( int t = 0; t < kThreads; ++t )
    {
        ptrs[t] = mgr->allocate( 64 );
        // Записываем уникальный маркер
        std::memset( ptrs[t], t + 1, 64 );
    }

    std::atomic<int>         corrupted{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &ptrs, &corrupted, kIter]()
            {
                auto* m = pmm::PersistMemoryManager::instance();
                for ( int i = 0; i < kIter; ++i )
                {
                    std::size_t new_sz = 64 + static_cast<std::size_t>( ( i % 4 ) + 1 ) * 64;
                    void*       p      = m->reallocate( ptrs[t], new_sz );
                    if ( p != nullptr )
                    {
                        // Проверяем, что первый байт не изменился (маркер потока)
                        unsigned char first = *static_cast<unsigned char*>( p );
                        if ( first != static_cast<unsigned char>( t + 1 ) && i == 0 )
                        {
                            corrupted.fetch_add( 1 );
                        }
                        // Обновляем маркер для следующей итерации
                        std::memset( p, t + 1, 64 );
                        ptrs[t] = p;
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
        if ( ptrs[t] != nullptr )
        {
            mgr->deallocate( ptrs[t] );
        }
    }

    PMM_TEST( mgr->validate(), "reallocate_correctness: validate() пройдена" );
    PMM_TEST( corrupted.load() == 0, "reallocate_correctness: данные не повреждены при reallocate" );

    pmm::PersistMemoryManager::destroy();
}

/**
 * @brief Параллельный вызов get_stats() из нескольких потоков.
 *
 * get_stats() — read-only операция. Несколько потоков вызывают её одновременно.
 * Проверяем, что возвращаемые значения согласованны (total >= allocated + free).
 */
static void test_concurrent_get_stats()
{
    constexpr std::size_t kMemSize = 8 * 1024 * 1024;
    constexpr int         kThreads = 6;
    constexpr int         kIter    = 100;

    make_manager( kMemSize );
    auto* mgr = pmm::PersistMemoryManager::instance();
    for ( int i = 0; i < 30; ++i )
    {
        mgr->allocate( 256 );
    }

    std::atomic<int>         inconsistent{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [&inconsistent, kIter]()
            {
                auto* m = pmm::PersistMemoryManager::instance();
                for ( int i = 0; i < kIter; ++i )
                {
                    auto stats = pmm::get_stats( m );
                    // Базовая проверка согласованности счётчиков
                    if ( stats.total_blocks != stats.free_blocks + stats.allocated_blocks )
                    {
                        inconsistent.fetch_add( 1 );
                    }
                }
            } );
    }

    for ( auto& th : threads )
    {
        th.join();
    }

    PMM_TEST( inconsistent.load() == 0, "concurrent_get_stats: счётчики согласованны при параллельном чтении" );

    pmm::PersistMemoryManager::destroy();
}

// ─── main ──────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== Тесты разделённых блокировок (Фаза 10) ===\n";

    test_concurrent_validate();
    test_readers_writers();
    test_reallocate_correctness();
    test_concurrent_get_stats();

    std::cout << "\nВсе тесты Фазы 10 пройдены.\n";
    return 0;
}
