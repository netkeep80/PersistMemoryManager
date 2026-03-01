/**
 * @file test_scenarios_issue34.cpp
 * @brief Нагрузочные и стресс-тесты PersistMemoryManager (Issue #34)
 *
 * Реализует три сценария из Issue #34:
 *
 *   Сценарий 1: «Шредер» — Интенсивная фрагментация и слияние (coalesce).
 *   Сценарий 2: «Персистентный цикл» — Целостность Save/Load с pptr.
 *   Сценарий 5: «Марафон» — Долгосрочная стабильность.
 */

#include "persist_memory_io.h"
#include "persist_memory_manager.h"

#include <algorithm>
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

    std::size_t next_block_size_shredder()
    {
        return static_cast<std::size_t>( ( next_n( 128 ) + 1 ) * 32 );
    }

    std::size_t next_block_size_marathon()
    {
        return static_cast<std::size_t>( ( next_n( 512 ) + 1 ) * 8 );
    }
};

} // namespace

// ─── Сценарий 1: «Шредер» ────────────────────────────────────────────────────

static bool test_shredder()
{
    const std::size_t memory_size = 64UL * 1024 * 1024; // 64 МБ
    void*             mem         = std::malloc( memory_size );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память (" << memory_size / 1024 / 1024 << " МБ)\n";
        return false;
    }

    if ( !pmm::PersistMemoryManager::create( mem, memory_size ) )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem );
        return false;
    }

    Rng rng( 31337 );

    // ── Фаза 1: 10 000 аллокаций со случайными размерами ──────────────────────
    {
        std::cout << "  Фаза 1: создание 10 000 блоков со случайными размерами...\n";
    }

    std::vector<pmm::pptr<std::uint8_t>> all_ptrs;
    all_ptrs.reserve( 10000 );

    auto t0     = now();
    int  failed = 0;
    for ( int i = 0; i < 10000; ++i )
    {
        std::size_t             sz  = rng.next_block_size_shredder();
        pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( sz );
        if ( !ptr.is_null() )
        {
            std::memset( ptr.get(), static_cast<int>( i & 0xFF ), sz );
            all_ptrs.push_back( ptr );
        }
        else
        {
            failed++;
        }
    }

    std::cout << "    Выделено: " << all_ptrs.size() << " / 10000" << "  неудачно: " << failed
              << "  время: " << elapsed_ms( t0, now() ) << " мс\n";

    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // Перемешиваем порядок указателей
    for ( std::size_t i = all_ptrs.size() - 1; i > 0; --i )
    {
        uint32_t j = rng.next_n( static_cast<uint32_t>( i + 1 ) );
        std::swap( all_ptrs[i], all_ptrs[j] );
    }

    // ── Фаза 2: случайное освобождение 50% блоков ─────────────────────────────
    {
        std::cout << "  Фаза 2: случайное освобождение 50% блоков...\n";
    }

    std::size_t                          half = all_ptrs.size() / 2;
    std::vector<pmm::pptr<std::uint8_t>> random_half( all_ptrs.begin(), all_ptrs.begin() + static_cast<std::ptrdiff_t>( half ) );
    std::vector<pmm::pptr<std::uint8_t>> sorted_half( all_ptrs.begin() + static_cast<std::ptrdiff_t>( half ), all_ptrs.end() );

    auto t1 = now();
    for ( auto& p : random_half )
    {
        pmm::PersistMemoryManager::deallocate_typed( p );
    }

    std::cout << "    Освобождено: " << random_half.size() << " блоков  время: " << elapsed_ms( t1, now() ) << " мс\n";

    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // ── Фаза 3: проверка фрагментации ─────────────────────────────────────────
    {
        std::cout << "  Фаза 3: фрагментация после случайного освобождения:\n";
        auto stats = pmm::get_stats();
        std::cout << "    Всего блоков: " << stats.total_blocks << "  свободных: " << stats.free_blocks
                  << "  занятых: " << stats.allocated_blocks << "\n";
        std::cout << "    Наибольший свободный: " << stats.largest_free / 1024 << " КБ"
                  << "  фрагментация: " << stats.total_fragmentation / 1024 << " КБ\n";

        PMM_TEST( stats.allocated_blocks == sorted_half.size() );
        PMM_TEST( stats.free_blocks >= 1 );
    }

    // ── Фаза 4: освобождение оставшихся 50% в порядке возрастания адресов ─────
    {
        std::cout << "  Фаза 4: освобождение оставшихся блоков в порядке возрастания адресов...\n";
    }

    // Сортируем по смещению для провоцирования слияния соседей
    std::sort( sorted_half.begin(), sorted_half.end(),
               []( const pmm::pptr<std::uint8_t>& a, const pmm::pptr<std::uint8_t>& b ) {
                   return a.offset() < b.offset();
               } );

    auto t2 = now();
    for ( auto& p : sorted_half )
    {
        pmm::PersistMemoryManager::deallocate_typed( p );
    }

    std::cout << "    Освобождено: " << sorted_half.size() << " блоков  время: " << elapsed_ms( t2, now() ) << " мс\n";

    // ── Фаза 5: финальная валидация ────────────────────────────────────────────
    {
        std::cout << "  Фаза 5: финальная валидация после полного освобождения:\n";

        PMM_TEST( pmm::PersistMemoryManager::validate() );

        auto stats = pmm::get_stats();
        std::cout << "    Всего блоков: " << stats.total_blocks << "  свободных: " << stats.free_blocks
                  << "  занятых: " << stats.allocated_blocks << "\n";
        std::cout << "    Наибольший свободный: " << stats.largest_free / 1024 << " КБ\n";

        PMM_TEST( stats.allocated_blocks == 0 );
        PMM_TEST( stats.free_blocks <= 10 );
        PMM_TEST( stats.largest_free > memory_size / 2 );
    }

    double total_ms = elapsed_ms( t0, now() );
    std::cout << "  Общее время: " << total_ms << " мс\n";

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

// ─── Сценарий 2: «Персистентный цикл» ────────────────────────────────────────

struct Node
{
    int             id;
    pmm::pptr<Node> next;
    unsigned int    checksum;
};

static unsigned int compute_checksum( int id, std::uint32_t next_offset )
{
    return static_cast<unsigned int>( id * 2654435761u ) ^ static_cast<unsigned int>( next_offset );
}

static bool test_persistent_cycle()
{
    const std::size_t memory_size = 4UL * 1024 * 1024;
    const char*       filename    = "test_issue34_heap.dat";
    const int         node_count  = 1000;

    // ── Фаза 1: построение связного списка ────────────────────────────────────
    {
        std::cout << "  Фаза 1: построение связного списка из " << node_count << " узлов...\n";
    }

    void* mem1 = std::malloc( memory_size );
    if ( mem1 == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить буфер\n";
        return false;
    }

    if ( !pmm::PersistMemoryManager::create( mem1, memory_size ) )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem1 );
        return false;
    }

    std::vector<pmm::pptr<Node>> nodes;
    nodes.reserve( node_count );

    for ( int i = 0; i < node_count; ++i )
    {
        pmm::pptr<Node> np = pmm::PersistMemoryManager::allocate_typed<Node>();
        if ( np.is_null() )
        {
            std::cerr << "  ОШИБКА: не удалось выделить узел " << i << "\n";
            pmm::PersistMemoryManager::destroy();
            std::free( mem1 );
            return false;
        }
        Node* n = np.get();
        n->id   = i;
        n->next = pmm::pptr<Node>();
        nodes.push_back( np );
    }

    for ( int i = 0; i < node_count - 1; ++i )
    {
        nodes[i].get()->next = nodes[i + 1];
    }

    for ( int i = 0; i < node_count; ++i )
    {
        Node* n     = nodes[i].get();
        n->checksum = compute_checksum( n->id, n->next.offset() );
    }

    std::uint32_t head_offset = nodes[0].offset();

    PMM_TEST( pmm::PersistMemoryManager::validate() );
    std::cout << "    Список построен, смещение головы: " << head_offset << "\n";

    // ── Фаза 2: сохранение образа ─────────────────────────────────────────────
    {
        std::cout << "  Фаза 2: сохранение в файл '" << filename << "'...\n";
    }

    auto t0    = now();
    bool saved = pmm::save( filename );
    PMM_TEST( saved );
    std::cout << "    Сохранено за " << elapsed_ms( t0, now() ) << " мс\n";

    // ── Фаза 3: уничтожение и загрузка в новый буфер ─────────────────────────
    {
        std::cout << "  Фаза 3: уничтожение и загрузка в новый буфер...\n";
    }

    pmm::PersistMemoryManager::destroy();
    std::free( mem1 );

    void* mem2 = std::malloc( memory_size );
    if ( mem2 == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить второй буфер\n";
        std::remove( filename );
        return false;
    }

    auto t1      = now();
    bool loaded  = pmm::load_from_file( filename, mem2, memory_size );
    if ( !loaded )
    {
        std::cerr << "  ОШИБКА: load_from_file вернул false\n";
        std::free( mem2 );
        std::remove( filename );
        return false;
    }
    std::cout << "    Загружено за " << elapsed_ms( t1, now() ) << " мс  (новый базовый адрес: " << mem2 << ")\n";

    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // ── Фаза 4: верификация списка через pptr::resolve() ─────────────────────
    {
        std::cout << "  Фаза 4: верификация " << node_count << " узлов через pptr::resolve()...\n";
    }

    pmm::pptr<Node> head( head_offset );
    PMM_TEST( !head.is_null() );

    auto t2        = now();
    int  traversed = 0;
    bool data_ok   = true;

    pmm::pptr<Node> cur = head;
    while ( !cur.is_null() )
    {
        Node* n = cur.get();
        if ( n == nullptr )
        {
            std::cerr << "  ОШИБКА: cur.get() вернул nullptr на узле " << traversed << "\n";
            data_ok = false;
            break;
        }

        if ( n->id != traversed )
        {
            std::cerr << "  ОШИБКА: ожидался id=" << traversed << ", получен id=" << n->id << "\n";
            data_ok = false;
            break;
        }

        unsigned int expected_cs = compute_checksum( n->id, n->next.offset() );
        if ( n->checksum != expected_cs )
        {
            std::cerr << "  ОШИБКА: контрольная сумма узла " << traversed << " не совпадает" << " (ожидалась "
                      << expected_cs << ", получена " << n->checksum << ")\n";
            data_ok = false;
            break;
        }

        cur = n->next;
        traversed++;
    }

    std::cout << "    Прошли по " << traversed << " узлам за " << elapsed_ms( t2, now() ) << " мс\n";

    PMM_TEST( data_ok );
    PMM_TEST( traversed == node_count );

    cur = head;
    while ( !cur.is_null() )
    {
        pmm::pptr<Node> next_node = cur.get()->next;
        pmm::PersistMemoryManager::deallocate_typed( cur );
        cur = next_node;
    }

    PMM_TEST( pmm::PersistMemoryManager::validate() );
    auto stats = pmm::get_stats();
    PMM_TEST( stats.allocated_blocks == 0 );

    pmm::PersistMemoryManager::destroy();
    std::free( mem2 );
    std::remove( filename );
    return true;
}

// ─── Сценарий 5: «Марафон» ───────────────────────────────────────────────────

static bool test_marathon()
{
    const std::size_t memory_size = 64UL * 1024 * 1024; // 64 МБ
    void*             mem         = std::malloc( memory_size );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память (" << memory_size / 1024 / 1024 << " МБ)\n";
        return false;
    }

    if ( !pmm::PersistMemoryManager::create( mem, memory_size ) )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem );
        return false;
    }

    Rng rng( 99991 );

    std::vector<pmm::pptr<std::uint8_t>> live;
    live.reserve( 50000 );

    const int total_iterations  = 1000000;
    const int validate_interval = 10000;

    int  alloc_ok     = 0;
    int  alloc_fail   = 0;
    int  dealloc_cnt  = 0;
    int  validate_cnt = 0;
    bool validate_ok  = true;

    auto t0 = now();
    std::cout << "  Запуск " << total_iterations << " итераций (60% alloc / 40% free)...\n";

    for ( int iter = 0; iter < total_iterations; ++iter )
    {
        if ( rng.next_n( 10 ) < 6 || live.empty() )
        {
            std::size_t             sz  = rng.next_block_size_marathon();
            pmm::pptr<std::uint8_t> ptr = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( sz );
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
            uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
            pmm::PersistMemoryManager::deallocate_typed( live[idx] );
            live[idx] = live.back();
            live.pop_back();
            dealloc_cnt++;
        }

        if ( ( iter + 1 ) % validate_interval == 0 )
        {
            validate_cnt++;
            if ( !pmm::PersistMemoryManager::validate() )
            {
                std::cerr << "  ОШИБКА: validate() вернул false на итерации " << ( iter + 1 ) << "\n";
                validate_ok = false;
                break;
            }

            if ( ( iter + 1 ) % 100000 == 0 )
            {
                auto        stats    = pmm::get_stats();
                std::size_t used_now = pmm::PersistMemoryManager::used_size();
                std::cout << "    iter=" << ( iter + 1 ) << "  живых=" << live.size() << "  alloc=" << alloc_ok
                          << "  fail=" << alloc_fail << "  free=" << dealloc_cnt << "\n"
                          << "    used=" << used_now / 1024 << " КБ" << "  frag=" << stats.total_fragmentation / 1024
                          << " КБ" << "  free_blocks=" << stats.free_blocks << "\n";
            }
        }
    }

    PMM_TEST( validate_ok );
    PMM_TEST( validate_cnt == total_iterations / validate_interval );

    std::cout << "  Освобождение " << live.size() << " оставшихся блоков...\n";
    for ( auto& p : live )
    {
        pmm::PersistMemoryManager::deallocate_typed( p );
    }
    live.clear();

    PMM_TEST( pmm::PersistMemoryManager::validate() );

    auto final_stats = pmm::get_stats();
    PMM_TEST( final_stats.allocated_blocks == 0 );

    double total_ms = elapsed_ms( t0, now() );
    std::cout << "  Итого: " << total_iterations << " итераций, " << alloc_ok << " аллокаций" << "  (" << alloc_fail
              << " неудач), " << dealloc_cnt << " освобождений\n";
    std::cout << "  validate() вызван " << validate_cnt << " раз, всегда true\n";
    std::cout << "  Общее время: " << total_ms << " мс\n";

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_scenarios_issue34 (Issue #34) ===\n";
    bool all_passed = true;

    PMM_RUN( "shredder (fragmentation & coalesce)", test_shredder );
    PMM_RUN( "persistent cycle (save/load pptr list)", test_persistent_cycle );
    PMM_RUN( "marathon (long-term stability)", test_marathon );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
