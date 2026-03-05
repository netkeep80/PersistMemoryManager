/**
 * @file test_shared_mutex.cpp
 * @brief Тесты разделённых блокировок PersistMemoryManager (Фаза 10).
 *
 * Проверяет корректность shared_mutex: параллельные читатели не блокируют
 * друг друга, а запись блокирует чтение. Дополнительно проверяется
 * корректность refactored reallocate() с unlock-перед-subvызовом.
 */

#include "pmm/legacy_manager.h"

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

static void make_manager( std::size_t size )
{
    void* mem = std::malloc( size );
    pmm::PersistMemoryManager<>::create( mem, size );
}

// ─── Тесты ─────────────────────────────────────────────────────────────────

/**
 * @brief Параллельный вызов validate() из нескольких потоков.
 */
static void test_concurrent_validate()
{
    constexpr std::size_t kMemSize = 4 * 1024 * 1024;
    constexpr int         kThreads = 8;
    constexpr int         kIter    = 100;

    make_manager( kMemSize );

    // Выделяем несколько блоков для нетривиального состояния
    for ( int i = 0; i < 20; ++i )
    {
        pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
    }

    std::atomic<int>         failures{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [&failures, kIter]()
            {
                for ( int i = 0; i < kIter; ++i )
                {
                    if ( !pmm::PersistMemoryManager<>::validate() )
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

    pmm::PersistMemoryManager<>::destroy();
}

/**
 * @brief Параллельный вызов validate() и allocate().
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
                while ( !stop.load() )
                {
                    if ( !pmm::PersistMemoryManager<>::validate() )
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
                std::vector<pmm::pptr<std::uint8_t>> ptrs;
                ptrs.reserve( 32 );
                for ( int i = 0; i < kIter; ++i )
                {
                    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
                    if ( !p.is_null() )
                    {
                        ptrs.push_back( p );
                    }
                    if ( ptrs.size() > 16 )
                    {
                        pmm::PersistMemoryManager<>::deallocate_typed( ptrs.front() );
                        ptrs.erase( ptrs.begin() );
                    }
                }
                for ( auto& p : ptrs )
                {
                    pmm::PersistMemoryManager<>::deallocate_typed( p );
                }
            } );
    }

    for ( int t = kReadThreads; t < kReadThreads + kWriteThreads; ++t )
    {
        threads[t].join();
    }
    stop.store( true );

    for ( int t = 0; t < kReadThreads; ++t )
    {
        threads[t].join();
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate(), "readers_writers: validate() после смешанных операций" );
    PMM_TEST( invalid_reads.load() == 0, "readers_writers: читатели не видели инвалидного состояния" );

    pmm::PersistMemoryManager<>::destroy();
}

/**
 * @brief Корректность reallocate() после перехода к unlock-before-subvызов.
 */
static void test_reallocate_correctness()
{
    constexpr std::size_t kMemSize = 16 * 1024 * 1024;
    constexpr int         kThreads = 4;
    constexpr int         kIter    = 200;

    make_manager( kMemSize );

    std::vector<pmm::pptr<std::uint8_t>> ptrs( kThreads );
    for ( int t = 0; t < kThreads; ++t )
    {
        ptrs[t] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
        std::memset( ptrs[t].get(), t + 1, 64 );
    }

    std::atomic<int>         corrupted{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &ptrs, &corrupted, kIter]()
            {
                for ( int i = 0; i < kIter; ++i )
                {
                    std::size_t             new_sz = 64 + static_cast<std::size_t>( ( i % 4 ) + 1 ) * 64;
                    pmm::pptr<std::uint8_t> p      = pmm::PersistMemoryManager<>::reallocate_typed( ptrs[t], new_sz );
                    if ( !p.is_null() )
                    {
                        unsigned char first = *p.get();
                        if ( first != static_cast<unsigned char>( t + 1 ) && i == 0 )
                        {
                            corrupted.fetch_add( 1 );
                        }
                        std::memset( p.get(), t + 1, 64 );
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
        if ( !ptrs[t].is_null() )
        {
            pmm::PersistMemoryManager<>::deallocate_typed( ptrs[t] );
        }
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate(), "reallocate_correctness: validate() пройдена" );
    PMM_TEST( corrupted.load() == 0, "reallocate_correctness: данные не повреждены при reallocate" );

    pmm::PersistMemoryManager<>::destroy();
}

/**
 * @brief Параллельный вызов get_stats() из нескольких потоков.
 */
static void test_concurrent_get_stats()
{
    constexpr std::size_t kMemSize = 8 * 1024 * 1024;
    constexpr int         kThreads = 6;
    constexpr int         kIter    = 100;

    make_manager( kMemSize );

    for ( int i = 0; i < 30; ++i )
    {
        pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    }

    std::atomic<int>         inconsistent{ 0 };
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [&inconsistent, kIter]()
            {
                for ( int i = 0; i < kIter; ++i )
                {
                    auto stats = pmm::get_stats();
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

    pmm::PersistMemoryManager<>::destroy();
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
