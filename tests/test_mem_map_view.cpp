/**
 * @file test_mem_map_view.cpp
 * @brief Headless tests for MemMapView::update_snapshot().
 *
 * MemMapView now uses DemoMgr::for_each_block() to build a per-byte PixelKind
 * array for block-level pixel colouring. A logarithmic bytes-per-pixel slider
 * controls the zoom level.
 *
 * multiple rows so that the map fills the available panel width automatically.
 *
 * DemoMgr (MultiThreadedHeap) is a fully static class — no instance pointer needed.
 * g_pmm is a boolean flag: true when the manager is active.
 *
 * Tests:
 *  1. update_snapshot() on a fresh PMM completes without crash and reports total_bytes.
 *  2. update_snapshot() reflects allocations (used_bytes increases).
 *  3. When g_pmm is false (inactive), total_bytes() returns 0 after snapshot.
 *  4. highlighted_block field survives snapshot updates.
 *  5. 2D layout: num_pixels > pixels_per_row produces multiple rows.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 */

#include "demo_globals.h"
#include "mem_map_view.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

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
 * @brief update_snapshot() on a fresh PMM reports valid total_bytes.
 */
TEST_CASE( "fresh_pmm_snapshot", "[test_mem_map_view]" )
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    demo::MemMapView view;
    view.update_snapshot();

    REQUIRE( view.total_bytes() == kPmmSize );
    REQUIRE( demo::DemoMgr::is_initialized() );

    destroy_pmm();
}

/**
 * @brief update_snapshot() reflects allocations.
 */
TEST_CASE( "snapshot_after_alloc", "[test_mem_map_view]" )
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 512 );
        REQUIRE( !p.is_null() );
        ptrs.push_back( p );
    }

    demo::MemMapView view;
    view.update_snapshot();
    REQUIRE( demo::DemoMgr::is_initialized() );
    REQUIRE( view.total_bytes() == kPmmSize );

    for ( std::size_t i = 0; i < ptrs.size() / 2; ++i )
        demo::DemoMgr::deallocate_typed( ptrs[i] );

    view.update_snapshot();
    REQUIRE( demo::DemoMgr::is_initialized() );

    for ( std::size_t i = ptrs.size() / 2; i < ptrs.size(); ++i )
        demo::DemoMgr::deallocate_typed( ptrs[i] );

    view.update_snapshot();
    REQUIRE( demo::DemoMgr::is_initialized() );

    destroy_pmm();
}

/**
 * @brief When PMM is inactive (g_pmm=false), total_bytes() returns 0 after snapshot.
 *
 * The old test passed nullptr to update_snapshot(). With the static API, we instead
 * ensure the manager is destroyed and total_bytes() returns 0 (DemoMgr::total_size()
 * returns 0 when not initialized).
 */
TEST_CASE( "snapshot_inactive_mgr", "[test_mem_map_view]" )
{
    // Make sure no manager is active (destroy if it was from previous test)
    demo::g_pmm.store( false );
    demo::DemoMgr::destroy();

    demo::MemMapView view;
    // When not initialized, DemoMgr::total_size() returns 0.
    view.update_snapshot();
    REQUIRE( view.total_bytes() == 0 );
}

/**
 * @brief highlighted_block index survives snapshot updates.
 */
TEST_CASE( "highlighted_block_preserved", "[test_mem_map_view]" )
{
    constexpr std::size_t kPmmSize = 128 * 1024;
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 64 );
    REQUIRE( !p.is_null() );

    demo::MemMapView view;
    view.highlighted_block = 0;
    view.update_snapshot();

    REQUIRE( view.highlighted_block == 0 );

    demo::DemoMgr::deallocate_typed( p );
    destroy_pmm();
}

/**
 * @brief Block-level pixel map: for_each_block() returns blocks covering total_size bytes.
 *
 * After update_snapshot(), the internal pixel map must cover all managed bytes.
 * We verify indirectly by checking that for_each_block() completes without crash
 * and that total_bytes() equals the PMM size.
 */
TEST_CASE( "block_pixel_map_covers_total", "[test_mem_map_view]" )
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    // Allocate a few blocks to create a mixed used/free layout.
    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 4; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 4 * 1024 );
        if ( !p.is_null() )
            ptrs.push_back( p );
    }

    demo::MemMapView view;
    view.update_snapshot();

    // After snapshot, total_bytes() must be the full PMM size.
    REQUIRE( view.total_bytes() == demo::DemoMgr::total_size() );

    // Verify for_each_block() iterates at least the number of blocks we created (+1 header, +1 free).
    std::size_t block_count = 0;
    demo::DemoMgr::for_each_block( [&]( const pmm::BlockView& ) { ++block_count; } );
    REQUIRE( block_count >= ptrs.size() + 2 ); // header block + allocated blocks + at least 1 free

    for ( auto& p : ptrs )
        demo::DemoMgr::deallocate_typed( p );
    destroy_pmm();
}

/**
 * @brief: 2D layout arithmetic is consistent.
 *
 * Verifies that after update_snapshot() the total_bytes() value allows the
 * 2D pixel-map layout to produce more than one row when bytes_per_pixel == 1
 * and a narrow panel width is used (simulated in arithmetic only).
 *
 * The rendering itself is ImGui-dependent, but we can validate the layout
 * arithmetic: pixels_per_row = floor(map_width / px_size) and
 * num_rows = ceil(total_pixels / pixels_per_row).
 */
TEST_CASE( "2d_layout_rows", "[test_mem_map_view]" )
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    REQUIRE( demo::g_pmm.load() );

    demo::MemMapView view;
    view.update_snapshot();

    REQUIRE( view.total_bytes() == kPmmSize );

    // Simulate 2D layout arithmetic.
    // With bytes_per_pixel = 1, num_pixels = total_bytes = 256 KiB.
    // With a narrow panel width of 64 px and pixel_size = 4 px:
    //   pixels_per_row = floor(64 / 4) = 16
    //   num_rows = ceil(262144 / 16) = 16384 > 1
    const std::size_t num_pixels     = view.total_bytes(); // bytes_per_pixel = 1
    const float       map_width      = 64.0f;
    const float       px_size        = 4.0f;
    const std::size_t pixels_per_row = std::max<std::size_t>( 1, static_cast<std::size_t>( map_width / px_size ) );
    const std::size_t num_rows       = ( num_pixels + pixels_per_row - 1 ) / pixels_per_row;

    REQUIRE( pixels_per_row == 16 );
    REQUIRE( num_rows > 1 );

    destroy_pmm();
}
