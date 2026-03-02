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
static void make_manager( std::size_t size )
{
    void* mem = std::malloc( size );
    pmm::PersistMemoryManager::create( mem, size );
}

// ─── Тесты ────────────────────────────────────────────────────────────────

/**
 * @brief Параллельное выделение памяти из нескольких потоков.
 */
static void test_concurrent_allocate()
{
    constexpr std::size_t kMemSize   = 32 * 1024 * 1024; // 32 МБ
    constexpr int         kThreads   = 4;
    constexpr int         kPerThread = 200;
    constexpr std::size_t kBlockSize = 64;

    make_manager( kMemSize );

    std::vector<std::vector<pmm::pptr<std::uint8_t>>> results( kThreads );
    std::vector<std::thread>                          threads;

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &results, kPerThread, kBlockSize]()
            {
                for ( int i = 0; i < kPerThread; ++i )
                {
                    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( kBlockSize );
                    if ( !p.is_null() )
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
    for ( auto& vec : results )
    {
        for ( auto& p : vec )
        {
            pmm::PersistMemoryManager::deallocate_typed( p );
        }
    }

    // Проверяем целостность
    PMM_TEST( pmm::PersistMemoryManager::validate(), "concurrent_allocate: validate() после параллельных аллокаций" );

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
                unsigned                             state = static_cast<unsigned>( t * 1234567 + 42 );
                std::vector<pmm::pptr<std::uint8_t>> live;
                live.reserve( 64 );

                for ( int i = 0; i < kIter; ++i )
                {
                    state          = state * 1664525u + 1013904223u;
                    std::size_t sz = 16 + ( ( state >> 16 ) % 128 ) * 8;

                    if ( live.empty() || ( state >> 31 ) == 0 )
                    {
                        pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( sz );
                        if ( !p.is_null() )
                        {
                            live.push_back( p );
                        }
                    }
                    else
                    {
                        std::size_t idx = ( state >> 16 ) % live.size();
                        pmm::PersistMemoryManager::deallocate_typed( live[idx] );
                        live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
                    }
                }

                for ( auto& p : live )
                {
                    pmm::PersistMemoryManager::deallocate_typed( p );
                }
            } );
    }

    for ( auto& th : threads )
    {
        th.join();
    }

    PMM_TEST( pmm::PersistMemoryManager::validate(),
              "concurrent_alloc_dealloc: validate() после чередующихся операций" );
    PMM_TEST( errors.load() == 0, "concurrent_alloc_dealloc: нет ошибок в потоках" );

    auto stats = pmm::get_stats();
    // Issue #75: BlockHeader_0 (ManagerHeader) always allocated
    PMM_TEST( stats.allocated_blocks == 1, "concurrent_alloc_dealloc: все блоки освобождены" );

    pmm::PersistMemoryManager::destroy();
}

/**
 * @brief Параллельный reallocate.
 */
static void test_concurrent_reallocate()
{
    constexpr std::size_t kMemSize = 32 * 1024 * 1024; // 32 МБ
    constexpr int         kThreads = 4;
    constexpr int         kIter    = 100;

    make_manager( kMemSize );

    std::vector<pmm::pptr<std::uint8_t>> blocks( kThreads );
    for ( int t = 0; t < kThreads; ++t )
    {
        blocks[t] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 64 );
    }

    std::vector<std::thread> threads;
    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [t, &blocks, kIter]()
            {
                for ( int i = 0; i < kIter; ++i )
                {
                    std::size_t             new_sz = 64 + static_cast<std::size_t>( i % 8 ) * 64;
                    pmm::pptr<std::uint8_t> p      = pmm::PersistMemoryManager::reallocate_typed( blocks[t], new_sz );
                    if ( !p.is_null() )
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
        if ( !blocks[t].is_null() )
            pmm::PersistMemoryManager::deallocate_typed( blocks[t] );
    }

    PMM_TEST( pmm::PersistMemoryManager::validate(),
              "concurrent_reallocate: validate() после параллельного reallocate" );

    pmm::PersistMemoryManager::destroy();
}

/**
 * @brief Запись данных в параллельных потоках без гонки данных.
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
                std::vector<std::pair<pmm::pptr<std::uint8_t>, int>> allocs;

                for ( int i = 0; i < kPerThread; ++i )
                {
                    pmm::pptr<std::uint8_t> p =
                        pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( sizeof( int ) );
                    if ( !p.is_null() )
                    {
                        int val = t * 1000 + i;
                        std::memcpy( p.get(), &val, sizeof( int ) );
                        allocs.emplace_back( p, val );
                    }
                }

                for ( auto& [p, expected] : allocs )
                {
                    int actual = 0;
                    std::memcpy( &actual, p.get(), sizeof( int ) );
                    if ( actual != expected )
                    {
                        mismatches.fetch_add( 1 );
                    }
                    pmm::PersistMemoryManager::deallocate_typed( p );
                }
            } );
    }

    for ( auto& th : threads )
    {
        th.join();
    }

    PMM_TEST( pmm::PersistMemoryManager::validate(), "no_data_races: validate() пройдена" );
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
