/**
 * @file test_persistence.cpp
 * @brief Тесты персистентности (save/load) — Фаза 3 (обновлено в Фазе 7)
 *
 * Фаза 7: синглтон, destroy() освобождает буфер.
 * load_from_file() и load() устанавливают синглтон автоматически.
 */

#include "persist_memory_io.h"
#include "persist_memory_manager.h"

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

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    PMM_TEST( pmm::save( mgr1, TEST_FILE ) );

    // Сохраняем статистику до destroy
    std::size_t total1 = mgr1->total_size();
    std::size_t used1  = mgr1->used_size();
    std::size_t free1  = mgr1->free_size();
    auto        stats1 = pmm::get_stats( mgr1 );

    pmm::PersistMemoryManager::destroy(); // освобождает mem1

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    PMM_TEST( mgr2->total_size() == total1 );
    PMM_TEST( mgr2->used_size() == used1 );
    PMM_TEST( mgr2->free_size() == free1 );

    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.total_blocks == stats1.total_blocks );
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );
    PMM_TEST( stats2.allocated_blocks == stats1.allocated_blocks );

    pmm::PersistMemoryManager::destroy();
    cleanup_file();
    return true;
}

static bool test_persistence_user_data_preserved()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    const std::size_t data_size = 256;
    void*             ptr1      = mgr1->allocate( data_size );
    PMM_TEST( ptr1 != nullptr );

    std::memset( ptr1, 0xCA, data_size );

    // Запоминаем смещение блока от базы
    std::ptrdiff_t block_offset =
        static_cast<std::uint8_t*>( ptr1 ) - static_cast<std::uint8_t*>( static_cast<void*>( mgr1 ) );

    PMM_TEST( pmm::save( mgr1, TEST_FILE ) );
    pmm::PersistMemoryManager::destroy();

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.allocated_blocks == 1 );

    // Данные по тому же смещению в новом буфере
    void*               ptr2 = static_cast<std::uint8_t*>( mem2 ) + block_offset;
    const std::uint8_t* p    = static_cast<const std::uint8_t*>( ptr2 );
    for ( std::size_t i = 0; i < data_size; i++ )
    {
        PMM_TEST( p[i] == 0xCA );
    }

    pmm::PersistMemoryManager::destroy();
    cleanup_file();
    return true;
}

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

    mgr1->deallocate( p2 );
    mgr1->deallocate( p4 );
    PMM_TEST( mgr1->validate() );

    auto        stats1 = pmm::get_stats( mgr1 );
    std::size_t total1 = mgr1->total_size();
    std::size_t used1  = mgr1->used_size();

    PMM_TEST( pmm::save( mgr1, TEST_FILE ) );
    pmm::PersistMemoryManager::destroy();

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.total_blocks == stats1.total_blocks );
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );
    PMM_TEST( stats2.allocated_blocks == stats1.allocated_blocks );
    PMM_TEST( mgr2->total_size() == total1 );
    PMM_TEST( mgr2->used_size() == used1 );

    pmm::PersistMemoryManager::destroy();
    cleanup_file();
    return true;
}

static bool test_persistence_allocate_after_load()
{
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    void* p1 = mgr1->allocate( 512 );
    PMM_TEST( p1 != nullptr );

    PMM_TEST( pmm::save( mgr1, TEST_FILE ) );
    pmm::PersistMemoryManager::destroy();

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    void* p2 = mgr2->allocate( 256 );
    PMM_TEST( p2 != nullptr );
    PMM_TEST( mgr2->validate() );

    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.allocated_blocks == 2 );

    mgr2->deallocate( p2 );
    PMM_TEST( mgr2->validate() );

    pmm::PersistMemoryManager::destroy();
    cleanup_file();
    return true;
}

static bool test_persistence_save_null_filename()
{
    const std::size_t size = 16 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::PersistMemoryManager::create( mem, size );
    PMM_TEST( mgr != nullptr );

    PMM_TEST( pmm::save( mgr, nullptr ) == false );

    pmm::PersistMemoryManager::destroy();
    return true;
}

static bool test_persistence_load_nonexistent_file()
{
    const std::size_t size = 16 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    pmm::PersistMemoryManager* mgr = pmm::load_from_file( "no_such_file_xyz123.dat", mem, size );
    PMM_TEST( mgr == nullptr );

    std::free( mem ); // load failed — free manually
    return true;
}

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

    pmm::PersistMemoryManager* mgr = pmm::load_from_file( TEST_FILE, mem, size );
    PMM_TEST( mgr == nullptr );

    std::free( mem ); // load failed — free manually
    cleanup_file();
    return true;
}

static bool test_persistence_buffer_too_small()
{
    const std::size_t size = 32 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );
    PMM_TEST( pmm::save( mgr1, TEST_FILE ) );
    pmm::PersistMemoryManager::destroy();

    const std::size_t small_size = 4 * 1024;
    void*             mem2       = std::malloc( small_size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, small_size );
    PMM_TEST( mgr2 == nullptr );

    std::free( mem2 ); // load failed — free manually
    cleanup_file();
    return true;
}

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

    auto        stats1 = pmm::get_stats( mgr1 );
    std::size_t total1 = mgr1->total_size();

    PMM_TEST( pmm::save( mgr1, TEST_FILE ) );
    pmm::PersistMemoryManager::destroy();

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );
    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    static const char* TEST_FILE2 = "test_heap2.dat";
    PMM_TEST( pmm::save( mgr2, TEST_FILE2 ) );
    pmm::PersistMemoryManager::destroy();

    void* mem3 = std::malloc( size );
    PMM_TEST( mem3 != nullptr );
    pmm::PersistMemoryManager* mgr3 = pmm::load_from_file( TEST_FILE2, mem3, size );
    PMM_TEST( mgr3 != nullptr );
    PMM_TEST( mgr3->validate() );

    auto stats3 = pmm::get_stats( mgr3 );
    PMM_TEST( stats3.total_blocks == stats1.total_blocks );
    PMM_TEST( stats3.allocated_blocks == stats1.allocated_blocks );
    PMM_TEST( mgr3->total_size() == total1 );

    pmm::PersistMemoryManager::destroy();
    std::remove( TEST_FILE );
    std::remove( TEST_FILE2 );
    return true;
}

static bool test_persistence_empty_manager()
{
    const std::size_t size = 16 * 1024;
    void*             mem1 = std::malloc( size );
    PMM_TEST( mem1 != nullptr );

    pmm::PersistMemoryManager* mgr1 = pmm::PersistMemoryManager::create( mem1, size );
    PMM_TEST( mgr1 != nullptr );

    auto stats1 = pmm::get_stats( mgr1 );
    PMM_TEST( pmm::save( mgr1, TEST_FILE ) );
    pmm::PersistMemoryManager::destroy();

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.allocated_blocks == 0 );
    PMM_TEST( stats2.free_blocks == stats1.free_blocks );

    void* p = mgr2->allocate( 512 );
    PMM_TEST( p != nullptr );
    PMM_TEST( mgr2->validate() );

    pmm::PersistMemoryManager::destroy();
    cleanup_file();
    return true;
}

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

    // Запоминаем смещения
    std::ptrdiff_t off1 = static_cast<std::uint8_t*>( p1 ) - static_cast<std::uint8_t*>( static_cast<void*>( mgr1 ) );
    std::ptrdiff_t off2 = static_cast<std::uint8_t*>( p2 ) - static_cast<std::uint8_t*>( static_cast<void*>( mgr1 ) );

    PMM_TEST( pmm::save( mgr1, TEST_FILE ) );
    pmm::PersistMemoryManager::destroy();

    void* mem2 = std::malloc( size );
    PMM_TEST( mem2 != nullptr );

    pmm::PersistMemoryManager* mgr2 = pmm::load_from_file( TEST_FILE, mem2, size );
    PMM_TEST( mgr2 != nullptr );
    PMM_TEST( mgr2->validate() );

    void* q1 = static_cast<std::uint8_t*>( mem2 ) + off1;
    void* q2 = static_cast<std::uint8_t*>( mem2 ) + off2;

    mgr2->deallocate( q1 );
    PMM_TEST( mgr2->validate() );

    mgr2->deallocate( q2 );
    PMM_TEST( mgr2->validate() );

    auto stats2 = pmm::get_stats( mgr2 );
    PMM_TEST( stats2.allocated_blocks == 0 );

    pmm::PersistMemoryManager::destroy();
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
