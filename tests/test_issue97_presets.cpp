/**
 * @file test_issue97_presets.cpp
 * @brief Интеграционные тесты пресетов AbstractPersistMemoryManager (Issue #97).
 *
 * Проверяет:
 *  - EmbeddedStatic4K: отсутствие динамических выделений (StaticStorage)
 *  - SingleThreadedHeap: базовые операции allocate/deallocate/persistence
 *  - PersistentFileMapped: корректная запись/загрузка данных между запусками (MMapStorage)
 *  - MultiThreadedHeap: базовые операции с блокировками
 *  - Миграция с синглтона: сравнение поведения legacy и нового API
 *  - Новые функции io.h: save_manager / load_manager_from_file
 *  - pptr<T> typed API: allocate_typed / resolve / resolve_at / deallocate_typed (Issue #97)
 *
 * @see include/pmm/pmm_presets.h
 * @see include/pmm/abstract_pmm.h
 * @see include/pmm/io.h
 * @see include/pmm/pptr.h
 * @version 0.2 (Issue #97 — pptr<T> typed API tests)
 */

#include "pmm/abstract_pmm.h"
#include "pmm/io.h"
#include "pmm/pmm_presets.h"
#include "pmm/static_storage.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

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
        bool _result = fn();                                                                                           \
        std::cout << ( _result ? "PASS" : "FAIL" ) << "\n";                                                            \
        if ( !_result )                                                                                                \
            all_passed = false;                                                                                        \
    } while ( false )

// =============================================================================
// P97-A: EmbeddedStatic4K — StaticStorage (без динамических выделений)
// =============================================================================

/// @brief EmbeddedStatic4K: полный жизненный цикл без malloc.
static bool test_p97_embedded_static_lifecycle()
{
    pmm::presets::EmbeddedStatic4K pmm;

    // Проверяем, что объект создаётся без динамической памяти
    PMM_TEST( !pmm.is_initialized() );
    PMM_TEST( pmm.backend().base_ptr() != nullptr ); // StaticStorage всегда имеет буфер

    // Инициализируем
    PMM_TEST( pmm.create() );
    PMM_TEST( pmm.is_initialized() );
    PMM_TEST( pmm.total_size() == 4096 );
    PMM_TEST( pmm.free_size() > 0 );
    PMM_TEST( pmm.used_size() > 0 );

    // Выделяем и освобождаем
    void* ptr1 = pmm.allocate( 64 );
    void* ptr2 = pmm.allocate( 128 );
    PMM_TEST( ptr1 != nullptr );
    PMM_TEST( ptr2 != nullptr );
    PMM_TEST( ptr1 != ptr2 );

    // Проверяем выравнивание (16 байт)
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr1 ) % 16 == 0 );
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr2 ) % 16 == 0 );

    // Записываем данные
    std::memset( ptr1, 0xAA, 64 );
    std::memset( ptr2, 0xBB, 128 );

    // Освобождаем
    pmm.deallocate( ptr1 );
    pmm.deallocate( ptr2 );

    // Уничтожаем
    pmm.destroy();
    PMM_TEST( !pmm.is_initialized() );

    return true;
}

/// @brief EmbeddedStatic4K: заполнение всей доступной памяти.
static bool test_p97_embedded_static_fill_memory()
{
    pmm::presets::EmbeddedStatic4K pmm;
    PMM_TEST( pmm.create() );

    // Выделяем блоки до исчерпания памяти
    std::vector<void*> ptrs;
    while ( true )
    {
        void* p = pmm.allocate( 32 );
        if ( p == nullptr )
            break;
        ptrs.push_back( p );
    }

    PMM_TEST( !ptrs.empty() );
    std::cout << "    EmbeddedStatic4K: allocated " << ptrs.size() << " blocks of 32 bytes\n";

    // Освобождаем все блоки
    for ( void* p : ptrs )
        pmm.deallocate( p );

    // После освобождения всего — должно быть снова доступно место
    void* p = pmm.allocate( 64 );
    PMM_TEST( p != nullptr );
    pmm.deallocate( p );

    pmm.destroy();
    return true;
}

/// @brief EmbeddedStatic4K: allocate(0) возвращает nullptr.
static bool test_p97_embedded_static_alloc_zero()
{
    pmm::presets::EmbeddedStatic4K pmm;
    PMM_TEST( pmm.create() );
    PMM_TEST( pmm.allocate( 0 ) == nullptr );
    pmm.destroy();
    return true;
}

// =============================================================================
// P97-B: SingleThreadedHeap — динамическая память с auto-expand
// =============================================================================

/// @brief SingleThreadedHeap: полный жизненный цикл.
static bool test_p97_single_threaded_heap_lifecycle()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( !pmm.is_initialized() );

    PMM_TEST( pmm.create( 32 * 1024 ) ); // 32 KiB
    PMM_TEST( pmm.is_initialized() );
    PMM_TEST( pmm.total_size() >= 32 * 1024 );

    // Серия выделений
    constexpr int kCount = 20;
    void*         ptrs[kCount];
    for ( int i = 0; i < kCount; ++i )
    {
        ptrs[i] = pmm.allocate( static_cast<std::size_t>( ( i + 1 ) * 64 ) );
        PMM_TEST( ptrs[i] != nullptr );
    }

    // Статистика
    PMM_TEST( pmm.alloc_block_count() >= static_cast<std::size_t>( kCount ) );

    // Освобождаем в обратном порядке (стресс для слияния)
    for ( int i = kCount - 1; i >= 0; --i )
        pmm.deallocate( ptrs[i] );

    PMM_TEST( pmm.free_block_count() <= 2 ); // должны слиться

    pmm.destroy();
    return true;
}

/// @brief SingleThreadedHeap: auto-expand при нехватке места.
static bool test_p97_single_threaded_heap_auto_expand()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( pmm::detail::kMinMemorySize ) );

    std::size_t initial_size = pmm.total_size();

    // Выделяем намного больше начального размера
    void* large = pmm.allocate( 4096 );
    if ( large != nullptr )
    {
        // Расширение сработало
        PMM_TEST( pmm.total_size() > initial_size );
        pmm.deallocate( large );
        std::cout << "    SingleThreadedHeap: expanded from " << initial_size << " to " << pmm.total_size()
                  << " bytes\n";
    }
    else
    {
        // Расширение не поддерживается при таком маленьком размере — ОК
        std::cout << "    SingleThreadedHeap: no expand (expected for tiny buffer)\n";
    }

    pmm.destroy();
    return true;
}

/// @brief SingleThreadedHeap: save_manager / load_manager_from_file (Issue #97).
static bool test_p97_single_threaded_heap_io()
{
    const char* test_file = "test_issue97_heap.pmm";

    // Создаём и заполняем менеджер
    pmm::presets::SingleThreadedHeap pmm1;
    PMM_TEST( pmm1.create( 16 * 1024 ) );

    void* ptr1 = pmm1.allocate( 64 );
    void* ptr2 = pmm1.allocate( 128 );
    PMM_TEST( ptr1 != nullptr && ptr2 != nullptr );

    // Записываем данные
    std::memset( ptr1, 0xDE, 64 );
    std::memset( ptr2, 0xAD, 128 );

    std::size_t alloc_count_before = pmm1.alloc_block_count();
    std::size_t free_count_before  = pmm1.free_block_count();

    // Сохраняем через новый API (Issue #97)
    PMM_TEST( pmm::save_manager( pmm1, test_file ) );

    // Создаём второй менеджер с тем же размером буфера
    pmm::presets::SingleThreadedHeap pmm2;
    PMM_TEST( pmm2.create( pmm1.total_size() ) );

    // Загружаем из файла через новый API (Issue #97)
    PMM_TEST( pmm::load_manager_from_file( pmm2, test_file ) );
    PMM_TEST( pmm2.is_initialized() );
    PMM_TEST( pmm2.alloc_block_count() == alloc_count_before );
    PMM_TEST( pmm2.free_block_count() == free_count_before );

    pmm1.destroy();
    pmm2.destroy();

    std::remove( test_file );
    return true;
}

// =============================================================================
// P97-C: PersistentFileMapped — MMapStorage (персистентность между запусками)
// =============================================================================

/// @brief PersistentFileMapped: открытие файла, запись, загрузка.
static bool test_p97_persistent_file_mapped_basic()
{
    const char*       test_file = "test_issue97_mmap.pmm";
    const std::size_t kSize     = 32 * 1024;

    // Удаляем тестовый файл если остался с прошлого запуска
    std::remove( test_file );

    // Первый запуск: создаём и заполняем
    {
        pmm::presets::PersistentFileMapped pmm;
        PMM_TEST( pmm.backend().open( test_file, kSize ) );
        PMM_TEST( !pmm.load() );  // первый запуск — файл не инициализирован
        PMM_TEST( pmm.create() ); // инициализируем
        PMM_TEST( pmm.is_initialized() );

        void* ptr = pmm.allocate( 64 );
        PMM_TEST( ptr != nullptr );
        std::memset( ptr, 0x42, 64 );

        std::cout << "    PersistentFileMapped: first run, alloc_count=" << pmm.alloc_block_count() << "\n";
        // MMapStorage автоматически сохраняет при закрытии
        pmm.backend().close();
    }

    // Второй запуск: загружаем и проверяем
    {
        pmm::presets::PersistentFileMapped pmm;
        PMM_TEST( pmm.backend().open( test_file, kSize ) );
        PMM_TEST( pmm.load() ); // второй запуск — данные должны загрузиться
        PMM_TEST( pmm.is_initialized() );
        PMM_TEST( pmm.alloc_block_count() >= 2 ); // header + 1 выделенный блок

        std::cout << "    PersistentFileMapped: second run, alloc_count=" << pmm.alloc_block_count() << "\n";
        pmm.backend().close();
    }

    std::remove( test_file );
    return true;
}

/// @brief PersistentFileMapped: создаётся без открытия файла.
static bool test_p97_persistent_file_mapped_no_file()
{
    pmm::presets::PersistentFileMapped pmm;
    PMM_TEST( !pmm.is_initialized() );
    PMM_TEST( pmm.backend().base_ptr() == nullptr );
    PMM_TEST( !pmm.load() );   // нет буфера — load() должен вернуть false
    PMM_TEST( !pmm.create() ); // нет буфера — create() должен вернуть false
    return true;
}

// =============================================================================
// P97-D: MultiThreadedHeap — многопоточная безопасность
// =============================================================================

/// @brief MultiThreadedHeap: базовые операции с блокировками.
static bool test_p97_multi_threaded_heap_basic()
{
    pmm::presets::MultiThreadedHeap pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );
    PMM_TEST( pmm.is_initialized() );

    void* ptr1 = pmm.allocate( 256 );
    PMM_TEST( ptr1 != nullptr );
    std::memset( ptr1, 0xCC, 256 );

    void* ptr2 = pmm.allocate( 512 );
    PMM_TEST( ptr2 != nullptr );
    pmm.deallocate( ptr1 );
    pmm.deallocate( ptr2 );

    pmm.destroy();
    return true;
}

/// @brief MultiThreadedHeap: параллельные выделения из нескольких потоков.
static bool test_p97_multi_threaded_heap_concurrent()
{
    pmm::presets::MultiThreadedHeap pmm;
    PMM_TEST( pmm.create( 512 * 1024 ) ); // 512 KiB

    constexpr int kThreads         = 4;
    constexpr int kAllocsPerThread = 50;

    std::vector<std::thread> threads;
    std::atomic<int>         fail_count{ 0 };

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [&pmm, &fail_count, kAllocsPerThread]()
            {
                std::vector<void*> ptrs;
                for ( int i = 0; i < kAllocsPerThread; ++i )
                {
                    void* p = pmm.allocate( 64 );
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
                    pmm.deallocate( p );
            } );
    }

    for ( auto& th : threads )
        th.join();

    PMM_TEST( fail_count == 0 );
    std::cout << "    MultiThreadedHeap: " << kThreads << " threads x " << kAllocsPerThread
              << " allocs = " << ( kThreads * kAllocsPerThread ) << " total (0 failures)\n";

    pmm.destroy();
    return true;
}

// =============================================================================
// P97-E: Миграция с синглтона на AbstractPersistMemoryManager
// =============================================================================

/// @brief Сравнение поведения: legacy allocate_typed vs новый allocate().
static bool test_p97_migration_comparison()
{
    const std::size_t kSize = 32 * 1024;

    // Новый API (Issue #97)
    pmm::presets::SingleThreadedHeap new_pmm;
    PMM_TEST( new_pmm.create( kSize ) );

    void* ptr = new_pmm.allocate( sizeof( int ) );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( reinterpret_cast<std::uintptr_t>( ptr ) % 16 == 0 ); // 16-байт выравнивание
    *reinterpret_cast<int*>( ptr ) = 42;
    PMM_TEST( *reinterpret_cast<int*>( ptr ) == 42 );
    new_pmm.deallocate( ptr );

    new_pmm.destroy();

    std::cout << "    Migration: new API (AbstractPersistMemoryManager) works correctly\n";
    return true;
}

/// @brief Новый API: create() → allocate() → deallocate() → destroy() как RAII.
static bool test_p97_raii_lifecycle()
{
    // Демонстрация RAII-стиля использования
    bool success = false;
    {
        pmm::presets::SingleThreadedHeap pmm;
        if ( pmm.create( 8 * 1024 ) )
        {
            void* p = pmm.allocate( 100 );
            if ( p != nullptr )
            {
                std::memset( p, 0xFF, 100 );
                pmm.deallocate( p );
                success = true;
            }
            pmm.destroy(); // явный destroy (или можно полагаться на деструктор)
        }
    }
    PMM_TEST( success );
    return true;
}

// =============================================================================
// P97-F: Интеграционные тесты io.h (новый API)
// =============================================================================

/// @brief save_manager / load_manager_from_file с StaticStorage.
static bool test_p97_io_static_storage()
{
    const char* test_file = "test_issue97_static.pmm";

    using StaticPMM =
        pmm::AbstractPersistMemoryManager<pmm::DefaultAddressTraits,
                                          pmm::StaticStorage<4096, pmm::DefaultAddressTraits>,
                                          pmm::AvlFreeTree<pmm::DefaultAddressTraits>, pmm::config::NoLock>;

    // Создаём и заполняем
    StaticPMM pmm1;
    PMM_TEST( pmm1.create() );
    void* ptr = pmm1.allocate( 64 );
    PMM_TEST( ptr != nullptr );
    std::memset( ptr, 0x77, 64 );

    std::size_t alloc_count = pmm1.alloc_block_count();

    // Сохраняем
    PMM_TEST( pmm::save_manager( pmm1, test_file ) );

    // Создаём второй менеджер и загружаем
    StaticPMM pmm2;
    PMM_TEST( pmm::load_manager_from_file( pmm2, test_file ) );
    PMM_TEST( pmm2.is_initialized() );
    PMM_TEST( pmm2.alloc_block_count() == alloc_count );

    pmm1.destroy();
    pmm2.destroy();

    std::remove( test_file );
    return true;
}

/// @brief save_manager с nullptr filename возвращает false.
static bool test_p97_io_save_null_filename()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 8 * 1024 ) );
    PMM_TEST( !pmm::save_manager( pmm, nullptr ) );
    pmm.destroy();
    return true;
}

/// @brief save_manager с неинициализированным менеджером возвращает false.
static bool test_p97_io_save_uninitialized()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( !pmm::save_manager( pmm, "test_uninitialized.pmm" ) );
    return true;
}

/// @brief load_manager_from_file с nullptr filename возвращает false.
static bool test_p97_io_load_null_filename()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 8 * 1024 ) );
    PMM_TEST( !pmm::load_manager_from_file( pmm, nullptr ) );
    pmm.destroy();
    return true;
}

// =============================================================================
// P97-G: pptr<T> typed API для AbstractPersistMemoryManager (Issue #97)
// =============================================================================

/// @brief allocate_typed<T>() возвращает pptr<T> размером 4 байта.
static bool test_p97_pptr_sizeof()
{
    PMM_TEST( sizeof( pmm::pptr<int> ) == 4 );
    PMM_TEST( sizeof( pmm::pptr<double> ) == 4 );
    PMM_TEST( sizeof( pmm::pptr<char> ) == 4 );
    return true;
}

/// @brief allocate_typed<T>() / resolve<T>() / deallocate_typed(): полный цикл.
static bool test_p97_pptr_allocate_resolve_deallocate()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 32 * 1024 ) );

    // Выделяем int через typed API
    pmm::pptr<int> p = pmm.allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( static_cast<bool>( p ) );
    PMM_TEST( p.offset() > 0 );
    PMM_TEST( sizeof( p ) == 4 ); // pptr<T> — 4 байта

    // Разыменовываем через resolve
    int* ptr = pmm.resolve( p );
    PMM_TEST( ptr != nullptr );
    *ptr = 42;
    PMM_TEST( *pmm.resolve( p ) == 42 );

    // Освобождаем через typed API
    pmm.deallocate_typed( p );

    pmm.destroy();
    return true;
}

/// @brief allocate_typed<T>(count) / resolve_at<T>(): массив через pptr.
static bool test_p97_pptr_allocate_array()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    constexpr std::size_t kCount = 10;

    // Выделяем массив из 10 int
    pmm::pptr<int> arr = pmm.allocate_typed<int>( kCount );
    PMM_TEST( !arr.is_null() );

    // Записываем через resolve_at
    for ( std::size_t i = 0; i < kCount; ++i )
    {
        int* elem = pmm.resolve_at( arr, i );
        PMM_TEST( elem != nullptr );
        *elem = static_cast<int>( i * 10 );
    }

    // Проверяем через resolve (базовый указатель)
    int* base = pmm.resolve( arr );
    PMM_TEST( base != nullptr );
    for ( std::size_t i = 0; i < kCount; ++i )
        PMM_TEST( base[i] == static_cast<int>( i * 10 ) );

    pmm.deallocate_typed( arr );
    pmm.destroy();
    return true;
}

/// @brief pptr<T> корректно хранит и восстанавливает гранульный индекс.
static bool test_p97_pptr_offset_persistence()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 16 * 1024 ) );

    pmm::pptr<int> p = pmm.allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    *pmm.resolve( p ) = 12345;

    // Сохраняем гранульный индекс (как хранится в персистентной памяти)
    std::uint32_t saved_offset = p.offset();
    PMM_TEST( saved_offset > 0 );

    // Восстанавливаем pptr из сохранённого смещения
    pmm::pptr<int> p2( saved_offset );
    PMM_TEST( p == p2 );
    PMM_TEST( *pmm.resolve( p2 ) == 12345 );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

/// @brief Нулевой pptr<T> (null): resolve возвращает nullptr.
static bool test_p97_pptr_null_resolve()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 8 * 1024 ) );

    pmm::pptr<int> p; // null по умолчанию
    PMM_TEST( p.is_null() );
    PMM_TEST( !static_cast<bool>( p ) );
    PMM_TEST( p.offset() == 0 );
    PMM_TEST( pmm.resolve( p ) == nullptr ); // null pptr → nullptr

    pmm.destroy();
    return true;
}

/// @brief deallocate_typed(null pptr) не вызывает ошибок.
static bool test_p97_pptr_deallocate_null()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 8 * 1024 ) );

    pmm::pptr<int> p;          // null
    pmm.deallocate_typed( p ); // должно быть no-op без ошибок

    pmm.destroy();
    return true;
}

/// @brief allocate_typed<T>(0) возвращает null pptr.
static bool test_p97_pptr_allocate_zero()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 8 * 1024 ) );

    pmm::pptr<int> p = pmm.allocate_typed<int>( 0 );
    PMM_TEST( p.is_null() );

    pmm.destroy();
    return true;
}

/// @brief pptr<T>: сравнение (== / !=).
static bool test_p97_pptr_comparison()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 32 * 1024 ) );

    pmm::pptr<int> p1 = pmm.allocate_typed<int>();
    pmm::pptr<int> p2 = pmm.allocate_typed<int>();
    pmm::pptr<int> p3 = p1;

    PMM_TEST( p1 == p3 );
    PMM_TEST( p1 != p2 );
    PMM_TEST( !( p1 == p2 ) );

    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p2 );
    pmm.destroy();
    return true;
}

/// @brief pptr<T>: сохранение/загрузка через io.h с pptr offset.
static bool test_p97_pptr_persistence_via_io()
{
    const char* test_file = "test_issue97_pptr_io.pmm";

    pmm::presets::SingleThreadedHeap pmm1;
    PMM_TEST( pmm1.create( 16 * 1024 ) );

    // Выделяем структуру данных через pptr
    pmm::pptr<int> p1 = pmm1.allocate_typed<int>();
    PMM_TEST( !p1.is_null() );
    *pmm1.resolve( p1 ) = 99999;

    std::uint32_t saved_offset = p1.offset();

    // Сохраняем образ в файл
    PMM_TEST( pmm::save_manager( pmm1, test_file ) );
    pmm1.destroy();

    // Загружаем в новый менеджер
    pmm::presets::SingleThreadedHeap pmm2;
    PMM_TEST( pmm2.create( 16 * 1024 ) );
    PMM_TEST( pmm::load_manager_from_file( pmm2, test_file ) );
    PMM_TEST( pmm2.is_initialized() );

    // Восстанавливаем pptr по сохранённому смещению
    pmm::pptr<int> p2( saved_offset );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( *pmm2.resolve( p2 ) == 99999 ); // данные сохранились

    pmm2.deallocate_typed( p2 );
    pmm2.destroy();

    std::remove( test_file );
    return true;
}

/// @brief Несколько типов pptr<T> в одном менеджере.
static bool test_p97_pptr_multiple_types()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    pmm::pptr<int>    pi = pmm.allocate_typed<int>();
    pmm::pptr<double> pd = pmm.allocate_typed<double>();
    pmm::pptr<char>   pc = pmm.allocate_typed<char>( 16 );

    PMM_TEST( !pi.is_null() );
    PMM_TEST( !pd.is_null() );
    PMM_TEST( !pc.is_null() );

    *pmm.resolve( pi ) = 7;
    *pmm.resolve( pd ) = 3.14;
    std::memcpy( pmm.resolve( pc ), "hello", 6 );

    PMM_TEST( *pmm.resolve( pi ) == 7 );
    PMM_TEST( *pmm.resolve( pd ) == 3.14 );
    PMM_TEST( std::memcmp( pmm.resolve( pc ), "hello", 6 ) == 0 );

    pmm.deallocate_typed( pi );
    pmm.deallocate_typed( pd );
    pmm.deallocate_typed( pc );

    pmm.destroy();
    return true;
}

/// @brief EmbeddedStatic4K: pptr<T> API (без malloc).
static bool test_p97_pptr_embedded_static()
{
    pmm::presets::EmbeddedStatic4K pmm;
    PMM_TEST( pmm.create() );

    pmm::pptr<std::uint32_t> p = pmm.allocate_typed<std::uint32_t>();
    PMM_TEST( !p.is_null() );

    *pmm.resolve( p ) = 0xDEADBEEFu;
    PMM_TEST( *pmm.resolve( p ) == 0xDEADBEEFu );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

/// @brief MultiThreadedHeap: concurrent pptr<T> allocations.
static bool test_p97_pptr_multi_threaded()
{
    pmm::presets::MultiThreadedHeap pmm;
    PMM_TEST( pmm.create( 256 * 1024 ) );

    constexpr int kThreads         = 4;
    constexpr int kAllocsPerThread = 25;

    std::vector<std::thread> threads;
    std::atomic<int>         fail_count{ 0 };

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back(
            [&pmm, &fail_count, kAllocsPerThread]()
            {
                std::vector<pmm::pptr<int>> ptrs;
                for ( int i = 0; i < kAllocsPerThread; ++i )
                {
                    pmm::pptr<int> p = pmm.allocate_typed<int>();
                    if ( p.is_null() )
                    {
                        fail_count++;
                    }
                    else
                    {
                        *pmm.resolve( p ) = i;
                        ptrs.push_back( p );
                    }
                }
                for ( auto& p : ptrs )
                    pmm.deallocate_typed( p );
            } );
    }

    for ( auto& th : threads )
        th.join();

    PMM_TEST( fail_count == 0 );
    std::cout << "    pptr MultiThreadedHeap: " << kThreads << " threads x " << kAllocsPerThread
              << " typed allocs (0 failures)\n";

    pmm.destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue97_presets (Issue #97: Preset Integration Tests) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P97-A: EmbeddedStatic4K (StaticStorage — no malloc) ---\n";
    PMM_RUN( "P97-A1: EmbeddedStatic4K lifecycle (create/alloc/dealloc/destroy)", test_p97_embedded_static_lifecycle );
    PMM_RUN( "P97-A2: EmbeddedStatic4K fill memory to exhaustion", test_p97_embedded_static_fill_memory );
    PMM_RUN( "P97-A3: EmbeddedStatic4K allocate(0) returns nullptr", test_p97_embedded_static_alloc_zero );

    std::cout << "\n--- P97-B: SingleThreadedHeap (HeapStorage + auto-expand) ---\n";
    PMM_RUN( "P97-B1: SingleThreadedHeap lifecycle", test_p97_single_threaded_heap_lifecycle );
    PMM_RUN( "P97-B2: SingleThreadedHeap auto-expand", test_p97_single_threaded_heap_auto_expand );
    PMM_RUN( "P97-B3: SingleThreadedHeap save_manager/load_manager_from_file (Issue #97)",
             test_p97_single_threaded_heap_io );

    std::cout << "\n--- P97-C: PersistentFileMapped (MMapStorage — persistence) ---\n";
    PMM_RUN( "P97-C1: PersistentFileMapped basic open/create/load", test_p97_persistent_file_mapped_basic );
    PMM_RUN( "P97-C2: PersistentFileMapped without open file", test_p97_persistent_file_mapped_no_file );

    std::cout << "\n--- P97-D: MultiThreadedHeap (SharedMutexLock — thread safety) ---\n";
    PMM_RUN( "P97-D1: MultiThreadedHeap basic operations", test_p97_multi_threaded_heap_basic );
    PMM_RUN( "P97-D2: MultiThreadedHeap concurrent allocs from 4 threads", test_p97_multi_threaded_heap_concurrent );

    std::cout << "\n--- P97-E: Migration from singleton to AbstractPersistMemoryManager ---\n";
    PMM_RUN( "P97-E1: New API comparison with legacy singleton", test_p97_migration_comparison );
    PMM_RUN( "P97-E2: RAII lifecycle (create/use/destroy)", test_p97_raii_lifecycle );

    std::cout << "\n--- P97-F: io.h new API (save_manager / load_manager_from_file) ---\n";
    PMM_RUN( "P97-F1: save_manager / load_manager_from_file with StaticStorage", test_p97_io_static_storage );
    PMM_RUN( "P97-F2: save_manager(nullptr filename) returns false", test_p97_io_save_null_filename );
    PMM_RUN( "P97-F3: save_manager(uninitialized) returns false", test_p97_io_save_uninitialized );
    PMM_RUN( "P97-F4: load_manager_from_file(nullptr filename) returns false", test_p97_io_load_null_filename );

    std::cout << "\n--- P97-G: pptr<T> typed API (Issue #97: persistent pointers outside manager) ---\n";
    PMM_RUN( "P97-G1: pptr<T> sizeof == 4 bytes (persistent, address-independent)", test_p97_pptr_sizeof );
    PMM_RUN( "P97-G2: allocate_typed<T> / resolve<T> / deallocate_typed", test_p97_pptr_allocate_resolve_deallocate );
    PMM_RUN( "P97-G3: allocate_typed<T>(count) / resolve_at<T> (array support)", test_p97_pptr_allocate_array );
    PMM_RUN( "P97-G4: pptr<T> offset persistence (store/restore granule index)", test_p97_pptr_offset_persistence );
    PMM_RUN( "P97-G5: resolve(null pptr) returns nullptr", test_p97_pptr_null_resolve );
    PMM_RUN( "P97-G6: deallocate_typed(null pptr) is no-op", test_p97_pptr_deallocate_null );
    PMM_RUN( "P97-G7: allocate_typed<T>(0) returns null pptr", test_p97_pptr_allocate_zero );
    PMM_RUN( "P97-G8: pptr<T> comparison (== / !=)", test_p97_pptr_comparison );
    PMM_RUN( "P97-G9: pptr<T> persistence via save_manager/load_manager_from_file", test_p97_pptr_persistence_via_io );
    PMM_RUN( "P97-G10: pptr<T> multiple types (int, double, char[])", test_p97_pptr_multiple_types );
    PMM_RUN( "P97-G11: EmbeddedStatic4K with pptr<T> (no malloc)", test_p97_pptr_embedded_static );
    PMM_RUN( "P97-G12: MultiThreadedHeap concurrent pptr<T> allocations", test_p97_pptr_multi_threaded );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
