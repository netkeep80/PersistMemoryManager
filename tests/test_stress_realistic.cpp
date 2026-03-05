/**
 * @file test_stress_realistic.cpp
 * @brief Реалистичный стресс-тест PersistMemoryManager (Issue #20)
 *
 * Тест симулирует реальный жизненный цикл использования менеджера памяти:
 *   Фаза 0: Создание PHASE0_ITERS начальных блоков (разогрев, заполнение).
 *   Фаза 1: PHASE1_ITERS итераций, 66% вероятность аллокации, 33% освобождения.
 *   Фаза 2: PHASE2_ITERS итераций, 50% аллокация / 50% освобождение.
 *   Фаза 3: 66% вероятность освобождения, 33% аллокации, до полного
 *            освобождения всей памяти.
 *
 * Iteration counts are reduced in Debug/coverage builds to avoid OOM in CI.
 */

#include "pmm/legacy_manager.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

// Reduce iteration counts in Debug/coverage builds to avoid OOM kills in CI.
#if defined( NDEBUG )
static constexpr int kPhase0Iters = 1000000;
static constexpr int kPhase1Iters = 1000000;
static constexpr int kPhase2Iters = 1000000;
#else
static constexpr int kPhase0Iters = 50000;
static constexpr int kPhase1Iters = 50000;
static constexpr int kPhase2Iters = 50000;
#endif

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

namespace
{

struct Rng
{
    uint32_t state;

    explicit Rng( uint32_t seed = 42 ) : state( seed ) {}

    uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    uint32_t next_n( uint32_t n ) { return ( next() >> 16 ) % n; }

    std::size_t next_block_size() { return static_cast<std::size_t>( ( next_n( 512 ) + 1 ) * 8 ); }
};

} // namespace

// ─── Основной реалистичный стресс-тест ────────────────────────────────────────

static bool test_stress_realistic()
{
    const std::size_t memory_size = 64UL * 1024 * 1024; // 64 МБ
    void*             mem         = std::malloc( memory_size );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память (" << memory_size / 1024 / 1024 << " МБ)\n";
        return false;
    }

    if ( !pmm::PersistMemoryManager<>::create( mem, memory_size ) )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem );
        return false;
    }

    Rng rng( 12345 );

    std::vector<pmm::pptr<std::uint8_t>> live;
    live.reserve( static_cast<std::size_t>( kPhase0Iters + kPhase1Iters ) );

    auto total_start = now();

    // ── Фаза 0: начальные аллокации ────────────────────────────────────────────
    {
        std::cout << "  Фаза 0: создание " << kPhase0Iters << " начальных блоков...\n";
        auto t0     = now();
        int  failed = 0;
        for ( int i = 0; i < kPhase0Iters; i++ )
        {
            std::size_t             sz  = rng.next_block_size();
            pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( sz );
            if ( !ptr.is_null() )
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
        std::cout << "    Выделено: " << live.size() << " / " << kPhase0Iters << "  неудачно: " << failed
                  << "  время: " << ms << " мс\n";

        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    }

    // ── Фаза 1: итерации, 66% аллокация / 33% освобождение ─────────────────────
    {
        std::cout << "  Фаза 1: " << kPhase1Iters << " итераций (66% alloc / 33% free)...\n";
        auto   t0          = now();
        int    alloc_ok    = 0;
        int    alloc_fail  = 0;
        int    dealloc_cnt = 0;
        size_t start_live  = live.size();

        for ( int i = 0; i < kPhase1Iters; i++ )
        {
            if ( rng.next_n( 3 ) < 2 )
            {
                std::size_t             sz  = rng.next_block_size();
                pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( sz );
                if ( !ptr.is_null() )
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
                if ( !live.empty() )
                {
                    uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
                    pmm::PersistMemoryManager<>::deallocate_typed( live[idx] );
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

        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
        PMM_TEST( live.size() > start_live );
    }

    // ── Фаза 2: итерации, 50% аллокация / 50% освобождение ─────────────────────
    {
        std::cout << "  Фаза 2: " << kPhase2Iters << " итераций (50% alloc / 50% free)...\n";
        auto t0          = now();
        int  alloc_ok    = 0;
        int  alloc_fail  = 0;
        int  dealloc_cnt = 0;

        for ( int i = 0; i < kPhase2Iters; i++ )
        {
            if ( rng.next_n( 2 ) == 0 )
            {
                std::size_t             sz  = rng.next_block_size();
                pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( sz );
                if ( !ptr.is_null() )
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
                if ( !live.empty() )
                {
                    uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
                    pmm::PersistMemoryManager<>::deallocate_typed( live[idx] );
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

        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
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
                std::size_t             sz  = rng.next_block_size();
                pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( sz );
                if ( !ptr.is_null() )
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
                if ( !live.empty() )
                {
                    uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
                    pmm::PersistMemoryManager<>::deallocate_typed( live[idx] );
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
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );

        auto stats = pmm::get_stats();
        PMM_TEST( stats.allocated_blocks == 1 ); // Issue #75: BlockHeader_0 always allocated
    }

    double total_ms = elapsed_ms( total_start, now() );
    std::cout << "  Общее время: " << total_ms << " мс\n";

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
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
