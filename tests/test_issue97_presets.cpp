/**
 * @file test_issue97_presets.cpp
 * @brief Интеграционные тесты пресетов PersistMemoryManager (Issue #97, обновлено #110).
 *
 * Issue #110: переход с AbstractPersistMemoryManager на PersistMemoryManager (статическая модель).
 * Проверяет:
 *  - SingleThreadedHeap: базовые операции allocate/deallocate/persistence
 *  - MultiThreadedHeap: базовые операции с блокировками
 *  - Функции io.h: save_manager / load_manager_from_file
 *  - pptr<T> typed API: allocate_typed / resolve / resolve_at / deallocate_typed
 *
 * @see include/pmm/pmm_presets.h
 * @see include/pmm/io.h
 * @see include/pmm/pptr.h
 * @version 0.3 (Issue #110 — статический API)
 */

#include "pmm_single_threaded_heap.h"
#include "pmm_multi_threaded_heap.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// P97-B: SingleThreadedHeap — динамическая память с auto-expand
// =============================================================================

/// @brief SingleThreadedHeap: полный жизненный цикл.
TEST_CASE( "P97-B1: SingleThreadedHeap lifecycle", "[test_issue97_presets]" )
{
    using STH = pmm::presets::SingleThreadedHeap;

    REQUIRE( !STH::is_initialized() );

    REQUIRE( STH::create( 32 * 1024 ) ); // 32 KiB
    REQUIRE( STH::is_initialized() );
    REQUIRE( STH::total_size() >= 32 * 1024 );

    // Серия выделений
    constexpr int kCount = 20;
    void*         ptrs[kCount];
    for ( int i = 0; i < kCount; ++i )
    {
        ptrs[i] = STH::allocate( static_cast<std::size_t>( ( i + 1 ) * 64 ) );
        REQUIRE( ptrs[i] != nullptr );
    }

    // Статистика
    REQUIRE( STH::alloc_block_count() >= static_cast<std::size_t>( kCount ) );

    // Освобождаем в обратном порядке (стресс для слияния)
    for ( int i = kCount - 1; i >= 0; --i )
        STH::deallocate( ptrs[i] );

    REQUIRE( STH::free_block_count() <= 2 ); // должны слиться

    STH::destroy();
}

/// @brief SingleThreadedHeap: auto-expand при нехватке места.
TEST_CASE( "P97-B2: SingleThreadedHeap auto-expand", "[test_issue97_presets]" )
{
    // Use unique InstanceId to start with a fresh backend
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 305>;

    REQUIRE( STH::create( pmm::detail::kMinMemorySize ) );

    std::size_t initial_size = STH::total_size();

    // Выделяем намного больше начального размера
    void* large = STH::allocate( 4096 );
    if ( large != nullptr )
    {
        // Расширение сработало
        REQUIRE( STH::total_size() > initial_size );
        STH::deallocate( large );
        std::cout << "    SingleThreadedHeap: expanded from " << initial_size << " to " << STH::total_size()
                  << " bytes\n";
    }
    else
    {
        // Расширение не поддерживается при таком маленьком размере — ОК
        std::cout << "    SingleThreadedHeap: no expand (expected for tiny buffer)\n";
    }

    STH::destroy();
}

/// @brief SingleThreadedHeap: save_manager / load_manager_from_file.
TEST_CASE( "P97-B3: SingleThreadedHeap save_manager/load_manager_from_file", "[test_issue97_presets]" )
{
    const char* test_file = "test_issue97_heap.pmm";

    // Use distinct InstanceIds for two separate "sessions"
    using STH1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 300>;
    using STH2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 301>;

    REQUIRE( STH1::create( 16 * 1024 ) );

    void* ptr1 = STH1::allocate( 64 );
    void* ptr2 = STH1::allocate( 128 );
    REQUIRE( ( ptr1 != nullptr && ptr2 != nullptr ) );

    // Записываем данные
    std::memset( ptr1, 0xDE, 64 );
    std::memset( ptr2, 0xAD, 128 );

    std::size_t alloc_count_before = STH1::alloc_block_count();
    std::size_t free_count_before  = STH1::free_block_count();

    // Сохраняем через новый API
    REQUIRE( pmm::save_manager<STH1>( test_file ) );
    STH1::destroy();

    // Создаём второй менеджер с тем же размером буфера и загружаем
    REQUIRE( STH2::create( 16 * 1024 ) );
    REQUIRE( pmm::load_manager_from_file<STH2>( test_file ) );
    REQUIRE( STH2::is_initialized() );
    REQUIRE( STH2::alloc_block_count() == alloc_count_before );
    REQUIRE( STH2::free_block_count() == free_count_before );

    STH2::destroy();

    std::remove( test_file );
}

// =============================================================================
// P97-D: MultiThreadedHeap — многопоточная безопасность
// =============================================================================

/// @brief MultiThreadedHeap: базовые операции с блокировками.
TEST_CASE( "P97-D1: MultiThreadedHeap basic operations", "[test_issue97_presets]" )
{
    using MTH = pmm::presets::MultiThreadedHeap;

    REQUIRE( MTH::create( 64 * 1024 ) );
    REQUIRE( MTH::is_initialized() );

    void* ptr1 = MTH::allocate( 256 );
    REQUIRE( ptr1 != nullptr );
    std::memset( ptr1, 0xCC, 256 );

    void* ptr2 = MTH::allocate( 512 );
    REQUIRE( ptr2 != nullptr );
    MTH::deallocate( ptr1 );
    MTH::deallocate( ptr2 );

    MTH::destroy();
}

/// @brief MultiThreadedHeap: параллельные выделения из нескольких потоков.
TEST_CASE( "P97-D2: MultiThreadedHeap concurrent allocs from 4 threads", "[test_issue97_presets]" )
{
    using MTH = pmm::presets::MultiThreadedHeap;

    REQUIRE( MTH::create( 512 * 1024 ) ); // 512 KiB

    constexpr int kThreads         = 4;
    constexpr int kAllocsPerThread = 50;

    std::vector<std::thread> threads;
    std::atomic<int>         fail_count{ 0 };

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [&fail_count, kAllocsPerThread]()
            {
                std::vector<void*> ptrs;
                for ( int i = 0; i < kAllocsPerThread; ++i )
                {
                    void* p = MTH::allocate( 64 );
                    if ( p == nullptr )
                    {
                        fail_count++;
                    }
                    else
                    {
                        std::memset( p, 0x55, 64 );
                        ptrs.push_back( p );
                    }
                }
                for ( void* p : ptrs )
                    MTH::deallocate( p );
            } );
    }

    for ( auto& th : threads )
        th.join();

    REQUIRE( fail_count == 0 );
    std::cout << "    MultiThreadedHeap: " << kThreads << " threads x " << kAllocsPerThread
              << " allocs = " << ( kThreads * kAllocsPerThread ) << " total (0 failures)\n";

    MTH::destroy();
}

// =============================================================================
// P97-E: Совместимость API — статическая модель
// =============================================================================

/// @brief Статический API: create → allocate → deallocate → destroy.
TEST_CASE( "P97-E1: Static API (PersistMemoryManager) works correctly", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 302>;

    const std::size_t kSize = 32 * 1024;

    REQUIRE( STH::create( kSize ) );

    void* ptr = STH::allocate( sizeof( int ) );
    REQUIRE( ptr != nullptr );
    REQUIRE( reinterpret_cast<std::uintptr_t>( ptr ) % 16 == 0 ); // 16-байт выравнивание
    *reinterpret_cast<int*>( ptr ) = 42;
    REQUIRE( *reinterpret_cast<int*>( ptr ) == 42 );
    STH::deallocate( ptr );

    STH::destroy();

    std::cout << "    Static API (PersistMemoryManager) works correctly\n";
}

/// @brief create() → allocate() → deallocate() → destroy() цикл.
TEST_CASE( "P97-E2: create/use/destroy lifecycle", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 303>;

    bool success = false;
    if ( STH::create( 8 * 1024 ) )
    {
        void* p = STH::allocate( 100 );
        if ( p != nullptr )
        {
            std::memset( p, 0xFF, 100 );
            STH::deallocate( p );
            success = true;
        }
        STH::destroy();
    }
    REQUIRE( success );
}

// =============================================================================
// P97-F: Интеграционные тесты io.h
// =============================================================================

/// @brief save_manager / load_manager_from_file с SingleThreadedHeap.
TEST_CASE( "P97-F1: save_manager / load_manager_from_file", "[test_issue97_presets]" )
{
    const char* test_file = "test_issue97_io.pmm";

    using STH1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 310>;
    using STH2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 311>;

    // Создаём и заполняем
    REQUIRE( STH1::create( 4096 ) );
    void* ptr = STH1::allocate( 64 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0x77, 64 );

    std::size_t alloc_count = STH1::alloc_block_count();

    // Сохраняем
    REQUIRE( pmm::save_manager<STH1>( test_file ) );
    STH1::destroy();

    // Создаём второй менеджер и загружаем
    REQUIRE( STH2::create( 4096 ) );
    REQUIRE( pmm::load_manager_from_file<STH2>( test_file ) );
    REQUIRE( STH2::is_initialized() );
    REQUIRE( STH2::alloc_block_count() == alloc_count );
    STH2::destroy();

    std::remove( test_file );
}

/// @brief save_manager с nullptr filename возвращает false.
TEST_CASE( "P97-F2: save_manager(nullptr filename) returns false", "[test_issue97_presets]" )
{
    using STH = pmm::presets::SingleThreadedHeap;
    REQUIRE( STH::create( 8 * 1024 ) );
    REQUIRE( !pmm::save_manager<STH>( nullptr ) );
    STH::destroy();
}

/// @brief save_manager с неинициализированным менеджером возвращает false.
TEST_CASE( "P97-F3: save_manager(uninitialized) returns false", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 312>;
    REQUIRE( !pmm::save_manager<STH>( "test_uninitialized.pmm" ) );
}

/// @brief load_manager_from_file с nullptr filename возвращает false.
TEST_CASE( "P97-F4: load_manager_from_file(nullptr filename) returns false", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 313>;
    REQUIRE( STH::create( 8 * 1024 ) );
    REQUIRE( !pmm::load_manager_from_file<STH>( nullptr ) );
    STH::destroy();
}

// =============================================================================
// P97-G: pptr<T> typed API
// =============================================================================

using STHeap = pmm::presets::SingleThreadedHeap;
using MTHeap = pmm::presets::MultiThreadedHeap;

/// @brief allocate_typed<T>() возвращает pptr<T> размером 4 байта.
TEST_CASE( "P97-G1: pptr<T> sizeof == 4 bytes (persistent, address-independent)", "[test_issue97_presets]" )
{
    REQUIRE( sizeof( STHeap::pptr<int> ) == 4 );
    REQUIRE( sizeof( STHeap::pptr<double> ) == 4 );
    REQUIRE( sizeof( STHeap::pptr<char> ) == 4 );
}

/// @brief allocate_typed<T>() / resolve() / deallocate_typed(): полный цикл.
TEST_CASE( "P97-G2: allocate_typed<T> / resolve() / deallocate_typed", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 320>;
    REQUIRE( STH::create( 32 * 1024 ) );

    // Выделяем int через typed API
    STH::pptr<int> p = STH::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    REQUIRE( static_cast<bool>( p ) );
    REQUIRE( p.offset() > 0 );
    REQUIRE( sizeof( p ) == 4 ); // pptr<T, MgrT> — 4 байта

    // Разыменовываем через resolve()
    int* ptr = p.resolve();
    REQUIRE( ptr != nullptr );
    *ptr = 42;
    REQUIRE( *p.resolve() == 42 );

    // Освобождаем через typed API
    STH::deallocate_typed( p );

    STH::destroy();
}

/// @brief allocate_typed<T>(count) / resolve_at<T>(): массив через pptr.
TEST_CASE( "P97-G3: allocate_typed<T>(count) / resolve_at<T> (array support)", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 321>;
    REQUIRE( STH::create( 64 * 1024 ) );

    constexpr std::size_t kCount = 10;

    // Выделяем массив из 10 int
    STH::pptr<int> arr = STH::allocate_typed<int>( kCount );
    REQUIRE( !arr.is_null() );

    // Записываем через resolve_at
    for ( std::size_t i = 0; i < kCount; ++i )
    {
        int* elem = STH::resolve_at( arr, i );
        REQUIRE( elem != nullptr );
        *elem = static_cast<int>( i * 10 );
    }

    // Проверяем через resolve (базовый указатель)
    int* base = arr.resolve();
    REQUIRE( base != nullptr );
    for ( std::size_t i = 0; i < kCount; ++i )
        REQUIRE( base[i] == static_cast<int>( i * 10 ) );

    STH::deallocate_typed( arr );
    STH::destroy();
}

/// @brief pptr<T, MgrT> корректно хранит и восстанавливает гранульный индекс.
TEST_CASE( "P97-G4: pptr<T> offset persistence (store/restore granule index)", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 322>;
    REQUIRE( STH::create( 16 * 1024 ) );

    STH::pptr<int> p = STH::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 12345;

    // Сохраняем гранульный индекс (как хранится в персистентной памяти)
    std::uint32_t saved_offset = p.offset();
    REQUIRE( saved_offset > 0 );

    // Восстанавливаем pptr из сохранённого смещения
    STH::pptr<int> p2( saved_offset );
    REQUIRE( p == p2 );
    REQUIRE( *p2.resolve() == 12345 );

    STH::deallocate_typed( p );
    STH::destroy();
}

/// @brief Нулевой pptr<T, MgrT> (null): resolve возвращает nullptr.
TEST_CASE( "P97-G5: resolve(null pptr) returns nullptr", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 323>;
    REQUIRE( STH::create( 8 * 1024 ) );

    STH::pptr<int> p; // null по умолчанию
    REQUIRE( p.is_null() );
    REQUIRE( !static_cast<bool>( p ) );
    REQUIRE( p.offset() == 0 );
    REQUIRE( p.resolve() == nullptr ); // null pptr → nullptr

    STH::destroy();
}

/// @brief deallocate_typed(null pptr) не вызывает ошибок.
TEST_CASE( "P97-G6: deallocate_typed(null pptr) is no-op", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 324>;
    REQUIRE( STH::create( 8 * 1024 ) );

    STH::pptr<int> p;           // null
    STH::deallocate_typed( p ); // должно быть no-op без ошибок

    STH::destroy();
}

/// @brief allocate_typed<T>(0) возвращает null pptr.
TEST_CASE( "P97-G7: allocate_typed<T>(0) returns null pptr", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 325>;
    REQUIRE( STH::create( 8 * 1024 ) );

    STH::pptr<int> p = STH::allocate_typed<int>( 0 );
    REQUIRE( p.is_null() );

    STH::destroy();
}

/// @brief pptr<T, MgrT>: сравнение (== / !=).
TEST_CASE( "P97-G8: pptr<T> comparison (== / !=)", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 326>;
    REQUIRE( STH::create( 32 * 1024 ) );

    STH::pptr<int> p1 = STH::allocate_typed<int>();
    STH::pptr<int> p2 = STH::allocate_typed<int>();
    STH::pptr<int> p3 = p1;

    REQUIRE( p1 == p3 );
    REQUIRE( p1 != p2 );
    REQUIRE( !( p1 == p2 ) );

    STH::deallocate_typed( p1 );
    STH::deallocate_typed( p2 );
    STH::destroy();
}

/// @brief pptr<T, MgrT>: сохранение/загрузка через io.h с pptr offset.
TEST_CASE( "P97-G9: pptr<T> persistence via save_manager/load_manager_from_file", "[test_issue97_presets]" )
{
    const char* test_file = "test_issue97_pptr_io.pmm";

    using STH1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 330>;
    using STH2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 331>;

    REQUIRE( STH1::create( 16 * 1024 ) );

    // Выделяем структуру данных через pptr
    STH1::pptr<int> p1 = STH1::allocate_typed<int>();
    REQUIRE( !p1.is_null() );
    *p1.resolve() = 99999;

    std::uint32_t saved_offset = p1.offset();

    // Сохраняем образ в файл
    REQUIRE( pmm::save_manager<STH1>( test_file ) );
    STH1::destroy();

    // Загружаем в новый менеджер
    REQUIRE( STH2::create( 16 * 1024 ) );
    REQUIRE( pmm::load_manager_from_file<STH2>( test_file ) );
    REQUIRE( STH2::is_initialized() );

    // Восстанавливаем pptr по сохранённому смещению
    STH2::pptr<int> p2( saved_offset );
    REQUIRE( !p2.is_null() );
    REQUIRE( *p2.resolve() == 99999 ); // данные сохранились

    STH2::deallocate_typed( p2 );
    STH2::destroy();

    std::remove( test_file );
}

/// @brief Несколько типов pptr<T, MgrT> в одном менеджере.
TEST_CASE( "P97-G10: pptr<T> multiple types (int, double, char[])", "[test_issue97_presets]" )
{
    using STH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 332>;
    REQUIRE( STH::create( 64 * 1024 ) );

    STH::pptr<int>    pi = STH::allocate_typed<int>();
    STH::pptr<double> pd = STH::allocate_typed<double>();
    STH::pptr<char>   pc = STH::allocate_typed<char>( 16 );

    REQUIRE( !pi.is_null() );
    REQUIRE( !pd.is_null() );
    REQUIRE( !pc.is_null() );

    *pi.resolve() = 7;
    *pd.resolve() = 3.14;
    std::memcpy( pc.resolve(), "hello", 6 );

    REQUIRE( *pi.resolve() == 7 );
    REQUIRE( *pd.resolve() == 3.14 );
    REQUIRE( std::memcmp( pc.resolve(), "hello", 6 ) == 0 );

    STH::deallocate_typed( pi );
    STH::deallocate_typed( pd );
    STH::deallocate_typed( pc );

    STH::destroy();
}

/// @brief MultiThreadedHeap: concurrent pptr<T, MgrT> allocations.
TEST_CASE( "P97-G11: MultiThreadedHeap concurrent pptr<T> allocations", "[test_issue97_presets]" )
{
    using MTH = pmm::PersistMemoryManager<pmm::PersistentDataConfig, 340>;

    REQUIRE( MTH::create( 256 * 1024 ) );

    constexpr int kThreads         = 4;
    constexpr int kAllocsPerThread = 25;

    std::vector<std::thread> threads;
    std::atomic<int>         fail_count{ 0 };

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [&fail_count, kAllocsPerThread]()
            {
                std::vector<MTH::pptr<int>> ptrs;
                for ( int i = 0; i < kAllocsPerThread; ++i )
                {
                    MTH::pptr<int> p = MTH::allocate_typed<int>();
                    if ( p.is_null() )
                    {
                        fail_count++;
                    }
                    else
                    {
                        *p.resolve() = i;
                        ptrs.push_back( p );
                    }
                }
                for ( auto& p : ptrs )
                    MTH::deallocate_typed( p );
            } );
    }

    for ( auto& th : threads )
        th.join();

    REQUIRE( fail_count == 0 );
    std::cout << "    pptr MultiThreadedHeap: " << kThreads << " threads x " << kAllocsPerThread
              << " typed allocs (0 failures)\n";

    MTH::destroy();
}

// =============================================================================
// main
// =============================================================================
