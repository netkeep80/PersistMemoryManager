/**
 * @file test_persistence.cpp
 * @brief Тесты персистентности (save/load) — Фаза 3
 *
 * Проверяет корректность сохранения и загрузки образа памяти из файла:
 * - сохранение и последующая загрузка восстанавливают все метаданные;
 * - загрузка по другому базовому адресу работает корректно (смещения);
 * - detect повреждённого или несовместимого образа;
 * - данные пользователя сохраняются без искажений;
 * - валидация менеджера после загрузки;
 * - граничные случаи (nullptr, несуществующий файл, нулевой размер и пр.).
 */

#include "persist_memory_manager.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>

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

// ─── Вспомогательный путь для временного файла ────────────────────────────────

static const char* TEST_FILE = "test_heap.dat";

/// Удалить временный файл (вызывается в конце каждого теста)
static void cleanup_file()
{
    std::remove( TEST_FILE );
}

// ─── Тестовые функции ─────────────────────────────────────────────────────────

/**
 * @brief Базовый round-trip: create → save → load → validate.
 *
 * Создаёт менеджер, сохраняет образ, загружает в новый буфер,
 * проверяет, что менеджер валиден и статистика совпадает.
 */
static bool test_persistence_basic_roundtrip()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    // Сохраняем образ
    PMM_TEST( mgr1->save( TEST_FILE ) );

    // Загружаем в другой буфер
    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    // Статистика должна совпадать
    PMM_TEST( mgr2->total_size() == mgr1->total_size() );
    PMM_TEST( mgr2->used_size() == mgr1->used_size() );
    PMM_TEST( mgr2->free_size() == mgr1->free_size() );

    auto stats1 = pmm::get_stats( mgr1 );
    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.total_blocks == stats1.total_blocks );
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );
    PMM_TEST( stats2.allocated_blocks == stats1.allocated_blocks );

    mgr1->destroy();
    mgr2->destroy();
    std::free( mem1 );
    std::free( mem2 );
    cleanup_file();
    return true;
}

/**
 * @brief Данные пользователя сохраняются корректно.
 *
 * Заполняем выделенный блок паттерном, сохраняем, загружаем,
 * проверяем, что данные не изменились.
 */
static bool test_persistence_user_data_preserved()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    // Выделяем блок и заполняем паттерном
    const std::size_t data_size = 256;
    void*             ptr1      = mgr1->allocate( data_size );
    PMM_TEST( ptr1 != nullptr );

    std::memset( ptr1, 0xCA, data_size );

    // Сохраняем образ
    PMM_TEST( mgr1->save( TEST_FILE ) );

    // Загружаем в другой буфер
    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    // Статистика блоков
    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.allocated_blocks == 1 );

    // Находим блок через обход и проверяем данные
    // Блок находится по тому же смещению в буфере mem2
    // Вычисляем смещение ptr1 от mem1
    std::ptrdiff_t offset = static_cast<std::uint8_t*>( ptr1 ) - static_cast<std::uint8_t*>( mem1 );
    void*          ptr2   = static_cast<std::uint8_t*>( mem2 ) + offset;

    const std::uint8_t* p = static_cast<const std::uint8_t*>( ptr2 );
    for ( std::size_t i = 0; i < data_size; i++ )
    {
        PMM_TEST( p[i] == 0xCA );
    }

    mgr1->destroy();
    mgr2->destroy();
    std::free( mem1 );
    std::free( mem2 );
    cleanup_file();
    return true;
}

/**
 * @brief Загрузка с несколькими выделенными блоками.
 *
 * Выделяем несколько блоков разных размеров, часть освобождаем,
 * сохраняем и загружаем. Проверяем, что статистика совпадает.
 */
static bool test_persistence_multiple_blocks()
{
    const std::size_t size = 128 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    void* p1 = mgr1->allocate( 128 );
    void* p2 = mgr1->allocate( 256 );
    void* p3 = mgr1->allocate( 512 );
    void* p4 = mgr1->allocate( 64 );
    PMM_TEST( p1 && p2 && p3 && p4 );

    // Освобождаем часть блоков
    mgr1->deallocate( p2 );
    mgr1->deallocate( p4 );
    PMM_TEST( mgr1->validate() );

    auto stats1 = pmm::get_stats( mgr1 );
    PMM_TEST( mgr1->save( TEST_FILE ) );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.total_blocks == stats1.total_blocks );
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );
    PMM_TEST( stats2.allocated_blocks == stats1.allocated_blocks );
    PMM_TEST( mgr2->total_size() == mgr1->total_size() );
    PMM_TEST( mgr2->used_size() == mgr1->used_size() );

    mgr1->destroy();
    mgr2->destroy();
    std::free( mem1 );
    std::free( mem2 );
    cleanup_file();
    return true;
}

/**
 * @brief Можно выделять память из загруженного менеджера.
 *
 * После загрузки образа allocate должен работать корректно.
 */
static bool test_persistence_allocate_after_load()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    void* p1 = mgr1->allocate( 512 );
    PMM_TEST( p1 != nullptr );

    PMM_TEST( mgr1->save( TEST_FILE ) );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    // Должно быть возможно выделить дополнительную память
    void* p2 = mgr2->allocate( 256 );
    PMM_TEST( p2 != nullptr );
    PMM_TEST( mgr2->validate() );

    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.allocated_blocks == 2 );

    mgr2->deallocate( p2 );
    PMM_TEST( mgr2->validate() );

    mgr1->destroy();
    mgr2->destroy();
    std::free( mem1 );
    std::free( mem2 );
    cleanup_file();
    return true;
}

/**
 * @brief save() возвращает false при nullptr имени файла.
 */
static bool test_persistence_save_null_filename()
{
    const std::size_t size = 16 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    PMM_TEST( mgr->save( nullptr ) == false );

    mgr->destroy();
    std::free( mem );
    return true;
}

/**
 * @brief load_from_file() возвращает nullptr для несуществующего файла.
 */
static bool test_persistence_load_nonexistent_file()
{
    const std::size_t size = 16 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::load_from_file( "no_such_file_xyz123.dat", mem, size );
    PMM_TEST( mgr == nullptr );

    std::free( mem );
    return true;
}

/**
 * @brief load_from_file() возвращает nullptr при nullptr аргументах.
 */
static bool test_persistence_load_null_args()
{
    const std::size_t size = 16 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::load_from_file( nullptr, mem, size ) == nullptr );
    PMM_TEST( pmm::load_from_file( TEST_FILE, nullptr, size ) == nullptr );
    PMM_TEST( pmm::load_from_file( TEST_FILE, mem, 0 ) == nullptr );

    std::free( mem );
    cleanup_file();
    return true;
}

/**
 * @brief Повреждённый заголовок (неверное магическое число) при загрузке → nullptr.
 *
 * Создаём файл с заполненным буфером (не PMM образ), пытаемся загрузить.
 */
static bool test_persistence_corrupted_image()
{
    // Записываем в файл «мусор» (нули — неверное магическое число)
    const std::size_t size = 16 * 1024;
    {
        std::FILE* f = std::fopen( TEST_FILE, "wb" );
        PMM_TEST( f != nullptr );
        std::uint8_t zeros[16 * 1024] = {};
        std::fwrite( zeros, 1, size, f );
        std::fclose( f );
    }

    void* mem = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::load_from_file( TEST_FILE, mem, size );
    PMM_TEST( mgr == nullptr ); // Ожидаем отказ из-за неверного magic

    std::free( mem );
    cleanup_file();
    return true;
}

/**
 * @brief Буфер меньше размера файла → load_from_file() возвращает nullptr.
 */
static bool test_persistence_buffer_too_small()
{
    const std::size_t size = 32 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );
    PMM_TEST( mgr1->save( TEST_FILE ) );

    // Пытаемся загрузить в буфер меньшего размера
    const std::size_t small_size = 4 * 1024;
    void*             mem2       = std::malloc( small_size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, small_size );
    PMM_TEST( mgr2 == nullptr ); // Буфер слишком мал

    mgr1->destroy();
    std::free( mem1 );
    std::free( mem2 );
    cleanup_file();
    return true;
}

/**
 * @brief Двойной save/load: save → load → save → load → validate.
 *
 * Проверяет идемпотентность операции: повторное сохранение и загрузка
 * не приводят к искажению данных.
 */
static bool test_persistence_double_save_load()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    void* p1 = mgr1->allocate( 128 );
    void* p2 = mgr1->allocate( 256 );
    PMM_TEST( p1 && p2 );
    std::memset( p1, 0xAA, 128 );
    std::memset( p2, 0xBB, 256 );

    // Первый цикл save/load
    PMM_TEST( mgr1->save( TEST_FILE ) );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );
    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    // Второй цикл save/load (сохраняем загруженный образ)
    static const char* TEST_FILE2 = "test_heap2.dat";
    PMM_TEST( mgr2->save( TEST_FILE2 ) );

    void* mem3 = std::malloc( size );
    PMM_TEST( mem3 != nullptr );
    pmm::PersistMemoryManager* mgr3 = pmm::load_from_file( TEST_FILE2, mem3, size );
    PMM_TEST( mgr3 != nullptr );
    PMM_TEST( mgr3->validate() );

    auto stats1 = pmm::get_stats( mgr1 );
    auto stats3 = pmm::get_stats( mgr3 );
    PMM_TEST( stats3.total_blocks == stats1.total_blocks );
    PMM_TEST( stats3.allocated_blocks == stats1.allocated_blocks );
    PMM_TEST( mgr3->total_size() == mgr1->total_size() );

    mgr1->destroy();
    mgr2->destroy();
    mgr3->destroy();
    std::free( mem1 );
    std::free( mem2 );
    std::free( mem3 );
    std::remove( TEST_FILE );
    std::remove( TEST_FILE2 );
    return true;
}

/**
 * @brief Сохранение и загрузка пустого (свежесозданного) менеджера.
 */
static bool test_persistence_empty_manager()
{
    const std::size_t size = 16 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    // Сохраняем без каких-либо аллокаций
    PMM_TEST( mgr1->save( TEST_FILE ) );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    auto stats1 = pmm::get_stats( mgr1 );
    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.allocated_blocks == 0 );
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );

    // После загрузки можно выделять память
    void* p = mgr2->allocate( 512 );
    PMM_TEST( p != nullptr );
    PMM_TEST( mgr2->validate() );

    mgr1->destroy();
    mgr2->destroy();
    std::free( mem1 );
    std::free( mem2 );
    cleanup_file();
    return true;
}

/**
 * @brief После загрузки deallocate работает корректно.
 */
static bool test_persistence_deallocate_after_load()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    void* p1 = mgr1->allocate( 256 );
    void* p2 = mgr1->allocate( 512 );
    PMM_TEST( p1 && p2 );

    PMM_TEST( mgr1->save( TEST_FILE ) );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    // Находим p1 в mem2 по смещению
    std::ptrdiff_t off1 = static_cast<std::uint8_t*>( p1 ) - static_cast<std::uint8_t*>( mem1 );
    std::ptrdiff_t off2 = static_cast<std::uint8_t*>( p2 ) - static_cast<std::uint8_t*>( mem1 );
    void*          q1   = static_cast<std::uint8_t*>( mem2 ) + off1;
    void*          q2   = static_cast<std::uint8_t*>( mem2 ) + off2;

    mgr2->deallocate( q1 );
    PMM_TEST( mgr2->validate() );

    mgr2->deallocate( q2 );
    PMM_TEST( mgr2->validate() );

    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.allocated_blocks == 0 );

    mgr1->destroy();
    mgr2->destroy();
    std::free( mem1 );
    std::free( mem2 );
    cleanup_file();
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_persistence ===\n";
    bool all_passed = true;

    PMM_RUN( "persistence_basic_roundtrip",       test_persistence_basic_roundtrip );
    PMM_RUN( "persistence_user_data_preserved",   test_persistence_user_data_preserved );
    PMM_RUN( "persistence_multiple_blocks",       test_persistence_multiple_blocks );
    PMM_RUN( "persistence_allocate_after_load",   test_persistence_allocate_after_load );
    PMM_RUN( "persistence_save_null_filename",    test_persistence_save_null_filename );
    PMM_RUN( "persistence_load_nonexistent_file", test_persistence_load_nonexistent_file );
    PMM_RUN( "persistence_load_null_args",        test_persistence_load_null_args );
    PMM_RUN( "persistence_corrupted_image",       test_persistence_corrupted_image );
    PMM_RUN( "persistence_buffer_too_small",      test_persistence_buffer_too_small );
    PMM_RUN( "persistence_double_save_load",      test_persistence_double_save_load );
    PMM_RUN( "persistence_empty_manager",         test_persistence_empty_manager );
    PMM_RUN( "persistence_deallocate_after_load", test_persistence_deallocate_after_load );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
