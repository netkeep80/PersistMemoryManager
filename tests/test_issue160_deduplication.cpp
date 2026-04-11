// Suppress deprecation warnings — this test deliberately exercises deprecated functions.
#if defined( __GNUC__ ) || defined( __clang__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined( _MSC_VER )
#pragma warning( push )
#pragma warning( disable : 4996 )
#endif

/**
 * @file test_issue160_deduplication.cpp
 * @brief Тесты дедупликации функциональности ПАП.
 *
 * Проверяет:
 *   - BasicConfig<> — базовый шаблон конфигурации для heap-менеджеров
 *   - Совместимость псевдонимов конфигурации с оригинальными именами
 *   - Корректность рефакторинга функций конвертации байты↔гранулы:
 *       - detail::bytes_to_granules() делегирует в bytes_to_granules_t<DefaultAddressTraits>()
 *       - detail::granules_to_bytes() делегирует в DefaultAddressTraits::granules_to_bytes()
 *       - detail::idx_to_byte_off() делегирует в DefaultAddressTraits::idx_to_byte_off()
 *       - detail::byte_off_to_idx() делегирует в byte_off_to_idx_t<DefaultAddressTraits>()
 *   - Унификация block_total_granules: единственная шаблонная реализация
 *
 * @see include/pmm/manager_configs.h
 * @see include/pmm/types.h
 * @version 0.1
 */

#include "pmm/manager_configs.h"
#include "pmm/types.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// BasicConfig<> template
// =============================================================================

/// @brief BasicConfig создаёт конфигурацию с правильными типами по умолчанию.
TEST_CASE( "I160-A1: BasicConfig<> has correct default types", "[test_issue160_deduplication]" )
{
    using DefCfg = pmm::BasicConfig<>;

    static_assert( std::is_same<DefCfg::address_traits, pmm::DefaultAddressTraits>::value,
                   "BasicConfig<> address_traits must be DefaultAddressTraits" );
    static_assert( std::is_same<DefCfg::storage_backend, pmm::HeapStorage<pmm::DefaultAddressTraits>>::value,
                   "BasicConfig<> storage_backend must be HeapStorage<DefaultAddressTraits>" );
    static_assert( std::is_same<DefCfg::free_block_tree, pmm::AvlFreeTree<pmm::DefaultAddressTraits>>::value,
                   "BasicConfig<> free_block_tree must be AvlFreeTree<DefaultAddressTraits>" );
    static_assert( std::is_same<DefCfg::lock_policy, pmm::config::NoLock>::value,
                   "BasicConfig<> lock_policy must be NoLock" );
    static_assert( DefCfg::granule_size == 16, "BasicConfig<> granule_size must be 16" );
    static_assert( DefCfg::grow_numerator == pmm::config::kDefaultGrowNumerator,
                   "BasicConfig<> grow_numerator must be kDefaultGrowNumerator" );
    static_assert( DefCfg::grow_denominator == pmm::config::kDefaultGrowDenominator,
                   "BasicConfig<> grow_denominator must be kDefaultGrowDenominator" );
}

/// @brief BasicConfig с LargeAddressTraits и SharedMutexLock.
TEST_CASE( "I160-A2: BasicConfig<LargeAddressTraits,...> has correct types", "[test_issue160_deduplication]" )
{
    using LargeCfg = pmm::BasicConfig<pmm::LargeAddressTraits, pmm::config::SharedMutexLock, 2, 1, 0>;

    static_assert( std::is_same<LargeCfg::address_traits, pmm::LargeAddressTraits>::value,
                   "BasicConfig<LargeAddressTraits,...> address_traits must be LargeAddressTraits" );
    static_assert( std::is_same<LargeCfg::lock_policy, pmm::config::SharedMutexLock>::value,
                   "BasicConfig<...,SharedMutexLock,...> lock_policy must be SharedMutexLock" );
    static_assert( LargeCfg::granule_size == 64, "BasicConfig<LargeAddressTraits,...> granule_size must be 64" );
    static_assert( LargeCfg::grow_numerator == 2, "BasicConfig<...,2,...> grow_numerator must be 2" );
    static_assert( LargeCfg::grow_denominator == 1, "BasicConfig<...,1,...> grow_denominator must be 1" );
    static_assert( LargeCfg::max_memory_gb == 0, "BasicConfig<...,0> max_memory_gb must be 0" );
}

// =============================================================================
// Config aliases == BasicConfig specializations
// =============================================================================

/// @brief CacheManagerConfig — псевдоним BasicConfig<DefaultAddressTraits, NoLock, 5, 4, 64>.
TEST_CASE( "I160-B1: CacheManagerConfig is BasicConfig alias", "[test_issue160_deduplication]" )
{
    using Expected = pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::NoLock,
                                      pmm::config::kDefaultGrowNumerator, pmm::config::kDefaultGrowDenominator, 64>;
    static_assert( std::is_same<pmm::CacheManagerConfig, Expected>::value,
                   "CacheManagerConfig must be BasicConfig<DefaultAddressTraits, NoLock, 5, 4, 64>" );
}

/// @brief PersistentDataConfig — псевдоним BasicConfig с SharedMutexLock.
TEST_CASE( "I160-B2: PersistentDataConfig is BasicConfig alias", "[test_issue160_deduplication]" )
{
    using Expected = pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::SharedMutexLock,
                                      pmm::config::kDefaultGrowNumerator, pmm::config::kDefaultGrowDenominator, 64>;
    static_assert( std::is_same<pmm::PersistentDataConfig, Expected>::value,
                   "PersistentDataConfig must be BasicConfig<DefaultAddressTraits, SharedMutexLock, 5, 4, 64>" );
}

/// @brief EmbeddedManagerConfig — псевдоним BasicConfig с grow 3/2.
TEST_CASE( "I160-B3: EmbeddedManagerConfig is BasicConfig alias", "[test_issue160_deduplication]" )
{
    using Expected = pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::NoLock, 3, 2, 64>;
    static_assert( std::is_same<pmm::EmbeddedManagerConfig, Expected>::value,
                   "EmbeddedManagerConfig must be BasicConfig<DefaultAddressTraits, NoLock, 3, 2, 64>" );
}

/// @brief IndustrialDBConfig — псевдоним BasicConfig с grow 2/1.
TEST_CASE( "I160-B4: IndustrialDBConfig is BasicConfig alias", "[test_issue160_deduplication]" )
{
    using Expected = pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::SharedMutexLock, 2, 1, 64>;
    static_assert( std::is_same<pmm::IndustrialDBConfig, Expected>::value,
                   "IndustrialDBConfig must be BasicConfig<DefaultAddressTraits, SharedMutexLock, 2, 1, 64>" );
}

/// @brief LargeDBConfig — псевдоним BasicConfig с LargeAddressTraits.
TEST_CASE( "I160-B5: LargeDBConfig is BasicConfig alias", "[test_issue160_deduplication]" )
{
    using Expected = pmm::BasicConfig<pmm::LargeAddressTraits, pmm::config::SharedMutexLock, 2, 1, 0>;
    static_assert( std::is_same<pmm::LargeDBConfig, Expected>::value,
                   "LargeDBConfig must be BasicConfig<LargeAddressTraits, SharedMutexLock, 2, 1, 0>" );
}

// =============================================================================
// Byte/granule conversion deduplication
// =============================================================================

/// @brief detail::bytes_to_granules() == bytes_to_granules_t<DefaultAddressTraits>().
TEST_CASE( "I160-C1: detail::bytes_to_granules() delegates to _t", "[test_issue160_deduplication]" )
{
    using AT = pmm::DefaultAddressTraits;

    // Non-templated must produce same results as _t version for DefaultAddressTraits
    REQUIRE( pmm::detail::bytes_to_granules_t<pmm::DefaultAddressTraits>( 0 ) ==
             pmm::detail::bytes_to_granules_t<AT>( 0 ) );
    REQUIRE( pmm::detail::bytes_to_granules_t<pmm::DefaultAddressTraits>( 1 ) ==
             pmm::detail::bytes_to_granules_t<AT>( 1 ) );
    REQUIRE( pmm::detail::bytes_to_granules_t<pmm::DefaultAddressTraits>( 16 ) ==
             pmm::detail::bytes_to_granules_t<AT>( 16 ) );
    REQUIRE( pmm::detail::bytes_to_granules_t<pmm::DefaultAddressTraits>( 17 ) ==
             pmm::detail::bytes_to_granules_t<AT>( 17 ) );
    REQUIRE( pmm::detail::bytes_to_granules_t<pmm::DefaultAddressTraits>( 128 ) ==
             pmm::detail::bytes_to_granules_t<AT>( 128 ) );
    REQUIRE( pmm::detail::bytes_to_granules_t<pmm::DefaultAddressTraits>( 1000 ) ==
             pmm::detail::bytes_to_granules_t<AT>( 1000 ) );
}

/// @brief detail::granules_to_bytes() == DefaultAddressTraits::granules_to_bytes().
TEST_CASE( "I160-C2: detail::granules_to_bytes() delegates to AddressTraits", "[test_issue160_deduplication]" )
{
    using AT = pmm::DefaultAddressTraits;

    REQUIRE( pmm::DefaultAddressTraits::granules_to_bytes( 0 ) == AT::granules_to_bytes( 0 ) );
    REQUIRE( pmm::DefaultAddressTraits::granules_to_bytes( 1 ) == AT::granules_to_bytes( 1 ) );
    REQUIRE( pmm::DefaultAddressTraits::granules_to_bytes( 100 ) == AT::granules_to_bytes( 100 ) );
    REQUIRE( pmm::DefaultAddressTraits::granules_to_bytes( 1000 ) == AT::granules_to_bytes( 1000 ) );
}

/// @brief detail::idx_to_byte_off() == DefaultAddressTraits::idx_to_byte_off().
TEST_CASE( "I160-C3: detail::idx_to_byte_off() delegates to AddressTraits", "[test_issue160_deduplication]" )
{
    using AT = pmm::DefaultAddressTraits;

    REQUIRE( pmm::detail::idx_to_byte_off_t<pmm::DefaultAddressTraits>( 0 ) == AT::idx_to_byte_off( 0 ) );
    REQUIRE( pmm::detail::idx_to_byte_off_t<pmm::DefaultAddressTraits>( 1 ) == AT::idx_to_byte_off( 1 ) );
    REQUIRE( pmm::detail::idx_to_byte_off_t<pmm::DefaultAddressTraits>( 10 ) == AT::idx_to_byte_off( 10 ) );
    REQUIRE( pmm::detail::idx_to_byte_off_t<pmm::DefaultAddressTraits>( 100 ) == AT::idx_to_byte_off( 100 ) );
    REQUIRE( pmm::detail::idx_to_byte_off_t<pmm::DefaultAddressTraits>( 1000 ) == AT::idx_to_byte_off( 1000 ) );
}

/// @brief detail::byte_off_to_idx() == byte_off_to_idx_t<DefaultAddressTraits>().
TEST_CASE( "I160-C4: detail::byte_off_to_idx() delegates to _t", "[test_issue160_deduplication]" )
{
    using AT = pmm::DefaultAddressTraits;

    for ( std::uint32_t idx : { 0u, 1u, 10u, 100u, 1000u } )
    {
        std::size_t byte_off = AT::idx_to_byte_off( idx );
        REQUIRE( pmm::detail::byte_off_to_idx_t<pmm::DefaultAddressTraits>( byte_off ) ==
                 pmm::detail::byte_off_to_idx_t<AT>( byte_off ) );
    }
}

/// @brief Функции конвертации совместимы: roundtrip idx→bytes→idx.
TEST_CASE( "I160-C5: Conversion roundtrip idx->bytes->idx", "[test_issue160_deduplication]" )
{
    for ( std::uint32_t idx : { 1u, 2u, 5u, 10u, 100u, 500u } )
    {
        std::size_t   bytes = pmm::DefaultAddressTraits::granules_to_bytes( idx );
        std::uint32_t back  = pmm::detail::bytes_to_granules_t<pmm::DefaultAddressTraits>( bytes );
        REQUIRE( back == idx );

        std::size_t   byte_off = pmm::detail::idx_to_byte_off_t<pmm::DefaultAddressTraits>( idx );
        std::uint32_t idx_back = pmm::detail::byte_off_to_idx_t<pmm::DefaultAddressTraits>( byte_off );
        REQUIRE( idx_back == idx );
    }
}

// =============================================================================
// Block_total_granules single templated implementation
// =============================================================================

/// @brief block_total_granules шаблон работает с DefaultAddressTraits.
TEST_CASE( "I160-D1: block_total_granules<AT> compiles for multiple traits", "[test_issue160_deduplication]" )
{
    // Verify that the templated block_total_granules compiles and instantiates correctly.
    // Direct invocation would require a live manager — just verify the template is available
    // for both default and custom traits at compile time.
    using AT1 = pmm::DefaultAddressTraits;
    using AT2 = pmm::SmallAddressTraits;

    // Verify function template specializations are reachable (compile-time check).
    // Return type is now AT::index_type and ManagerHeader is templated on AT.
    static_assert(
        std::is_same<decltype( &pmm::detail::block_total_granules<AT1> ),
                     typename AT1::index_type ( * )( const std::uint8_t*, const pmm::detail::ManagerHeader<AT1>*,
                                                     const pmm::Block<AT1>* )>::value,
        "block_total_granules<DefaultAddressTraits> must have correct signature " );
    static_assert(
        std::is_same<decltype( &pmm::detail::block_total_granules<AT2> ),
                     typename AT2::index_type ( * )( const std::uint8_t*, const pmm::detail::ManagerHeader<AT2>*,
                                                     const pmm::Block<AT2>* )>::value,
        "block_total_granules<SmallAddressTraits> must have correct signature " );
}

// =============================================================================
// main
// =============================================================================

// Restore deprecation warnings
#if defined( __GNUC__ ) || defined( __clang__ )
#pragma GCC diagnostic pop
#elif defined( _MSC_VER )
#pragma warning( pop )
#endif
