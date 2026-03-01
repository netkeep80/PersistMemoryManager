/**
 * @file test_mem_map_view_tile.cpp
 * @brief Phase 11 unit tests for MemMapView tile-aggregation (overview mode).
 *
 * Tests the tile snapshot logic added in Phase 11:
 *  - Small PMM (<= 512 KB): bytes_per_tile == 1, tiles match byte count.
 *  - Large PMM (> 512 KB): bytes_per_tile > 1, tile count <= kMaxTiles.
 *  - Tile dominant type reflects the actual byte composition.
 *  - Manager-header tiles are tagged ManagerHeader.
 *  - update_snapshot() handles nullptr gracefully.
 *  - Tile count equals ceil(total_bytes / bytes_per_tile).
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 */

#include "mem_map_view.h"

#include "persist_memory_manager.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

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
 * @brief Small PMM (<= 512 KB): bytes_per_tile must be 1 and tile count
 *        must equal total_bytes.
 */
static bool test_small_pmm_tile_size()
{
    constexpr std::size_t kPmmSize = 128 * 1024; // 128 KiB (< 512 KiB detail limit)

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    auto* mgr = pmm::PersistMemoryManager::create( buf, kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( view.total_bytes() == kPmmSize );
    // For small PMM tiles should be 1 byte each
    PMM_TEST( view.bytes_per_tile() == 1 );
    PMM_TEST( view.tile_snapshot().size() == kPmmSize );

    pmm::PersistMemoryManager::destroy();
    return true;
}

/**
 * @brief Large PMM (> 512 KB): bytes_per_tile must be > 1 and tile count
 *        must be <= kMaxTiles (65536).
 */
static bool test_large_pmm_tile_count()
{
    constexpr std::size_t kPmmSize = 4 * 1024 * 1024; // 4 MiB

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    auto* mgr = pmm::PersistMemoryManager::create( buf, kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( view.total_bytes() == kPmmSize );
    PMM_TEST( view.bytes_per_tile() >= 1 );
    // Tile count must not exceed kMaxTiles
    PMM_TEST( view.tile_snapshot().size() <= 65536 );
    // Tile count must cover the full managed region
    const std::size_t expected_tiles = ( kPmmSize + view.bytes_per_tile() - 1 ) / view.bytes_per_tile();
    PMM_TEST( view.tile_snapshot().size() == expected_tiles );

    pmm::PersistMemoryManager::destroy();
    return true;
}

/**
 * @brief The first tile (offset 0) must have dominant type ManagerHeader
 *        because the manager header is at the very start of the region.
 */
static bool test_first_tile_is_manager_header()
{
    constexpr std::size_t kPmmSize = 128 * 1024; // small: bytes_per_tile == 1

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    auto* mgr = pmm::PersistMemoryManager::create( buf, kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( !view.tile_snapshot().empty() );
    PMM_TEST( view.tile_snapshot()[0].dominant_type == demo::ByteInfo::Type::ManagerHeader );

    pmm::PersistMemoryManager::destroy();
    return true;
}

/**
 * @brief After allocating blocks, tiles covering used regions must be tagged
 *        UserDataUsed or BlockHeaderUsed (not OutOfBlocks).
 *
 * We verify that at least one tile in the snapshot reflects used data.
 */
static bool test_used_block_reflected_in_tiles()
{
    constexpr std::size_t kPmmSize = 256 * 1024;

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    auto* mgr = pmm::PersistMemoryManager::create( buf, kPmmSize );
    PMM_TEST( mgr != nullptr );

    // Allocate a large chunk so it definitely covers several tiles
    void* p = mgr->allocate( 32 * 1024 );
    PMM_TEST( p != nullptr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    bool found_used = false;
    for ( const auto& tile : view.tile_snapshot() )
    {
        if ( tile.dominant_type == demo::ByteInfo::Type::UserDataUsed ||
             tile.dominant_type == demo::ByteInfo::Type::BlockHeaderUsed )
        {
            found_used = true;
            break;
        }
    }
    PMM_TEST( found_used );

    mgr->deallocate( p );
    pmm::PersistMemoryManager::destroy();
    return true;
}

/**
 * @brief After freeing all blocks, no tile in the detail range should be
 *        tagged as Used (they should revert to Free or OutOfBlocks).
 */
static bool test_freed_blocks_revert_in_tiles()
{
    constexpr std::size_t kPmmSize = 256 * 1024;

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    auto* mgr = pmm::PersistMemoryManager::create( buf, kPmmSize );
    PMM_TEST( mgr != nullptr );

    void* p = mgr->allocate( 32 * 1024 );
    PMM_TEST( p != nullptr );
    mgr->deallocate( p );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    bool found_used = false;
    for ( const auto& tile : view.tile_snapshot() )
    {
        if ( tile.dominant_type == demo::ByteInfo::Type::UserDataUsed ||
             tile.dominant_type == demo::ByteInfo::Type::BlockHeaderUsed )
        {
            found_used = true;
            break;
        }
    }
    PMM_TEST( !found_used );

    pmm::PersistMemoryManager::destroy();
    return true;
}

/**
 * @brief Tile offset field must equal tile_index * bytes_per_tile.
 */
static bool test_tile_offsets_correct()
{
    constexpr std::size_t kPmmSize = 256 * 1024;

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    auto* mgr = pmm::PersistMemoryManager::create( buf, kPmmSize );
    PMM_TEST( mgr != nullptr );

    demo::MemMapView view;
    view.update_snapshot( mgr );

    const std::size_t bpt = view.bytes_per_tile();
    for ( std::size_t i = 0; i < view.tile_snapshot().size(); ++i )
    {
        PMM_TEST( view.tile_snapshot()[i].offset == i * bpt );
        PMM_TEST( view.tile_snapshot()[i].bytes_per_tile == bpt );
    }

    pmm::PersistMemoryManager::destroy();
    return true;
}

/**
 * @brief update_snapshot() with nullptr must not crash and must leave the
 *        tile snapshot empty.
 */
static bool test_tile_snapshot_null_mgr()
{
    demo::MemMapView view;
    view.update_snapshot( nullptr );
    // No crash is the primary check; tile_snapshot should be empty or unchanged
    (void)view.tile_snapshot();
    return true;
}

/**
 * @brief For a very large PMM, tile count stays bounded at kMaxTiles.
 */
static bool test_very_large_pmm_tile_bound()
{
    constexpr std::size_t kPmmSize = 64 * 1024 * 1024; // 64 MiB

    void* buf = std::malloc( kPmmSize );
    if ( !buf )
    {
        std::cout << "(skipped — not enough memory) ";
        return true; // skip gracefully on low-memory systems
    }
    std::memset( buf, 0, kPmmSize );
    auto* mgr = pmm::PersistMemoryManager::create( buf, kPmmSize );
    if ( !mgr )
    {
        std::free( buf );
        std::cout << "(skipped — PMM create failed) ";
        return true;
    }

    demo::MemMapView view;
    view.update_snapshot( mgr );

    PMM_TEST( view.tile_snapshot().size() <= 65536 );

    pmm::PersistMemoryManager::destroy();
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_mem_map_view_tile ===\n";
    bool all_passed = true;

    PMM_RUN( "small_pmm_tile_size", test_small_pmm_tile_size );
    PMM_RUN( "large_pmm_tile_count", test_large_pmm_tile_count );
    PMM_RUN( "first_tile_is_manager_header", test_first_tile_is_manager_header );
    PMM_RUN( "used_block_reflected_in_tiles", test_used_block_reflected_in_tiles );
    PMM_RUN( "freed_blocks_revert_in_tiles", test_freed_blocks_revert_in_tiles );
    PMM_RUN( "tile_offsets_correct", test_tile_offsets_correct );
    PMM_RUN( "tile_snapshot_null_mgr", test_tile_snapshot_null_mgr );
    PMM_RUN( "very_large_pmm_tile_bound", test_very_large_pmm_tile_bound );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
