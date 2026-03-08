/**
 * @file test_issue108_static_model.cpp
 * @brief Тесты статической модели менеджера персистентной памяти (Issue #108).
 *
 * Issue #108: рефакторинг от объектной модели к статической.
 *   - StaticMemoryManager<ConfigT, InstanceId> — все члены и методы статические
 *   - pptr<T, ManagerT>::resolve() — без аргументов (вызывает статический метод)
 *   - pptr<T, ManagerT>::operator* и operator-> — для удобного разыменования
 *   - pptr<T, ManagerT>::index_type — использует ManagerT::index_type если доступен
 *   - InstanceId — несколько независимых менеджеров одной конфигурации
 *
 * Тесты:
 *   P108-A: StaticMemoryManager — базовые операции
 *     - create()/destroy() — статические методы
 *     - allocate_typed<T>() — статический метод, возвращает pptr<T>
 *     - deallocate_typed(p) — статический метод
 *     - resolve<T>(p) — статический метод разыменования
 *
 *   P108-B: pptr — статическая модель разыменования
 *     - p.resolve() — без аргументов
 *     - operator* и operator->
 *     - index_type из менеджера
 *
 *   P108-C: InstanceId — независимые экземпляры
 *     - Менеджеры с разными InstanceId независимы
 *     - Разные типы pptr для разных InstanceId
 *
 *   P108-D: Изоляция тестов через destroy()
 *     - destroy() сбрасывает состояние
 *     - Повторная инициализация работает корректно
 *
 *   P108-E: Совместимость — объектная модель не сломана
 *     - Существующий API AbstractPersistMemoryManager работает
 *     - pptr::resolve(mgr) для объектной модели работает
 *
 * @see include/pmm/static_memory_manager.h — StaticMemoryManager
 * @see include/pmm/pptr.h — pptr (обновлённый)
 * @version 0.1 (Issue #108)
 */

#include "pmm/manager_configs.h"
#include "pmm/pmm_presets.h"
#include "pmm/pptr.h"
#include "pmm/static_memory_manager.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

// ─── Test macros ─────────────────────────────────────────────────────────────

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
// P108-A: StaticMemoryManager — базовые операции
// =============================================================================

/// @brief StaticMemoryManager — создание, аллокация, деаллокация, уничтожение.
static bool test_p108_basic_lifecycle()
{
    // Используем уникальный InstanceId=10 чтобы не конфликтовать с другими тестами
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 10>;

    PMM_TEST( !Mgr::is_initialized() );

    PMM_TEST( Mgr::create( 64 * 1024 ) );
    PMM_TEST( Mgr::is_initialized() );
    PMM_TEST( Mgr::total_size() >= 64 * 1024 );
    PMM_TEST( Mgr::free_size() > 0 );

    // Аллокация через статический метод
    Mgr::pptr<int> p = Mgr::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( static_cast<bool>( p ) );
    PMM_TEST( p.offset() > 0 );

    // Статический resolve
    int* raw = Mgr::resolve( p );
    PMM_TEST( raw != nullptr );
    *raw = 42;
    PMM_TEST( *Mgr::resolve( p ) == 42 );

    // Деаллокация
    Mgr::deallocate_typed( p );
    PMM_TEST( Mgr::is_initialized() );

    Mgr::destroy();
    PMM_TEST( !Mgr::is_initialized() );

    return true;
}

/// @brief Аллокация массива через StaticMemoryManager.
static bool test_p108_array_allocation()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 11>;

    PMM_TEST( Mgr::create( 256 * 1024 ) );

    const std::size_t count   = 10;
    Mgr::pptr<int>    arr_ptr = Mgr::allocate_typed<int>( count );
    PMM_TEST( !arr_ptr.is_null() );

    int* arr = Mgr::resolve( arr_ptr );
    PMM_TEST( arr != nullptr );

    for ( std::size_t i = 0; i < count; i++ )
        arr[i] = static_cast<int>( i * 100 );

    for ( std::size_t i = 0; i < count; i++ )
        PMM_TEST( Mgr::resolve( arr_ptr )[i] == static_cast<int>( i * 100 ) );

    // resolve_at
    for ( std::size_t i = 0; i < count; i++ )
        PMM_TEST( *Mgr::resolve_at( arr_ptr, i ) == static_cast<int>( i * 100 ) );

    Mgr::deallocate_typed( arr_ptr );
    Mgr::destroy();

    return true;
}

/// @brief StaticMemoryManager — несколько аллокаций разных типов.
static bool test_p108_multiple_types()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 12>;

    PMM_TEST( Mgr::create( 256 * 1024 ) );

    Mgr::pptr<int>    pi = Mgr::allocate_typed<int>();
    Mgr::pptr<double> pd = Mgr::allocate_typed<double>();
    Mgr::pptr<char>   pc = Mgr::allocate_typed<char>( 16 );

    PMM_TEST( !pi.is_null() );
    PMM_TEST( !pd.is_null() );
    PMM_TEST( !pc.is_null() );

    *Mgr::resolve( pi ) = 7;
    *Mgr::resolve( pd ) = 2.718;
    std::memcpy( Mgr::resolve( pc ), "world", 6 );

    PMM_TEST( *Mgr::resolve( pi ) == 7 );
    PMM_TEST( *Mgr::resolve( pd ) == 2.718 );
    PMM_TEST( std::memcmp( Mgr::resolve( pc ), "world", 6 ) == 0 );

    Mgr::deallocate_typed( pi );
    Mgr::deallocate_typed( pd );
    Mgr::deallocate_typed( pc );
    Mgr::destroy();

    return true;
}

/// @brief StaticMemoryManager — auto-expand при нехватке памяти.
static bool test_p108_auto_expand()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 13>;

    PMM_TEST( Mgr::create( 8 * 1024 ) );

    std::size_t initial_total = Mgr::total_size();

    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !p1.is_null() );

    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 4 * 1024 );
    PMM_TEST( !p2.is_null() );

    PMM_TEST( Mgr::is_initialized() );
    PMM_TEST( Mgr::total_size() > initial_total );

    Mgr::destroy();

    return true;
}

// =============================================================================
// P108-B: pptr — статическая модель разыменования
// =============================================================================

/// @brief pptr::resolve() — без аргументов (статическая модель).
static bool test_p108_pptr_resolve_no_arg()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 20>;

    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p = Mgr::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // resolve() без аргументов
    int* raw = p.resolve();
    PMM_TEST( raw != nullptr );

    *raw = 99;
    PMM_TEST( *p.resolve() == 99 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();

    return true;
}

/// @brief pptr::operator* (статическая модель).
static bool test_p108_pptr_operator_deref()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 21>;

    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p = Mgr::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // operator* — разыменование без аргументов
    *p = 123;
    PMM_TEST( *p == 123 );

    *p = 456;
    PMM_TEST( *p == 456 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();

    return true;
}

/// @brief pptr::operator-> — доступ к полям структуры (статическая модель).
static bool test_p108_pptr_operator_arrow()
{
    struct Point
    {
        int x = 0;
        int y = 0;
    };

    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 22>;

    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<Point> p = Mgr::allocate_typed<Point>();
    PMM_TEST( !p.is_null() );

    // Инициализация через resolve (чтобы убрать мусор)
    Mgr::resolve( p )->x = 0;
    Mgr::resolve( p )->y = 0;

    // operator-> — доступ к полям
    p->x = 10;
    p->y = 20;

    PMM_TEST( p->x == 10 );
    PMM_TEST( p->y == 20 );

    // Проверяем что это те же данные через resolve
    PMM_TEST( Mgr::resolve( p )->x == 10 );
    PMM_TEST( Mgr::resolve( p )->y == 20 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();

    return true;
}

/// @brief pptr::index_type — тип индекса из менеджера (Issue #108 comment).
static bool test_p108_pptr_index_type()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 23>;

    // StaticMemoryManager предоставляет index_type = DefaultAddressTraits::index_type = uint32_t
    static_assert( std::is_same_v<Mgr::index_type, std::uint32_t>,
                   "StaticMemoryManager::index_type must be uint32_t (DefaultAddressTraits)" );

    // pptr<T, Mgr>::index_type должен совпадать с Mgr::index_type
    static_assert( std::is_same_v<Mgr::pptr<int>::index_type, Mgr::index_type>,
                   "pptr::index_type must match manager::index_type" );

    PMM_TEST( (std::is_same_v<Mgr::pptr<int>::index_type, std::uint32_t>));

    // sizeof(pptr<T, StaticMemoryManager>) == sizeof(uint32_t) == 4
    static_assert( sizeof( Mgr::pptr<int> ) == 4, "sizeof(pptr<int, StaticMemoryManager>) must be 4" );
    PMM_TEST( sizeof( Mgr::pptr<int> ) == 4 );

    return true;
}

/// @brief pptr null-указатель в статической модели.
static bool test_p108_pptr_null_static()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 24>;

    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p; // null по умолчанию
    PMM_TEST( p.is_null() );
    PMM_TEST( !static_cast<bool>( p ) );

    // resolve() для null возвращает nullptr
    PMM_TEST( p.resolve() == nullptr );

    // Статический resolve тоже возвращает nullptr
    PMM_TEST( Mgr::resolve( p ) == nullptr );

    Mgr::destroy();

    return true;
}

/// @brief pptr — деаллокация null (статическая модель).
static bool test_p108_pptr_deallocate_null_static()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 25>;

    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p;
    Mgr::deallocate_typed( p ); // деаллокация null должна быть безопасна
    PMM_TEST( Mgr::is_initialized() );

    Mgr::destroy();

    return true;
}

// =============================================================================
// P108-C: InstanceId — независимые экземпляры
// =============================================================================

/// @brief Менеджеры с разными InstanceId независимы.
static bool test_p108_instance_independence()
{
    using Mgr0 = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 30>;
    using Mgr1 = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 31>;

    // Разные типы менеджеров
    static_assert( !std::is_same_v<Mgr0, Mgr1>, "Different InstanceId must produce different manager types" );

    // Разные типы pptr
    static_assert( !std::is_same_v<Mgr0::pptr<int>, Mgr1::pptr<int>>,
                   "pptr from different InstanceId must be different types" );

    PMM_TEST( !Mgr0::is_initialized() );
    PMM_TEST( !Mgr1::is_initialized() );

    PMM_TEST( Mgr0::create( 16 * 1024 ) );
    PMM_TEST( Mgr1::create( 32 * 1024 ) );

    PMM_TEST( Mgr0::is_initialized() );
    PMM_TEST( Mgr1::is_initialized() );

    // Разные размеры
    PMM_TEST( Mgr1::total_size() > Mgr0::total_size() );

    // Независимые аллокации
    Mgr0::pptr<int> p0 = Mgr0::allocate_typed<int>();
    Mgr1::pptr<int> p1 = Mgr1::allocate_typed<int>();

    PMM_TEST( !p0.is_null() );
    PMM_TEST( !p1.is_null() );

    // Разыменование через статические методы своего менеджера
    *p0 = 111;
    *p1 = 222;

    PMM_TEST( *p0 == 111 );
    PMM_TEST( *p1 == 222 );

    // p0 и p1 — разные типы, нельзя смешать
    // (проверка на уровне компилятора — Mgr0::pptr<int> != Mgr1::pptr<int>)

    Mgr0::deallocate_typed( p0 );
    Mgr1::deallocate_typed( p1 );

    Mgr0::destroy();
    Mgr1::destroy();

    return true;
}

/// @brief Три независимых менеджера одной конфигурации через InstanceId.
static bool test_p108_three_instances()
{
    using A = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 40>;
    using B = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 41>;
    using C = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 42>;

    PMM_TEST( A::create( 8 * 1024 ) );
    PMM_TEST( B::create( 16 * 1024 ) );
    PMM_TEST( C::create( 32 * 1024 ) );

    A::pptr<int> pa = A::allocate_typed<int>();
    B::pptr<int> pb = B::allocate_typed<int>();
    C::pptr<int> pc = C::allocate_typed<int>();

    *pa = 1;
    *pb = 2;
    *pc = 3;

    PMM_TEST( *pa == 1 );
    PMM_TEST( *pb == 2 );
    PMM_TEST( *pc == 3 );

    A::deallocate_typed( pa );
    B::deallocate_typed( pb );
    C::deallocate_typed( pc );

    A::destroy();
    B::destroy();
    C::destroy();

    return true;
}

/// @brief Менеджеры с разными конфигурациями также независимы.
static bool test_p108_different_configs()
{
    using CacheMgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 50>;
    using DataMgr  = pmm::StaticMemoryManager<pmm::PersistentDataConfig, 50>;

    // Разные конфигурации при одном InstanceId → разные типы
    static_assert( !std::is_same_v<CacheMgr, DataMgr>,
                   "Different ConfigT with same InstanceId must produce different manager types" );

    PMM_TEST( CacheMgr::create( 16 * 1024 ) );
    PMM_TEST( DataMgr::create( 32 * 1024 ) );

    CacheMgr::pptr<double> cp = CacheMgr::allocate_typed<double>();
    DataMgr::pptr<double>  dp = DataMgr::allocate_typed<double>();

    *cp = 3.14;
    *dp = 2.72;

    PMM_TEST( *cp == 3.14 );
    PMM_TEST( *dp == 2.72 );

    CacheMgr::deallocate_typed( cp );
    DataMgr::deallocate_typed( dp );

    CacheMgr::destroy();
    DataMgr::destroy();

    return true;
}

// =============================================================================
// P108-D: Изоляция тестов через destroy()
// =============================================================================

/// @brief destroy() сбрасывает состояние, повторная инициализация работает.
static bool test_p108_reinitialize_after_destroy()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 60>;

    // Первый цикл
    PMM_TEST( Mgr::create( 16 * 1024 ) );
    PMM_TEST( Mgr::is_initialized() );

    Mgr::pptr<int> p1 = Mgr::allocate_typed<int>();
    PMM_TEST( !p1.is_null() );
    *p1 = 100;
    PMM_TEST( *p1 == 100 );

    Mgr::destroy();
    PMM_TEST( !Mgr::is_initialized() );

    // Второй цикл — повторная инициализация
    PMM_TEST( Mgr::create( 32 * 1024 ) );
    PMM_TEST( Mgr::is_initialized() );

    Mgr::pptr<int> p2 = Mgr::allocate_typed<int>();
    PMM_TEST( !p2.is_null() );
    *p2 = 200;
    PMM_TEST( *p2 == 200 );

    Mgr::deallocate_typed( p2 );
    Mgr::destroy();
    PMM_TEST( !Mgr::is_initialized() );

    return true;
}

/// @brief destroy() без предшествующего create() безопасно.
static bool test_p108_destroy_uninitialized()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 61>;

    PMM_TEST( !Mgr::is_initialized() );
    Mgr::destroy(); // должно быть безопасно
    PMM_TEST( !Mgr::is_initialized() );

    return true;
}

/// @brief Статистика корректно отражает состояние.
static bool test_p108_statistics()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 62>;

    PMM_TEST( Mgr::create( 64 * 1024 ) );

    std::size_t total = Mgr::total_size();
    PMM_TEST( total >= 64 * 1024 );

    std::size_t free_before = Mgr::free_size();
    PMM_TEST( free_before > 0 );

    Mgr::pptr<std::uint64_t> p = Mgr::allocate_typed<std::uint64_t>();
    PMM_TEST( !p.is_null() );

    PMM_TEST( Mgr::free_size() < free_before );
    PMM_TEST( Mgr::alloc_block_count() >= 1 );

    Mgr::deallocate_typed( p );
    PMM_TEST( Mgr::free_size() >= free_before );

    Mgr::destroy();
    PMM_TEST( Mgr::total_size() == 0 );

    return true;
}

// =============================================================================
// P108-E: Унифицированный API PersistMemoryManager (Issue #110)
// =============================================================================

/// @brief PersistMemoryManager статический API работает (Issue #110 — унификация).
static bool test_p108_unified_static_api_works()
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 80>;

    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p = Mgr::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // Статическая модель: resolve() без аргументов
    int* raw = p.resolve();
    PMM_TEST( raw != nullptr );
    *raw = 42;
    PMM_TEST( *p.resolve() == 42 );

    // Через статический метод менеджера
    PMM_TEST( *Mgr::resolve<int>( p ) == 42 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();

    return true;
}

/// @brief pptr::index_type для PersistMemoryManager — из address_traits::index_type.
static bool test_p108_unified_index_type()
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 81>;

    // PersistMemoryManager предоставляет address_traits::index_type (uint32_t)
    static_assert( sizeof( Mgr::pptr<int> ) == 4, "pptr with PersistMemoryManager must be 4 bytes" );
    PMM_TEST( sizeof( Mgr::pptr<int> ) == 4 );

    return true;
}

/// @brief Сравнение pptr в статической модели.
static bool test_p108_pptr_comparison_static()
{
    using Mgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig, 70>;

    PMM_TEST( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p1 = Mgr::allocate_typed<int>();
    Mgr::pptr<int> p2 = Mgr::allocate_typed<int>();
    Mgr::pptr<int> p3 = p1;

    PMM_TEST( p1 == p3 );
    PMM_TEST( p1 != p2 );
    PMM_TEST( !( p1 == p2 ) );

    Mgr::deallocate_typed( p1 );
    Mgr::deallocate_typed( p2 );
    Mgr::destroy();

    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue108_static_model (Issue #108) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P108-A: StaticMemoryManager — базовые операции ---\n";
    PMM_RUN( "P108-A1: basic lifecycle (create/alloc/dealloc/destroy)", test_p108_basic_lifecycle );
    PMM_RUN( "P108-A2: array allocation", test_p108_array_allocation );
    PMM_RUN( "P108-A3: multiple types", test_p108_multiple_types );
    PMM_RUN( "P108-A4: auto-expand", test_p108_auto_expand );

    std::cout << "\n--- P108-B: pptr — статическая модель разыменования ---\n";
    PMM_RUN( "P108-B1: pptr::resolve() без аргументов", test_p108_pptr_resolve_no_arg );
    PMM_RUN( "P108-B2: pptr::operator* разыменование", test_p108_pptr_operator_deref );
    PMM_RUN( "P108-B3: pptr::operator-> доступ к полям", test_p108_pptr_operator_arrow );
    PMM_RUN( "P108-B4: pptr::index_type из менеджера", test_p108_pptr_index_type );
    PMM_RUN( "P108-B5: null pptr в статической модели", test_p108_pptr_null_static );
    PMM_RUN( "P108-B6: деаллокация null (статическая модель)", test_p108_pptr_deallocate_null_static );

    std::cout << "\n--- P108-C: InstanceId — независимые экземпляры ---\n";
    PMM_RUN( "P108-C1: менеджеры с разными InstanceId независимы", test_p108_instance_independence );
    PMM_RUN( "P108-C2: три независимых экземпляра", test_p108_three_instances );
    PMM_RUN( "P108-C3: разные конфигурации независимы", test_p108_different_configs );

    std::cout << "\n--- P108-D: Изоляция тестов через destroy() ---\n";
    PMM_RUN( "P108-D1: повторная инициализация после destroy()", test_p108_reinitialize_after_destroy );
    PMM_RUN( "P108-D2: destroy() неинициализированного менеджера", test_p108_destroy_uninitialized );
    PMM_RUN( "P108-D3: статистика корректна", test_p108_statistics );

    std::cout << "\n--- P108-E: Унифицированный API PersistMemoryManager (Issue #110) ---\n";
    PMM_RUN( "P108-E1: PersistMemoryManager статический API работает", test_p108_unified_static_api_works );
    PMM_RUN( "P108-E2: pptr::index_type из address_traits", test_p108_unified_index_type );
    PMM_RUN( "P108-E3: сравнение pptr в статической модели", test_p108_pptr_comparison_static );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
