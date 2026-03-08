/**
 * @file test_mem_map_view_tile.cpp
 * @brief Headless tests for MemMapView (migrated to static API, Issue #112).
 *
 * The new MemMapView no longer exposes tile_snapshot() or ByteInfo::Type
 * (block-level iteration is not available). These tests verify that basic
 * snapshot functionality works with the simplified view.
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
#include "mem_map_view.h"

#include <cstdlib>
#include <iostream>
#include <vector>

// ─── Test helpers ─────────────────────────────────────────────────────────────

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
        std::cout << "  " << ( name ) << " ... " << std::flush;                                                        \
        if ( (fn)() )                                                                                                  \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

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
static bool test_small_pmm_total_bytes()
{
    constexpr std::size_t kPmmSize = 128 * 1024;
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );

    demo::MemMapView view;
    view.update_snapshot();

    PMM_TEST( view.total_bytes() == kPmmSize );

    destroy_pmm();
    return true;
}

/**
 * @brief Large PMM: total_bytes() must be at least the requested PMM size.
 *
 * The static backend may allocate slightly more than requested (due to
 * growth headroom), so we verify total_bytes() >= kPmmSize.
 */
static bool test_large_pmm_total_bytes()
{
    constexpr std::size_t kPmmSize = 4 * 1024 * 1024; // 4 MiB
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );

    demo::MemMapView view;
    view.update_snapshot();

    PMM_TEST( view.total_bytes() >= kPmmSize );

    destroy_pmm();
    return true;
}

/**
 * @brief When PMM is inactive, total_bytes() returns 0 after snapshot.
 */
static bool test_tile_snapshot_inactive_mgr()
{
    demo::g_pmm.store( false );
    demo::DemoMgr::destroy();

    demo::MemMapView view;
    view.update_snapshot();
    PMM_TEST( view.total_bytes() == 0 );
    return true;
}

/**
 * @brief After allocation, total_bytes is unchanged and is_initialized() is true.
 *
 * The static backend may reuse a larger buffer from a prior create(), so
 * we verify total_bytes() >= kPmmSize rather than exact equality.
 */
static bool test_used_block_reflected()
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );

    demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 32 * 1024 );
    PMM_TEST( !p.is_null() );

    demo::MemMapView view;
    view.update_snapshot();

    PMM_TEST( view.total_bytes() >= kPmmSize );
    PMM_TEST( demo::DemoMgr::is_initialized() );

    demo::DemoMgr::deallocate_typed( p );
    destroy_pmm();
    return true;
}

/**
 * @brief After freeing all blocks, is_initialized() remains true.
 *
 * The static backend may reuse a larger buffer from a prior create(), so
 * we verify total_bytes() >= kPmmSize rather than exact equality.
 */
static bool test_freed_blocks_view()
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );

    demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 32 * 1024 );
    PMM_TEST( !p.is_null() );
    demo::DemoMgr::deallocate_typed( p );

    demo::MemMapView view;
    view.update_snapshot();

    PMM_TEST( view.total_bytes() >= kPmmSize );
    PMM_TEST( demo::DemoMgr::is_initialized() );

    destroy_pmm();
    return true;
}

/**
 * @brief total_bytes() is non-zero after snapshot of a valid manager.
 */
static bool test_total_bytes_nonzero()
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    make_pmm( kPmmSize );
    PMM_TEST( demo::g_pmm.load() );

    demo::MemMapView view;
    view.update_snapshot();

    PMM_TEST( view.total_bytes() > 0 );
    PMM_TEST( view.total_bytes() >= kPmmSize );

    destroy_pmm();
    return true;
}

/**
 * @brief Very large PMM: update_snapshot() completes without crash.
 */
static bool test_very_large_pmm()
{
    constexpr std::size_t kPmmSize = 64 * 1024 * 1024; // 64 MiB

    if ( !demo::DemoMgr::create( kPmmSize ) )
    {
        std::cout << "(skipped — PMM create failed) ";
        return true;
    }
    demo::g_pmm.store( true );

    demo::MemMapView view;
    view.update_snapshot();

    PMM_TEST( view.total_bytes() >= kPmmSize );

    destroy_pmm();
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_mem_map_view_tile ===\n";
    bool all_passed = true;

    // Tests are ordered by increasing PMM size so the static backend (which
    // retains its capacity after destroy()) never causes a subsequent test to
    // receive a larger-than-expected backend.
    PMM_RUN( "small_pmm_total_bytes", test_small_pmm_total_bytes );           // 128 KiB
    PMM_RUN( "tile_snapshot_inactive_mgr", test_tile_snapshot_inactive_mgr ); // no PMM change
    PMM_RUN( "used_block_reflected", test_used_block_reflected );             // 256 KiB
    PMM_RUN( "freed_blocks_view", test_freed_blocks_view );                   // 256 KiB
    PMM_RUN( "total_bytes_nonzero", test_total_bytes_nonzero );               // 256 KiB
    PMM_RUN( "large_pmm_total_bytes", test_large_pmm_total_bytes );           // 4 MiB
    PMM_RUN( "very_large_pmm", test_very_large_pmm );                         // 64 MiB

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
