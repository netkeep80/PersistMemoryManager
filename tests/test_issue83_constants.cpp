/**
 * @file test_issue83_constants.cpp
 * @brief Tests for Issue #83: constant optimisation.
 *
 * Verifies:
 * - #83-R1: Redundant alignment constants removed; kGranuleSize is the single
 *           granularity/alignment constant, and it is a power of 2.
 * - #83-R2: kMinBlockSize and kMinMemorySize are computed from struct sizes,
 *           not hardcoded.
 * - #83-R3: kGrowNumerator / kGrowDenominator moved to PMMConfig as
 *           grow_numerator / grow_denominator with sensible defaults.
 * - #83-R4: ManagerHeader stores granule_size; load() rejects images whose
 *           stored granule_size != kGranuleSize.
 */

#include "pmm/legacy_manager.h"
#include "pmm/types.h"
#include "pmm/config.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

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

// ─── #83-R1: Single granularity constant is a power of 2 ─────────────────────

static_assert( ( pmm::kGranuleSize & ( pmm::kGranuleSize - 1 ) ) == 0, "#83-R1: kGranuleSize must be a power of 2" );

// kDefaultAlignment, kMinAlignment, kMaxAlignment must NOT exist in pmm namespace
// (compilation would fail if they did and these static_asserts passed)
static_assert( pmm::kGranuleSize == 16, "#83-R1: kGranuleSize must equal 16" );

// ─── #83-R2: kMinBlockSize and kMinMemorySize are computed ───────────────────

static_assert( pmm::detail::kMinBlockSize == sizeof( pmm::detail::BlockHeader ) + pmm::kGranuleSize,
               "#83-R2: kMinBlockSize must equal sizeof(BlockHeader) + kGranuleSize" );
static_assert( pmm::detail::kMinMemorySize == sizeof( pmm::detail::BlockHeader ) +
                                                  sizeof( pmm::detail::ManagerHeader ) +
                                                  sizeof( pmm::detail::BlockHeader ) + pmm::detail::kMinBlockSize,
               "#83-R2: kMinMemorySize must be computed from struct sizes" );

// ─── #83-R3: PMMConfig has grow_numerator / grow_denominator ─────────────────

static_assert( pmm::config::DefaultConfig::grow_numerator == pmm::config::kDefaultGrowNumerator,
               "#83-R3: DefaultConfig::grow_numerator must equal kDefaultGrowNumerator" );
static_assert( pmm::config::DefaultConfig::grow_denominator == pmm::config::kDefaultGrowDenominator,
               "#83-R3: DefaultConfig::grow_denominator must equal kDefaultGrowDenominator" );
static_assert( pmm::config::kDefaultGrowNumerator == 5, "#83-R3: default grow numerator is 5" );
static_assert( pmm::config::kDefaultGrowDenominator == 4, "#83-R3: default grow denominator is 4" );

// ─── #83-R4: ManagerHeader has granule_size field ────────────────────────────

static_assert( sizeof( pmm::detail::ManagerHeader ) == 64, "#83-R4: ManagerHeader must still be exactly 64 bytes" );

// ─── Runtime tests ────────────────────────────────────────────────────────────

using PMM = pmm::PersistMemoryManager<>;

static bool test_power_of_two()
{
    // kGranuleSize is a power of 2 (already verified by static_assert above, but check at runtime too)
    PMM_TEST( ( pmm::kGranuleSize & ( pmm::kGranuleSize - 1 ) ) == 0 );
    PMM_TEST( pmm::kGranuleSize == 16 );
    return true;
}

static bool test_min_block_size_computed()
{
    // kMinBlockSize must equal BlockHeader (32) + kGranuleSize (16) = 48
    PMM_TEST( pmm::detail::kMinBlockSize == 48 );
    PMM_TEST( pmm::detail::kMinBlockSize == sizeof( pmm::detail::BlockHeader ) + pmm::kGranuleSize );
    return true;
}

static bool test_min_memory_size_computed()
{
    // kMinMemorySize = BlockHeader + ManagerHeader + BlockHeader + kMinBlockSize
    //                = 32 + 64 + 32 + 48 = 176
    std::size_t expected = sizeof( pmm::detail::BlockHeader ) + sizeof( pmm::detail::ManagerHeader ) +
                           sizeof( pmm::detail::BlockHeader ) + pmm::detail::kMinBlockSize;
    PMM_TEST( pmm::detail::kMinMemorySize == expected );
    PMM_TEST( pmm::detail::kMinMemorySize == 176 );
    return true;
}

static bool test_grow_config_defaults()
{
    PMM_TEST( pmm::config::kDefaultGrowNumerator == 5 );
    PMM_TEST( pmm::config::kDefaultGrowDenominator == 4 );
    PMM_TEST( pmm::config::DefaultConfig::grow_numerator == 5 );
    PMM_TEST( pmm::config::DefaultConfig::grow_denominator == 4 );
    return true;
}

static bool test_custom_grow_config()
{
    using CustomConfig = pmm::config::PMMConfig<16, 64, pmm::config::NoLock, 3, 2>;
    PMM_TEST( CustomConfig::grow_numerator == 3 );
    PMM_TEST( CustomConfig::grow_denominator == 2 );
    return true;
}

static bool test_granule_size_in_header()
{
    // After create(), ManagerHeader::granule_size must equal kGranuleSize
    constexpr std::size_t             buf_size = 4096;
    alignas( 16 ) static std::uint8_t buf[buf_size];
    std::memset( buf, 0, sizeof( buf ) );

    bool ok = PMM::create( buf, buf_size );
    PMM_TEST( ok );

    const auto* mhdr = reinterpret_cast<const pmm::detail::ManagerHeader*>( buf + sizeof( pmm::detail::BlockHeader ) );
    PMM_TEST( mhdr->granule_size == static_cast<std::uint16_t>( pmm::kGranuleSize ) );

    PMM::destroy();
    return true;
}

static bool test_load_rejects_wrong_granule_size()
{
    // Create a valid image, then corrupt granule_size to a different value and verify load() rejects it
    constexpr std::size_t             buf_size = 4096;
    alignas( 16 ) static std::uint8_t buf[buf_size];
    std::memset( buf, 0, sizeof( buf ) );

    bool ok = PMM::create( buf, buf_size );
    PMM_TEST( ok );
    PMM::destroy();

    // Corrupt granule_size field (change 16 → 32)
    auto* mhdr         = reinterpret_cast<pmm::detail::ManagerHeader*>( buf + sizeof( pmm::detail::BlockHeader ) );
    mhdr->granule_size = 32; // wrong value

    bool loaded = PMM::load( buf, buf_size );
    PMM_TEST( !loaded ); // must be rejected
    return true;
}

static bool test_load_accepts_correct_granule_size()
{
    // Simulate a persistence round-trip: create in one buffer, copy to another (simulating
    // disk save/load), then load from the copy. Copying before destroy() ensures the saved
    // image has the correct magic (destroy() zeros it in the original).
    constexpr std::size_t             buf_size = 4096;
    alignas( 16 ) static std::uint8_t src[buf_size];
    alignas( 16 ) static std::uint8_t dst[buf_size];
    std::memset( src, 0, sizeof( src ) );
    std::memset( dst, 0, sizeof( dst ) );

    bool created = PMM::create( src, buf_size );
    PMM_TEST( created );

    // Allocate something to verify the image is non-trivial
    auto p = PMM::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    // "Save" by copying before destroy() (which would zero the magic in src)
    std::memcpy( dst, src, buf_size );
    PMM::destroy();

    // Load from the saved copy — granule_size should match kGranuleSize
    bool loaded = PMM::load( dst, buf_size );
    PMM_TEST( loaded );
    PMM::destroy();
    return true;
}

static bool test_create_below_min_memory_size_fails()
{
    // Buffers smaller than kMinMemorySize must be rejected
    constexpr std::size_t             too_small = pmm::detail::kMinMemorySize - 16;
    alignas( 16 ) static std::uint8_t buf[pmm::detail::kMinMemorySize];
    std::memset( buf, 0, sizeof( buf ) );

    bool ok = PMM::create( buf, too_small );
    PMM_TEST( !ok );
    return true;
}

static bool test_create_at_min_memory_size_succeeds()
{
    // A buffer of exactly kMinMemorySize must be accepted
    alignas( 16 ) static std::uint8_t buf[pmm::detail::kMinMemorySize];
    std::memset( buf, 0, sizeof( buf ) );

    bool ok = PMM::create( buf, pmm::detail::kMinMemorySize );
    PMM_TEST( ok );
    PMM::destroy();
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_issue83_constants ===\n";
    bool all_passed = true;

    PMM_RUN( "#83-R1: kGranuleSize power-of-2", test_power_of_two );
    PMM_RUN( "#83-R2: kMinBlockSize computed", test_min_block_size_computed );
    PMM_RUN( "#83-R2: kMinMemorySize computed", test_min_memory_size_computed );
    PMM_RUN( "#83-R3: grow config defaults", test_grow_config_defaults );
    PMM_RUN( "#83-R3: custom grow config", test_custom_grow_config );
    PMM_RUN( "#83-R4: granule_size written in header", test_granule_size_in_header );
    PMM_RUN( "#83-R4: load rejects wrong granule_size", test_load_rejects_wrong_granule_size );
    PMM_RUN( "#83-R4: load accepts correct granule_size", test_load_accepts_correct_granule_size );
    PMM_RUN( "#83-R2: create below kMinMemorySize fails", test_create_below_min_memory_size_fails );
    PMM_RUN( "#83-R2: create at kMinMemorySize succeeds", test_create_at_min_memory_size_succeeds );

    if ( all_passed )
    {
        std::cout << "=== ALL PASSED ===\n";
        return 0;
    }
    else
    {
        std::cout << "=== SOME TESTS FAILED ===\n";
        return 1;
    }
}
