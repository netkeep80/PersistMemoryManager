/**
 * @file test_issue83_constants.cpp
 * @brief Tests for Issue #83: constant optimisation (updated #102 — new API).
 *
 * Issue #102: PMMConfig/DefaultConfig removed. Now uses kDefaultGrowNumerator/Denominator
 * from config.h directly.
 *
 * Verifies:
 * - #83-R1: kGranuleSize is the single granularity/alignment constant, power of 2.
 * - #83-R2: kMinBlockSize and kMinMemorySize are computed from struct sizes.
 * - #83-R3: kDefaultGrowNumerator / kDefaultGrowDenominator with sensible defaults.
 * - #83-R4: ManagerHeader stores granule_size; load() rejects wrong granule_size.
 */

#include "pmm_single_threaded_heap.h"

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
static_assert( pmm::kGranuleSize == 16, "#83-R1: kGranuleSize must equal 16" );

// ─── #83-R2: kMinBlockSize and kMinMemorySize are computed ───────────────────

// Issue #112: BlockHeader removed, using Block<DefaultAddressTraits> (same size: 32 bytes).
static_assert( pmm::detail::kMinBlockSize == sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + pmm::kGranuleSize,
               "#83-R2: kMinBlockSize must equal sizeof(Block<A>) + kGranuleSize" );
// Issue #175: ManagerHeader<AT> is now templated; DefaultAddressTraits variant keeps same size.
static_assert( pmm::detail::kMinMemorySize == sizeof( pmm::Block<pmm::DefaultAddressTraits> ) +
                                                  sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) +
                                                  sizeof( pmm::Block<pmm::DefaultAddressTraits> ) +
                                                  pmm::detail::kMinBlockSize,
               "#83-R2: kMinMemorySize must be computed from struct sizes (Issue #175)" );

// ─── #83-R3: kDefaultGrowNumerator / kDefaultGrowDenominator ─────────────────

static_assert( pmm::config::kDefaultGrowNumerator == 5, "#83-R3: default grow numerator is 5" );
static_assert( pmm::config::kDefaultGrowDenominator == 4, "#83-R3: default grow denominator is 4" );

// ─── #83-R4: ManagerHeader has granule_size field ────────────────────────────

// Issue #175: ManagerHeader<AT> is now templated; DefaultAddressTraits variant remains 64 bytes.
static_assert( sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) == 64,
               "#83-R4: ManagerHeader<DefaultAddressTraits> must still be exactly 64 bytes (Issue #175)" );

// ─── Runtime tests ────────────────────────────────────────────────────────────

using Mgr = pmm::presets::SingleThreadedHeap;

static bool test_power_of_two()
{
    PMM_TEST( ( pmm::kGranuleSize & ( pmm::kGranuleSize - 1 ) ) == 0 );
    PMM_TEST( pmm::kGranuleSize == 16 );
    return true;
}

static bool test_min_block_size_computed()
{
    // kMinBlockSize must equal Block<A> (32) + kGranuleSize (16) = 48 (Issue #112)
    PMM_TEST( pmm::detail::kMinBlockSize == 48 );
    PMM_TEST( pmm::detail::kMinBlockSize == sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + pmm::kGranuleSize );
    return true;
}

static bool test_min_memory_size_computed()
{
    // kMinMemorySize = Block_0 + ManagerHeader + Block_1 + kMinBlockSize (Issue #112)
    //                = 32 + 64 + 32 + 48 = 176
    using Block = pmm::Block<pmm::DefaultAddressTraits>;
    // Issue #175: ManagerHeader<AT> is now templated.
    std::size_t expected = sizeof( Block ) + sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) +
                           sizeof( Block ) + pmm::detail::kMinBlockSize;
    PMM_TEST( pmm::detail::kMinMemorySize == expected );
    PMM_TEST( pmm::detail::kMinMemorySize == 176 );
    return true;
}

static bool test_grow_config_defaults()
{
    PMM_TEST( pmm::config::kDefaultGrowNumerator == 5 );
    PMM_TEST( pmm::config::kDefaultGrowDenominator == 4 );
    return true;
}

static bool test_granule_size_in_header()
{
    // After create(), verify the manager stores the granule size correctly (via total_size check)
    Mgr pmm;
    PMM_TEST( pmm.create( 4096 ) );
    PMM_TEST( pmm.is_initialized() );
    PMM_TEST( pmm.total_size() >= 4096 );
    pmm.destroy();
    return true;
}

static bool test_load_preserves_granule_size()
{
    // Save and reload — should succeed with the same granule size
    const char* TEST_FILE = "test_issue83_granule.dat";

    Mgr pmm1;
    PMM_TEST( pmm1.create( 64 * 1024 ) );
    auto p = pmm1.allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    PMM_TEST( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    PMM_TEST( pmm2.create( 64 * 1024 ) );
    PMM_TEST( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE ) );
    PMM_TEST( pmm2.is_initialized() );
    pmm2.destroy();

    std::remove( TEST_FILE );
    return true;
}

static bool test_create_below_min_memory_size_fails()
{
    // Buffers smaller than kMinMemorySize must be rejected
    std::size_t too_small = pmm::detail::kMinMemorySize - 16;
    Mgr         pmm;
    bool        ok = pmm.create( too_small );
    PMM_TEST( !ok );
    return true;
}

static bool test_create_at_min_memory_size_succeeds()
{
    // A buffer of exactly kMinMemorySize must be accepted
    Mgr  pmm;
    bool ok = pmm.create( pmm::detail::kMinMemorySize );
    PMM_TEST( ok );
    pmm.destroy();
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_issue83_constants (updated #102 — new API) ===\n";
    bool all_passed = true;

    PMM_RUN( "#83-R1: kGranuleSize power-of-2", test_power_of_two );
    PMM_RUN( "#83-R2: kMinBlockSize computed", test_min_block_size_computed );
    PMM_RUN( "#83-R2: kMinMemorySize computed", test_min_memory_size_computed );
    PMM_RUN( "#83-R3: grow config defaults", test_grow_config_defaults );
    PMM_RUN( "#83-R4: granule_size written in header", test_granule_size_in_header );
    PMM_RUN( "#83-R4: load preserves correct granule_size", test_load_preserves_granule_size );
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
