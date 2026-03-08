/**
 * @file test_issue87_phase7.cpp
 * @brief Тесты Phase 7: PersistMemoryManager (Issue #87, migrated to static API, Issue #112).
 *
 * Изначально тесты проверяли AbstractPersistMemoryManager<>, который был удалён
 * в рамках рефакторинга. Теперь аналогичные тесты написаны для
 * PersistMemoryManager<ConfigT, InstanceId> — унифицированного статического менеджера.
 *
 * Проверяет:
 *  - PersistMemoryManager<> компилируется с настройками по умолчанию
 *  - create(size) инициализирует менеджер через HeapStorage
 *  - allocate()/deallocate() работают корректно
 *  - Параметрическая инстанциация с пользовательской конфигурацией (StaticStorage)
 *  - Статистика: total_size, used_size, free_size, block_count
 *
 * @see include/pmm/persist_memory_manager.h
 * @see plan_issue87.md §5 «Фаза 7: AbstractPersistMemoryManager»
 * @version 0.2 (Issue #112 — migrated from AbstractPersistMemoryManager to PersistMemoryManager)
 */

#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pptr.h"
#include "pmm/static_storage.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

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

// =============================================================================
// Конфигурации для тестов
// =============================================================================

// Конфигурация с StaticStorage (4096 байт, без блокировок).
// Аналог старого StaticPMM в AbstractPersistMemoryManager.
struct StaticStorageConfig
{
    using address_traits  = pmm::DefaultAddressTraits;
    using storage_backend = pmm::StaticStorage<4096, pmm::DefaultAddressTraits>;
    using free_block_tree = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;
    using lock_policy     = pmm::config::NoLock;
};

// =============================================================================
// Phase 7 tests: PersistMemoryManager (заменяет AbstractPersistMemoryManager)
// =============================================================================

// ─── P7-A: Компиляция и типы ─────────────────────────────────────────────────

/// @brief PersistMemoryManager<> компилируется и имеет корректные алиасы.
static bool test_p7_pmm_aliases()
{
    using PMM = pmm::PersistMemoryManager<>;
    static_assert( std::is_same<PMM::address_traits, pmm::DefaultAddressTraits>::value,
                   "PersistMemoryManager<>::address_traits must be DefaultAddressTraits" );
    static_assert( std::is_same<PMM::storage_backend, pmm::HeapStorage<pmm::DefaultAddressTraits>>::value,
                   "PersistMemoryManager<>::storage_backend must be HeapStorage<Default>" );
    static_assert( std::is_same<PMM::free_block_tree, pmm::AvlFreeTree<pmm::DefaultAddressTraits>>::value,
                   "PersistMemoryManager<>::free_block_tree must be AvlFreeTree<Default>" );
    return true;
}

/// @brief CacheManagerConfig (NoLock) и PersistentDataConfig (SharedMutexLock) компилируются.
static bool test_p7_predefined_configs()
{
    // CacheManagerConfig — NoLock
    static_assert(
        std::is_same<pmm::CacheManagerConfig::lock_policy, pmm::config::NoLock>::value,
        "CacheManagerConfig must use NoLock" );
    // PersistentDataConfig — SharedMutexLock
    static_assert(
        std::is_same<pmm::PersistentDataConfig::lock_policy, pmm::config::SharedMutexLock>::value,
        "PersistentDataConfig must use SharedMutexLock" );
    return true;
}

// ─── P7-B: Жизненный цикл ────────────────────────────────────────────────────

/// @brief create(size) инициализирует менеджер (HeapStorage).
static bool test_p7_create_with_heap_storage()
{
    // Используем уникальный InstanceId=100 чтобы не конфликтовать с другими тестами
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 100>;
    PMM::destroy(); // ensure clean state
    PMM_TEST( !PMM::is_initialized() );

    PMM_TEST( PMM::create( 8192 ) );
    PMM_TEST( PMM::is_initialized() );
    PMM_TEST( PMM::total_size() >= 8192 );
    PMM_TEST( PMM::free_size() > 0 );

    PMM::destroy();
    PMM_TEST( !PMM::is_initialized() );
    return true;
}

/// @brief create(size) с размером меньше kMinMemorySize возвращает false.
static bool test_p7_create_too_small()
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 101>;
    PMM::destroy(); // ensure clean state
    PMM_TEST( !PMM::create( 1 ) ); // слишком маленький буфер
    PMM_TEST( !PMM::is_initialized() );
    return true;
}

// ─── P7-C: StaticStorage + create() ─────────────────────────────────────────

/// @brief create() без аргументов инициализирует на StaticStorage.
static bool test_p7_create_with_static_storage()
{
    using PMM = pmm::PersistMemoryManager<StaticStorageConfig, 102>;
    PMM::destroy(); // ensure clean state
    PMM_TEST( !PMM::is_initialized() );

    PMM_TEST( PMM::create() ); // StaticStorage уже имеет буфер
    PMM_TEST( PMM::is_initialized() );
    PMM_TEST( PMM::total_size() == 4096 );

    PMM::destroy();
    PMM_TEST( !PMM::is_initialized() );
    return true;
}

// ─── P7-D: allocate() / deallocate() ─────────────────────────────────────────

/// @brief allocate(size) выделяет блок, deallocate(ptr) освобождает.
static bool test_p7_allocate_deallocate()
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 103>;
    PMM::destroy(); // ensure clean state
    PMM_TEST( PMM::create( 8192 ) );

    // Выделяем блок
    void* ptr = PMM::allocate( 128 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( PMM::alloc_block_count() > 0 );

    // Записываем данные
    std::memset( ptr, 0xAA, 128 );

    // Освобождаем
    PMM::deallocate( ptr );

    PMM::destroy();
    return true;
}

/// @brief allocate(0) возвращает nullptr.
static bool test_p7_allocate_zero()
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 104>;
    PMM::destroy(); // ensure clean state
    PMM_TEST( PMM::create( 8192 ) );
    PMM_TEST( PMM::allocate( 0 ) == nullptr );
    PMM::destroy();
    return true;
}

/// @brief Множественные allocate/deallocate без ошибок.
static bool test_p7_multiple_allocate_deallocate()
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 105>;
    PMM::destroy(); // ensure clean state
    PMM_TEST( PMM::create( 16 * 1024 ) );

    constexpr int kCount = 10;
    void*         ptrs[kCount];

    for ( int i = 0; i < kCount; ++i )
    {
        ptrs[i] = PMM::allocate( 64 );
        PMM_TEST( ptrs[i] != nullptr );
    }

    for ( int i = 0; i < kCount; ++i )
    {
        PMM::deallocate( ptrs[i] );
    }

    // После освобождения всего, свободное место должно быть близко к исходному
    PMM_TEST( PMM::free_size() > 0 );

    PMM::destroy();
    return true;
}

/// @brief StaticStorage + allocate/deallocate работает.
static bool test_p7_static_storage_allocate()
{
    using PMM = pmm::PersistMemoryManager<StaticStorageConfig, 106>;
    PMM::destroy(); // ensure clean state
    PMM_TEST( PMM::create() );

    void* ptr = PMM::allocate( 64 );
    PMM_TEST( ptr != nullptr );

    // Убеждаемся, что ptr внутри буфера
    std::uint8_t* base = PMM::backend().base_ptr();
    PMM_TEST( static_cast<std::uint8_t*>( ptr ) >= base );
    PMM_TEST( static_cast<std::uint8_t*>( ptr ) < base + 4096 );

    PMM::deallocate( ptr );
    PMM::destroy();
    return true;
}

// ─── P7-E: Статистика ─────────────────────────────────────────────────────────

/// @brief total_size(), used_size(), free_size() корректны после create().
static bool test_p7_statistics()
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 107>;
    PMM::destroy(); // ensure clean state
    PMM_TEST( PMM::create( 8192 ) );

    PMM_TEST( PMM::total_size() >= 8192 );
    PMM_TEST( PMM::used_size() > 0 ); // заголовок занят
    PMM_TEST( PMM::free_size() > 0 ); // есть свободное место
    PMM_TEST( PMM::used_size() + PMM::free_size() <= PMM::total_size() );
    PMM_TEST( PMM::block_count() >= 2 ); // минимум 2 блока (header + free)

    PMM::destroy();
    return true;
}

// ─── P7-F: Typed API (pptr) ─────────────────────────────────────────────────

/// @brief allocate_typed<T>()/deallocate_typed() с pptr работает корректно.
static bool test_p7_typed_api()
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 108>;
    PMM::destroy(); // ensure clean state
    PMM_TEST( PMM::create( 8192 ) );

    // Аллокация через типизированный API
    PMM::pptr<int> p = PMM::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Разыменование
    int* raw = PMM::resolve( p );
    PMM_TEST( raw != nullptr );
    *raw = 42;
    PMM_TEST( *PMM::resolve( p ) == 42 );

    // operator* и operator->
    *p = 99;
    PMM_TEST( *p == 99 );

    PMM::deallocate_typed( p );
    PMM::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase7 (Phase 7: PersistMemoryManager, migrated from AbstractPersistMemoryManager) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P7-A: Compilation and type aliases ---\n";
    PMM_RUN( "P7-A1: PersistMemoryManager<> type aliases", test_p7_pmm_aliases );
    PMM_RUN( "P7-A2: CacheManagerConfig and PersistentDataConfig", test_p7_predefined_configs );

    std::cout << "\n--- P7-B: Lifecycle ---\n";
    PMM_RUN( "P7-B1: create(size) with HeapStorage", test_p7_create_with_heap_storage );
    PMM_RUN( "P7-B2: create(1) fails (too small)", test_p7_create_too_small );

    std::cout << "\n--- P7-C: StaticStorage ---\n";
    PMM_RUN( "P7-C1: create() with StaticStorage<4096>", test_p7_create_with_static_storage );

    std::cout << "\n--- P7-D: allocate/deallocate ---\n";
    PMM_RUN( "P7-D1: allocate(128)/deallocate works", test_p7_allocate_deallocate );
    PMM_RUN( "P7-D2: allocate(0) returns nullptr", test_p7_allocate_zero );
    PMM_RUN( "P7-D3: multiple allocate/deallocate", test_p7_multiple_allocate_deallocate );
    PMM_RUN( "P7-D4: StaticStorage allocate/deallocate", test_p7_static_storage_allocate );

    std::cout << "\n--- P7-E: Statistics ---\n";
    PMM_RUN( "P7-E1: total_size/used_size/free_size after create", test_p7_statistics );

    std::cout << "\n--- P7-F: Typed API (pptr) ---\n";
    PMM_RUN( "P7-F1: allocate_typed/deallocate_typed with pptr", test_p7_typed_api );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
