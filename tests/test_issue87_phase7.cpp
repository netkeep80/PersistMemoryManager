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

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

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
TEST_CASE( "P7-A1: PersistMemoryManager<> type aliases", "[test_issue87_phase7]" )
{
    using PMM = pmm::PersistMemoryManager<>;
    static_assert( std::is_same<PMM::address_traits, pmm::DefaultAddressTraits>::value,
                   "PersistMemoryManager<>::address_traits must be DefaultAddressTraits" );
    static_assert( std::is_same<PMM::storage_backend, pmm::HeapStorage<pmm::DefaultAddressTraits>>::value,
                   "PersistMemoryManager<>::storage_backend must be HeapStorage<Default>" );
    static_assert( std::is_same<PMM::free_block_tree, pmm::AvlFreeTree<pmm::DefaultAddressTraits>>::value,
                   "PersistMemoryManager<>::free_block_tree must be AvlFreeTree<Default>" );
}

/// @brief CacheManagerConfig (NoLock) и PersistentDataConfig (SharedMutexLock) компилируются.
TEST_CASE( "P7-A2: CacheManagerConfig and PersistentDataConfig", "[test_issue87_phase7]" )
{
    // CacheManagerConfig — NoLock
    static_assert( std::is_same<pmm::CacheManagerConfig::lock_policy, pmm::config::NoLock>::value,
                   "CacheManagerConfig must use NoLock" );
    // PersistentDataConfig — SharedMutexLock
    static_assert( std::is_same<pmm::PersistentDataConfig::lock_policy, pmm::config::SharedMutexLock>::value,
                   "PersistentDataConfig must use SharedMutexLock" );
}

// ─── P7-B: Жизненный цикл ────────────────────────────────────────────────────

/// @brief create(size) инициализирует менеджер (HeapStorage).
TEST_CASE( "P7-B1: create(size) with HeapStorage", "[test_issue87_phase7]" )
{
    // Используем уникальный InstanceId=100 чтобы не конфликтовать с другими тестами
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 100>;
    PMM::destroy(); // ensure clean state
    REQUIRE( !PMM::is_initialized() );

    REQUIRE( PMM::create( 8192 ) );
    REQUIRE( PMM::is_initialized() );
    REQUIRE( PMM::total_size() >= 8192 );
    REQUIRE( PMM::free_size() > 0 );

    PMM::destroy();
    REQUIRE( !PMM::is_initialized() );
}

/// @brief create(size) с размером меньше kMinMemorySize возвращает false.
TEST_CASE( "P7-B2: create(1) fails (too small)", "[test_issue87_phase7]" )
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 101>;
    PMM::destroy();               // ensure clean state
    REQUIRE( !PMM::create( 1 ) ); // слишком маленький буфер
    REQUIRE( !PMM::is_initialized() );
}

// ─── P7-C: StaticStorage + create() ─────────────────────────────────────────

/// @brief create() без аргументов инициализирует на StaticStorage.
TEST_CASE( "P7-C1: create() with StaticStorage<4096>", "[test_issue87_phase7]" )
{
    using PMM = pmm::PersistMemoryManager<StaticStorageConfig, 102>;
    PMM::destroy(); // ensure clean state
    REQUIRE( !PMM::is_initialized() );

    REQUIRE( PMM::create() ); // StaticStorage уже имеет буфер
    REQUIRE( PMM::is_initialized() );
    REQUIRE( PMM::total_size() == 4096 );

    PMM::destroy();
    REQUIRE( !PMM::is_initialized() );
}

// ─── P7-D: allocate() / deallocate() ─────────────────────────────────────────

/// @brief allocate(size) выделяет блок, deallocate(ptr) освобождает.
TEST_CASE( "P7-D1: allocate(128)/deallocate works", "[test_issue87_phase7]" )
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 103>;
    PMM::destroy(); // ensure clean state
    REQUIRE( PMM::create( 8192 ) );

    // Выделяем блок
    void* ptr = PMM::allocate( 128 );
    REQUIRE( ptr != nullptr );
    REQUIRE( PMM::alloc_block_count() > 0 );

    // Записываем данные
    std::memset( ptr, 0xAA, 128 );

    // Освобождаем
    PMM::deallocate( ptr );

    PMM::destroy();
}

/// @brief allocate(0) возвращает nullptr.
TEST_CASE( "P7-D2: allocate(0) returns nullptr", "[test_issue87_phase7]" )
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 104>;
    PMM::destroy(); // ensure clean state
    REQUIRE( PMM::create( 8192 ) );
    REQUIRE( PMM::allocate( 0 ) == nullptr );
    PMM::destroy();
}

/// @brief Множественные allocate/deallocate без ошибок.
TEST_CASE( "P7-D3: multiple allocate/deallocate", "[test_issue87_phase7]" )
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 105>;
    PMM::destroy(); // ensure clean state
    REQUIRE( PMM::create( 16 * 1024 ) );

    constexpr int kCount = 10;
    void*         ptrs[kCount];

    for ( int i = 0; i < kCount; ++i )
    {
        ptrs[i] = PMM::allocate( 64 );
        REQUIRE( ptrs[i] != nullptr );
    }

    for ( int i = 0; i < kCount; ++i )
    {
        PMM::deallocate( ptrs[i] );
    }

    // После освобождения всего, свободное место должно быть близко к исходному
    REQUIRE( PMM::free_size() > 0 );

    PMM::destroy();
}

/// @brief StaticStorage + allocate/deallocate работает.
TEST_CASE( "P7-D4: StaticStorage allocate/deallocate", "[test_issue87_phase7]" )
{
    using PMM = pmm::PersistMemoryManager<StaticStorageConfig, 106>;
    PMM::destroy(); // ensure clean state
    REQUIRE( PMM::create() );

    void* ptr = PMM::allocate( 64 );
    REQUIRE( ptr != nullptr );

    // Убеждаемся, что ptr внутри буфера
    std::uint8_t* base = PMM::backend().base_ptr();
    REQUIRE( static_cast<std::uint8_t*>( ptr ) >= base );
    REQUIRE( static_cast<std::uint8_t*>( ptr ) < base + 4096 );

    PMM::deallocate( ptr );
    PMM::destroy();
}

// ─── P7-E: Статистика ─────────────────────────────────────────────────────────

/// @brief total_size(), used_size(), free_size() корректны после create().
TEST_CASE( "P7-E1: total_size/used_size/free_size after create", "[test_issue87_phase7]" )
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 107>;
    PMM::destroy(); // ensure clean state
    REQUIRE( PMM::create( 8192 ) );

    REQUIRE( PMM::total_size() >= 8192 );
    REQUIRE( PMM::used_size() > 0 ); // заголовок занят
    REQUIRE( PMM::free_size() > 0 ); // есть свободное место
    REQUIRE( PMM::used_size() + PMM::free_size() <= PMM::total_size() );
    REQUIRE( PMM::block_count() >= 2 ); // минимум 2 блока (header + free)

    PMM::destroy();
}

// ─── P7-F: Typed API (pptr) ─────────────────────────────────────────────────

/// @brief allocate_typed<T>()/deallocate_typed() с pptr работает корректно.
TEST_CASE( "P7-F1: allocate_typed/deallocate_typed with pptr", "[test_issue87_phase7]" )
{
    using PMM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 108>;
    PMM::destroy(); // ensure clean state
    REQUIRE( PMM::create( 8192 ) );

    // Аллокация через типизированный API
    PMM::pptr<int> p = PMM::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    // Разыменование
    int* raw = PMM::resolve( p );
    REQUIRE( raw != nullptr );
    *raw = 42;
    REQUIRE( *PMM::resolve( p ) == 42 );

    // operator* и operator->
    *p = 99;
    REQUIRE( *p == 99 );

    PMM::deallocate_typed( p );
    PMM::destroy();
}

// =============================================================================
// main
// =============================================================================
