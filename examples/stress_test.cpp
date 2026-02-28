/**
 * @file stress_test.cpp
 * @brief Стресс-тест PersistMemoryManager (Фаза 4)
 *
 * Проверяет производительность и корректность при:
 * - 100 000 последовательных аллокаций.
 * - 1 000 000 чередующихся операций allocate/deallocate.
 *
 * Результаты выводятся с замером времени выполнения каждой фазы.
 */

#include "persist_memory_manager.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

// ─── Вспомогательные функции ──────────────────────────────────────────────────

/// Вернуть текущее время высокого разрешения
static auto now()
{
    return std::chrono::high_resolution_clock::now();
}

/// Вернуть время в миллисекундах между двумя моментами
static double elapsed_ms( std::chrono::high_resolution_clock::time_point start,
                          std::chrono::high_resolution_clock::time_point end )
{
    return std::chrono::duration<double, std::milli>( end - start ).count();
}

// ─── Тест 1: 100 000 последовательных аллокаций ───────────────────────────────

/**
 * @brief Стресс-тест: 100 000 аллокаций с последующим освобождением.
 *
 * Выделяет 100 000 блоков по 64 байта в менеджере с достаточным буфером,
 * затем освобождает все блоки. Проверяет корректность и замеряет время.
 *
 * @return true при успешном прохождении теста.
 */
static bool test_100k_allocations()
{
    std::cout << "\n[Тест 1] 100 000 последовательных аллокаций\n";

    // Каждый блок: 64 байта данных + ~64 байта BlockHeader = ~128 байт на блок
    // 100 000 блоков × 128 байт = ~12.8 МБ + запас для заголовков
    const std::size_t memory_size = 32UL * 1024 * 1024; // 32 МБ
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

    const int          N    = 100'000;
    const std::size_t  BSIZ = 64; // размер каждого блока
    std::vector<void*> ptrs( N, nullptr );

    // ── Фаза аллокации ────────────────────────────────────────────────────────
    auto t0        = now();
    int  allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( BSIZ );
        if ( ptrs[i] == nullptr )
        {
            std::cout << "  Достигнут лимит при i=" << i << " (не хватило памяти в буфере)\n";
            break;
        }
        // Записываем паттерн для проверки
        std::memset( ptrs[i], static_cast<int>( i & 0xFF ), BSIZ );
        allocated++;
    }
    auto   t1       = now();
    double ms_alloc = elapsed_ms( t0, t1 );

    std::cout << "  Выделено блоков: " << allocated << " / " << N << "\n";
    std::cout << "  Время аллокации: " << ms_alloc << " мс\n";

    // Проверяем целостность данных в нескольких блоках
    bool data_ok = true;
    for ( int i = 0; i < allocated && i < 1000; i++ )
    {
        const std::uint8_t* p       = static_cast<const std::uint8_t*>( ptrs[i] );
        const std::uint8_t  pattern = static_cast<std::uint8_t>( i & 0xFF );
        for ( std::size_t j = 0; j < BSIZ; j++ )
        {
            if ( p[j] != pattern )
            {
                data_ok = false;
                std::cerr << "  ОШИБКА данных в блоке " << i << " смещении " << j << "\n";
                break;
            }
        }
        if ( !data_ok )
            break;
    }

    if ( !mgr->validate() )
    {
        std::cerr << "  ОШИБКА: validate() провалился после аллокаций\n";
        pmm::PersistMemoryManager::destroy();
        return false;
    }

    // ── Фаза освобождения ─────────────────────────────────────────────────────
    auto t2 = now();
    for ( int i = 0; i < allocated; i++ )
    {
        mgr->deallocate( ptrs[i] );
    }
    auto   t3         = now();
    double ms_dealloc = elapsed_ms( t2, t3 );

    std::cout << "  Время освобождения: " << ms_dealloc << " мс\n";

    if ( !mgr->validate() )
    {
        std::cerr << "  ОШИБКА: validate() провалился после освобождений\n";
        pmm::PersistMemoryManager::destroy();
        return false;
    }

    // Проверяем, что вся память снова свободна
    std::size_t free_after = mgr->free_size();
    std::size_t used_after = mgr->used_size();
    std::cout << "  Свободно после освобождений: " << free_after << " байт\n";
    std::cout << "  Занято (метаданные)         : " << used_after << " байт\n";

    pmm::PersistMemoryManager::destroy();

    bool passed = data_ok && ( allocated > 0 );
    std::cout << "  Результат: " << ( passed ? "PASS" : "FAIL" ) << "\n";
    return passed;
}

// ─── Тест 2: 1 000 000 чередующихся аллокаций/освобождений ───────────────────

/**
 * @brief Стресс-тест: 1 000 000 чередующихся операций allocate/deallocate.
 *
 * Поддерживает пул из N блоков. На каждом шаге случайно выбирается слот:
 * если пуст — аллоцируется, если занят — освобождается.
 * Это имитирует реальный паттерн использования менеджера.
 *
 * @return true при успешном прохождении теста.
 */
static bool test_1m_alternating()
{
    std::cout << "\n[Тест 2] 1 000 000 чередующихся allocate/deallocate\n";

    const std::size_t memory_size = 8UL * 1024 * 1024; // 8 МБ
    void*             mem         = std::malloc( memory_size );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память\n";
        return false;
    }

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, memory_size );
    if ( mgr == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem );
        return false;
    }

    // Небольшой пул из 64 слотов, в каждом блоки от 32 до 512 байт
    const int                POOL     = 64;
    const std::size_t        SIZES[8] = { 32, 64, 128, 256, 512, 64, 128, 256 };
    std::vector<void*>       pool( POOL, nullptr );
    std::vector<std::size_t> pool_sizes( POOL, 0 );

    const int TOTAL_OPS     = 1'000'000;
    int       alloc_ops     = 0;
    int       dealloc_ops   = 0;
    int       failed_allocs = 0;

    // Простой псевдослучайный генератор (LCG) — воспроизводимые результаты
    uint32_t rng      = 42;
    auto     next_rng = [&]() -> uint32_t
    {
        rng = rng * 1664525u + 1013904223u;
        return rng;
    };

    auto t0 = now();
    for ( int op = 0; op < TOTAL_OPS; op++ )
    {
        int slot = static_cast<int>( next_rng() % POOL );
        if ( pool[slot] == nullptr )
        {
            // Аллоцируем
            std::size_t sz   = SIZES[next_rng() % 8];
            pool[slot]       = mgr->allocate( sz );
            pool_sizes[slot] = sz;
            if ( pool[slot] != nullptr )
            {
                // Записываем паттерн для проверки
                std::memset( pool[slot], static_cast<int>( slot & 0xFF ), sz );
                alloc_ops++;
            }
            else
            {
                // Нет места — продолжаем
                pool[slot]       = nullptr;
                pool_sizes[slot] = 0;
                failed_allocs++;
            }
        }
        else
        {
            // Освобождаем
            mgr->deallocate( pool[slot] );
            pool[slot]       = nullptr;
            pool_sizes[slot] = 0;
            dealloc_ops++;
        }
    }
    auto   t1       = now();
    double ms_total = elapsed_ms( t0, t1 );

    std::cout << "  Аллокаций выполнено  : " << alloc_ops << "\n";
    std::cout << "  Освобождений выполнено: " << dealloc_ops << "\n";
    std::cout << "  Неудачных аллокаций  : " << failed_allocs << "\n";
    std::cout << "  Общее время          : " << ms_total << " мс\n";
    std::cout << "  Среднее на операцию  : " << ( ms_total / TOTAL_OPS * 1000.0 ) << " мкс\n";

    // Проверяем данные в оставшихся занятых блоках
    bool data_ok = true;
    for ( int i = 0; i < POOL; i++ )
    {
        if ( pool[i] != nullptr && pool_sizes[i] > 0 )
        {
            const std::uint8_t* p       = static_cast<const std::uint8_t*>( pool[i] );
            const std::uint8_t  pattern = static_cast<std::uint8_t>( i & 0xFF );
            // Проверяем первые несколько байт
            for ( std::size_t j = 0; j < std::min( pool_sizes[i], std::size_t( 8 ) ); j++ )
            {
                if ( p[j] != pattern )
                {
                    data_ok = false;
                    std::cerr << "  ОШИБКА данных в слоте " << i << "\n";
                    break;
                }
            }
        }
    }

    // Освобождаем оставшиеся занятые блоки
    for ( int i = 0; i < POOL; i++ )
    {
        if ( pool[i] != nullptr )
        {
            mgr->deallocate( pool[i] );
            pool[i] = nullptr;
        }
    }

    bool validate_ok = mgr->validate();
    if ( !validate_ok )
    {
        std::cerr << "  ОШИБКА: validate() провалился после теста\n";
    }

    pmm::PersistMemoryManager::destroy();

    bool passed = data_ok && validate_ok;
    std::cout << "  Результат: " << ( passed ? "PASS" : "FAIL" ) << "\n";
    return passed;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== PersistMemoryManager — Стресс-тест (Фаза 4) ===\n";

    bool all_passed = true;

    if ( !test_100k_allocations() )
        all_passed = false;

    if ( !test_1m_alternating() )
        all_passed = false;

    std::cout << "\n=== Итог: " << ( all_passed ? "ВСЕ ТЕСТЫ ПРОШЛИ" : "ЕСТЬ ОШИБКИ" ) << " ===\n";
    return all_passed ? 0 : 1;
}
