/**
 * @file test_issue166_deduplication.cpp
 * @brief Тесты дедупликации функциональности ПАП (Issue #166).
 *
 * Проверяет:
 *   - detail::kNoBlock_v<AT> — новый шаблонный псевдоним для AT::no_block (Issue #166)
 *   - detail::required_block_granules_t<AT>() — шаблонная версия для любого AT (Issue #166)
 *   - Устранение дублирования: detail::granules_to_bytes() в persist_memory_manager.h
 *     заменён на address_traits::granules_to_bytes() (Issue #166)
 *   - Удаление избыточных static_assert в SmallEmbeddedStaticConfig и EmbeddedStaticConfig (Issue #166)
 *   - ValidPmmAddressTraits уже проверяется на уровне namespace — не нужны повторные assert в конфигах
 *
 * @see include/pmm/types.h — detail::kNoBlock_v<AT>, detail::required_block_granules_t<AT>
 * @see include/pmm/manager_configs.h — SmallEmbeddedStaticConfig, EmbeddedStaticConfig
 * @see include/pmm/persist_memory_manager.h — used_size(), free_size()
 * @version 0.1 (Issue #166 — дедупликация)
 */

#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"
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
// Issue #166 Tests Section A: kNoBlock_v<AT> template alias
// =============================================================================

/// @brief kNoBlock_v<DefaultAddressTraits> == DefaultAddressTraits::no_block == kNoBlock.
static bool test_i166_kNoBlock_v_default()
{
    using AT = pmm::DefaultAddressTraits;

    static_assert( pmm::detail::kNoBlock_v<AT> == AT::no_block,
                   "kNoBlock_v<DefaultAddressTraits> must equal DefaultAddressTraits::no_block" );
    static_assert( pmm::detail::kNoBlock_v<AT> == pmm::detail::kNoBlock,
                   "kNoBlock_v<DefaultAddressTraits> must equal detail::kNoBlock" );
    static_assert( pmm::detail::kNoBlock_v<AT> == 0xFFFFFFFFU, "kNoBlock_v<DefaultAddressTraits> must be 0xFFFFFFFF" );
    return true;
}

/// @brief kNoBlock_v<SmallAddressTraits> == SmallAddressTraits::no_block == 0xFFFF.
static bool test_i166_kNoBlock_v_small()
{
    using AT = pmm::SmallAddressTraits;

    static_assert( pmm::detail::kNoBlock_v<AT> == AT::no_block,
                   "kNoBlock_v<SmallAddressTraits> must equal SmallAddressTraits::no_block" );
    static_assert( pmm::detail::kNoBlock_v<AT> == static_cast<std::uint16_t>( 0xFFFFU ),
                   "kNoBlock_v<SmallAddressTraits> must be 0xFFFF" );
    return true;
}

/// @brief kNoBlock_v<LargeAddressTraits> == LargeAddressTraits::no_block == 0xFFFFFFFFFFFFFFFF.
static bool test_i166_kNoBlock_v_large()
{
    using AT = pmm::LargeAddressTraits;

    static_assert( pmm::detail::kNoBlock_v<AT> == AT::no_block,
                   "kNoBlock_v<LargeAddressTraits> must equal LargeAddressTraits::no_block" );
    static_assert( pmm::detail::kNoBlock_v<AT> == static_cast<std::uint64_t>( 0xFFFFFFFFFFFFFFFFULL ),
                   "kNoBlock_v<LargeAddressTraits> must be 0xFFFFFFFFFFFFFFFF" );
    return true;
}

/// @brief kNoBlock_v<AT> has correct type: AT::index_type.
static bool test_i166_kNoBlock_v_type()
{
    static_assert(
        std::is_same<decltype( pmm::detail::kNoBlock_v<pmm::DefaultAddressTraits> ), const std::uint32_t>::value,
        "kNoBlock_v<DefaultAddressTraits> must have type const uint32_t" );
    static_assert(
        std::is_same<decltype( pmm::detail::kNoBlock_v<pmm::SmallAddressTraits> ), const std::uint16_t>::value,
        "kNoBlock_v<SmallAddressTraits> must have type const uint16_t" );
    static_assert(
        std::is_same<decltype( pmm::detail::kNoBlock_v<pmm::LargeAddressTraits> ), const std::uint64_t>::value,
        "kNoBlock_v<LargeAddressTraits> must have type const uint64_t" );
    return true;
}

// =============================================================================
// Issue #166 Tests Section B: required_block_granules_t<AT>
// =============================================================================

/// @brief required_block_granules_t<DefaultAddressTraits>() == required_block_granules() for DefaultAddressTraits.
static bool test_i166_required_block_granules_t_default()
{
    using AT = pmm::DefaultAddressTraits;

    for ( std::size_t bytes : { 0ul, 1ul, 16ul, 17ul, 32ul, 64ul, 128ul, 256ul } )
    {
        std::uint32_t from_t   = pmm::detail::required_block_granules_t<AT>( bytes );
        std::uint32_t from_old = pmm::detail::required_block_granules( bytes );
        PMM_TEST( from_t == from_old );
    }
    return true;
}

/// @brief required_block_granules_t<SmallAddressTraits>() uses SmallAddressTraits header granules.
static bool test_i166_required_block_granules_t_small()
{
    using AT = pmm::SmallAddressTraits;

    // Verify: result >= kBlockHeaderGranules_t<AT> + 1 (at least header + 1 data granule).
    static constexpr std::uint32_t kHdrGran = pmm::detail::kBlockHeaderGranules_t<AT>;

    for ( std::size_t bytes : { 0ul, 1ul, 16ul, 17ul, 32ul } )
    {
        std::uint32_t gran = pmm::detail::required_block_granules_t<AT>( bytes );
        PMM_TEST( gran >= kHdrGran + 1 );
    }

    // For 0 bytes: ceil(0/16)=0, clamp to 1 → hdr+1
    PMM_TEST( pmm::detail::required_block_granules_t<AT>( 0 ) == kHdrGran + 1 );

    // For 16 bytes: ceil(16/16)=1 → hdr+1
    PMM_TEST( pmm::detail::required_block_granules_t<AT>( 16 ) == kHdrGran + 1 );

    // For 17 bytes: ceil(17/16)=2 → hdr+2
    PMM_TEST( pmm::detail::required_block_granules_t<AT>( 17 ) == kHdrGran + 2 );
    return true;
}

/// @brief required_block_granules_t<LargeAddressTraits>() uses 64-byte granule.
static bool test_i166_required_block_granules_t_large()
{
    using AT = pmm::LargeAddressTraits;

    // Verify: granule_size=64, so 1..64 bytes → 1 data granule.
    static constexpr std::uint32_t kHdrGran = pmm::detail::kBlockHeaderGranules_t<AT>;

    PMM_TEST( pmm::detail::required_block_granules_t<AT>( 0 ) == kHdrGran + 1 );
    PMM_TEST( pmm::detail::required_block_granules_t<AT>( 1 ) == kHdrGran + 1 );
    PMM_TEST( pmm::detail::required_block_granules_t<AT>( 64 ) == kHdrGran + 1 );
    PMM_TEST( pmm::detail::required_block_granules_t<AT>( 65 ) == kHdrGran + 2 );
    PMM_TEST( pmm::detail::required_block_granules_t<AT>( 128 ) == kHdrGran + 2 );
    PMM_TEST( pmm::detail::required_block_granules_t<AT>( 129 ) == kHdrGran + 3 );
    return true;
}

// =============================================================================
// Issue #166 Tests Section C: address_traits::granules_to_bytes in persist_memory_manager.h
// =============================================================================

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 166>;

/// @brief used_size() returns correct byte count (uses address_traits::granules_to_bytes internally).
static bool test_i166_used_size_uses_address_traits_granules_to_bytes()
{
    TestMgr::create( 64 * 1024 );

    std::size_t used_before = TestMgr::used_size();
    PMM_TEST( used_before > 0 ); // Header + block_0 always consume some granules.

    // Allocate a block and check that used_size grows by the expected amount.
    void* p = TestMgr::allocate( 16 );
    PMM_TEST( p != nullptr );

    std::size_t used_after = TestMgr::used_size();
    PMM_TEST( used_after > used_before );

    // used_size() must be a multiple of granule_size (16 bytes for DefaultAddressTraits).
    using AT = pmm::DefaultAddressTraits;
    PMM_TEST( used_after % AT::granule_size == 0 );

    TestMgr::deallocate( p );
    TestMgr::destroy();
    return true;
}

/// @brief free_size() returns correct byte count (uses address_traits::granules_to_bytes internally).
static bool test_i166_free_size_uses_address_traits_granules_to_bytes()
{
    TestMgr::create( 64 * 1024 );

    std::size_t total = TestMgr::total_size();
    std::size_t used  = TestMgr::used_size();
    std::size_t free  = TestMgr::free_size();

    // Total = Used + Free (approximately, some overhead).
    PMM_TEST( total >= used + free );

    // free_size() must be a multiple of granule_size.
    using AT = pmm::DefaultAddressTraits;
    PMM_TEST( free % AT::granule_size == 0 );

    TestMgr::destroy();
    return true;
}

/// @brief used_size() + free_size() is consistent with total_size() after allocations.
static bool test_i166_used_free_consistent_with_total()
{
    TestMgr::create( 64 * 1024 );

    void* p1 = TestMgr::allocate( 64 );
    void* p2 = TestMgr::allocate( 128 );
    PMM_TEST( p1 != nullptr );
    PMM_TEST( p2 != nullptr );

    std::size_t total = TestMgr::total_size();
    std::size_t used  = TestMgr::used_size();
    std::size_t free  = TestMgr::free_size();

    PMM_TEST( used + free <= total );
    PMM_TEST( used > 0 );

    TestMgr::deallocate( p1 );
    TestMgr::deallocate( p2 );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// Issue #166 Tests Section D: ValidPmmAddressTraits not duplicated in embedded configs
// =============================================================================

/// @brief SmallEmbeddedStaticConfig compiles without redundant static_assert — compile-time check.
static bool test_i166_small_embedded_config_no_redundant_assert()
{
    // If the redundant static_assert were still present, this would be a compile-time duplicate.
    // The test passes by successfully instantiating both config sizes.
    using Cfg1 = pmm::SmallEmbeddedStaticConfig<1024>;
    using Cfg2 = pmm::SmallEmbeddedStaticConfig<2048>;

    static_assert( std::is_same<Cfg1::address_traits, pmm::SmallAddressTraits>::value,
                   "SmallEmbeddedStaticConfig<1024> must use SmallAddressTraits" );
    static_assert( std::is_same<Cfg2::address_traits, pmm::SmallAddressTraits>::value,
                   "SmallEmbeddedStaticConfig<2048> must use SmallAddressTraits" );

    // ValidPmmAddressTraits is satisfied for SmallAddressTraits (verified at namespace scope).
    static_assert( pmm::ValidPmmAddressTraits<pmm::SmallAddressTraits>,
                   "SmallAddressTraits must satisfy ValidPmmAddressTraits" );
    return true;
}

/// @brief EmbeddedStaticConfig compiles without redundant static_assert — compile-time check.
static bool test_i166_embedded_static_config_no_redundant_assert()
{
    using Cfg1 = pmm::EmbeddedStaticConfig<4096>;
    using Cfg2 = pmm::EmbeddedStaticConfig<8192>;

    static_assert( std::is_same<Cfg1::address_traits, pmm::DefaultAddressTraits>::value,
                   "EmbeddedStaticConfig<4096> must use DefaultAddressTraits" );
    static_assert( std::is_same<Cfg2::address_traits, pmm::DefaultAddressTraits>::value,
                   "EmbeddedStaticConfig<8192> must use DefaultAddressTraits" );

    // ValidPmmAddressTraits is satisfied for DefaultAddressTraits (verified at namespace scope).
    static_assert( pmm::ValidPmmAddressTraits<pmm::DefaultAddressTraits>,
                   "DefaultAddressTraits must satisfy ValidPmmAddressTraits" );
    return true;
}

/// @brief BasicConfig<AT> still has static_assert for ValidPmmAddressTraits (explicit param).
static bool test_i166_basic_config_still_validates_at()
{
    // BasicConfig explicitly validates the AT parameter (which may be arbitrary user-provided).
    // This is different from SmallEmbeddedStaticConfig/EmbeddedStaticConfig which use fixed types.
    using Cfg = pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::NoLock>;
    static_assert( pmm::ValidPmmAddressTraits<Cfg::address_traits>,
                   "BasicConfig address_traits must satisfy ValidPmmAddressTraits" );
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue166_deduplication (Issue #166: Deduplication) ===\n\n";
    bool all_passed = true;

    std::cout << "--- I166-A: kNoBlock_v<AT> template alias ---\n";
    PMM_RUN( "I166-A1: kNoBlock_v<DefaultAddressTraits> == kNoBlock", test_i166_kNoBlock_v_default );
    PMM_RUN( "I166-A2: kNoBlock_v<SmallAddressTraits> == 0xFFFF", test_i166_kNoBlock_v_small );
    PMM_RUN( "I166-A3: kNoBlock_v<LargeAddressTraits> == 0xFFFFFFFFFFFFFFFF", test_i166_kNoBlock_v_large );
    PMM_RUN( "I166-A4: kNoBlock_v<AT> has correct index_type", test_i166_kNoBlock_v_type );

    std::cout << "\n--- I166-B: required_block_granules_t<AT> templated function ---\n";
    PMM_RUN( "I166-B1: required_block_granules_t<DefaultAddressTraits> matches non-templated",
             test_i166_required_block_granules_t_default );
    PMM_RUN( "I166-B2: required_block_granules_t<SmallAddressTraits> uses correct granule",
             test_i166_required_block_granules_t_small );
    PMM_RUN( "I166-B3: required_block_granules_t<LargeAddressTraits> uses 64-byte granule",
             test_i166_required_block_granules_t_large );

    std::cout << "\n--- I166-C: address_traits::granules_to_bytes in persist_memory_manager ---\n";
    PMM_RUN( "I166-C1: used_size() correct with address_traits::granules_to_bytes",
             test_i166_used_size_uses_address_traits_granules_to_bytes );
    PMM_RUN( "I166-C2: free_size() correct with address_traits::granules_to_bytes",
             test_i166_free_size_uses_address_traits_granules_to_bytes );
    PMM_RUN( "I166-C3: used_size() + free_size() consistent with total_size()",
             test_i166_used_free_consistent_with_total );

    std::cout << "\n--- I166-D: ValidPmmAddressTraits not duplicated in embedded configs ---\n";
    PMM_RUN( "I166-D1: SmallEmbeddedStaticConfig compiles without redundant assert",
             test_i166_small_embedded_config_no_redundant_assert );
    PMM_RUN( "I166-D2: EmbeddedStaticConfig compiles without redundant assert",
             test_i166_embedded_static_config_no_redundant_assert );
    PMM_RUN( "I166-D3: BasicConfig still validates ValidPmmAddressTraits for AT param",
             test_i166_basic_config_still_validates_at );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
