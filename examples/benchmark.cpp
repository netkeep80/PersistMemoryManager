/**
 * @file benchmark.cpp
 * @brief Бенчмарк производительности PersistMemoryManager (Фаза 6)
 *
 * Измеряет производительность allocate/deallocate согласно целевым показателям ТЗ:
 *   - allocate 100K блоков ≤ 100 мс
 *   - deallocate 100K блоков ≤ 100 мс
 *
 * Результаты выводятся в виде таблицы с указанием соответствия целевым показателям.
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

// ─── Бенчмарк 1: 100 000 последовательных аллокаций ──────────────────────────

/**
 * @brief Бенчмарк: 100 000 последовательных аллокаций.
 *
 * Цель: allocate 100K блоков ≤ 100 мс.
 *
 * @return true если целевой показатель достигнут.
 */
static bool bench_100k_alloc()
{
    const std::size_t MEMORY_SIZE = 32UL * 1024 * 1024; // 32 МБ
    const int         N           = 100'000;
    const std::size_t BLOCK_SIZE  = 64; // байт

    void* mem = std::malloc( MEMORY_SIZE );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память\n";
        return false;
    }

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    if ( mgr == nullptr )
    {
        std::free( mem );
        return false;
    }

    std::vector<void*> ptrs( N, nullptr );

    // ── Аллокация ─────────────────────────────────────────────────────────────
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

    // ── Деаллокация ───────────────────────────────────────────────────────────
    auto t2 = now();
    for ( int i = 0; i < allocated; i++ )
    {
        mgr->deallocate( ptrs[i] );
    }
    auto   t3         = now();
    double ms_dealloc = elapsed_ms( t2, t3 );

    bool valid      = mgr->validate();
    bool alloc_ok   = ( ms_alloc <= 100.0 );
    bool dealloc_ok = ( ms_dealloc <= 100.0 );

    std::cout << "  Выделено блоков    : " << allocated << " / " << N << "\n";
    std::cout << "  Время allocate     : " << ms_alloc << " мс" << " [цель ≤ 100 мс: " << ( alloc_ok ? "PASS" : "FAIL" )
              << "]\n";
    std::cout << "  Время deallocate   : " << ms_dealloc << " мс"
              << " [цель ≤ 100 мс: " << ( dealloc_ok ? "PASS" : "FAIL" ) << "]\n";
    std::cout << "  Validate           : " << ( valid ? "OK" : "FAIL" ) << "\n";

    pmm::PersistMemoryManager::destroy();

    return alloc_ok && dealloc_ok && valid && ( allocated == N );
}

// ─── Бенчмарк 2: 100K блоков разного размера ──────────────────────────────────

/**
 * @brief Бенчмарк: 100 000 аллокаций блоков разного размера.
 *
 * Цель: allocate 100K блоков ≤ 100 мс при переменных размерах.
 *
 * @return true если целевой показатель достигнут.
 */
static bool bench_100k_mixed_sizes()
{
    const std::size_t MEMORY_SIZE = 64UL * 1024 * 1024; // 64 МБ
    const int         N           = 100'000;

    void* mem = std::malloc( MEMORY_SIZE );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память\n";
        return false;
    }

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    if ( mgr == nullptr )
    {
        std::free( mem );
        return false;
    }

    // Размеры блоков: 32, 64, 128, 256 байт (по 25K каждый)
    const std::size_t SIZES[4] = { 32, 64, 128, 256 };

    std::vector<void*> ptrs( N, nullptr );

    // ── Аллокация ─────────────────────────────────────────────────────────────
    auto t0        = now();
    int  allocated = 0;
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( SIZES[i % 4] );
        if ( ptrs[i] == nullptr )
            break;
        allocated++;
    }
    auto   t1       = now();
    double ms_alloc = elapsed_ms( t0, t1 );

    // ── Деаллокация ───────────────────────────────────────────────────────────
    auto t2 = now();
    for ( int i = 0; i < allocated; i++ )
    {
        mgr->deallocate( ptrs[i] );
    }
    auto   t3         = now();
    double ms_dealloc = elapsed_ms( t2, t3 );

    bool valid      = mgr->validate();
    bool alloc_ok   = ( ms_alloc <= 100.0 );
    bool dealloc_ok = ( ms_dealloc <= 100.0 );

    std::cout << "  Выделено блоков    : " << allocated << " / " << N << "\n";
    std::cout << "  Время allocate     : " << ms_alloc << " мс" << " [цель ≤ 100 мс: " << ( alloc_ok ? "PASS" : "FAIL" )
              << "]\n";
    std::cout << "  Время deallocate   : " << ms_dealloc << " мс"
              << " [цель ≤ 100 мс: " << ( dealloc_ok ? "PASS" : "FAIL" ) << "]\n";
    std::cout << "  Validate           : " << ( valid ? "OK" : "FAIL" ) << "\n";

    pmm::PersistMemoryManager::destroy();

    return alloc_ok && dealloc_ok && valid && ( allocated == N );
}

// ─── Бенчмарк 3: производительность reallocate ────────────────────────────────

/**
 * @brief Бенчмарк: 10 000 reallocate операций.
 *
 * @return true при успехе.
 */
static bool bench_reallocate()
{
    const std::size_t MEMORY_SIZE = 16UL * 1024 * 1024; // 16 МБ
    const int         N           = 10'000;

    void* mem = std::malloc( MEMORY_SIZE );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память\n";
        return false;
    }

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, MEMORY_SIZE );
    if ( mgr == nullptr )
    {
        std::free( mem );
        return false;
    }

    std::vector<void*> ptrs( N, nullptr );

    // Выделяем начальные блоки
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = mgr->allocate( 64 );
        if ( ptrs[i] != nullptr )
        {
            std::memset( ptrs[i], i & 0xFF, 64 );
        }
    }

    // ── Reallocate (увеличение размера) ───────────────────────────────────────
    auto t0               = now();
    int  realloc_ok_count = 0;
    for ( int i = 0; i < N; i++ )
    {
        if ( ptrs[i] == nullptr )
            continue;
        void* new_ptr = mgr->reallocate( ptrs[i], 128 );
        if ( new_ptr != nullptr )
        {
            ptrs[i] = new_ptr;
            realloc_ok_count++;
        }
    }
    auto   t1 = now();
    double ms = elapsed_ms( t0, t1 );

    bool valid = mgr->validate();

    // Освобождаем
    for ( int i = 0; i < N; i++ )
    {
        if ( ptrs[i] != nullptr )
        {
            mgr->deallocate( ptrs[i] );
            ptrs[i] = nullptr;
        }
    }

    std::cout << "  Realloc выполнено  : " << realloc_ok_count << " / " << N << "\n";
    std::cout << "  Время reallocate   : " << ms << " мс\n";
    std::cout << "  Validate           : " << ( valid ? "OK" : "FAIL" ) << "\n";

    pmm::PersistMemoryManager::destroy();

    return valid;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== PersistMemoryManager — Бенчмарк (Фаза 6) ===\n";
    std::cout << "Целевые показатели: allocate/deallocate 100K блоков ≤ 100 мс\n\n";

    bool all_passed = true;

    std::cout << "[Бенчмарк 1] 100K блоков по 64 байта (последовательно)\n";
    if ( !bench_100k_alloc() )
        all_passed = false;

    std::cout << "\n[Бенчмарк 2] 100K блоков разного размера (32–256 байт)\n";
    if ( !bench_100k_mixed_sizes() )
        all_passed = false;

    std::cout << "\n[Бенчмарк 3] 10K операций reallocate\n";
    if ( !bench_reallocate() )
        all_passed = false;

    std::cout << "\n=== Итог: " << ( all_passed ? "ВСЕ ЦЕЛИ ДОСТИГНУТЫ" : "ЕСТЬ НЕСООТВЕТСТВИЯ" ) << " ===\n";
    return all_passed ? 0 : 1;
}
