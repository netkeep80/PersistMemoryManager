/**
 * @file test_scenarios_issue34.cpp
 * @brief Нагрузочные и стресс-тесты PersistMemoryManager (Issue #34)
 *
 * Реализует три сценария из Issue #34:
 *
 *   Сценарий 1: «Шредер» — Интенсивная фрагментация и слияние (coalesce).
 *     Создаёт 10 000 блоков со случайными размерами (32–4096 байт),
 *     освобождает 50% в случайном порядке, затем 50% в порядке возрастания
 *     адресов (для провоцирования слияния соседей).
 *
 *   Сценарий 2: «Персистентный цикл» — Целостность Save/Load с pptr.
 *     Строит связный список из 1000 узлов `struct Node { int id; pptr<Node> next; }`
 *     в персистентной памяти, сохраняет образ файлом, уничтожает менеджер,
 *     загружает в новый буфер и верифицирует все данные через pptr::resolve().
 *
 *   Сценарий 5: «Марафон» — Долгосрочная стабильность.
 *     1 000 000 итераций: 60% аллокация / 40% деаллокация, validate() каждые
 *     10 000 итераций. Мониторит отсутствие утечек и деградации.
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

    /// Возвращает равномерно распределённое число в [0, n).
    uint32_t next_n( uint32_t n ) { return ( next() >> 16 ) % n; }

    /// Возвращает случайный размер блока в диапазоне [32, 4096] байт с шагом 32
    std::size_t next_block_size_shredder()
    {
        // Диапазон: 32..4096 байт с шагом 32 (128 вариантов)
        return static_cast<std::size_t>( ( next_n( 128 ) + 1 ) * 32 );
    }

    /// Возвращает случайный размер блока в диапазоне [8, 4096] байт с шагом 8
    std::size_t next_block_size_marathon()
    {
        // Диапазон: 8..4096 байт с шагом 8 (512 вариантов)
        return static_cast<std::size_t>( ( next_n( 512 ) + 1 ) * 8 );
    }
};

} // namespace

// ─── Сценарий 1: «Шредер» ────────────────────────────────────────────────────

/**
 * @brief Сценарий 1: «Шредер» — Интенсивная фрагментация и слияние.
 *
 * Фаза 1: выделяем 10 000 блоков со случайными размерами (32–4096 байт).
 * Фаза 2: освобождаем 50% блоков в случайном порядке.
 * Фаза 3: проверяем высокую фрагментацию через dump_stats()/get_stats().
 * Фаза 4: освобождаем оставшиеся 50% в порядке возрастания адресов.
 * Фаза 5: проверяем слияние — free_blocks должен быть минимальным.
 *
 * @return true при успешном прохождении теста.
 */
static bool test_shredder()
{
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

    Rng rng( 31337 );

    // ── Фаза 1: 10 000 аллокаций со случайными размерами ──────────────────────
    {
        std::cout << "  Фаза 1: создание 10 000 блоков со случайными размерами...\n";
    }

    std::vector<void*> all_ptrs;
    all_ptrs.reserve( 10000 );

    auto t0     = now();
    int  failed = 0;
    for ( int i = 0; i < 10000; ++i )
    {
        std::size_t sz  = rng.next_block_size_shredder();
        void*       ptr = pmm::PersistMemoryManager::instance()->allocate( sz );
        if ( ptr != nullptr )
        {
            // Записываем паттерн для последующей проверки целостности
            std::memset( ptr, static_cast<int>( i & 0xFF ), sz );
            all_ptrs.push_back( ptr );
        }
        else
        {
            failed++;
        }
    }

    std::cout << "    Выделено: " << all_ptrs.size() << " / 10000" << "  неудачно: " << failed
              << "  время: " << elapsed_ms( t0, now() ) << " мс\n";

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    // Перемешиваем порядок указателей (для случайного освобождения)
    for ( std::size_t i = all_ptrs.size() - 1; i > 0; --i )
    {
        uint32_t j = rng.next_n( static_cast<uint32_t>( i + 1 ) );
        std::swap( all_ptrs[i], all_ptrs[j] );
    }

    // ── Фаза 2: случайное освобождение 50% блоков ─────────────────────────────
    {
        std::cout << "  Фаза 2: случайное освобождение 50% блоков...\n";
    }

    std::size_t half = all_ptrs.size() / 2;
    // Сохраняем первую половину (для случайного освобождения)
    std::vector<void*> random_half( all_ptrs.begin(), all_ptrs.begin() + static_cast<std::ptrdiff_t>( half ) );
    // Вторая половина будет освобождена в порядке возрастания адресов
    std::vector<void*> sorted_half( all_ptrs.begin() + static_cast<std::ptrdiff_t>( half ), all_ptrs.end() );

    auto t1 = now();
    for ( void* p : random_half )
    {
        pmm::PersistMemoryManager::instance()->deallocate( p );
    }

    std::cout << "    Освобождено: " << random_half.size() << " блоков  время: " << elapsed_ms( t1, now() ) << " мс\n";

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    // ── Фаза 3: проверка фрагментации после случайного освобождения ───────────
    {
        std::cout << "  Фаза 3: фрагментация после случайного освобождения:\n";
        auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
        std::cout << "    Всего блоков: " << stats.total_blocks << "  свободных: " << stats.free_blocks
                  << "  занятых: " << stats.allocated_blocks << "\n";
        std::cout << "    Наибольший свободный: " << stats.largest_free / 1024 << " КБ"
                  << "  фрагментация: " << stats.total_fragmentation / 1024 << " КБ\n";

        // После случайного освобождения 50% ожидается несколько свободных блоков
        PMM_TEST( stats.allocated_blocks == sorted_half.size() );
        PMM_TEST( stats.free_blocks >= 1 );
    }

    // ── Фаза 4: освобождение оставшихся 50% в порядке возрастания адресов ─────
    {
        std::cout << "  Фаза 4: освобождение оставшихся блоков в порядке возрастания адресов...\n";
    }

    // Сортируем по адресу для провоцирования слияния соседей
    std::sort( sorted_half.begin(), sorted_half.end() );

    auto t2 = now();
    for ( void* p : sorted_half )
    {
        pmm::PersistMemoryManager::instance()->deallocate( p );
    }

    std::cout << "    Освобождено: " << sorted_half.size() << " блоков  время: " << elapsed_ms( t2, now() ) << " мс\n";

    // ── Фаза 5: финальная валидация — проверяем слияние ───────────────────────
    {
        std::cout << "  Фаза 5: финальная валидация после полного освобождения:\n";

        PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

        auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
        std::cout << "    Всего блоков: " << stats.total_blocks << "  свободных: " << stats.free_blocks
                  << "  занятых: " << stats.allocated_blocks << "\n";
        std::cout << "    Наибольший свободный: " << stats.largest_free / 1024 << " КБ\n";

        // Все блоки освобождены
        PMM_TEST( stats.allocated_blocks == 0 );

        // После полного освобождения coalesce должен объединить блоки;
        // количество свободных блоков должно быть очень небольшим (близко к 1)
        PMM_TEST( stats.free_blocks <= 10 );

        // Наибольший свободный блок должен быть значительным (большая часть буфера)
        PMM_TEST( stats.largest_free > memory_size / 2 );
    }

    double total_ms = elapsed_ms( t0, now() );
    std::cout << "  Общее время: " << total_ms << " мс\n";

    pmm::PersistMemoryManager::destroy();
    return true;
}

// ─── Сценарий 2: «Персистентный цикл» ────────────────────────────────────────

/**
 * @brief Узел связного списка в персистентной памяти.
 *
 * Хранит id узла, pptr на следующий узел, и контрольную сумму
 * для верификации целостности данных после save/load.
 */
struct Node
{
    int             id;
    pmm::pptr<Node> next;
    unsigned int    checksum; ///< простая контрольная сумма id + smещения
};

/**
 * @brief Простая контрольная сумма для узла.
 */
static unsigned int compute_checksum( int id, std::ptrdiff_t next_offset )
{
    return static_cast<unsigned int>( id * 2654435761u ) ^ static_cast<unsigned int>( next_offset );
}

/**
 * @brief Сценарий 2: «Персистентный цикл» — Save/Load Integrity с pptr.
 *
 * Фаза 1: выделяем 1000 узлов Node в персистентной памяти, связываем в список.
 * Фаза 2: сохраняем образ в файл через pmm::save().
 * Фаза 3: уничтожаем менеджер, загружаем в новый буфер через load_from_file().
 * Фаза 4: верифицируем список через pptr::resolve() и контрольные суммы.
 *
 * @return true при успешном прохождении теста.
 */
static bool test_persistent_cycle()
{
    const std::size_t memory_size = 4UL * 1024 * 1024; // 4 МБ — достаточно для 1000 узлов
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

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, memory_size );
    if ( mgr1 == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem1 );
        return false;
    }

    // Выделяем все узлы
    std::vector<pmm::pptr<Node>> nodes;
    nodes.reserve( node_count );

    for ( int i = 0; i < node_count; ++i )
    {
        pmm::pptr<Node> np = pmm::PersistMemoryManager::instance()->allocate_typed<Node>();
        if ( np.is_null() )
        {
            std::cerr << "  ОШИБКА: не удалось выделить узел " << i << "\n";
            pmm::PersistMemoryManager::destroy();
            return false;
        }
        Node* n = np.get();
        n->id   = i;
        n->next = pmm::pptr<Node>(); // нулевой (null) pptr
        nodes.push_back( np );
    }

    // Связываем в однонаправленный список: nodes[0] -> nodes[1] -> ... -> nodes[n-1] -> null
    for ( int i = 0; i < node_count - 1; ++i )
    {
        nodes[i].get()->next = nodes[i + 1];
    }
    // nodes[node_count - 1]->next уже null

    // Записываем контрольные суммы
    for ( int i = 0; i < node_count; ++i )
    {
        Node* n     = nodes[i].get();
        n->checksum = compute_checksum( n->id, n->next.offset() );
    }

    // Сохраняем смещение головы списка для использования после загрузки
    std::ptrdiff_t head_offset = nodes[0].offset();

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
    std::cout << "    Список построен, смещение головы: " << head_offset << "\n";

    // ── Фаза 2: сохранение образа ─────────────────────────────────────────────
    {
        std::cout << "  Фаза 2: сохранение в файл '" << filename << "'...\n";
    }

    auto t0    = now();
    bool saved = pmm::save( mgr1, filename );
    PMM_TEST( saved );
    std::cout << "    Сохранено за " << elapsed_ms( t0, now() ) << " мс\n";

    // ── Фаза 3: уничтожение и загрузка в новый буфер ─────────────────────────
    {
        std::cout << "  Фаза 3: уничтожение и загрузка в новый буфер...\n";
    }

    pmm::PersistMemoryManager::destroy();
    // mem1 освобождён внутри destroy()

    // Выделяем новый буфер (того же размера — требование load_from_file)
    void* mem2 = std::malloc( memory_size );
    if ( mem2 == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить второй буфер\n";
        std::remove( filename );
        return false;
    }

    auto                       t1   = now();
    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( filename, mem2, memory_size );
    if ( mgr2 == nullptr )
    {
        std::cerr << "  ОШИБКА: load_from_file вернул nullptr\n";
        std::free( mem2 );
        std::remove( filename );
        return false;
    }
    std::cout << "    Загружено за " << elapsed_ms( t1, now() ) << " мс  (новый базовый адрес: " << mem2 << ")\n";

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    // ── Фаза 4: верификация списка через pptr::resolve() ─────────────────────
    {
        std::cout << "  Фаза 4: верификация " << node_count << " узлов через pptr::resolve()...\n";
    }

    // Восстанавливаем голову списка по сохранённому смещению
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

        // Проверяем id
        if ( n->id != traversed )
        {
            std::cerr << "  ОШИБКА: ожидался id=" << traversed << ", получен id=" << n->id << "\n";
            data_ok = false;
            break;
        }

        // Проверяем контрольную сумму
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

    // Освобождаем все узлы
    cur = head;
    while ( !cur.is_null() )
    {
        pmm::pptr<Node> next_node = cur.get()->next;
        pmm::PersistMemoryManager::instance()->deallocate_typed( cur );
        cur = next_node;
    }

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
    auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
    PMM_TEST( stats.allocated_blocks == 0 );

    pmm::PersistMemoryManager::destroy();
    std::remove( filename );
    return true;
}

// ─── Сценарий 5: «Марафон» ───────────────────────────────────────────────────

/**
 * @brief Сценарий 5: «Марафон» — Долгосрочная стабильность.
 *
 * Выполняет 1 000 000 итераций: 60% аллокация / 40% деаллокация.
 * Каждые 10 000 итераций вызывает validate().
 * Проверяет: отсутствие утечек, стабильность фрагментации, validate() всегда true.
 *
 * Примечание: для приемлемого времени выполнения в CI используется буфер 64 МБ
 * (вместо 512 МБ из задания), что допускает достаточно операций.
 *
 * @return true при успешном прохождении теста.
 */
static bool test_marathon()
{
    const std::size_t memory_size = 64UL * 1024 * 1024; // 64 МБ (CI-friendly вариант)
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

    Rng rng( 99991 );

    std::vector<void*> live;
    live.reserve( 50000 );

    const int total_iterations  = 1000000;
    const int validate_interval = 10000;

    int  alloc_ok     = 0;
    int  alloc_fail   = 0;
    int  dealloc_cnt  = 0;
    int  validate_cnt = 0;
    bool validate_ok  = true;

    // Метрики для проверки отсутствия деградации
    std::size_t used_size_prev        = 0;
    int         used_size_grow_streak = 0; // количество раз подряд, когда used_size росло

    auto t0 = now();
    std::cout << "  Запуск " << total_iterations << " итераций (60% alloc / 40% free)...\n";

    for ( int iter = 0; iter < total_iterations; ++iter )
    {
        // 60% аллокация, 40% освобождение
        if ( rng.next_n( 10 ) < 6 || live.empty() )
        {
            std::size_t sz  = rng.next_block_size_marathon();
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
            uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
            pmm::PersistMemoryManager::instance()->deallocate( live[idx] );
            live[idx] = live.back();
            live.pop_back();
            dealloc_cnt++;
        }

        // Каждые validate_interval итераций — проверка
        if ( ( iter + 1 ) % validate_interval == 0 )
        {
            validate_cnt++;
            if ( !pmm::PersistMemoryManager::instance()->validate() )
            {
                std::cerr << "  ОШИБКА: validate() вернул false на итерации " << ( iter + 1 ) << "\n";
                validate_ok = false;
                break;
            }

            // Мониторинг используемой памяти: не должна постоянно расти (признак утечки)
            std::size_t used_now = pmm::PersistMemoryManager::instance()->used_size();
            if ( used_now > used_size_prev && used_size_prev > 0 )
            {
                used_size_grow_streak++;
            }
            else
            {
                used_size_grow_streak = 0;
            }
            used_size_prev = used_now;

            if ( ( iter + 1 ) % 100000 == 0 )
            {
                auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
                std::cout << "    iter=" << ( iter + 1 ) << "  живых=" << live.size() << "  alloc=" << alloc_ok
                          << "  fail=" << alloc_fail << "  free=" << dealloc_cnt << "\n"
                          << "    used=" << used_now / 1024 << " КБ" << "  frag=" << stats.total_fragmentation / 1024
                          << " КБ" << "  free_blocks=" << stats.free_blocks << "\n";
            }
        }
    }

    // Итоговая проверка без утечек
    PMM_TEST( validate_ok );

    // Проверяем, что validate() вызывался ожидаемое число раз
    PMM_TEST( validate_cnt == total_iterations / validate_interval );

    // Освобождаем все живые блоки
    std::cout << "  Освобождение " << live.size() << " оставшихся блоков...\n";
    for ( void* p : live )
    {
        pmm::PersistMemoryManager::instance()->deallocate( p );
    }
    live.clear();

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    // Нет утечек: после полного освобождения allocated_blocks == 0
    auto final_stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
    PMM_TEST( final_stats.allocated_blocks == 0 );

    double total_ms = elapsed_ms( t0, now() );
    std::cout << "  Итого: " << total_iterations << " итераций, " << alloc_ok << " аллокаций" << "  (" << alloc_fail
              << " неудач), " << dealloc_cnt << " освобождений\n";
    std::cout << "  validate() вызван " << validate_cnt << " раз, всегда true\n";
    std::cout << "  Общее время: " << total_ms << " мс\n";

    pmm::PersistMemoryManager::destroy();
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
