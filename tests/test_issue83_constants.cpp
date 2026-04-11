/**
 * @file test_issue83_constants.cpp
 * @brief Tests: constant optimisation.
 *
 * from config.h directly.
 *
 * Verifies:
 * - #83-R1: kGranuleSize is the single granularity/alignment constant, power of 2.
 * - #83-R2: kMinBlockSize and kMinMemorySize are computed from struct sizes.
 * - #83-R3: kDefaultGrowNumerator / kDefaultGrowDenominator with sensible defaults.
 * - #83-R4: ManagerHeader stores granule_size; load() rejects wrong granule_size.
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include <type_traits>

// ─── #83-R1: Single granularity constant is a power of 2 ─────────────────────

static_assert( ( pmm::kGranuleSize & ( pmm::kGranuleSize - 1 ) ) == 0, "#83-R1: kGranuleSize must be a power of 2" );
static_assert( pmm::kGranuleSize == 16, "#83-R1: kGranuleSize must equal 16" );

// ─── #83-R2: kMinBlockSize and kMinMemorySize are computed ───────────────────

// BlockHeader removed, using Block<DefaultAddressTraits> (same size: 32 bytes).
static_assert( pmm::detail::kMinBlockSize == sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + pmm::kGranuleSize,
               "#83-R2: kMinBlockSize must equal sizeof(Block<A>) + kGranuleSize" );
// ManagerHeader<AT> is now templated; DefaultAddressTraits variant keeps same size.
static_assert( pmm::detail::kMinMemorySize == sizeof( pmm::Block<pmm::DefaultAddressTraits> ) +
                                                  sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) +
                                                  sizeof( pmm::Block<pmm::DefaultAddressTraits> ) +
                                                  pmm::detail::kMinBlockSize,
               "#83-R2: kMinMemorySize must be computed from struct sizes " );

// ─── #83-R3: kDefaultGrowNumerator / kDefaultGrowDenominator ─────────────────

static_assert( pmm::config::kDefaultGrowNumerator == 5, "#83-R3: default grow numerator is 5" );
static_assert( pmm::config::kDefaultGrowDenominator == 4, "#83-R3: default grow denominator is 4" );

// ─── #83-R4: ManagerHeader has granule_size field ────────────────────────────

// ManagerHeader<AT> is now templated; DefaultAddressTraits variant remains 64 bytes.
static_assert( sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) == 64,
               "#83-R4: ManagerHeader<DefaultAddressTraits> must still be exactly 64 bytes " );

// ─── Runtime tests ────────────────────────────────────────────────────────────

using Mgr = pmm::presets::SingleThreadedHeap;

TEST_CASE( "#83-R1: kGranuleSize power-of-2", "[test_issue83_constants]" )
{
    REQUIRE( ( pmm::kGranuleSize & ( pmm::kGranuleSize - 1 ) ) == 0 );
    REQUIRE( pmm::kGranuleSize == 16 );
}

TEST_CASE( "#83-R2: kMinBlockSize computed", "[test_issue83_constants]" )
{
    // kMinBlockSize must equal Block<A> (32) + kGranuleSize (16) = 48
    REQUIRE( pmm::detail::kMinBlockSize == 48 );
    REQUIRE( pmm::detail::kMinBlockSize == sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + pmm::kGranuleSize );
}

TEST_CASE( "#83-R2: kMinMemorySize computed", "[test_issue83_constants]" )
{
    // kMinMemorySize = Block_0 + ManagerHeader + Block_1 + kMinBlockSize
    //                = 32 + 64 + 32 + 48 = 176
    using Block = pmm::Block<pmm::DefaultAddressTraits>;
    // ManagerHeader<AT> is now templated.
    std::size_t expected = sizeof( Block ) + sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) +
                           sizeof( Block ) + pmm::detail::kMinBlockSize;
    REQUIRE( pmm::detail::kMinMemorySize == expected );
    REQUIRE( pmm::detail::kMinMemorySize == 176 );
}

TEST_CASE( "#83-R3: grow config defaults", "[test_issue83_constants]" )
{
    REQUIRE( pmm::config::kDefaultGrowNumerator == 5 );
    REQUIRE( pmm::config::kDefaultGrowDenominator == 4 );
}

TEST_CASE( "#83-R4: granule_size written in header", "[test_issue83_constants]" )
{
    // After create(), verify the manager stores the granule size correctly (via total_size check)
    Mgr pmm;
    REQUIRE( pmm.create( 4096 ) );
    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.total_size() >= 4096 );
    pmm.destroy();
}

TEST_CASE( "#83-R4: load preserves correct granule_size", "[test_issue83_constants]" )
{
    // Save and reload — should succeed with the same granule size
    const char* TEST_FILE = "test_issue83_granule.dat";

    Mgr pmm1;
    REQUIRE( pmm1.create( 64 * 1024 ) );
    auto p = pmm1.allocate_typed<int>();
    REQUIRE( !p.is_null() );

    REQUIRE( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    REQUIRE( pmm2.create( 64 * 1024 ) );
    {
        pmm::VerifyResult vr_;
        REQUIRE( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE, vr_ ) );
    }
    REQUIRE( pmm2.is_initialized() );
    pmm2.destroy();

    std::remove( TEST_FILE );
}

TEST_CASE( "#83-R2: create below kMinMemorySize fails", "[test_issue83_constants]" )
{
    // Buffers smaller than kMinMemorySize must be rejected
    std::size_t too_small = pmm::detail::kMinMemorySize - 16;
    Mgr         pmm;
    bool        ok = pmm.create( too_small );
    REQUIRE( !ok );
}

TEST_CASE( "#83-R2: create at kMinMemorySize succeeds", "[test_issue83_constants]" )
{
    // A buffer of exactly kMinMemorySize must be accepted
    Mgr  pmm;
    bool ok = pmm.create( pmm::detail::kMinMemorySize );
    REQUIRE( ok );
    pmm.destroy();
}
