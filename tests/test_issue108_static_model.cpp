/**
 * @file test_issue108_static_model.cpp
 * @brief Тесты статической модели менеджера персистентной памяти.
 *
 *   - PersistMemoryManager<ConfigT, InstanceId> — все члены и методы статические
 *   - pptr<T, ManagerT>::resolve() — без аргументов (вызывает статический метод)
 *   - pptr<T, ManagerT>::operator* и operator-> — для удобного разыменования
 *   - pptr<T, ManagerT>::index_type — использует ManagerT::index_type если доступен
 *   - InstanceId — несколько независимых менеджеров одной конфигурации
 *
 * Тесты:
 *   P108-A: PersistMemoryManager — базовые операции
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
 *   P108-E: Унифицированный API PersistMemoryManager
 *     - PersistMemoryManager статический API работает
 *     - pptr::resolve() без аргументов
 *
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @see include/pmm/pptr.h — pptr (обновлённый)
 * @version 0.2
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

// ─── Test macros ─────────────────────────────────────────────────────────────

// =============================================================================
// P108-A: StaticMemoryManager — базовые операции
// =============================================================================

/// @brief StaticMemoryManager — создание, аллокация, деаллокация, уничтожение.
TEST_CASE( "P108-A1: basic lifecycle (create/alloc/dealloc/destroy)", "[test_issue108_static_model]" )
{
    // Используем уникальный InstanceId=10 чтобы не конфликтовать с другими тестами
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 10>;

    REQUIRE( !Mgr::is_initialized() );

    REQUIRE( Mgr::create( 64 * 1024 ) );
    REQUIRE( Mgr::is_initialized() );
    REQUIRE( Mgr::total_size() >= 64 * 1024 );
    REQUIRE( Mgr::free_size() > 0 );

    // Аллокация через статический метод
    Mgr::pptr<int> p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    REQUIRE( static_cast<bool>( p ) );
    REQUIRE( p.offset() > 0 );

    // Статический resolve
    int* raw = Mgr::resolve( p );
    REQUIRE( raw != nullptr );
    *raw = 42;
    REQUIRE( *Mgr::resolve( p ) == 42 );

    // Деаллокация
    Mgr::deallocate_typed( p );
    REQUIRE( Mgr::is_initialized() );

    Mgr::destroy();
    REQUIRE( !Mgr::is_initialized() );
}

/// @brief Аллокация массива через StaticMemoryManager.
TEST_CASE( "P108-A2: array allocation", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 11>;

    REQUIRE( Mgr::create( 256 * 1024 ) );

    const std::size_t count   = 10;
    Mgr::pptr<int>    arr_ptr = Mgr::allocate_typed<int>( count );
    REQUIRE( !arr_ptr.is_null() );

    int* arr = Mgr::resolve( arr_ptr );
    REQUIRE( arr != nullptr );

    for ( std::size_t i = 0; i < count; i++ )
        arr[i] = static_cast<int>( i * 100 );

    for ( std::size_t i = 0; i < count; i++ )
        REQUIRE( Mgr::resolve( arr_ptr )[i] == static_cast<int>( i * 100 ) );

    // resolve_at
    for ( std::size_t i = 0; i < count; i++ )
        REQUIRE( *Mgr::resolve_at( arr_ptr, i ) == static_cast<int>( i * 100 ) );

    Mgr::deallocate_typed( arr_ptr );
    Mgr::destroy();
}

/// @brief StaticMemoryManager — несколько аллокаций разных типов.
TEST_CASE( "P108-A3: multiple types", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 12>;

    REQUIRE( Mgr::create( 256 * 1024 ) );

    Mgr::pptr<int>    pi = Mgr::allocate_typed<int>();
    Mgr::pptr<double> pd = Mgr::allocate_typed<double>();
    Mgr::pptr<char>   pc = Mgr::allocate_typed<char>( 16 );

    REQUIRE( !pi.is_null() );
    REQUIRE( !pd.is_null() );
    REQUIRE( !pc.is_null() );

    *Mgr::resolve( pi ) = 7;
    *Mgr::resolve( pd ) = 2.718;
    std::memcpy( Mgr::resolve( pc ), "world", 6 );

    REQUIRE( *Mgr::resolve( pi ) == 7 );
    REQUIRE( *Mgr::resolve( pd ) == 2.718 );
    REQUIRE( std::memcmp( Mgr::resolve( pc ), "world", 6 ) == 0 );

    Mgr::deallocate_typed( pi );
    Mgr::deallocate_typed( pd );
    Mgr::deallocate_typed( pc );
    Mgr::destroy();
}

/// @brief StaticMemoryManager — auto-expand при нехватке памяти.
TEST_CASE( "P108-A4: auto-expand", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 13>;

    REQUIRE( Mgr::create( 8 * 1024 ) );

    std::size_t initial_total = Mgr::total_size();

    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 4 * 1024 );
    REQUIRE( !p1.is_null() );

    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 4 * 1024 );
    REQUIRE( !p2.is_null() );

    REQUIRE( Mgr::is_initialized() );
    REQUIRE( Mgr::total_size() > initial_total );

    Mgr::destroy();
}

// =============================================================================
// P108-B: pptr — статическая модель разыменования
// =============================================================================

/// @brief pptr::resolve() — без аргументов (статическая модель).
TEST_CASE( "P108-B1: pptr::resolve() без аргументов", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 20>;

    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    // resolve() без аргументов
    int* raw = p.resolve();
    REQUIRE( raw != nullptr );

    *raw = 99;
    REQUIRE( *p.resolve() == 99 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}

/// @brief pptr::operator* (статическая модель).
TEST_CASE( "P108-B2: pptr::operator* разыменование", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 21>;

    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    // operator* — разыменование без аргументов
    *p = 123;
    REQUIRE( *p == 123 );

    *p = 456;
    REQUIRE( *p == 456 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}

/// @brief pptr::operator-> — доступ к полям структуры (статическая модель).
TEST_CASE( "P108-B3: pptr::operator-> доступ к полям", "[test_issue108_static_model]" )
{
    struct Point
    {
        int x = 0;
        int y = 0;
    };

    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 22>;

    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<Point> p = Mgr::allocate_typed<Point>();
    REQUIRE( !p.is_null() );

    // Инициализация через resolve (чтобы убрать мусор)
    Mgr::resolve( p )->x = 0;
    Mgr::resolve( p )->y = 0;

    // operator-> — доступ к полям
    p->x = 10;
    p->y = 20;

    REQUIRE( p->x == 10 );
    REQUIRE( p->y == 20 );

    // Проверяем что это те же данные через resolve
    REQUIRE( Mgr::resolve( p )->x == 10 );
    REQUIRE( Mgr::resolve( p )->y == 20 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}

/// @brief pptr::index_type — тип индекса из менеджера.
TEST_CASE( "P108-B4: pptr::index_type из менеджера", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 23>;

    // StaticMemoryManager предоставляет index_type = DefaultAddressTraits::index_type = uint32_t
    static_assert( std::is_same_v<Mgr::index_type, std::uint32_t>,
                   "StaticMemoryManager::index_type must be uint32_t (DefaultAddressTraits)" );

    // pptr<T, Mgr>::index_type должен совпадать с Mgr::index_type
    static_assert( std::is_same_v<Mgr::pptr<int>::index_type, Mgr::index_type>,
                   "pptr::index_type must match manager::index_type" );

    REQUIRE( (std::is_same_v<Mgr::pptr<int>::index_type, std::uint32_t>));

    // sizeof(pptr<T, StaticMemoryManager>) == sizeof(uint32_t) == 4
    static_assert( sizeof( Mgr::pptr<int> ) == 4, "sizeof(pptr<int, StaticMemoryManager>) must be 4" );
    REQUIRE( sizeof( Mgr::pptr<int> ) == 4 );
}

/// @brief pptr null-указатель в статической модели.
TEST_CASE( "P108-B5: null pptr в статической модели", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 24>;

    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p; // null по умолчанию
    REQUIRE( p.is_null() );
    REQUIRE( !static_cast<bool>( p ) );

    // resolve() для null возвращает nullptr
    REQUIRE( p.resolve() == nullptr );

    // Статический resolve тоже возвращает nullptr
    REQUIRE( Mgr::resolve( p ) == nullptr );

    Mgr::destroy();
}

/// @brief pptr — деаллокация null (статическая модель).
TEST_CASE( "P108-B6: деаллокация null (статическая модель)", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25>;

    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p;
    Mgr::deallocate_typed( p ); // деаллокация null должна быть безопасна
    REQUIRE( Mgr::is_initialized() );

    Mgr::destroy();
}

// =============================================================================
// P108-C: InstanceId — независимые экземпляры
// =============================================================================

/// @brief Менеджеры с разными InstanceId независимы.
TEST_CASE( "P108-C1: менеджеры с разными InstanceId независимы", "[test_issue108_static_model]" )
{
    using Mgr0 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 30>;
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 31>;

    // Разные типы менеджеров
    static_assert( !std::is_same_v<Mgr0, Mgr1>, "Different InstanceId must produce different manager types" );

    // Разные типы pptr
    static_assert( !std::is_same_v<Mgr0::pptr<int>, Mgr1::pptr<int>>,
                   "pptr from different InstanceId must be different types" );

    REQUIRE( !Mgr0::is_initialized() );
    REQUIRE( !Mgr1::is_initialized() );

    REQUIRE( Mgr0::create( 16 * 1024 ) );
    REQUIRE( Mgr1::create( 32 * 1024 ) );

    REQUIRE( Mgr0::is_initialized() );
    REQUIRE( Mgr1::is_initialized() );

    // Разные размеры
    REQUIRE( Mgr1::total_size() > Mgr0::total_size() );

    // Независимые аллокации
    Mgr0::pptr<int> p0 = Mgr0::allocate_typed<int>();
    Mgr1::pptr<int> p1 = Mgr1::allocate_typed<int>();

    REQUIRE( !p0.is_null() );
    REQUIRE( !p1.is_null() );

    // Разыменование через статические методы своего менеджера
    *p0 = 111;
    *p1 = 222;

    REQUIRE( *p0 == 111 );
    REQUIRE( *p1 == 222 );

    // p0 и p1 — разные типы, нельзя смешать
    // (проверка на уровне компилятора — Mgr0::pptr<int> != Mgr1::pptr<int>)

    Mgr0::deallocate_typed( p0 );
    Mgr1::deallocate_typed( p1 );

    Mgr0::destroy();
    Mgr1::destroy();
}

/// @brief Три независимых менеджера одной конфигурации через InstanceId.
TEST_CASE( "P108-C2: три независимых экземпляра", "[test_issue108_static_model]" )
{
    using A = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 40>;
    using B = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 41>;
    using C = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 42>;

    REQUIRE( A::create( 8 * 1024 ) );
    REQUIRE( B::create( 16 * 1024 ) );
    REQUIRE( C::create( 32 * 1024 ) );

    A::pptr<int> pa = A::allocate_typed<int>();
    B::pptr<int> pb = B::allocate_typed<int>();
    C::pptr<int> pc = C::allocate_typed<int>();

    *pa = 1;
    *pb = 2;
    *pc = 3;

    REQUIRE( *pa == 1 );
    REQUIRE( *pb == 2 );
    REQUIRE( *pc == 3 );

    A::deallocate_typed( pa );
    B::deallocate_typed( pb );
    C::deallocate_typed( pc );

    A::destroy();
    B::destroy();
    C::destroy();
}

/// @brief Менеджеры с разными конфигурациями также независимы.
TEST_CASE( "P108-C3: разные конфигурации независимы", "[test_issue108_static_model]" )
{
    using CacheMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 50>;
    using DataMgr  = pmm::PersistMemoryManager<pmm::PersistentDataConfig, 50>;

    // Разные конфигурации при одном InstanceId → разные типы
    static_assert( !std::is_same_v<CacheMgr, DataMgr>,
                   "Different ConfigT with same InstanceId must produce different manager types" );

    REQUIRE( CacheMgr::create( 16 * 1024 ) );
    REQUIRE( DataMgr::create( 32 * 1024 ) );

    CacheMgr::pptr<double> cp = CacheMgr::allocate_typed<double>();
    DataMgr::pptr<double>  dp = DataMgr::allocate_typed<double>();

    *cp = 3.14;
    *dp = 2.72;

    REQUIRE( *cp == 3.14 );
    REQUIRE( *dp == 2.72 );

    CacheMgr::deallocate_typed( cp );
    DataMgr::deallocate_typed( dp );

    CacheMgr::destroy();
    DataMgr::destroy();
}

// =============================================================================
// P108-D: Изоляция тестов через destroy()
// =============================================================================

/// @brief destroy() сбрасывает состояние, повторная инициализация работает.
TEST_CASE( "P108-D1: повторная инициализация после destroy()", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 60>;

    // Первый цикл
    REQUIRE( Mgr::create( 16 * 1024 ) );
    REQUIRE( Mgr::is_initialized() );

    Mgr::pptr<int> p1 = Mgr::allocate_typed<int>();
    REQUIRE( !p1.is_null() );
    *p1 = 100;
    REQUIRE( *p1 == 100 );

    Mgr::destroy();
    REQUIRE( !Mgr::is_initialized() );

    // Второй цикл — повторная инициализация
    REQUIRE( Mgr::create( 32 * 1024 ) );
    REQUIRE( Mgr::is_initialized() );

    Mgr::pptr<int> p2 = Mgr::allocate_typed<int>();
    REQUIRE( !p2.is_null() );
    *p2 = 200;
    REQUIRE( *p2 == 200 );

    Mgr::deallocate_typed( p2 );
    Mgr::destroy();
    REQUIRE( !Mgr::is_initialized() );
}

/// @brief destroy() без предшествующего create() безопасно.
TEST_CASE( "P108-D2: destroy() неинициализированного менеджера", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 61>;

    REQUIRE( !Mgr::is_initialized() );
    Mgr::destroy(); // должно быть безопасно
    REQUIRE( !Mgr::is_initialized() );
}

/// @brief Статистика корректно отражает состояние.
TEST_CASE( "P108-D3: статистика корректна", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 62>;

    REQUIRE( Mgr::create( 64 * 1024 ) );

    std::size_t total = Mgr::total_size();
    REQUIRE( total >= 64 * 1024 );

    std::size_t free_before = Mgr::free_size();
    REQUIRE( free_before > 0 );

    Mgr::pptr<std::uint64_t> p = Mgr::allocate_typed<std::uint64_t>();
    REQUIRE( !p.is_null() );

    REQUIRE( Mgr::free_size() < free_before );
    REQUIRE( Mgr::alloc_block_count() >= 1 );

    Mgr::deallocate_typed( p );
    REQUIRE( Mgr::free_size() >= free_before );

    Mgr::destroy();
    REQUIRE( Mgr::total_size() == 0 );
}

// =============================================================================
// P108-E: Унифицированный API PersistMemoryManager
// =============================================================================

/// @brief PersistMemoryManager статический API работает.
TEST_CASE( "P108-E1: PersistMemoryManager статический API работает", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 80>;

    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    // Статическая модель: resolve() без аргументов
    int* raw = p.resolve();
    REQUIRE( raw != nullptr );
    *raw = 42;
    REQUIRE( *p.resolve() == 42 );

    // Через статический метод менеджера
    REQUIRE( *Mgr::resolve<int>( p ) == 42 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}

/// @brief pptr::index_type для PersistMemoryManager — из address_traits::index_type.
TEST_CASE( "P108-E2: pptr::index_type из address_traits", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 81>;

    // PersistMemoryManager предоставляет address_traits::index_type (uint32_t)
    static_assert( sizeof( Mgr::pptr<int> ) == 4, "pptr with PersistMemoryManager must be 4 bytes" );
    REQUIRE( sizeof( Mgr::pptr<int> ) == 4 );
}

/// @brief Сравнение pptr в статической модели.
TEST_CASE( "P108-E3: сравнение pptr в статической модели", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 70>;

    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<int> p1 = Mgr::allocate_typed<int>();
    Mgr::pptr<int> p2 = Mgr::allocate_typed<int>();
    Mgr::pptr<int> p3 = p1;

    REQUIRE( p1 == p3 );
    REQUIRE( p1 != p2 );
    REQUIRE( !( p1 == p2 ) );

    Mgr::deallocate_typed( p1 );
    Mgr::deallocate_typed( p2 );
    Mgr::destroy();
}

// =============================================================================
// PART F:
// =============================================================================

/// @brief Блок, заблокированный навечно, не может быть освобождён.
TEST_CASE( "P108-F1: lock_block_permanent блокирует навечно (нельзя освободить)", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 126>;

    REQUIRE( Mgr::create( 64 * 1024 ) );

    void* p = Mgr::allocate( 64 );
    REQUIRE( p != nullptr );

    // До блокировки: блок должен быть не заблокирован
    REQUIRE( !Mgr::is_permanently_locked( p ) );

    // Заблокировать навечно
    REQUIRE( Mgr::lock_block_permanent( p ) == true );

    // После блокировки: блок должен быть заблокирован
    REQUIRE( Mgr::is_permanently_locked( p ) );

    std::size_t alloc_count_before = Mgr::alloc_block_count();

    // Попытка освободить — должна быть проигнорирована
    Mgr::deallocate( p );

    // alloc_count не должен измениться
    REQUIRE( Mgr::alloc_block_count() == alloc_count_before );

    Mgr::destroy();
}

/// @brief lock_block_permanent возвращает false для nullptr и свободных блоков.
TEST_CASE( "P108-F2: lock_block_permanent граничные случаи (nullptr)", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 127>;

    // nullptr
    REQUIRE( Mgr::lock_block_permanent( nullptr ) == false );

    // Неинициализированный менеджер
    REQUIRE( Mgr::is_permanently_locked( nullptr ) == false );

    REQUIRE( Mgr::create( 64 * 1024 ) );

    // Обычный аллоцированный блок: кратковременно блокируем и разблокируем нельзя, но lock должен сработать
    void* p = Mgr::allocate( 32 );
    REQUIRE( p != nullptr );
    REQUIRE( !Mgr::is_permanently_locked( p ) );
    REQUIRE( Mgr::lock_block_permanent( p ) == true );
    REQUIRE( Mgr::is_permanently_locked( p ) );

    Mgr::destroy();
}

/// @brief Обычные блоки (не заблокированные) всё ещё могут быть освобождены.
TEST_CASE( "P108-F3: незаблокированные блоки по-прежнему освобождаются", "[test_issue108_static_model]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 128>;

    REQUIRE( Mgr::create( 64 * 1024 ) );

    void* p1 = Mgr::allocate( 32 );
    void* p2 = Mgr::allocate( 32 );
    REQUIRE( ( p1 != nullptr && p2 != nullptr ) );

    // Блокируем p1 навечно
    REQUIRE( Mgr::lock_block_permanent( p1 ) );

    std::size_t alloc_before = Mgr::alloc_block_count();
    REQUIRE( alloc_before >= 2 );

    // Освобождаем p2 (не заблокированный) — должно сработать (alloc_count уменьшится)
    Mgr::deallocate( p2 );
    REQUIRE( Mgr::alloc_block_count() == alloc_before - 1 );

    // Освобождаем p1 (заблокированный) — не должно освободиться (alloc_count не изменится)
    std::size_t alloc_after_p2 = Mgr::alloc_block_count();
    Mgr::deallocate( p1 );
    REQUIRE( Mgr::alloc_block_count() == alloc_after_p2 );

    Mgr::destroy();
}

// =============================================================================
// main
// =============================================================================
