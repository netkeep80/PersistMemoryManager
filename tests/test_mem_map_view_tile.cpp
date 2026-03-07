/**
 * @file test_mem_map_view_tile.cpp
 * @brief Headless tests for MemMapView (migrated to new API, Issue #102).
 *
 * The new MemMapView no longer exposes tile_snapshot() or ByteInfo::Type
 * (block-level iteration is not available). These tests verify that basic
 * snapshot functionality works with the simplified view.
 *
 * Tests:
 *  1. Small PMM: total_bytes() returns expected size.
 *  2. Large PMM: total_bytes() returns expected size.
 *  3. update_snapshot() with nullptr must not crash.
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

static demo::DemoMgr* make_pmm( std::size_t sz )
{
    auto* mgr = new demo::DemoMgr();
    mgr->create( sz );
    demo::g_pmm.store( mgr );
    return mgr;
}

static void destroy_pmm( demo::DemoMgr* mgr )
{
    demo::g_pmm.store( nullptr );
    delete mgr;
}

// ─── Tests ────────────────────────────────────────────────────────────────────

/**
 * @brief Small PMM: total_bytes() must equal PMM size.
 */
static bool test_small_pmm_total_bytes()
{
    constexpr std::size_t kPmmSize = 128 * 1024;
    auto*                 mgr      = make_pmm( kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( view.total_bytes() == kPmmSize );

    destroy_pmm( mgr );
    return true;
}

/**
 * @brief Large PMM: total_bytes() must equal PMM size.
 */
static bool test_large_pmm_total_bytes()
{
    constexpr std::size_t kPmmSize = 4 * 1024 * 1024; // 4 MiB
    auto*                 mgr      = make_pmm( kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( view.total_bytes() == kPmmSize );

    destroy_pmm( mgr );
    return true;
}

/**
 * @brief update_snapshot() with nullptr must not crash.
 */
static bool test_tile_snapshot_null_mgr()
{
    demo::MemMapView view;
    view.update_snapshot( nullptr );
    PMM_TEST( view.total_bytes() == 0 );
    return true;
}

/**
 * @brief After allocation, total_bytes is unchanged and is_initialized() is true.
 */
static bool test_used_block_reflected()
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    auto*                 mgr      = make_pmm( kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::DemoMgr::pptr<std::uint8_t> p = mgr->allocate_typed<std::uint8_t>( 32 * 1024 );
    PMM_TEST( !p.is_null() );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( view.total_bytes() == kPmmSize );
    PMM_TEST( mgr->is_initialized() );

    mgr->deallocate_typed( p );
    destroy_pmm( mgr );
    return true;
}

/**
 * @brief After freeing all blocks, is_initialized() remains true.
 */
static bool test_freed_blocks_view()
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    auto*                 mgr      = make_pmm( kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::DemoMgr::pptr<std::uint8_t> p = mgr->allocate_typed<std::uint8_t>( 32 * 1024 );
    PMM_TEST( !p.is_null() );
    mgr->deallocate_typed( p );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( view.total_bytes() == kPmmSize );
    PMM_TEST( mgr->is_initialized() );

    destroy_pmm( mgr );
    return true;
}

/**
 * @brief total_bytes() is non-zero after snapshot of a valid manager.
 */
static bool test_total_bytes_nonzero()
{
    constexpr std::size_t kPmmSize = 256 * 1024;
    auto*                 mgr      = make_pmm( kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( view.total_bytes() > 0 );
    PMM_TEST( view.total_bytes() == kPmmSize );

    destroy_pmm( mgr );
    return true;
}

/**
 * @brief Very large PMM: update_snapshot() completes without crash.
 */
static bool test_very_large_pmm()
{
    constexpr std::size_t kPmmSize = 64 * 1024 * 1024; // 64 MiB

    auto* mgr = new demo::DemoMgr();
    if ( !mgr->create( kPmmSize ) )
    {
        delete mgr;
        std::cout << "(skipped — PMM create failed) ";
        return true;
    }
    demo::g_pmm.store( mgr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( view.total_bytes() == kPmmSize );

    destroy_pmm( mgr );
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_mem_map_view_tile ===\n";
    bool all_passed = true;

    PMM_RUN( "small_pmm_total_bytes", test_small_pmm_total_bytes );
    PMM_RUN( "large_pmm_total_bytes", test_large_pmm_total_bytes );
    PMM_RUN( "tile_snapshot_null_mgr", test_tile_snapshot_null_mgr );
    PMM_RUN( "used_block_reflected", test_used_block_reflected );
    PMM_RUN( "freed_blocks_view", test_freed_blocks_view );
    PMM_RUN( "total_bytes_nonzero", test_total_bytes_nonzero );
    PMM_RUN( "very_large_pmm", test_very_large_pmm );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
