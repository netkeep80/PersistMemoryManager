/**
 * @file test_issue100.cpp
 * @brief Тесты для Issue #100: Phase 1 — Infrastructure Preparation.
 *
 * Проверяет:
 *   P100-A: pptr<T, ManagerT> — двухпараметрический указатель
 *     - sizeof(pptr<T, void>) == 4 (не изменился)
 *     - sizeof(pptr<T, ManagerT>) == 4 (ManagerT не хранится)
 *     - Обратная совместимость: pmm::pptr<T> == pmm::pptr<T, void>
 *     - Неявное преобразование pptr<T, void> ↔ pptr<T, ManagerT>
 *     - pptr<T, ManagerT>::resolve(mgr) эквивалентен mgr.resolve<T>(p)
 *     - element_type и manager_type typedefs
 *
 *   P100-B: AbstractPersistMemoryManager — manager_type и nested pptr<T>
 *     - manager_type typedef ссылается на сам тип менеджера
 *     - Nested alias Manager::pptr<T> == pmm::pptr<T, manager_type>
 *     - allocate_typed возвращает Manager::pptr<T>
 *     - resolve() принимает как Manager::pptr<T>, так и pmm::pptr<T, void>
 *     - deallocate_typed() принимает как Manager::pptr<T>, так и pmm::pptr<T, void>
 *     - Полный цикл с Manager::pptr<T>
 *
 *   P100-C: manager_concept.h — is_persist_memory_manager<T>
 *     - Проверяет AbstractPersistMemoryManager через концепцию
 *     - Проверяет presets через концепцию
 *     - Отклоняет int и non-manager типы
 *
 *   P100-D: static_manager_factory.h — StaticPersistMemoryManager<ConfigT, Tag>
 *     - Разные теги = разные типы (TypeA::pptr<int> != TypeB::pptr<int>)
 *     - Полный жизненный цикл StaticPersistMemoryManager
 *     - Статически-типизированное разыменование через tag
 *     - StaticPersistMemoryManager удовлетворяет is_persist_memory_manager_v
 *
 *   P100-E: manager_configs.h — готовые конфигурации
 *     - CacheManagerConfig (NoLock)
 *     - PersistentDataConfig (SharedMutexLock)
 *     - EmbeddedManagerConfig (NoLock)
 *     - IndustrialDBConfig (SharedMutexLock)
 *
 * @see include/pmm/pptr.h
 * @see include/pmm/abstract_pmm.h
 * @see include/pmm/manager_concept.h
 * @see include/pmm/static_manager_factory.h
 * @see include/pmm/manager_configs.h
 * @version 0.1 (Issue #100 — Phase 1: Infrastructure Preparation)
 */

#include "pmm/abstract_pmm.h"
#include "pmm/manager_concept.h"
#include "pmm/manager_configs.h"
#include "pmm/pmm_presets.h"
#include "pmm/pptr.h"
#include "pmm/static_manager_factory.h"

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
        bool _result = fn();                                                                                           \
        std::cout << ( _result ? "PASS" : "FAIL" ) << "\n";                                                            \
        if ( !_result )                                                                                                \
            all_passed = false;                                                                                        \
    } while ( false )

// =============================================================================
// P100-A: pptr<T, ManagerT> — двухпараметрический указатель
// =============================================================================

/// @brief pptr<T, ManagerT> хранит только 4 байта — ManagerT не хранится.
static bool test_p100_pptr_sizeof()
{
    using MgrType = pmm::presets::SingleThreadedHeap;

    // Без менеджера (void по умолчанию)
    static_assert( sizeof( pmm::pptr<int> ) == 4, "sizeof(pptr<int>) must be 4" );
    static_assert( sizeof( pmm::pptr<double> ) == 4, "sizeof(pptr<double>) must be 4" );
    static_assert( sizeof( pmm::pptr<char> ) == 4, "sizeof(pptr<char>) must be 4" );
    static_assert( sizeof( pmm::pptr<int, void> ) == 4, "sizeof(pptr<int,void>) must be 4" );

    // С менеджером — размер НЕ изменился (ManagerT не хранится)
    static_assert( sizeof( pmm::pptr<int, MgrType> ) == 4, "sizeof(pptr<int,MgrType>) must be 4" );
    static_assert( sizeof( pmm::pptr<double, MgrType> ) == 4, "sizeof(pptr<double,MgrType>) must be 4" );
    static_assert( sizeof( MgrType::pptr<int> ) == 4, "sizeof(Manager::pptr<int>) must be 4" );

    PMM_TEST( sizeof( pmm::pptr<int> ) == 4 );
    PMM_TEST( sizeof( pmm::pptr<int, MgrType> ) == 4 );
    PMM_TEST( sizeof( MgrType::pptr<int> ) == 4 );
    return true;
}

/// @brief pptr<T, void> и pptr<T, ManagerT> имеют typedefs element_type и manager_type.
static bool test_p100_pptr_typedefs()
{
    using MgrType = pmm::presets::SingleThreadedHeap;

    // element_type
    static_assert( std::is_same_v<pmm::pptr<int>::element_type, int> );
    static_assert( std::is_same_v<pmm::pptr<double>::element_type, double> );
    static_assert( std::is_same_v<pmm::pptr<int, MgrType>::element_type, int> );

    // manager_type
    static_assert( std::is_same_v<pmm::pptr<int>::manager_type, void> );
    static_assert( std::is_same_v<pmm::pptr<int, void>::manager_type, void> );
    static_assert( std::is_same_v<pmm::pptr<int, MgrType>::manager_type, MgrType> );
    static_assert( std::is_same_v<MgrType::pptr<int>::manager_type, MgrType> );

    return true;
}

/// @brief pptr<T> (void) и pptr<T, ManagerT> используют одно смещение — тот же индекс.
static bool test_p100_pptr_same_offset()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 16 * 1024 ) );

    // Выделяем через новый API — получаем Manager::pptr<int>
    pmm::presets::SingleThreadedHeap::pptr<int> p_bound = pmm.allocate_typed<int>();
    PMM_TEST( !p_bound.is_null() );

    // Конвертируем в старый стиль pmm::pptr<int, void>
    pmm::pptr<int> p_void = p_bound; // неявная конвертация
    PMM_TEST( !p_void.is_null() );

    // Смещения совпадают
    PMM_TEST( p_bound.offset() == p_void.offset() );

    // Обратная конвертация
    pmm::presets::SingleThreadedHeap::pptr<int> p_bound2 = p_void;
    PMM_TEST( p_bound2.offset() == p_void.offset() );
    PMM_TEST( p_bound == p_bound2 );

    pmm.deallocate_typed( p_void ); // deallocate через старый стиль
    pmm.destroy();
    return true;
}

/// @brief pptr<T, ManagerT>::resolve(mgr) эквивалентен mgr.resolve<T>(p).
static bool test_p100_pptr_resolve_method()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 16 * 1024 ) );

    using MyPptr = pmm::presets::SingleThreadedHeap::pptr<int>;

    MyPptr p = pmm.allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Разыменование через метод pptr::resolve(mgr)
    int* ptr1 = p.resolve( pmm );
    PMM_TEST( ptr1 != nullptr );

    // Эквивалентно mgr.resolve<T>(p)
    int* ptr2 = pmm.resolve( p );
    PMM_TEST( ptr2 != nullptr );

    // Оба указывают на одно место
    PMM_TEST( ptr1 == ptr2 );

    // Проверяем чтение/запись через оба метода
    *ptr1 = 42;
    PMM_TEST( *ptr2 == 42 );

    *ptr2 = 99;
    PMM_TEST( *p.resolve( pmm ) == 99 );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

/// @brief Null pptr<T, ManagerT> корректно конвертируется и is_null().
static bool test_p100_pptr_null_conversion()
{
    using MgrType = pmm::presets::SingleThreadedHeap;

    // Null в обоих формах
    pmm::pptr<int>     void_null;
    MgrType::pptr<int> bound_null;

    PMM_TEST( void_null.is_null() );
    PMM_TEST( !static_cast<bool>( void_null ) );
    PMM_TEST( bound_null.is_null() );
    PMM_TEST( !static_cast<bool>( bound_null ) );

    // Конвертация сохраняет null
    pmm::pptr<int> converted_to_void = bound_null;
    PMM_TEST( converted_to_void.is_null() );
    PMM_TEST( converted_to_void.offset() == 0 );

    MgrType::pptr<int> converted_to_bound = void_null;
    PMM_TEST( converted_to_bound.is_null() );
    PMM_TEST( converted_to_bound.offset() == 0 );

    return true;
}

// =============================================================================
// P100-B: AbstractPersistMemoryManager — manager_type и nested pptr<T>
// =============================================================================

/// @brief Manager::manager_type ссылается на сам тип менеджера.
static bool test_p100_manager_type_typedef()
{
    using MgrType = pmm::presets::SingleThreadedHeap;

    static_assert( std::is_same_v<MgrType::manager_type, MgrType>,
                   "Manager::manager_type must be the manager's own type" );

    static_assert( std::is_same_v<pmm::DefaultAbstractPMM::manager_type, pmm::DefaultAbstractPMM>,
                   "DefaultAbstractPMM::manager_type must be DefaultAbstractPMM" );

    return true;
}

/// @brief Manager::pptr<T> == pmm::pptr<T, manager_type>.
static bool test_p100_nested_pptr_alias()
{
    using MgrType = pmm::presets::SingleThreadedHeap;

    // Manager::pptr<T> должен быть pmm::pptr<T, MgrType>
    static_assert( std::is_same_v<MgrType::pptr<int>, pmm::pptr<int, MgrType>>,
                   "Manager::pptr<int> must be pmm::pptr<int, manager_type>" );

    static_assert( std::is_same_v<MgrType::pptr<double>, pmm::pptr<double, MgrType>>,
                   "Manager::pptr<double> must be pmm::pptr<double, manager_type>" );

    // Другие типы менеджеров имеют разные pptr<T>
    using MgrType2 = pmm::presets::MultiThreadedHeap;
    static_assert( !std::is_same_v<MgrType::pptr<int>, MgrType2::pptr<int>>,
                   "pptr from different manager types must be different types" );

    return true;
}

/// @brief allocate_typed возвращает Manager::pptr<T>, который можно хранить как pmm::pptr<T>.
static bool test_p100_allocate_typed_returns_manager_pptr()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 32 * 1024 ) );

    using MgrType = pmm::presets::SingleThreadedHeap;

    // Можно хранить в Manager::pptr<T>
    MgrType::pptr<int> bound_ptr = pmm.allocate_typed<int>();
    PMM_TEST( !bound_ptr.is_null() );

    // Можно хранить в pmm::pptr<T> (backward compat)
    pmm::pptr<int> void_ptr = pmm.allocate_typed<int>();
    PMM_TEST( !void_ptr.is_null() );

    // resolve через оба
    *pmm.resolve( bound_ptr ) = 111;
    *pmm.resolve( void_ptr )  = 222;

    PMM_TEST( *pmm.resolve( bound_ptr ) == 111 );
    PMM_TEST( *pmm.resolve( void_ptr ) == 222 );

    // Разные адреса
    PMM_TEST( bound_ptr.offset() != void_ptr.offset() );

    pmm.deallocate_typed( bound_ptr );
    pmm.deallocate_typed( void_ptr );
    pmm.destroy();
    return true;
}

/// @brief resolve() и deallocate_typed() принимают оба варианта pptr.
static bool test_p100_resolve_accepts_both_pptr()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 16 * 1024 ) );

    using MgrType = pmm::presets::SingleThreadedHeap;

    MgrType::pptr<int> bound_ptr = pmm.allocate_typed<int>();
    PMM_TEST( !bound_ptr.is_null() );

    // Старый стиль
    pmm::pptr<int> void_ptr = bound_ptr; // конвертация

    // resolve принимает оба
    int* p1 = pmm.resolve( bound_ptr );
    int* p2 = pmm.resolve( void_ptr );
    PMM_TEST( p1 != nullptr );
    PMM_TEST( p1 == p2 ); // одинаковые смещения — одинаковые адреса

    // deallocate_typed принимает оба форматы
    // Используем bound_ptr для освобождения
    pmm.deallocate_typed( bound_ptr );

    pmm.destroy();
    return true;
}

/// @brief Полный цикл allocate/write/read/deallocate с Manager::pptr<T>.
static bool test_p100_full_lifecycle_with_manager_pptr()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    using MgrPptr = pmm::presets::SingleThreadedHeap::pptr<int>;

    // Выделяем массив из 5 элементов
    MgrPptr arr = pmm.allocate_typed<int>( 5 );
    PMM_TEST( !arr.is_null() );

    // Записываем через resolve и resolve_at
    for ( int i = 0; i < 5; ++i )
    {
        int* elem = pmm.resolve_at( arr, static_cast<std::size_t>( i ) );
        PMM_TEST( elem != nullptr );
        *elem = i * 10;
    }

    // Читаем через resolve
    int* base = pmm.resolve( arr );
    PMM_TEST( base != nullptr );
    for ( int i = 0; i < 5; ++i )
        PMM_TEST( base[i] == i * 10 );

    // Разыменование через pptr::resolve(mgr)
    int* base2 = arr.resolve( pmm );
    PMM_TEST( base2 == base );

    // Освобождение
    pmm.deallocate_typed( arr );
    pmm.destroy();
    return true;
}

// =============================================================================
// P100-C: manager_concept.h — is_persist_memory_manager<T>
// =============================================================================

/// @brief Проверка AbstractPersistMemoryManager через концепцию.
static bool test_p100_concept_abstract_pmm()
{
    // AbstractPersistMemoryManager удовлетворяет концепции
    static_assert( pmm::is_persist_memory_manager_v<pmm::DefaultAbstractPMM>,
                   "DefaultAbstractPMM must satisfy is_persist_memory_manager" );

    static_assert( pmm::is_persist_memory_manager_v<pmm::SingleThreadedAbstractPMM>,
                   "SingleThreadedAbstractPMM must satisfy is_persist_memory_manager" );

    PMM_TEST( pmm::is_persist_memory_manager_v<pmm::DefaultAbstractPMM> );
    PMM_TEST( pmm::is_persist_memory_manager_v<pmm::SingleThreadedAbstractPMM> );
    return true;
}

/// @brief Проверка pmm_presets через концепцию.
static bool test_p100_concept_presets()
{
    static_assert( pmm::is_persist_memory_manager_v<pmm::presets::SingleThreadedHeap> );
    static_assert( pmm::is_persist_memory_manager_v<pmm::presets::MultiThreadedHeap> );
    static_assert( pmm::is_persist_memory_manager_v<pmm::presets::EmbeddedStatic4K> );
    static_assert( pmm::is_persist_memory_manager_v<pmm::presets::PersistentFileMapped> );
    static_assert( pmm::is_persist_memory_manager_v<pmm::presets::IndustrialDB> );

    PMM_TEST( pmm::is_persist_memory_manager_v<pmm::presets::SingleThreadedHeap> );
    PMM_TEST( pmm::is_persist_memory_manager_v<pmm::presets::MultiThreadedHeap> );
    PMM_TEST( pmm::is_persist_memory_manager_v<pmm::presets::EmbeddedStatic4K> );
    return true;
}

/// @brief Обычные типы не удовлетворяют концепции.
static bool test_p100_concept_rejects_non_managers()
{
    static_assert( !pmm::is_persist_memory_manager_v<int>, "int must not satisfy is_persist_memory_manager" );

    static_assert( !pmm::is_persist_memory_manager_v<double>, "double must not satisfy is_persist_memory_manager" );

    struct NotAManager
    {
        void foo() {}
    };
    static_assert( !pmm::is_persist_memory_manager_v<NotAManager>,
                   "NotAManager must not satisfy is_persist_memory_manager" );

    PMM_TEST( !pmm::is_persist_memory_manager_v<int> );
    PMM_TEST( !pmm::is_persist_memory_manager_v<double> );
    return true;
}

// =============================================================================
// P100-D: static_manager_factory.h — StaticPersistMemoryManager<ConfigT, Tag>
// =============================================================================

/// @brief Разные теги создают разные типы менеджеров и разные типы pptr.
static bool test_p100_static_factory_different_tags()
{
    struct TagA
    {
    };
    struct TagB
    {
    };

    using MgrA = pmm::StaticPersistMemoryManager<pmm::config::DefaultConfig, TagA>;
    using MgrB = pmm::StaticPersistMemoryManager<pmm::config::DefaultConfig, TagB>;

    // Разные типы менеджеров
    static_assert( !std::is_same_v<MgrA, MgrB>, "Different tags must produce different manager types" );

    // Разные типы pptr
    static_assert( !std::is_same_v<MgrA::pptr<int>, MgrB::pptr<int>>,
                   "pptr from different manager tags must be different types" );

    // Одинаковые размеры pptr
    static_assert( sizeof( MgrA::pptr<int> ) == 4 );
    static_assert( sizeof( MgrB::pptr<int> ) == 4 );

    PMM_TEST( sizeof( MgrA::pptr<int> ) == 4 );
    PMM_TEST( sizeof( MgrB::pptr<int> ) == 4 );
    return true;
}

/// @brief Полный жизненный цикл StaticPersistMemoryManager.
static bool test_p100_static_factory_lifecycle()
{
    struct MyTag
    {
    };
    using MyMgr = pmm::StaticPersistMemoryManager<pmm::CacheManagerConfig, MyTag>;

    MyMgr mgr;
    PMM_TEST( !mgr.is_initialized() );

    PMM_TEST( mgr.create( 32 * 1024 ) );
    PMM_TEST( mgr.is_initialized() );
    PMM_TEST( mgr.total_size() >= 32 * 1024 );
    PMM_TEST( mgr.free_size() > 0 );

    // Выделение через typed API
    MyMgr::pptr<int> p = mgr.allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Разыменование через метод pptr (требует экземпляр менеджера)
    *p.resolve( mgr ) = 123;
    PMM_TEST( *p.resolve( mgr ) == 123 );

    // Разыменование через метод менеджера
    PMM_TEST( *mgr.resolve( p ) == 123 );

    // Освобождение
    mgr.deallocate_typed( p );
    mgr.destroy();
    PMM_TEST( !mgr.is_initialized() );

    return true;
}

/// @brief StaticPersistMemoryManager удовлетворяет is_persist_memory_manager_v.
static bool test_p100_static_factory_concept()
{
    struct MyTag
    {
    };
    using MyMgr = pmm::StaticPersistMemoryManager<pmm::config::DefaultConfig, MyTag>;

    static_assert( pmm::is_persist_memory_manager_v<MyMgr>,
                   "StaticPersistMemoryManager must satisfy is_persist_memory_manager" );
    static_assert( pmm::is_persist_memory_manager_v<pmm::StaticPersistMemoryManager<>>,
                   "StaticPersistMemoryManager<> must satisfy is_persist_memory_manager" );

    PMM_TEST( pmm::is_persist_memory_manager_v<MyMgr> );
    PMM_TEST( pmm::is_persist_memory_manager_v<pmm::StaticPersistMemoryManager<>> );
    return true;
}

/// @brief Два разных экземпляра StaticPersistMemoryManager с разными тегами работают независимо.
static bool test_p100_static_factory_multiple_instances()
{
    struct CacheTag
    {
    };
    struct DataTag
    {
    };

    using CacheMgr = pmm::StaticPersistMemoryManager<pmm::CacheManagerConfig, CacheTag>;
    using DataMgr  = pmm::StaticPersistMemoryManager<pmm::PersistentDataConfig, DataTag>;

    CacheMgr cache;
    DataMgr  data;

    PMM_TEST( cache.create( 16 * 1024 ) );
    PMM_TEST( data.create( 32 * 1024 ) );

    CacheMgr::pptr<int> cp = cache.allocate_typed<int>();
    DataMgr::pptr<int>  dp = data.allocate_typed<int>();

    PMM_TEST( !cp.is_null() );
    PMM_TEST( !dp.is_null() );

    *cp.resolve( cache ) = 111;
    *dp.resolve( data )  = 222;

    PMM_TEST( *cp.resolve( cache ) == 111 );
    PMM_TEST( *dp.resolve( data ) == 222 );

    // cp и dp — разные типы (CacheMgr::pptr<int> != DataMgr::pptr<int>)
    static_assert( !std::is_same_v<CacheMgr::pptr<int>, DataMgr::pptr<int>> );

    cache.deallocate_typed( cp );
    data.deallocate_typed( dp );

    cache.destroy();
    data.destroy();
    return true;
}

// =============================================================================
// P100-E: manager_configs.h — готовые конфигурации
// =============================================================================

/// @brief CacheManagerConfig использует NoLock.
static bool test_p100_configs_cache()
{
    static_assert( std::is_same_v<pmm::CacheManagerConfig::lock_policy, pmm::config::NoLock>,
                   "CacheManagerConfig must use NoLock" );
    static_assert( pmm::CacheManagerConfig::granule_size == 16 );
    static_assert( pmm::CacheManagerConfig::max_memory_gb == 64 );

    struct CacheTag
    {
    };
    using CacheMgr = pmm::StaticPersistMemoryManager<pmm::CacheManagerConfig, CacheTag>;

    CacheMgr mgr;
    PMM_TEST( mgr.create( 8 * 1024 ) );

    CacheMgr::pptr<double> p = mgr.allocate_typed<double>();
    PMM_TEST( !p.is_null() );
    *p.resolve( mgr ) = 3.14;
    PMM_TEST( *p.resolve( mgr ) == 3.14 );

    mgr.deallocate_typed( p );
    mgr.destroy();
    return true;
}

/// @brief PersistentDataConfig использует SharedMutexLock.
static bool test_p100_configs_persistent()
{
    static_assert( std::is_same_v<pmm::PersistentDataConfig::lock_policy, pmm::config::SharedMutexLock>,
                   "PersistentDataConfig must use SharedMutexLock" );

    struct PDataTag
    {
    };
    using PDataMgr = pmm::StaticPersistMemoryManager<pmm::PersistentDataConfig, PDataTag>;

    PDataMgr mgr;
    PMM_TEST( mgr.create( 16 * 1024 ) );

    PDataMgr::pptr<std::uint64_t> p = mgr.allocate_typed<std::uint64_t>();
    PMM_TEST( !p.is_null() );
    *p.resolve( mgr ) = 0xDEADBEEFCAFEBABEull;
    PMM_TEST( *p.resolve( mgr ) == 0xDEADBEEFCAFEBABEull );

    mgr.deallocate_typed( p );
    mgr.destroy();
    return true;
}

/// @brief EmbeddedManagerConfig использует NoLock с консервативным ростом.
static bool test_p100_configs_embedded()
{
    static_assert( std::is_same_v<pmm::EmbeddedManagerConfig::lock_policy, pmm::config::NoLock>,
                   "EmbeddedManagerConfig must use NoLock" );
    static_assert( pmm::EmbeddedManagerConfig::grow_numerator == 3 );
    static_assert( pmm::EmbeddedManagerConfig::grow_denominator == 2 );

    struct EmbTag
    {
    };
    using EmbMgr = pmm::StaticPersistMemoryManager<pmm::EmbeddedManagerConfig, EmbTag>;

    EmbMgr mgr;
    PMM_TEST( mgr.create( 4 * 1024 ) );
    PMM_TEST( mgr.is_initialized() );

    EmbMgr::pptr<char> p = mgr.allocate_typed<char>( 16 );
    PMM_TEST( !p.is_null() );
    std::memcpy( p.resolve( mgr ), "hello world!", 12 );
    PMM_TEST( std::memcmp( p.resolve( mgr ), "hello world!", 12 ) == 0 );

    mgr.deallocate_typed( p );
    mgr.destroy();
    return true;
}

/// @brief IndustrialDBConfig использует SharedMutexLock с агрессивным ростом.
static bool test_p100_configs_industrial()
{
    static_assert( std::is_same_v<pmm::IndustrialDBConfig::lock_policy, pmm::config::SharedMutexLock>,
                   "IndustrialDBConfig must use SharedMutexLock" );
    static_assert( pmm::IndustrialDBConfig::grow_numerator == 2 );
    static_assert( pmm::IndustrialDBConfig::grow_denominator == 1 );

    struct DBTag
    {
    };
    using DBMgr = pmm::StaticPersistMemoryManager<pmm::IndustrialDBConfig, DBTag>;

    DBMgr mgr;
    PMM_TEST( mgr.create( 64 * 1024 ) );
    PMM_TEST( mgr.is_initialized() );

    // Выделяем несколько элементов
    DBMgr::pptr<int> p1 = mgr.allocate_typed<int>();
    DBMgr::pptr<int> p2 = mgr.allocate_typed<int>();
    PMM_TEST( !p1.is_null() );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( p1 != p2 );

    *p1.resolve( mgr ) = 1;
    *p2.resolve( mgr ) = 2;
    PMM_TEST( *p1.resolve( mgr ) == 1 );
    PMM_TEST( *p2.resolve( mgr ) == 2 );

    mgr.deallocate_typed( p1 );
    mgr.deallocate_typed( p2 );
    mgr.destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue100 (Issue #100: Phase 1 Infrastructure Preparation) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P100-A: pptr<T, ManagerT> (двухпараметрический указатель) ---\n";
    PMM_RUN( "P100-A1: sizeof(pptr<T, ManagerT>) == 4 (ManagerT не хранится)", test_p100_pptr_sizeof );
    PMM_RUN( "P100-A2: pptr<T> typedefs element_type и manager_type", test_p100_pptr_typedefs );
    PMM_RUN( "P100-A3: pptr<T,void> ↔ pptr<T,ManagerT> сохраняют одно смещение", test_p100_pptr_same_offset );
    PMM_RUN( "P100-A4: pptr<T,ManagerT>::resolve(mgr) == mgr.resolve<T>(p)", test_p100_pptr_resolve_method );
    PMM_RUN( "P100-A5: null pptr корректно конвертируется", test_p100_pptr_null_conversion );

    std::cout << "\n--- P100-B: AbstractPersistMemoryManager — manager_type и nested pptr<T> ---\n";
    PMM_RUN( "P100-B1: Manager::manager_type == Manager", test_p100_manager_type_typedef );
    PMM_RUN( "P100-B2: Manager::pptr<T> == pmm::pptr<T, manager_type>", test_p100_nested_pptr_alias );
    PMM_RUN( "P100-B3: allocate_typed возвращает Manager::pptr<T>", test_p100_allocate_typed_returns_manager_pptr );
    PMM_RUN( "P100-B4: resolve/deallocate_typed принимают оба варианта pptr", test_p100_resolve_accepts_both_pptr );
    PMM_RUN( "P100-B5: полный цикл с Manager::pptr<T>", test_p100_full_lifecycle_with_manager_pptr );

    std::cout << "\n--- P100-C: manager_concept.h — is_persist_memory_manager<T> ---\n";
    PMM_RUN( "P100-C1: AbstractPersistMemoryManager удовлетворяет концепции", test_p100_concept_abstract_pmm );
    PMM_RUN( "P100-C2: pmm_presets удовлетворяют концепции", test_p100_concept_presets );
    PMM_RUN( "P100-C3: обычные типы отклоняются концепцией", test_p100_concept_rejects_non_managers );

    std::cout << "\n--- P100-D: static_manager_factory.h — StaticPersistMemoryManager<ConfigT, Tag> ---\n";
    PMM_RUN( "P100-D1: разные теги = разные типы менеджеров и pptr", test_p100_static_factory_different_tags );
    PMM_RUN( "P100-D2: полный жизненный цикл StaticPersistMemoryManager", test_p100_static_factory_lifecycle );
    PMM_RUN( "P100-D3: StaticPersistMemoryManager удовлетворяет концепции", test_p100_static_factory_concept );
    PMM_RUN( "P100-D4: несколько экземпляров с разными тегами работают независимо",
             test_p100_static_factory_multiple_instances );

    std::cout << "\n--- P100-E: manager_configs.h — готовые конфигурации ---\n";
    PMM_RUN( "P100-E1: CacheManagerConfig (NoLock)", test_p100_configs_cache );
    PMM_RUN( "P100-E2: PersistentDataConfig (SharedMutexLock)", test_p100_configs_persistent );
    PMM_RUN( "P100-E3: EmbeddedManagerConfig (NoLock, консервативный рост)", test_p100_configs_embedded );
    PMM_RUN( "P100-E4: IndustrialDBConfig (SharedMutexLock, агрессивный рост)", test_p100_configs_industrial );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
