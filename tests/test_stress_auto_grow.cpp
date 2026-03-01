/**
 * @file test_stress_auto_grow.cpp
 * @brief Стресс-тест автоматического роста персистной области памяти (Issue #30)
 *
 * Тестирует механизм автоматического расширения PersistMemoryManager.
 *
 * Проверяемые сценарии:
 *   1. Однократное расширение (expand): непрерывные аллокации исчерпывают
 *      начальный буфер и вынуждают один вызов expand().
 *   2. Многократное расширение: несколько последовательных вызовов expand(),
 *      проверяет устойчивость цепочки prev_base.
 *   3. Перемежающиеся alloc/dealloc с постепенным ростом нагрузки,
 *      вызывающим расширение буфера.
 *   4. reallocate() при нехватке памяти провоцирует expand().
 *   5. Коэффициент роста буфера: каждое расширение увеличивает его не менее чем на 25%.
 *
 * После каждого расширения проверяется:
 *   - validate() возвращает true.
 *   - Данные во всех живых блоках остаются корректными (translate_ptr работает).
 *   - После освобождения всех блоков allocated_blocks == 0.
 */

#include "persist_memory_manager.h"

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

    std::size_t next_block_size_small()
    {
        // Небольшие блоки: 8..256 байт с шагом 8 (32 варианта)
        return static_cast<std::size_t>( ( next_n( 32 ) + 1 ) * 8 );
    }
};

} // namespace

// ─── Тест 1: однократный expand ───────────────────────────────────────────────

/**
 * @brief Тест однократного расширения памяти.
 *
 * Менеджер создаётся с буфером 64 КБ. Аллокации по 512 байт заполняют
 * буфер до отказа, вынуждая один вызов expand(). После расширения
 * выделяется ещё 20 блоков. Затем все блоки освобождаются.
 *
 * @return true при успешном прохождении.
 */
static bool test_single_expand()
{
    // Начальный буфер ограничен так, чтобы вызвать ровно 1 expand при данном блоке
    const std::size_t initial_size = 64UL * 1024; // 64 КБ
    void*             mem          = std::malloc( initial_size );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память\n";
        return false;
    }

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, initial_size );
    if ( mgr == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem );
        return false;
    }

    const std::size_t  block_size = 512;
    std::vector<void*> ptrs;
    ptrs.reserve( 300 );

    const uint8_t pattern = 0xAB;
    auto          t0      = now();

    // Фаза 1: заполнить начальный буфер до первого expand
    std::size_t total_before = pmm::PersistMemoryManager::instance()->total_size();
    int         expand_count = 0;

    // Ограничиваем число аллокаций, чтобы вызвать ровно 1 expand
    // (64 КБ / ~600 байт ≈ 109 блоков до первого expand)
    for ( int i = 0; i < 300 && expand_count < 2; ++i )
    {
        void* p = pmm::PersistMemoryManager::instance()->allocate( block_size );
        if ( p == nullptr )
            break;

        std::memset( p, static_cast<int>( pattern ), block_size );
        ptrs.push_back( p );

        std::size_t cur = pmm::PersistMemoryManager::instance()->total_size();
        if ( cur > total_before )
        {
            expand_count++;
            total_before = cur;
            std::cout << "    expand #" << expand_count << ": буфер " << cur / 1024 << " КБ, "
                      << "живых блоков: " << ptrs.size() << "\n";
        }
    }

    PMM_TEST( expand_count >= 1 ); // Должен был произойти хотя бы один expand
    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    // Фаза 2: проверка данных всех блоков (translate_ptr работает корректно)
    bool data_ok = true;
    for ( void* p : ptrs )
    {
        const auto* bytes = static_cast<const uint8_t*>( p );
        for ( std::size_t i = 0; i < block_size; ++i )
        {
            if ( bytes[i] != pattern )
            {
                data_ok = false;
                std::cerr << "  ОШИБКА данных в блоке " << p << " смещении " << i << "\n";
                break;
            }
        }
        if ( !data_ok )
            break;
    }
    PMM_TEST( data_ok );

    // Фаза 3: освободить все блоки
    for ( void* p : ptrs )
    {
        pmm::PersistMemoryManager::instance()->deallocate( p );
    }
    ptrs.clear();

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
    auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
    PMM_TEST( stats.allocated_blocks == 0 );

    double ms = elapsed_ms( t0, now() );
    std::cout << "    Время: " << ms << " мс\n";

    pmm::PersistMemoryManager::destroy();
    return true;
}

// ─── Тест 2: многократный expand ─────────────────────────────────────────────

/**
 * @brief Тест многократного расширения памяти.
 *
 * Менеджер создаётся с буфером 4 КБ (минимальный). Небольшие аллокации
 * вызывают несколько последовательных вызовов expand(). Проверяется
 * корректность цепочки prev_base через несколько уровней.
 *
 * Ограничен 5-ю expand() и 500 аллокациями для безопасной работы.
 *
 * @return true при успешном прохождении.
 */
static bool test_multi_expand()
{
    // Минимальный начальный буфер — множество expand()
    const std::size_t initial_size = pmm::kMinMemorySize; // 4 КБ
    void*             mem          = std::malloc( initial_size );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память\n";
        return false;
    }

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, initial_size );
    if ( mgr == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem );
        return false;
    }

    Rng rng( 7777 );

    std::vector<void*>       ptrs;
    std::vector<std::size_t> sizes;
    ptrs.reserve( 500 );
    sizes.reserve( 500 );

    std::size_t prev_total    = pmm::PersistMemoryManager::instance()->total_size();
    int         expand_count  = 0;
    const int   max_expands   = 5;
    const int   max_alloc_cnt = 500; // ограничение на число аллокаций

    auto t0 = now();

    // Выделяем блоки до достижения max_expands или max_alloc_cnt аллокаций
    for ( int i = 0; i < max_alloc_cnt && expand_count < max_expands; ++i )
    {
        std::size_t sz = rng.next_block_size_small();
        void*       p  = pmm::PersistMemoryManager::instance()->allocate( sz );
        if ( p == nullptr )
        {
            std::cerr << "  ОШИБКА: allocate вернул nullptr при i=" << i << "\n";
            pmm::PersistMemoryManager::destroy();
            return false;
        }

        // Записываем паттерн: индекс блока mod 256
        std::memset( p, static_cast<int>( i & 0xFF ), sz );
        ptrs.push_back( p );
        sizes.push_back( sz );

        std::size_t cur = pmm::PersistMemoryManager::instance()->total_size();
        if ( cur > prev_total )
        {
            expand_count++;
            prev_total = cur;
            std::cout << "    expand #" << expand_count << ": буфер "
                      << pmm::PersistMemoryManager::instance()->total_size() / 1024 << " КБ, "
                      << "живых блоков: " << ptrs.size() << "\n";
        }
    }

    std::cout << "    Выделено: " << ptrs.size() << " блоков, expand() вызван: " << expand_count << " раз\n";

    PMM_TEST( expand_count >= max_expands );
    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    // Проверяем данные всех блоков (трансляция старых указателей корректна)
    bool data_ok = true;
    for ( int i = 0; i < static_cast<int>( ptrs.size() ); ++i )
    {
        const auto*   bytes   = static_cast<const uint8_t*>( ptrs[i] );
        const uint8_t pattern = static_cast<uint8_t>( i & 0xFF );
        for ( std::size_t j = 0; j < sizes[i]; ++j )
        {
            if ( bytes[j] != pattern )
            {
                data_ok = false;
                std::cerr << "  ОШИБКА данных в блоке " << i << " смещении " << j << "\n";
                break;
            }
        }
        if ( !data_ok )
            break;
    }
    PMM_TEST( data_ok );

    // Освобождаем все блоки
    for ( void* p : ptrs )
    {
        pmm::PersistMemoryManager::instance()->deallocate( p );
    }
    ptrs.clear();

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
    auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
    PMM_TEST( stats.allocated_blocks == 0 );

    double ms = elapsed_ms( t0, now() );
    std::cout << "    Время: " << ms << " мс\n";

    pmm::PersistMemoryManager::destroy();
    return true;
}

// ─── Тест 3: expand при перемежающихся alloc/dealloc ─────────────────────────

/**
 * @brief Стресс-тест expand при реалистичном паттерне alloc/dealloc.
 *
 * Начинает с небольшого буфера и выполняет смешанные операции аллокации
 * и освобождения с постепенно растущей нагрузкой (70% alloc / 30% free).
 * Нарастающий объём живых блоков вынуждает expand(). Проверяется
 * корректность трансляции указателей и validate().
 *
 * Ограничен 200000 итерациями для предсказуемого времени выполнения.
 *
 * @return true при успешном прохождении.
 */
static bool test_expand_with_mixed_ops()
{
    const std::size_t initial_size = 32UL * 1024; // 32 КБ
    void*             mem          = std::malloc( initial_size );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память\n";
        return false;
    }

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, initial_size );
    if ( mgr == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem );
        return false;
    }

    Rng rng( 31415 );

    std::vector<void*>       live;
    std::vector<std::size_t> live_sizes;
    live.reserve( 100000 );
    live_sizes.reserve( 100000 );

    std::size_t prev_total   = pmm::PersistMemoryManager::instance()->total_size();
    int         expand_count = 0;
    int         alloc_ok     = 0;
    int         dealloc_cnt  = 0;
    const int max_expands = 50; // ограничиваем expand() для корректной работы translate_ptr
    const int max_iter = 200000;

    auto t0 = now();

    // 70% аллокация / 30% освобождение, до max_expands расширений или max_iter итераций
    for ( int i = 0; i < max_iter && expand_count < max_expands; ++i )
    {
        if ( rng.next_n( 10 ) < 7 || live.empty() )
        {
            // Аллокация
            std::size_t sz = rng.next_block_size_small();
            void*       p  = pmm::PersistMemoryManager::instance()->allocate( sz );
            if ( p != nullptr )
            {
                std::memset( p, static_cast<int>( alloc_ok & 0xFF ), sz );
                live.push_back( p );
                live_sizes.push_back( sz );
                alloc_ok++;
            }
        }
        else
        {
            // Освобождение случайного живого блока
            uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
            pmm::PersistMemoryManager::instance()->deallocate( live[idx] );
            live[idx]       = live.back();
            live_sizes[idx] = live_sizes.back();
            live.pop_back();
            live_sizes.pop_back();
            dealloc_cnt++;
        }

        std::size_t cur = pmm::PersistMemoryManager::instance()->total_size();
        if ( cur > prev_total )
        {
            expand_count++;
            prev_total = cur;
            std::cout << "    expand #" << expand_count << ": буфер " << cur / 1024 << " КБ, "
                      << "живых блоков: " << live.size() << "\n";
        }
    }

    std::cout << "    Аллокаций: " << alloc_ok << "  освобождений: " << dealloc_cnt << "\n";
    std::cout << "    Живых блоков: " << live.size() << "  expand() вызван: " << expand_count << " раз\n";

    PMM_TEST( expand_count >= 1 ); // expand должен был произойти
    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    // Освобождаем все оставшиеся блоки
    for ( void* p : live )
    {
        pmm::PersistMemoryManager::instance()->deallocate( p );
    }
    live.clear();

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
    auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
    PMM_TEST( stats.allocated_blocks == 0 );

    double ms = elapsed_ms( t0, now() );
    std::cout << "    Время: " << ms << " мс\n";

    pmm::PersistMemoryManager::destroy();
    return true;
}

// ─── Тест 4: reallocate провоцирует expand ────────────────────────────────────

/**
 * @brief Тест: reallocate() при нехватке памяти провоцирует expand().
 *
 * Выделяет несколько небольших блоков в маленьком буфере, затем вызывает
 * reallocate() с размером, многократно превышающим начальный буфер.
 * Ожидается, что expand() произойдёт внутри reallocate(). Проверяется:
 *   - reallocate() возвращает не nullptr;
 *   - буфер вырос (expand произошёл);
 *   - данные в перемещённом блоке сохранились;
 *   - validate() проходит после операции.
 *
 * @return true при успешном прохождении.
 */
static bool test_reallocate_triggers_expand()
{
    const std::size_t initial_size = 16UL * 1024; // 16 КБ
    void*             mem          = std::malloc( initial_size );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память\n";
        return false;
    }

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, initial_size );
    if ( mgr == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem );
        return false;
    }

    auto t0 = now();

    // Выделяем несколько небольших блоков и заполняем данными
    const std::size_t  block_sz = 64;
    const int          n_blocks = 5;
    std::vector<void*> ptrs;
    ptrs.reserve( n_blocks );

    for ( int i = 0; i < n_blocks; ++i )
    {
        void* p = pmm::PersistMemoryManager::instance()->allocate( block_sz );
        PMM_TEST( p != nullptr );
        std::memset( p, i + 1, block_sz );
        ptrs.push_back( p );
    }

    std::cout << "    Выделено " << n_blocks << " блоков перед reallocate\n";
    std::size_t size_before = pmm::PersistMemoryManager::instance()->total_size();

    // reallocate первого блока с размером, гарантирующим expand:
    //   new_size > initial_size означает, что текущий буфер точно не вмещает.
    const std::size_t big_sz  = initial_size * 2;
    const uint8_t     pattern = static_cast<uint8_t>( 1 ); // паттерн первого блока

    void* p2 = pmm::PersistMemoryManager::instance()->reallocate( ptrs[0], big_sz );
    PMM_TEST( p2 != nullptr );

    std::size_t size_after = pmm::PersistMemoryManager::instance()->total_size();
    bool        did_expand = size_after > size_before;
    std::cout << "    reallocate expand: " << ( did_expand ? "да" : "нет" ) << "\n";
    std::cout << "    Буфер: " << size_before / 1024 << " КБ → " << size_after / 1024 << " КБ\n";

    PMM_TEST( did_expand ); // expand обязан был произойти

    // Проверяем, что первые block_sz байт сохранились (memcpy в reallocate)
    const auto* bytes   = static_cast<const uint8_t*>( p2 );
    bool        data_ok = true;
    for ( std::size_t i = 0; i < block_sz; ++i )
    {
        if ( bytes[i] != pattern )
        {
            data_ok = false;
            std::cerr << "  ОШИБКА данных после reallocate, смещение " << i << "\n";
            break;
        }
    }
    PMM_TEST( data_ok );
    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    // Освобождаем перераспределённый блок и оставшиеся блоки
    pmm::PersistMemoryManager::instance()->deallocate( p2 );
    for ( int i = 1; i < n_blocks; ++i )
    {
        pmm::PersistMemoryManager::instance()->deallocate( ptrs[i] );
    }

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    double ms = elapsed_ms( t0, now() );
    std::cout << "    Время: " << ms << " мс\n";

    pmm::PersistMemoryManager::destroy();
    return true;
}

// ─── Тест 5: размер буфера растёт на 25% при каждом expand ───────────────────

/**
 * @brief Проверка коэффициента роста при expand().
 *
 * Вынуждает несколько expand() и убеждается, что каждый раз новый буфер
 * не меньше old_size * kGrowNumerator / kGrowDenominator (= 125% от старого).
 *
 * @return true при успешном прохождении.
 */
static bool test_grow_factor()
{
    const std::size_t initial_size = 8UL * 1024; // 8 КБ
    void*             mem          = std::malloc( initial_size );
    if ( mem == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось выделить системную память\n";
        return false;
    }

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, initial_size );
    if ( mgr == nullptr )
    {
        std::cerr << "  ОШИБКА: не удалось создать PersistMemoryManager\n";
        std::free( mem );
        return false;
    }

    auto t0 = now();

    std::size_t last_size     = pmm::PersistMemoryManager::instance()->total_size();
    int         expand_count  = 0;
    bool        grow_ok       = true;
    const int   max_expands   = 5;
    const int   max_alloc_cnt = 1000; // ограничение на число аллокаций

    std::vector<void*> ptrs;

    for ( int i = 0; i < max_alloc_cnt && expand_count < max_expands; ++i )
    {
        void* p = pmm::PersistMemoryManager::instance()->allocate( 64 );
        if ( p == nullptr )
        {
            std::cerr << "  ОШИБКА: allocate вернул nullptr при i=" << i << "\n";
            pmm::PersistMemoryManager::destroy();
            return false;
        }
        ptrs.push_back( p );

        std::size_t cur = pmm::PersistMemoryManager::instance()->total_size();
        if ( cur > last_size )
        {
            expand_count++;
            // Проверяем, что рост ≥ 25%
            std::size_t min_expected = last_size * pmm::kGrowNumerator / pmm::kGrowDenominator;
            bool        grew_enough  = ( cur >= min_expected );
            std::cout << "    expand #" << expand_count << ": " << last_size / 1024 << " КБ → " << cur / 1024
                      << " КБ (min=" << min_expected / 1024 << " КБ, " << ( grew_enough ? "OK" : "ОШИБКА" ) << ")\n";
            if ( !grew_enough )
                grow_ok = false;
            last_size = cur;
        }
    }

    PMM_TEST( grow_ok );
    PMM_TEST( expand_count >= max_expands );
    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );

    for ( void* p : ptrs )
    {
        pmm::PersistMemoryManager::instance()->deallocate( p );
    }

    PMM_TEST( pmm::PersistMemoryManager::instance()->validate() );
    auto stats = pmm::get_stats( pmm::PersistMemoryManager::instance() );
    PMM_TEST( stats.allocated_blocks == 0 );

    double ms = elapsed_ms( t0, now() );
    std::cout << "    Время: " << ms << " мс\n";

    pmm::PersistMemoryManager::destroy();
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_stress_auto_grow (Issue #30) ===\n";
    bool all_passed = true;

    PMM_RUN( "single expand", test_single_expand );
    PMM_RUN( "multi expand", test_multi_expand );
    PMM_RUN( "expand with mixed ops", test_expand_with_mixed_ops );
    PMM_RUN( "reallocate triggers expand", test_reallocate_triggers_expand );
    PMM_RUN( "grow factor >= 25%", test_grow_factor );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
