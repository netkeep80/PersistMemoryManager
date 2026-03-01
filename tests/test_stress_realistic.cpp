/**
 * @file test_stress_realistic.cpp
 * @brief Реалистичный стресс-тест PersistMemoryManager (Issue #20)
 *
 * Тест симулирует реальный жизненный цикл использования менеджера памяти:
 *   Фаза 0: Создание 100000 начальных блоков (разогрев, заполнение).
 *   Фаза 1: 100000 итераций, 66% вероятность аллокации, 33% освобождения.
 *   Фаза 2: 100000 итераций, 50% аллокация / 50% освобождение.
 *   Фаза 3: 66% вероятность освобождения, 33% аллокации, до полного
 *            освобождения всей памяти.
 *
 * Размеры блоков: от 8 байт до 4 КБ.
 *
 * Примечание: количество итераций в фазах 1 и 2 ограничено 100000
 * вместо требуемых в задаче 100000, поскольку deallocate() имеет
 * сложность O(n) по числу живых блоков. При 100000+ блоках и 100000
 * итерациях суммарная стоимость операций делает тест неприемлемо
 * медленным для CI.
 */

#include "persist_memory_manager.h"

#include <chrono>
#include <cstdlib>
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

// ─── Псевдослучайный генератор (LCG) ──────────────────────────────────────────

struct Rng
{
    uint32_t state;

    explicit Rng( uint32_t seed = 42 ) : state( seed ) {}

    uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    /// Возвращает равномерно распределённое число в [0, n).
    /// Использует старшие 16 бит для избежания короткого периода младших бит LCG.
    uint32_t next_n( uint32_t n ) { return ( next() >> 16 ) % n; }

    /// Возвращает случайный размер блока в диапазоне [8, 4096] байт
    std::size_t next_block_size()
    {
        // Диапазон: 8..4096 байт с шагом 8 (всего 512 вариантов)
        return static_cast<std::size_t>( ( next_n( 512 ) + 1 ) * 8 );
    }
};

// ─── Основной реалистичный стресс-тест ────────────────────────────────────────

/**
 * @brief Реалистичный стресс-тест с четырьмя фазами.
 *
 * Фаза 0: 100000 начальных аллокаций (разогрев).
 * Фаза 1: 100000 итераций, 66% аллокация / 33% освобождение.
 * Фаза 2: 100000 итераций, 50% аллокация / 50% освобождение.
 * Фаза 3: 66% освобождение / 33% аллокация до полного освобождения памяти.
 *
 * @return true при успешном прохождении теста.
 */
static bool test_stress_realistic()
{
    // Достаточно большой буфер для всех фаз теста.
    // 100000 начальных блоков + ~3 300 в пике (Фаза 1) = ~13 300 блоков.
    // Максимальный размер блока 4 КБ + ~64 байт заголовок ≈ 4 КБ.
    // 13 300 × 4 КБ ≈ 53 МБ; берём 64 МБ с запасом.
    const std::size_t memory_size = 64UL * 1024 * 1024; // 64 МБ
    void*             mem         = std::malloc( memory_size );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память (" << memory_size / 1024 / 1024 << " МБ)\n";
        return false;
    }

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, memory_size );
    if ( mgr == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem );
        return false;
    }

    Rng rng( 12345 );

    // Динамический пул указателей на живые блоки
    std::vector<void*> live;
    live.reserve( 2 * 100000 );

    auto total_start = now();

    // ── Фаза 0: 100000 начальных аллокаций ────────────────────────────────────
    {
        std::cout << "  Фаза 0: создание 100000 начальных блоков...\n";
        auto t0     = now();
        int  failed = 0;
        for ( int i = 0; i < 100000; i++ )
        {
            std::size_t sz  = rng.next_block_size();
            void*       ptr = pmm::PersistMemoryManager::instance()->allocate( sz );
            if ( ptr != nullptr )
            {
                live.push_back( ptr );
            }
            else
            {
                failed++;
            }
        }
        auto   t1 = now();
        double ms = elapsed_ms( t0, t1 );
        std::cout << "    Выделено: " << live.size() << " / 100000" << "  неудачно: " << failed << "  время: " << ms
                  << " мс\n";

        PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
    }

    // ── Фаза 1: 100000 итераций, 66% аллокация / 33% освобождение ─────────────
    {
        std::cout << "  Фаза 1: 100000 итераций (66% alloc / 33% free)...\n";
        auto   t0          = now();
        int    alloc_ok    = 0;
        int    alloc_fail  = 0;
        int    dealloc_cnt = 0;
        size_t start_live  = live.size();

        for ( int i = 0; i < 100000; i++ )
        {
            // 66% — аллокация (0..1 из 0..2 → 0 или 1 → аллокация)
            if ( rng.next_n( 3 ) < 2 )
            {
                // Аллокация
                std::size_t sz  = rng.next_block_size();
                void*       ptr = pmm::PersistMemoryManager::instance()->allocate( sz );
                if ( ptr != nullptr )
                {
                    live.push_back( ptr );
                    alloc_ok++;
                }
                else
                {
                    alloc_fail++;
                }
            }
            else
            {
                // Освобождение — только если есть живые блоки
                if ( !live.empty() )
                {
                    uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
                    pmm::PersistMemoryManager::instance()->deallocate( live[idx] );
                    live[idx] = live.back();
                    live.pop_back();
                    dealloc_cnt++;
                }
            }
        }

        auto   t1 = now();
        double ms = elapsed_ms( t0, t1 );
        std::cout << "    Аллокаций: " << alloc_ok << "  неудачно: " << alloc_fail << "  освобождений: " << dealloc_cnt
                  << "\n"
                  << "    Живых блоков: " << start_live << " → " << live.size() << "  время: " << ms << " мс\n";

        PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
        // В Фазе 1 создаётся больше, чем удаляется — live должен вырасти
        PMM_TEST( live.size() > start_live );
    }

    // ── Фаза 2: 100000 итераций, 50% аллокация / 50% освобождение ─────────────
    {
        std::cout << "  Фаза 2: 100000 итераций (50% alloc / 50% free)...\n";
        auto t0          = now();
        int  alloc_ok    = 0;
        int  alloc_fail  = 0;
        int  dealloc_cnt = 0;

        for ( int i = 0; i < 100000; i++ )
        {
            if ( rng.next_n( 2 ) == 0 )
            {
                // Аллокация
                std::size_t sz  = rng.next_block_size();
                void*       ptr = pmm::PersistMemoryManager::instance()->allocate( sz );
                if ( ptr != nullptr )
                {
                    live.push_back( ptr );
                    alloc_ok++;
                }
                else
                {
                    alloc_fail++;
                }
            }
            else
            {
                // Освобождение
                if ( !live.empty() )
                {
                    uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
                    pmm::PersistMemoryManager::instance()->deallocate( live[idx] );
                    live[idx] = live.back();
                    live.pop_back();
                    dealloc_cnt++;
                }
            }
        }

        auto   t1 = now();
        double ms = elapsed_ms( t0, t1 );
        std::cout << "    Аллокаций: " << alloc_ok << "  неудачно: " << alloc_fail << "  освобождений: " << dealloc_cnt
                  << "\n"
                  << "    Живых блоков после фазы: " << live.size() << "  время: " << ms << " мс\n";

        PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
    }

    // ── Фаза 3: 66% освобождение / 33% аллокация, до полного освобождения ─────
    {
        std::cout << "  Фаза 3: 66% free / 33% alloc, до полного освобождения...\n";
        auto t0          = now();
        int  alloc_ok    = 0;
        int  alloc_fail  = 0;
        int  dealloc_cnt = 0;
        int  iterations  = 0;

        while ( !live.empty() )
        {
            iterations++;

            if ( rng.next_n( 3 ) < 1 )
            {
                // 33% — аллокация
                std::size_t sz  = rng.next_block_size();
                void*       ptr = pmm::PersistMemoryManager::instance()->allocate( sz );
                if ( ptr != nullptr )
                {
                    live.push_back( ptr );
                    alloc_ok++;
                }
                else
                {
                    alloc_fail++;
                }
            }
            else
            {
                // 66% — освобождение
                if ( !live.empty() )
                {
                    uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
                    pmm::PersistMemoryManager::instance()->deallocate( live[idx] );
                    live[idx] = live.back();
                    live.pop_back();
                    dealloc_cnt++;
                }
            }
        }

        auto   t1 = now();
        double ms = elapsed_ms( t0, t1 );
        std::cout << "    Итераций: " << iterations << "  аллокаций: " << alloc_ok << "  неудачно: " << alloc_fail
                  << "  освобождений: " << dealloc_cnt << "\n"
                  << "    Живых блоков после фазы: " << live.size() << "  время: " << ms << " мс\n";

        PMM_TEST( live.empty() );
        PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

        // Вся память должна быть освобождена — 0 выделенных блоков
        auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
        PMM_TEST( stats.allocated_blocks == 0 );
    }

    double total_ms = elapsed_ms( total_start, now() );
    std::cout << "  Общее время: " << total_ms << " мс\n";

    pmm::PersistMemoryManager::destroy();
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_stress_realistic (Issue #20) ===\n";
    bool all_passed = true;

    PMM_RUN( "stress realistic", test_stress_realistic );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
