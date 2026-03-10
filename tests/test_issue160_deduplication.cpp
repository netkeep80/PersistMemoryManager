/**
 * @file test_issue160_deduplication.cpp
 * @brief Тесты дедупликации функциональности ПАП (Issue #160).
 *
 * Проверяет:
 *   - BasicConfig<> — базовый шаблон конфигурации для heap-менеджеров (Issue #160)
 *   - Совместимость псевдонимов конфигурации с оригинальными именами
 *   - Корректность рефакторинга функций конвертации байты↔гранулы:
 *       - detail::bytes_to_granules() делегирует в bytes_to_granules_t<DefaultAddressTraits>()
 *       - detail::granules_to_bytes() делегирует в DefaultAddressTraits::granules_to_bytes()
 *       - detail::idx_to_byte_off() делегирует в DefaultAddressTraits::idx_to_byte_off()
 *       - detail::byte_off_to_idx() делегирует в byte_off_to_idx_t<DefaultAddressTraits>()
 *   - Унификация block_total_granules: единственная шаблонная реализация (Issue #160)
 *
 * @see include/pmm/manager_configs.h
 * @see include/pmm/types.h
 * @version 0.1 (Issue #160 — дедупликация)
 */

#include "pmm/manager_configs.h"
#include "pmm/types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
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
// Issue #160 Tests Section A: BasicConfig<> template
// =============================================================================

/// @brief BasicConfig создаёт конфигурацию с правильными типами по умолчанию.
static bool test_i160_basic_config_default_types()
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
    return true;
}

/// @brief BasicConfig с LargeAddressTraits и SharedMutexLock.
static bool test_i160_basic_config_large_db_params()
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
    return true;
}

// =============================================================================
// Issue #160 Tests Section B: Config aliases == BasicConfig specializations
// =============================================================================

/// @brief CacheManagerConfig — псевдоним BasicConfig<DefaultAddressTraits, NoLock, 5, 4, 64>.
static bool test_i160_cache_manager_config_is_basic_config()
{
    using Expected = pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::NoLock,
                                      pmm::config::kDefaultGrowNumerator, pmm::config::kDefaultGrowDenominator, 64>;
    static_assert( std::is_same<pmm::CacheManagerConfig, Expected>::value,
                   "CacheManagerConfig must be BasicConfig<DefaultAddressTraits, NoLock, 5, 4, 64>" );
    return true;
}

/// @brief PersistentDataConfig — псевдоним BasicConfig с SharedMutexLock.
static bool test_i160_persistent_data_config_is_basic_config()
{
    using Expected = pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::SharedMutexLock,
                                      pmm::config::kDefaultGrowNumerator, pmm::config::kDefaultGrowDenominator, 64>;
    static_assert( std::is_same<pmm::PersistentDataConfig, Expected>::value,
                   "PersistentDataConfig must be BasicConfig<DefaultAddressTraits, SharedMutexLock, 5, 4, 64>" );
    return true;
}

/// @brief EmbeddedManagerConfig — псевдоним BasicConfig с grow 3/2.
static bool test_i160_embedded_manager_config_is_basic_config()
{
    using Expected = pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::NoLock, 3, 2, 64>;
    static_assert( std::is_same<pmm::EmbeddedManagerConfig, Expected>::value,
                   "EmbeddedManagerConfig must be BasicConfig<DefaultAddressTraits, NoLock, 3, 2, 64>" );
    return true;
}

/// @brief IndustrialDBConfig — псевдоним BasicConfig с grow 2/1.
static bool test_i160_industrial_db_config_is_basic_config()
{
    using Expected = pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::SharedMutexLock, 2, 1, 64>;
    static_assert( std::is_same<pmm::IndustrialDBConfig, Expected>::value,
                   "IndustrialDBConfig must be BasicConfig<DefaultAddressTraits, SharedMutexLock, 2, 1, 64>" );
    return true;
}

/// @brief LargeDBConfig — псевдоним BasicConfig с LargeAddressTraits.
static bool test_i160_large_db_config_is_basic_config()
{
    using Expected = pmm::BasicConfig<pmm::LargeAddressTraits, pmm::config::SharedMutexLock, 2, 1, 0>;
    static_assert( std::is_same<pmm::LargeDBConfig, Expected>::value,
                   "LargeDBConfig must be BasicConfig<LargeAddressTraits, SharedMutexLock, 2, 1, 0>" );
    return true;
}

// =============================================================================
// Issue #160 Tests Section C: Byte/granule conversion deduplication
// =============================================================================

/// @brief detail::bytes_to_granules() == bytes_to_granules_t<DefaultAddressTraits>().
static bool test_i160_bytes_to_granules_delegates_to_t()
{
    using AT = pmm::DefaultAddressTraits;

    // Non-templated must produce same results as _t version for DefaultAddressTraits
    PMM_TEST( pmm::detail::bytes_to_granules( 0 ) == pmm::detail::bytes_to_granules_t<AT>( 0 ) );
    PMM_TEST( pmm::detail::bytes_to_granules( 1 ) == pmm::detail::bytes_to_granules_t<AT>( 1 ) );
    PMM_TEST( pmm::detail::bytes_to_granules( 16 ) == pmm::detail::bytes_to_granules_t<AT>( 16 ) );
    PMM_TEST( pmm::detail::bytes_to_granules( 17 ) == pmm::detail::bytes_to_granules_t<AT>( 17 ) );
    PMM_TEST( pmm::detail::bytes_to_granules( 128 ) == pmm::detail::bytes_to_granules_t<AT>( 128 ) );
    PMM_TEST( pmm::detail::bytes_to_granules( 1000 ) == pmm::detail::bytes_to_granules_t<AT>( 1000 ) );
    return true;
}

/// @brief detail::granules_to_bytes() == DefaultAddressTraits::granules_to_bytes().
static bool test_i160_granules_to_bytes_delegates_to_traits()
{
    using AT = pmm::DefaultAddressTraits;

    PMM_TEST( pmm::detail::granules_to_bytes( 0 ) == AT::granules_to_bytes( 0 ) );
    PMM_TEST( pmm::detail::granules_to_bytes( 1 ) == AT::granules_to_bytes( 1 ) );
    PMM_TEST( pmm::detail::granules_to_bytes( 100 ) == AT::granules_to_bytes( 100 ) );
    PMM_TEST( pmm::detail::granules_to_bytes( 1000 ) == AT::granules_to_bytes( 1000 ) );
    return true;
}

/// @brief detail::idx_to_byte_off() == DefaultAddressTraits::idx_to_byte_off().
static bool test_i160_idx_to_byte_off_delegates_to_traits()
{
    using AT = pmm::DefaultAddressTraits;

    PMM_TEST( pmm::detail::idx_to_byte_off( 0 ) == AT::idx_to_byte_off( 0 ) );
    PMM_TEST( pmm::detail::idx_to_byte_off( 1 ) == AT::idx_to_byte_off( 1 ) );
    PMM_TEST( pmm::detail::idx_to_byte_off( 10 ) == AT::idx_to_byte_off( 10 ) );
    PMM_TEST( pmm::detail::idx_to_byte_off( 100 ) == AT::idx_to_byte_off( 100 ) );
    PMM_TEST( pmm::detail::idx_to_byte_off( 1000 ) == AT::idx_to_byte_off( 1000 ) );
    return true;
}

/// @brief detail::byte_off_to_idx() == byte_off_to_idx_t<DefaultAddressTraits>().
static bool test_i160_byte_off_to_idx_delegates_to_t()
{
    using AT = pmm::DefaultAddressTraits;

    for ( std::uint32_t idx : { 0u, 1u, 10u, 100u, 1000u } )
    {
        std::size_t byte_off = AT::idx_to_byte_off( idx );
        PMM_TEST( pmm::detail::byte_off_to_idx( byte_off ) == pmm::detail::byte_off_to_idx_t<AT>( byte_off ) );
    }
    return true;
}

/// @brief Функции конвертации совместимы: roundtrip idx→bytes→idx.
static bool test_i160_conversion_roundtrip()
{
    for ( std::uint32_t idx : { 1u, 2u, 5u, 10u, 100u, 500u } )
    {
        std::size_t   bytes = pmm::detail::granules_to_bytes( idx );
        std::uint32_t back  = pmm::detail::bytes_to_granules( bytes );
        PMM_TEST( back == idx );

        std::size_t   byte_off = pmm::detail::idx_to_byte_off( idx );
        std::uint32_t idx_back = pmm::detail::byte_off_to_idx( byte_off );
        PMM_TEST( idx_back == idx );
    }
    return true;
}

// =============================================================================
// Issue #160 Tests Section D: block_total_granules single templated implementation
// =============================================================================

/// @brief block_total_granules шаблон работает с DefaultAddressTraits.
static bool test_i160_block_total_granules_templated()
{
    // Verify that the templated block_total_granules compiles and instantiates correctly.
    // Direct invocation would require a live manager — just verify the template is available
    // for both default and custom traits at compile time.
    using AT1 = pmm::DefaultAddressTraits;
    using AT2 = pmm::SmallAddressTraits;

    // Verify function template specializations are reachable (compile-time check).
    // Issue #175: return type is now AT::index_type and ManagerHeader is templated on AT.
    static_assert(
        std::is_same<decltype( &pmm::detail::block_total_granules<AT1> ),
                     typename AT1::index_type ( * )( const std::uint8_t*, const pmm::detail::ManagerHeader<AT1>*,
                                                     const pmm::Block<AT1>* )>::value,
        "block_total_granules<DefaultAddressTraits> must have correct signature (Issue #175)" );
    static_assert(
        std::is_same<decltype( &pmm::detail::block_total_granules<AT2> ),
                     typename AT2::index_type ( * )( const std::uint8_t*, const pmm::detail::ManagerHeader<AT2>*,
                                                     const pmm::Block<AT2>* )>::value,
        "block_total_granules<SmallAddressTraits> must have correct signature (Issue #175)" );
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue160_deduplication (Issue #160: Deduplication) ===\n\n";
    bool all_passed = true;

    std::cout << "--- I160-A: BasicConfig<> template ---\n";
    PMM_RUN( "I160-A1: BasicConfig<> has correct default types", test_i160_basic_config_default_types );
    PMM_RUN( "I160-A2: BasicConfig<LargeAddressTraits,...> has correct types", test_i160_basic_config_large_db_params );

    std::cout << "\n--- I160-B: Config aliases == BasicConfig specializations ---\n";
    PMM_RUN( "I160-B1: CacheManagerConfig is BasicConfig alias", test_i160_cache_manager_config_is_basic_config );
    PMM_RUN( "I160-B2: PersistentDataConfig is BasicConfig alias", test_i160_persistent_data_config_is_basic_config );
    PMM_RUN( "I160-B3: EmbeddedManagerConfig is BasicConfig alias", test_i160_embedded_manager_config_is_basic_config );
    PMM_RUN( "I160-B4: IndustrialDBConfig is BasicConfig alias", test_i160_industrial_db_config_is_basic_config );
    PMM_RUN( "I160-B5: LargeDBConfig is BasicConfig alias", test_i160_large_db_config_is_basic_config );

    std::cout << "\n--- I160-C: Byte/granule conversion deduplication ---\n";
    PMM_RUN( "I160-C1: detail::bytes_to_granules() delegates to _t", test_i160_bytes_to_granules_delegates_to_t );
    PMM_RUN( "I160-C2: detail::granules_to_bytes() delegates to AddressTraits",
             test_i160_granules_to_bytes_delegates_to_traits );
    PMM_RUN( "I160-C3: detail::idx_to_byte_off() delegates to AddressTraits",
             test_i160_idx_to_byte_off_delegates_to_traits );
    PMM_RUN( "I160-C4: detail::byte_off_to_idx() delegates to _t", test_i160_byte_off_to_idx_delegates_to_t );
    PMM_RUN( "I160-C5: Conversion roundtrip idx->bytes->idx", test_i160_conversion_roundtrip );

    std::cout << "\n--- I160-D: block_total_granules single templated implementation ---\n";
    PMM_RUN( "I160-D1: block_total_granules<AT> compiles for multiple traits",
             test_i160_block_total_granules_templated );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
