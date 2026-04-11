/**
 * @file test_issue146_configs.cpp
 * @brief Тесты переосмысления конфигураций менеджеров ПАП.
 *
 * Проверяет:
 *   - Корректность новых конфигураций из manager_configs.h:
 *       - EmbeddedStaticConfig<N> — StaticStorage с DefaultAddressTraits
 *       - kMinGranuleSize — минимальный размер гранулы (4 байта)
 *   - Корректность нового пресета из pmm_presets.h:
 *       - EmbeddedStaticHeap<N> — менеджер с фиксированным статическим пулом
 *   - Статические проверки (static_assert) для всех конфигураций
 *   - Функциональность всех пресетов (create/allocate/deallocate/destroy)
 *
 * @see include/pmm/manager_configs.h
 * @see include/pmm/pmm_presets.h
 * @version 0.1
 */

#include "pmm_single_threaded_heap.h"
#include "pmm_multi_threaded_heap.h"
#include "pmm_embedded_heap.h"
#include "pmm_industrial_db_heap.h"
#include "pmm_embedded_static_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// Static checks (compile-time validation)
// =============================================================================

/// @brief kMinGranuleSize == 4 (размер слова архитектуры).
TEST_CASE( "I146-A1: kMinGranuleSize == 4 (architecture word size)", "[test_issue146_configs]" )
{
    static_assert( pmm::kMinGranuleSize == 4, "kMinGranuleSize must be 4 (architecture word size)" );
}

/// @brief Все конфигурации имеют granule_size >= kMinGranuleSize.
TEST_CASE( "I146-A2: All configs have granule_size >= kMinGranuleSize", "[test_issue146_configs]" )
{
    static_assert( pmm::CacheManagerConfig::granule_size >= pmm::kMinGranuleSize,
                   "CacheManagerConfig: granule_size must be >= kMinGranuleSize" );
    static_assert( pmm::PersistentDataConfig::granule_size >= pmm::kMinGranuleSize,
                   "PersistentDataConfig: granule_size must be >= kMinGranuleSize" );
    static_assert( pmm::EmbeddedManagerConfig::granule_size >= pmm::kMinGranuleSize,
                   "EmbeddedManagerConfig: granule_size must be >= kMinGranuleSize" );
    static_assert( pmm::IndustrialDBConfig::granule_size >= pmm::kMinGranuleSize,
                   "IndustrialDBConfig: granule_size must be >= kMinGranuleSize" );
    static_assert( pmm::EmbeddedStaticConfig<4096>::granule_size >= pmm::kMinGranuleSize,
                   "EmbeddedStaticConfig: granule_size must be >= kMinGranuleSize" );
}

/// @brief Все конфигурации имеют granule_size — степень двойки.
TEST_CASE( "I146-A3: All configs have granule_size as power of 2", "[test_issue146_configs]" )
{
    static_assert( ( pmm::CacheManagerConfig::granule_size & ( pmm::CacheManagerConfig::granule_size - 1 ) ) == 0,
                   "CacheManagerConfig: granule_size must be power of 2" );
    static_assert( ( pmm::PersistentDataConfig::granule_size & ( pmm::PersistentDataConfig::granule_size - 1 ) ) == 0,
                   "PersistentDataConfig: granule_size must be power of 2" );
    static_assert( ( pmm::EmbeddedManagerConfig::granule_size & ( pmm::EmbeddedManagerConfig::granule_size - 1 ) ) == 0,
                   "EmbeddedManagerConfig: granule_size must be power of 2" );
    static_assert( ( pmm::IndustrialDBConfig::granule_size & ( pmm::IndustrialDBConfig::granule_size - 1 ) ) == 0,
                   "IndustrialDBConfig: granule_size must be power of 2" );
    static_assert(
        ( pmm::EmbeddedStaticConfig<4096>::granule_size & ( pmm::EmbeddedStaticConfig<4096>::granule_size - 1 ) ) == 0,
        "EmbeddedStaticConfig: granule_size must be power of 2" );
}

/// @brief EmbeddedStaticConfig использует StaticStorage (нет heap).
TEST_CASE( "I146-A4: EmbeddedStaticConfig uses StaticStorage (no heap)", "[test_issue146_configs]" )
{
    using Config = pmm::EmbeddedStaticConfig<4096>;
    static_assert( std::is_same<Config::storage_backend, pmm::StaticStorage<4096, pmm::DefaultAddressTraits>>::value,
                   "EmbeddedStaticConfig must use StaticStorage<4096, DefaultAddressTraits>" );
    static_assert( std::is_same<Config::lock_policy, pmm::config::NoLock>::value,
                   "EmbeddedStaticConfig must use NoLock" );
}

/// @brief EmbeddedStaticConfig pptr размером 4 байта (32-bit адресация).
TEST_CASE( "I146-A5: EmbeddedStaticHeap pptr size == 4 bytes", "[test_issue146_configs]" )
{
    using ESH = pmm::presets::EmbeddedStaticHeap<4096>;
    static_assert( sizeof( ESH::pptr<int> ) == 4, "EmbeddedStaticHeap pptr<int> must be 4 bytes" );
}

// =============================================================================
// EmbeddedStaticHeap (StaticStorage, no heap)
// =============================================================================

/// @brief EmbeddedStaticHeap<4096>: базовый жизненный цикл.
TEST_CASE( "I146-B1: EmbeddedStaticHeap<4096> lifecycle", "[test_issue146_configs]" )
{
    using ESH = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<4096>, 1460>;

    REQUIRE( !ESH::is_initialized() );

    // StaticStorage уже содержит буфер 4096 байт
    // create() работает когда initial_size <= BufferSize
    bool created = ESH::create( 4096 );
    REQUIRE( created );
    REQUIRE( ESH::is_initialized() );
    REQUIRE( ESH::total_size() == 4096 );

    void* ptr = ESH::allocate( 64 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xAB, 64 );
    ESH::deallocate( ptr );

    ESH::destroy();
    REQUIRE( !ESH::is_initialized() );
}

/// @brief EmbeddedStaticHeap: typed allocation via pptr<T>.
TEST_CASE( "I146-B2: EmbeddedStaticHeap<4096> typed allocation", "[test_issue146_configs]" )
{
    using ESH = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<4096>, 1461>;

    REQUIRE( ESH::create( 4096 ) );

    ESH::pptr<int> p = ESH::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    REQUIRE( p.offset() > 0 );

    *p.resolve() = 12345;
    REQUIRE( *p.resolve() == 12345 );

    ESH::deallocate_typed( p );
    ESH::destroy();
}

/// @brief EmbeddedStaticHeap: StaticStorage не расширяется (expand всегда false).
TEST_CASE( "I146-B3: EmbeddedStaticHeap<4096> no expand (StaticStorage)", "[test_issue146_configs]" )
{
    using ESH = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<4096>, 1462>;

    REQUIRE( ESH::create( 4096 ) );
    std::size_t sz_before = ESH::total_size();

    // Выделяем больше, чем осталось в пуле — expand вернёт false, allocate вернёт nullptr
    // (StaticStorage не может расшириться)
    void* large = ESH::allocate( 4080 ); // Почти весь буфер — заголовки занимают место
    // Результат зависит от overhead — просто проверяем стабильность
    if ( large != nullptr )
        ESH::deallocate( large );

    // Размер не изменился — StaticStorage
    REQUIRE( ESH::total_size() == sz_before );

    ESH::destroy();
}

/// @brief EmbeddedStaticHeap: множественные аллокации в статическом пуле.
TEST_CASE( "I146-B4: EmbeddedStaticHeap<4096> multiple allocations", "[test_issue146_configs]" )
{
    using ESH = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<4096>, 1463>;

    REQUIRE( ESH::create( 4096 ) );

    void* ptrs[8];
    for ( int i = 0; i < 8; ++i )
    {
        ptrs[i] = ESH::allocate( 64 );
        REQUIRE( ptrs[i] != nullptr );
        std::memset( ptrs[i], static_cast<int>( i ), 64 );
    }

    // Проверяем, что данные корректны
    for ( int i = 0; i < 8; ++i )
    {
        std::uint8_t* bytes = static_cast<std::uint8_t*>( ptrs[i] );
        REQUIRE( bytes[0] == static_cast<std::uint8_t>( i ) );
    }

    for ( int i = 0; i < 8; ++i )
        ESH::deallocate( ptrs[i] );

    ESH::destroy();
}

// =============================================================================
// Verification of all preset lock policies
// =============================================================================

/// @brief Все пресеты имеют корректные политики блокировок.
TEST_CASE( "I146-C1: All preset lock policies are correct", "[test_issue146_configs]" )
{
    using STH = pmm::presets::SingleThreadedHeap;
    using MTH = pmm::presets::MultiThreadedHeap;
    using EMB = pmm::presets::EmbeddedHeap;
    using IDB = pmm::presets::IndustrialDBHeap;
    using ESH = pmm::presets::EmbeddedStaticHeap<4096>;

    static_assert( std::is_same<STH::thread_policy, pmm::config::NoLock>::value, "SingleThreadedHeap must use NoLock" );
    static_assert( std::is_same<MTH::thread_policy, pmm::config::SharedMutexLock>::value,
                   "MultiThreadedHeap must use SharedMutexLock" );
    static_assert( std::is_same<EMB::thread_policy, pmm::config::NoLock>::value, "EmbeddedHeap must use NoLock" );
    static_assert( std::is_same<IDB::thread_policy, pmm::config::SharedMutexLock>::value,
                   "IndustrialDBHeap must use SharedMutexLock" );
    static_assert( std::is_same<ESH::thread_policy, pmm::config::NoLock>::value, "EmbeddedStaticHeap must use NoLock" );
}

/// @brief Все пресеты используют DefaultAddressTraits (uint32_t, 16B granule).
TEST_CASE( "I146-C2: All presets use DefaultAddressTraits", "[test_issue146_configs]" )
{
    using STH = pmm::presets::SingleThreadedHeap;
    using MTH = pmm::presets::MultiThreadedHeap;
    using EMB = pmm::presets::EmbeddedHeap;
    using IDB = pmm::presets::IndustrialDBHeap;
    using ESH = pmm::presets::EmbeddedStaticHeap<4096>;

    static_assert( std::is_same<STH::address_traits, pmm::DefaultAddressTraits>::value,
                   "SingleThreadedHeap must use DefaultAddressTraits" );
    static_assert( std::is_same<MTH::address_traits, pmm::DefaultAddressTraits>::value,
                   "MultiThreadedHeap must use DefaultAddressTraits" );
    static_assert( std::is_same<EMB::address_traits, pmm::DefaultAddressTraits>::value,
                   "EmbeddedHeap must use DefaultAddressTraits" );
    static_assert( std::is_same<IDB::address_traits, pmm::DefaultAddressTraits>::value,
                   "IndustrialDBHeap must use DefaultAddressTraits" );
    static_assert( std::is_same<ESH::address_traits, pmm::DefaultAddressTraits>::value,
                   "EmbeddedStaticHeap must use DefaultAddressTraits" );
}

// =============================================================================
// Growth rate verification
// =============================================================================

/// @brief Конфигурации имеют корректные коэффициенты роста.
TEST_CASE( "I146-D1: All config grow ratios are correct", "[test_issue146_configs]" )
{
    // CacheManagerConfig: 25% growth
    static_assert( pmm::CacheManagerConfig::grow_numerator == pmm::config::kDefaultGrowNumerator,
                   "CacheManagerConfig grow_numerator must be kDefaultGrowNumerator" );
    static_assert( pmm::CacheManagerConfig::grow_denominator == pmm::config::kDefaultGrowDenominator,
                   "CacheManagerConfig grow_denominator must be kDefaultGrowDenominator" );

    // PersistentDataConfig: 25% growth
    static_assert( pmm::PersistentDataConfig::grow_numerator == pmm::config::kDefaultGrowNumerator,
                   "PersistentDataConfig grow_numerator must be kDefaultGrowNumerator" );
    static_assert( pmm::PersistentDataConfig::grow_denominator == pmm::config::kDefaultGrowDenominator,
                   "PersistentDataConfig grow_denominator must be kDefaultGrowDenominator" );

    // EmbeddedManagerConfig: 50% growth (3/2)
    static_assert( pmm::EmbeddedManagerConfig::grow_numerator == 3, "EmbeddedManagerConfig grow_numerator must be 3" );
    static_assert( pmm::EmbeddedManagerConfig::grow_denominator == 2,
                   "EmbeddedManagerConfig grow_denominator must be 2" );

    // IndustrialDBConfig: 100% growth (2/1)
    static_assert( pmm::IndustrialDBConfig::grow_numerator == 2, "IndustrialDBConfig grow_numerator must be 2" );
    static_assert( pmm::IndustrialDBConfig::grow_denominator == 1, "IndustrialDBConfig grow_denominator must be 1" );

    // EmbeddedStaticConfig: 50% growth (3/2, but not used since StaticStorage doesn't expand)
    static_assert( pmm::EmbeddedStaticConfig<4096>::grow_numerator == 3,
                   "EmbeddedStaticConfig grow_numerator must be 3" );
    static_assert( pmm::EmbeddedStaticConfig<4096>::grow_denominator == 2,
                   "EmbeddedStaticConfig grow_denominator must be 2" );
}

// =============================================================================
// EmbeddedStaticHeap preset via pmm_presets.h
// =============================================================================

/// @brief EmbeddedStaticHeap<8192> — использование пресета с нестандартным размером.
TEST_CASE( "I146-E1: EmbeddedStaticHeap<8192> preset lifecycle", "[test_issue146_configs]" )
{
    // Используем уникальный InstanceId чтобы не конфликтовать с другими тестами
    using ESH8K = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<8192>, 1470>;

    REQUIRE( !ESH8K::is_initialized() );
    REQUIRE( ESH8K::create( 8192 ) );
    REQUIRE( ESH8K::is_initialized() );
    REQUIRE( ESH8K::total_size() == 8192 );

    ESH8K::pptr<double> p = ESH8K::allocate_typed<double>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 3.14159;
    REQUIRE( *p.resolve() == 3.14159 );

    ESH8K::deallocate_typed( p );
    ESH8K::destroy();
    REQUIRE( !ESH8K::is_initialized() );
}

// =============================================================================
// main
// =============================================================================
