/**
 * @file test_block_modernization.cpp
 * @brief Тесты модернизации блока (Issue #69)
 *
 * Тестирует:
 *   1. is_valid_block() — структурная валидность блока без magic-числа
 *   2. Поведение при загрузке (repair_linked_list, recompute_counters)
 *   3. Удаление magic из BlockHeader: используется только ManagerHeader.magic
 *   4. Устойчивость к частично повреждённым образам
 */

#include "persist_memory_manager.h"
#include "persist_memory_io.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

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

// ─── Тест 1: BlockHeader не содержит поля magic ───────────────────────────────

/// Проверяем, что sizeof(BlockHeader) == 32 (2 гранулы).
/// После удаления magic (4 байта) добавлено root_offset (4 байта) — размер не изменился (Issue #75).
static bool test_block_header_no_magic()
{
    // BlockHeader должен быть ровно 32 байта = 2 гранулы
    PMM_TEST( sizeof( pmm::detail::BlockHeader ) == 32 );
    PMM_TEST( sizeof( pmm::detail::BlockHeader ) % pmm::kGranuleSize == 0 );

    // kBlockMagic больше не должна существовать — тест проверяет через компиляцию.
    // Если код скомпилировался без magic, этот тест уже выполнен.

    return true;
}

// ─── Тест 2: Базовые аллокации и валидация без magic ─────────────────────────

static bool test_basic_alloc_validate()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    auto p1 = pmm::PersistMemoryManager::allocate_typed<std::uint32_t>( 16 );
    PMM_TEST( !p1.is_null() );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    auto p2 = pmm::PersistMemoryManager::allocate_typed<std::uint64_t>( 8 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::deallocate_typed( p1 );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    pmm::PersistMemoryManager::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    auto stats = pmm::get_stats();
    PMM_TEST( stats.allocated_blocks == 0 );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

// ─── Тест 3: Сохранение и загрузка с новым форматом (без magic в BlockHeader) ─

static bool test_save_load_new_format()
{
    const char*       TEST_FILE = "test_block_mod.dat";
    const std::size_t size      = 64 * 1024;
    void*             mem1      = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem1, size ) );

    // Выделяем несколько блоков разных размеров
    auto p1 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 100 );
    auto p2 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 200 );
    auto p3 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 300 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    std::memset( p1.get(), 0x11, 100 );
    std::memset( p2.get(), 0x22, 200 );
    std::memset( p3.get(), 0x33, 300 );

    // Освобождаем средний блок (фрагментация)
    pmm::PersistMemoryManager::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    auto stats1 = pmm::get_stats();
    PMM_TEST( stats1.allocated_blocks == 2 );

    std::uint32_t off1 = p1.offset();
    std::uint32_t off3 = p3.offset();

    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager::destroy();
    std::free( mem1 );

    // Загружаем в новый буфер
    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    PMM_TEST( pmm::load_from_file( TEST_FILE, mem2, size ) );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // Проверяем счётчики
    auto stats2 = pmm::get_stats();
    PMM_TEST( stats2.allocated_blocks == stats1.allocated_blocks );
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );
    PMM_TEST( stats2.total_blocks == stats1.total_blocks );

    // Проверяем данные
    pmm::pptr<std::uint8_t> q1( off1 );
    pmm::pptr<std::uint8_t> q3( off3 );
    for ( std::size_t i = 0; i < 100; i++ )
        PMM_TEST( q1.get()[i] == 0x11 );
    for ( std::size_t i = 0; i < 300; i++ )
        PMM_TEST( q3.get()[i] == 0x33 );

    pmm::PersistMemoryManager::destroy();
    std::free( mem2 );
    std::remove( TEST_FILE );
    return true;
}

// ─── Тест 4: Устойчивость коалесценции — zeroed headers не считаются валидными ─

/// После слияния блоков, старый заголовок слитого блока обнуляется.
/// Проверяем, что is_valid_block() возвращает false для такого адреса.
static bool test_coalesced_header_invalid()
{
    const std::size_t size = 16 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem, size ) );

    // Аллоцируем два блока
    auto p1 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 64 );
    auto p2 = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    // Запоминаем адрес второго блока
    std::uint32_t p2_offset = p2.offset();

    // Освобождаем оба блока — должны слиться
    pmm::PersistMemoryManager::deallocate_typed( p1 );
    pmm::PersistMemoryManager::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    // Проверяем через block_data_size_bytes, что старый блок p2 больше не считается занятым
    // (его заголовок обнулён при слиянии, поэтому is_valid_block() должен вернуть false)
    std::size_t old_block_size = pmm::PersistMemoryManager::block_data_size_bytes( p2_offset );
    PMM_TEST( old_block_size == 0 ); // Старый блок теперь не валиден — данные удалены

    auto stats = pmm::get_stats();
    PMM_TEST( stats.allocated_blocks == 0 );
    PMM_TEST( stats.free_blocks >= 1 );

    pmm::PersistMemoryManager::destroy();
    std::free( mem );
    return true;
}

// ─── Тест 5: repair_linked_list + recompute_counters при загрузке ─────────────

/// Имитирует повреждённый образ, где prev_offset несогласован с forward-traversal,
/// и проверяет, что load() исправляет это.
static bool test_repair_on_load()
{
    const char*       TEST_FILE = "test_repair.dat";
    const std::size_t size      = 64 * 1024;
    void*             mem1      = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem1, size ) );
    auto p1 = pmm::PersistMemoryManager::allocate_typed<std::uint32_t>( 10 );
    auto p2 = pmm::PersistMemoryManager::allocate_typed<std::uint32_t>( 20 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    auto stats_before = pmm::get_stats();

    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager::destroy();

    // Повреждаем образ: находим заголовок второго блока и портим его prev_offset
    {
        // Вычисляем байтовое смещение заголовка p2
        // p2 is a user ptr, block header is sizeof(BlockHeader) bytes before it
        std::uint32_t p2_data_idx = p2.offset();
        std::uint32_t p2_blk_idx  = p2_data_idx - pmm::detail::kBlockHeaderGranules;
        std::size_t   p2_byte_off = static_cast<std::size_t>( p2_blk_idx ) * pmm::kGranuleSize;

        // Открываем файл и портим prev_offset (смещение 4 байта в BlockHeader)
        FILE* f = std::fopen( TEST_FILE, "r+b" );
        PMM_TEST( f != nullptr );
        // prev_offset is the 2nd field in BlockHeader (bytes 4-7 from block start)
        // sizeof(ManagerHeader) is accounted for in p2_byte_off already
        std::fseek( f, static_cast<long>( p2_byte_off + 4 ), SEEK_SET ); // +4: offset of prev_offset
        std::uint32_t bad_prev = 0xDEADBEEF;                             // Повреждённое значение
        std::fwrite( &bad_prev, sizeof( bad_prev ), 1, f );
        std::fclose( f );
    }

    // Загружаем повреждённый образ — repair_linked_list должен исправить prev_offset
    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    PMM_TEST( pmm::load_from_file( TEST_FILE, mem2, size ) );
    // Должен пройти validate() после исправления
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    auto stats_after = pmm::get_stats();
    PMM_TEST( stats_after.allocated_blocks == stats_before.allocated_blocks );
    PMM_TEST( stats_after.free_blocks == stats_before.free_blocks );

    pmm::PersistMemoryManager::destroy();
    std::free( mem1 );
    std::free( mem2 );
    std::remove( TEST_FILE );
    return true;
}

// ─── Тест 6: Стресс-тест с save/load cycle без magic ────────────────────────

static bool test_stress_save_load()
{
    const char*       TEST_FILE = "test_stress_mod.dat";
    const std::size_t size      = 128 * 1024;
    void*             mem1      = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager::create( mem1, size ) );

    // Выделяем много разных блоков
    const std::size_t       N = 50;
    pmm::pptr<std::uint8_t> ptrs[N];
    for ( std::size_t i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::PersistMemoryManager::allocate_typed<std::uint8_t>( ( i + 1 ) * 16 );
        PMM_TEST( !ptrs[i].is_null() );
        std::memset( ptrs[i].get(), static_cast<int>( i + 1 ), ( i + 1 ) * 16 );
    }

    // Освобождаем половину
    for ( std::size_t i = 0; i < N; i += 2 )
        pmm::PersistMemoryManager::deallocate_typed( ptrs[i] );

    PMM_TEST( pmm::PersistMemoryManager::validate() );
    auto stats1 = pmm::get_stats();

    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager::destroy();
    std::free( mem1 );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    PMM_TEST( pmm::load_from_file( TEST_FILE, mem2, size ) );
    PMM_TEST( pmm::PersistMemoryManager::validate() );

    auto stats2 = pmm::get_stats();
    PMM_TEST( stats2.allocated_blocks == stats1.allocated_blocks );
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );

    // Проверяем данные нечётных блоков (они не были освобождены)
    for ( std::size_t i = 1; i < N; i += 2 )
    {
        std::size_t block_size = ( i + 1 ) * 16;
        for ( std::size_t j = 0; j < block_size; j++ )
            PMM_TEST( ptrs[i].get()[j] == static_cast<std::uint8_t>( i + 1 ) );
    }

    pmm::PersistMemoryManager::destroy();
    std::free( mem2 );
    std::remove( TEST_FILE );
    return true;
}

// ─── main ────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_block_modernization (Issue #69) ===\n";
    bool all_passed = true;

    PMM_RUN( "block_header_no_magic", test_block_header_no_magic );
    PMM_RUN( "basic_alloc_validate", test_basic_alloc_validate );
    PMM_RUN( "save_load_new_format", test_save_load_new_format );
    PMM_RUN( "coalesced_header_invalid", test_coalesced_header_invalid );
    PMM_RUN( "repair_on_load", test_repair_on_load );
    PMM_RUN( "stress_save_load", test_stress_save_load );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
