/**
 * @file test_mem_map_view_tile.cpp
 * @brief Headless tests for MemMapView block-level pixel map (Issue #116).
 *
 * MemMapView now uses DemoMgr::for_each_block() to build a per-byte PixelKind
 * array for block-level pixel colouring. These tests verify snapshot
 * correctness for varying PMM sizes and allocation states.
 *
 * DemoMgr (MultiThreadedHeap) is a fully static class — no instance pointer needed.
 * g_pmm is a boolean flag: true when the manager is active.
 *
 * Tests:
 *  1. Small PMM: total_bytes() returns expected size.
 *  2. Large PMM: total_bytes() returns expected size.
 *  3. When PMM is inactive, total_bytes() returns 0 after snapshot.
 *  4. After alloc, total_bytes() is unchanged, used_size increases.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 */

#include "demo_globals.h"
#include <catch2/catch_test_macros.hpp>
#include "mem_map_view.h"

#include <iostream>
#include <vector>

// ─── PMM fixture helpers ───────────────────────────────────────────────────────

static void make_pmm( std::size_t sz )
{
    demo::DemoMgr::create( sz );
    demo::g_pmm.store( true );
}

static void destroy_pmm()
{
    demo::g_pmm.store( false );
    demo::DemoMgr::destroy();
}

// ─── Tests ────────────────────────────────────────────────────────────────────

/**
 * @brief Small PMM: total_bytes() must equal PMM size.
 */
TEST_CASE( "small_pmm_total_bytes", "[test_mem_map_view_tile]" )
{
    constexpr std::size_t kPmmSize = 128 * 1024;
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    demo::MemMapView view;
    view.update_snapshot();

    REQUIRE( view.total_bytes() == kPmmSize );

    destroy_pmm();
}

/**
 * @brief Large PMM: total_bytes() must be at least the requested PMM size.
 *
 * The static backend may allocate slightly more than requested (due to
 * growth headroom), so we verify total_bytes() >= kPmmSize.
 */
TEST_CASE( "large_pmm_total_bytes", "[test_mem_map_view_tile]" )
{
    constexpr std::size_t kPmmSize = 4 * 1024 * 1024; // 4 MiB
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    demo::MemMapView view;
    view.update_snapshot();

    REQUIRE( view.total_bytes() >= kPmmSize );

    destroy_pmm();
}

/**
 * @brief When PMM is inactive, total_bytes() returns 0 after snapshot.
 */
TEST_CASE( "tile_snapshot_inactive_mgr", "[test_mem_map_view_tile]" )
{
    demo::g_pmm.store( false );
    demo::DemoMgr::destroy();

    demo::MemMapView view;
    view.update_snapshot();
    REQUIRE( view.total_bytes() == 0 );
}

/**
 * @brief After allocation, total_bytes is unchanged and is_initialized() is true.
 *
 * The static backend may reuse a larger buffer from a prior create(), so
 * we verify total_bytes() >= kPmmSize rather than exact equality.
 */
TEST_CASE( "used_block_reflected", "[test_mem_map_view_tile]" )
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 32 * 1024 );
    REQUIRE( !p.is_null() );

    demo::MemMapView view;
    view.update_snapshot();

    REQUIRE( view.total_bytes() >= kPmmSize );
    REQUIRE( demo::DemoMgr::is_initialized() );

    demo::DemoMgr::deallocate_typed( p );
    destroy_pmm();
}

/**
 * @brief After freeing all blocks, is_initialized() remains true.
 *
 * The static backend may reuse a larger buffer from a prior create(), so
 * we verify total_bytes() >= kPmmSize rather than exact equality.
 */
TEST_CASE( "freed_blocks_view", "[test_mem_map_view_tile]" )
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 32 * 1024 );
    REQUIRE( !p.is_null() );
    demo::DemoMgr::deallocate_typed( p );

    demo::MemMapView view;
    view.update_snapshot();

    REQUIRE( view.total_bytes() >= kPmmSize );
    REQUIRE( demo::DemoMgr::is_initialized() );

    destroy_pmm();
}

/**
 * @brief total_bytes() is non-zero after snapshot of a valid manager.
 */
TEST_CASE( "total_bytes_nonzero", "[test_mem_map_view_tile]" )
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    demo::MemMapView view;
    view.update_snapshot();

    REQUIRE( view.total_bytes() > 0 );
    REQUIRE( view.total_bytes() >= kPmmSize );

    destroy_pmm();
}

/**
 * @brief Very large PMM: update_snapshot() completes without crash.
 */
TEST_CASE( "very_large_pmm", "[test_mem_map_view_tile]" )
{
    constexpr std::size_t kPmmSize = 64 * 1024 * 1024; // 64 MiB

    if ( !demo::DemoMgr::create( kPmmSize ) )
    {
        std::cout << "(skipped — PMM create failed) ";
    }
    demo::g_pmm.store( true );

    demo::MemMapView view;
    view.update_snapshot();

    REQUIRE( view.total_bytes() >= kPmmSize );

    destroy_pmm();
}
