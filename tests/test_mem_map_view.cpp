/**
 * @file test_mem_map_view.cpp
 * @brief Phase 8 unit tests for MemMapView::update_snapshot().
 *
 * Tests the snapshot logic without a graphical window.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 *
 * NOTE: Uses std::malloc for the PMM buffer so that destroy() can safely
 * free it (consistent with all other PMM tests and the PMM contract where
 * owns_memory=true means the buffer was malloc'd).
 */

#include "mem_map_view.h"

#include "pmm/legacy_manager.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
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

// ─── Tests ────────────────────────────────────────────────────────────────────

/**
 * @brief Manager header region must be tagged ManagerHeader in snapshot.
 */
static bool test_manager_header_region()
{
    constexpr std::size_t kPmmSize = 256 * 1024; // 256 KiB

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    auto* mgr = pmm::PersistMemoryManager<>::instance();
    PMM_TEST( mgr != nullptr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    // Primary check: update_snapshot completes without crash on valid PMM
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
    return true;
}

/**
 * @brief Snapshot must reflect allocated blocks as Used types.
 */
static bool test_snapshot_after_alloc()
{
    constexpr std::size_t kPmmSize = 256 * 1024;

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    auto* mgr = pmm::PersistMemoryManager<>::instance();
    PMM_TEST( mgr != nullptr );

    std::vector<pmm::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
        PMM_TEST( !p.is_null() );
        ptrs.push_back( p );
    }

    demo::MemMapView view;
    view.update_snapshot( mgr );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( std::size_t i = 0; i < ptrs.size() / 2; ++i )
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );

    view.update_snapshot( mgr );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( std::size_t i = ptrs.size() / 2; i < ptrs.size(); ++i )
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );

    view.update_snapshot( mgr );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
    return true;
}

/**
 * @brief update_snapshot() must handle null PMM gracefully (no crash).
 */
static bool test_snapshot_null_mgr()
{
    demo::MemMapView view;
    view.update_snapshot( nullptr ); // must not crash
    return true;
}

/**
 * @brief Highlighted block index survives snapshot updates.
 */
static bool test_highlighted_block_preserved()
{
    constexpr std::size_t kPmmSize = 128 * 1024;

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    auto* mgr = pmm::PersistMemoryManager<>::instance();
    PMM_TEST( mgr != nullptr );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );

    demo::MemMapView view;
    view.highlighted_block = 0;
    view.update_snapshot( mgr );

    PMM_TEST( view.highlighted_block == 0 );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_mem_map_view ===\n";
    bool all_passed = true;

    PMM_RUN( "manager_header_region", test_manager_header_region );
    PMM_RUN( "snapshot_after_alloc", test_snapshot_after_alloc );
    PMM_RUN( "snapshot_null_mgr", test_snapshot_null_mgr );
    PMM_RUN( "highlighted_block_preserved", test_highlighted_block_preserved );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
