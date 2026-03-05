/**
 * @file test_persistence.cpp
 * @brief Тесты персистентности (save/load) — Фаза 3 (обновлено в Issue #61)
 *
 * Issue #61: менеджер — полностью статический класс.
 *   - save(filename) и load_from_file(filename, buf, size) без PersistMemoryManager*.
 *   - Все операции через статические методы PersistMemoryManager::xxx().
 *   - Выделение через allocate_typed<T>(), освобождение через deallocate_typed().
 */

#include "pmm/io.h"
#include "pmm/legacy_manager.h"

#include <cassert>
#include <cstdio>
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

static const char* TEST_FILE = "test_heap.dat";

static void cleanup_file()
{
    std::remove( TEST_FILE );
}

static bool test_persistence_basic_roundtrip()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem1, size ) );

    // Сохраняем статистику до destroy
    std::size_t total1 = pmm::PersistMemoryManager<>::total_size();
    std::size_t used1  = pmm::PersistMemoryManager<>::used_size();
    std::size_t free1  = pmm::PersistMemoryManager<>::free_size();
    auto        stats1 = pmm::get_stats();

    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem1 );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    PMM_TEST( pmm::load_from_file( TEST_FILE, mem2, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    PMM_TEST( pmm::PersistMemoryManager<>::total_size() == total1 );
    PMM_TEST( pmm::PersistMemoryManager<>::used_size() == used1 );
    PMM_TEST( pmm::PersistMemoryManager<>::free_size() == free1 );

    auto stats2 = pmm::get_stats();
    PMM_TEST( stats2.total_blocks == stats1.total_blocks );
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );
    PMM_TEST( stats2.allocated_blocks == stats1.allocated_blocks );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem2 );
    cleanup_file();
    return true;
}

static bool test_persistence_user_data_preserved()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem1, size ) );

    const std::size_t       data_size = 256;
    pmm::pptr<std::uint8_t> ptr1      = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( data_size );
    PMM_TEST( !ptr1.is_null() );

    std::memset( ptr1.get(), 0xCA, data_size );

    // Запоминаем смещение (гранульный индекс)
    std::uint32_t saved_offset = ptr1.offset();

    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem1 );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    PMM_TEST( pmm::load_from_file( TEST_FILE, mem2, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats2 = pmm::get_stats();
    PMM_TEST( stats2.allocated_blocks == 2 ); // Issue #75: 1 user block + BlockHeader_0

    // Восстанавливаем pptr по сохранённому смещению
    pmm::pptr<std::uint8_t> ptr2( saved_offset );
    const std::uint8_t*     p = ptr2.get();
    for ( std::size_t i = 0; i < data_size; i++ )
        PMM_TEST( p[i] == 0xCA );

    pmm::PersistMemoryManager<>::deallocate_typed( ptr2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem2 );
    cleanup_file();
    return true;
}

static bool test_persistence_multiple_blocks()
{
    const std::size_t size = 128 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem1, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p3 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
    pmm::pptr<std::uint8_t> p4 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() && !p4.is_null() );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::deallocate_typed( p4 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto        stats1 = pmm::get_stats();
    std::size_t total1 = pmm::PersistMemoryManager<>::total_size();
    std::size_t used1  = pmm::PersistMemoryManager<>::used_size();

    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem1 );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    PMM_TEST( pmm::load_from_file( TEST_FILE, mem2, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats2 = pmm::get_stats();
    PMM_TEST( stats2.total_blocks == stats1.total_blocks );
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );
    PMM_TEST( stats2.allocated_blocks == stats1.allocated_blocks );
    PMM_TEST( pmm::PersistMemoryManager<>::total_size() == total1 );
    PMM_TEST( pmm::PersistMemoryManager<>::used_size() == used1 );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem2 );
    cleanup_file();
    return true;
}

static bool test_persistence_allocate_after_load()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem1, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !p1.is_null() );

    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem1 );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    PMM_TEST( pmm::load_from_file( TEST_FILE, mem2, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats2 = pmm::get_stats();
    PMM_TEST( stats2.allocated_blocks == 3 ); // Issue #75: 1 pre-load + 1 new user + BlockHeader_0

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem2 );
    cleanup_file();
    return true;
}

static bool test_persistence_save_null_filename()
{
    const std::size_t size = 16 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    PMM_TEST( pmm::save( nullptr ) == false );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

static bool test_persistence_load_nonexistent_file()
{
    const std::size_t size = 16 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( !pmm::load_from_file( "no_such_file_xyz123.dat", mem, size ) );

    std::free( mem );
    return true;
}

static bool test_persistence_load_null_args()
{
    const std::size_t size = 16 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( !pmm::load_from_file( nullptr, mem, size ) );
    PMM_TEST( !pmm::load_from_file( TEST_FILE, nullptr, size ) );
    PMM_TEST( !pmm::load_from_file( TEST_FILE, mem, 0 ) );

    std::free( mem );
    cleanup_file();
    return true;
}

static bool test_persistence_corrupted_image()
{
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

    PMM_TEST( !pmm::load_from_file( TEST_FILE, mem, size ) );

    std::free( mem );
    cleanup_file();
    return true;
}

static bool test_persistence_buffer_too_small()
{
    const std::size_t size = 32 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem1, size ) );
    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem1 );

    const std::size_t small_size = 4 * 1024;
    void*             mem2       = std::malloc( small_size );
    PMM_TEST( mem2 != nullptr );

    PMM_TEST( !pmm::load_from_file( TEST_FILE, mem2, small_size ) );

    std::free( mem2 );
    cleanup_file();
    return true;
}

static bool test_persistence_double_save_load()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem1, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );
    std::memset( p1.get(), 0xAA, 128 );
    std::memset( p2.get(), 0xBB, 256 );

    auto        stats1 = pmm::get_stats();
    std::size_t total1 = pmm::PersistMemoryManager<>::total_size();

    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem1 );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );
    PMM_TEST( pmm::load_from_file( TEST_FILE, mem2, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    static const char* TEST_FILE2 = "test_heap2.dat";
    PMM_TEST( pmm::save( TEST_FILE2 ) );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem2 );

    void* mem3 = std::malloc( size );
    PMM_TEST( mem3 != nullptr );
    PMM_TEST( pmm::load_from_file( TEST_FILE2, mem3, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats3 = pmm::get_stats();
    PMM_TEST( stats3.total_blocks == stats1.total_blocks );
    PMM_TEST( stats3.allocated_blocks == stats1.allocated_blocks );
    PMM_TEST( pmm::PersistMemoryManager<>::total_size() == total1 );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem3 );
    std::remove( TEST_FILE );
    std::remove( TEST_FILE2 );
    return true;
}

static bool test_persistence_empty_manager()
{
    const std::size_t size = 16 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem1, size ) );

    auto stats1 = pmm::get_stats();
    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem1 );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    PMM_TEST( pmm::load_from_file( TEST_FILE, mem2, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats2 = pmm::get_stats();
    PMM_TEST( stats2.allocated_blocks == 1 ); // Issue #75: only BlockHeader_0 remains
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem2 );
    cleanup_file();
    return true;
}

static bool test_persistence_deallocate_after_load()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem1, size ) );

    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !p1.is_null() && !p2.is_null() );

    // Запоминаем гранульные индексы
    std::uint32_t off1 = p1.offset();
    std::uint32_t off2 = p2.offset();

    PMM_TEST( pmm::save( TEST_FILE ) );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem1 );

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    PMM_TEST( pmm::load_from_file( TEST_FILE, mem2, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    // Восстанавливаем pptr по сохранённым смещениям
    pmm::pptr<std::uint8_t> q1( off1 );
    pmm::pptr<std::uint8_t> q2( off2 );

    pmm::PersistMemoryManager<>::deallocate_typed( q1 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( q2 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    auto stats2 = pmm::get_stats();
    PMM_TEST( stats2.allocated_blocks == 1 ); // Issue #75: only BlockHeader_0 remains

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem2 );
    cleanup_file();
    return true;
}

int main()
{
    std::cout << "=== test_persistence ===\n";
    bool all_passed = true;

    PMM_RUN( "persistence_basic_roundtrip", test_persistence_basic_roundtrip );
    PMM_RUN( "persistence_user_data_preserved", test_persistence_user_data_preserved );
    PMM_RUN( "persistence_multiple_blocks", test_persistence_multiple_blocks );
    PMM_RUN( "persistence_allocate_after_load", test_persistence_allocate_after_load );
    PMM_RUN( "persistence_save_null_filename", test_persistence_save_null_filename );
    PMM_RUN( "persistence_load_nonexistent_file", test_persistence_load_nonexistent_file );
    PMM_RUN( "persistence_load_null_args", test_persistence_load_null_args );
    PMM_RUN( "persistence_corrupted_image", test_persistence_corrupted_image );
    PMM_RUN( "persistence_buffer_too_small", test_persistence_buffer_too_small );
    PMM_RUN( "persistence_double_save_load", test_persistence_double_save_load );
    PMM_RUN( "persistence_empty_manager", test_persistence_empty_manager );
    PMM_RUN( "persistence_deallocate_after_load", test_persistence_deallocate_after_load );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
