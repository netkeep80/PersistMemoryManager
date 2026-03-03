/**
 * @file test_avl_tree_view.cpp
 * @brief Issue #65 headless tests for AvlTreeView and for_each_free_block_avl().
 *
 * Tests:
 *  1. Empty PMM (no free blocks) → snapshot is empty.
 *  2. After a single allocation the free tree has exactly one node.
 *  3. After multiple allocations the snapshot count equals free_count.
 *  4. AVL structural invariant: every node's offset matches its parent's left/right link.
 *  5. update_snapshot() on null PMM must not crash.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 */

#include "avl_tree_view.h"

#include "persist_memory_manager.h"

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
 * @brief Freshly created PMM (nothing allocated): free tree has one large block.
 */
static bool test_empty_pmm_has_one_free_block()
{
    constexpr std::size_t kPmmSize = 256 * 1024; // 256 KiB

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    auto* mgr = pmm::PersistMemoryManager<>::instance();
    PMM_TEST( mgr != nullptr );

    demo::AvlTreeView view;
    view.update_snapshot( mgr );

    // Freshly created PMM: one large free block covering the whole managed region.
    PMM_TEST( view.snapshot().size() == 1 );

    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
    return true;
}

/**
 * @brief After allocating all free space the snapshot must be empty.
 */
static bool test_fully_allocated_has_empty_snapshot()
{
    constexpr std::size_t kPmmSize = 4096; // Smallest valid PMM

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    auto* mgr = pmm::PersistMemoryManager<>::instance();
    PMM_TEST( mgr != nullptr );

    // Allocate until OOM
    std::vector<pmm::pptr<std::uint8_t>> ptrs;
    while ( true )
    {
        auto p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 16 );
        if ( p.is_null() )
            break;
        ptrs.push_back( p );
    }

    demo::AvlTreeView view;
    view.update_snapshot( mgr );

    // When fully allocated no free blocks remain.
    PMM_TEST( view.snapshot().empty() );

    // Free everything and check the snapshot is non-empty again.
    for ( auto& p : ptrs )
        pmm::PersistMemoryManager<>::deallocate_typed( p );

    view.update_snapshot( mgr );
    PMM_TEST( !view.snapshot().empty() );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
    return true;
}

/**
 * @brief snapshot().size() must equal free_count reported by ManagerInfo.
 */
static bool test_snapshot_count_matches_free_count()
{
    constexpr std::size_t kPmmSize = 256 * 1024;

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    auto* mgr = pmm::PersistMemoryManager<>::instance();
    PMM_TEST( mgr != nullptr );

    // Allocate and free in a pattern that creates fragmentation.
    std::vector<pmm::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 20; ++i )
    {
        auto p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
        PMM_TEST( !p.is_null() );
        ptrs.push_back( p );
    }
    // Free every other block to create fragmentation.
    for ( int i = 0; i < 20; i += 2 )
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[static_cast<std::size_t>( i )] );

    demo::AvlTreeView view;
    view.update_snapshot( mgr );

    pmm::ManagerInfo info = pmm::get_manager_info();
    PMM_TEST( view.snapshot().size() == info.free_count );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
    return true;
}

/**
 * @brief Each snapshot node's offset must appear as left or right child of its parent.
 *
 * This verifies that the AVL structural links in FreeBlockView are consistent.
 */
static bool test_avl_parent_child_links_consistent()
{
    constexpr std::size_t kPmmSize = 256 * 1024;

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    auto* mgr = pmm::PersistMemoryManager<>::instance();
    PMM_TEST( mgr != nullptr );

    // Build a fragmented free tree with several nodes.
    std::vector<pmm::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 30; ++i )
    {
        std::size_t sz = static_cast<std::size_t>( 64 + i * 32 ); // varying sizes
        auto        p  = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( sz );
        if ( p.is_null() )
            break;
        ptrs.push_back( p );
    }
    // Free every other block so several separate free blocks exist.
    for ( std::size_t i = 0; i < ptrs.size(); i += 2 )
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );

    demo::AvlTreeView view;
    view.update_snapshot( mgr );

    const auto& snap = view.snapshot();

    // Build an offset→node map for O(1) lookup.
    std::vector<std::ptrdiff_t> all_offsets;
    all_offsets.reserve( snap.size() );
    for ( const auto& n : snap )
        all_offsets.push_back( n.offset );

    // For every non-root node, its offset must appear as a child of its parent.
    for ( const auto& node : snap )
    {
        if ( node.parent_offset < 0 )
            continue; // root — no parent to check

        // Find the parent node in snapshot.
        const demo::AvlNodeSnapshot* parent_ns = nullptr;
        for ( const auto& n : snap )
        {
            if ( n.offset == node.parent_offset )
            {
                parent_ns = &n;
                break;
            }
        }
        PMM_TEST( parent_ns != nullptr ); // parent must be in snapshot

        // Node must be the left or right child of its parent.
        bool is_left  = ( parent_ns->left_offset == node.offset );
        bool is_right = ( parent_ns->right_offset == node.offset );
        PMM_TEST( is_left || is_right );
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
    return true;
}

/**
 * @brief update_snapshot() with null mgr must not crash and must leave snapshot empty.
 */
static bool test_null_mgr_no_crash()
{
    demo::AvlTreeView view;
    view.update_snapshot( nullptr ); // must not crash
    PMM_TEST( view.snapshot().empty() );
    return true;
}

/**
 * @brief for_each_free_block_avl() must iterate all free blocks.
 */
static bool test_for_each_free_block_avl_count()
{
    constexpr std::size_t kPmmSize = 256 * 1024;

    void* buf = std::malloc( kPmmSize );
    PMM_TEST( buf != nullptr );
    std::memset( buf, 0, kPmmSize );
    PMM_TEST( pmm::PersistMemoryManager<>::create( buf, kPmmSize ) );

    // Allocate and free to create several free blocks.
    std::vector<pmm::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        auto p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
        PMM_TEST( !p.is_null() );
        ptrs.push_back( p );
    }
    for ( int i = 0; i < 10; i += 2 )
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[static_cast<std::size_t>( i )] );

    std::size_t avl_count = 0;
    pmm::for_each_free_block_avl( [&]( const pmm::FreeBlockView& ) { ++avl_count; } );

    pmm::ManagerInfo info = pmm::get_manager_info();
    PMM_TEST( avl_count == info.free_count );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    pmm::PersistMemoryManager<>::destroy();
    std::free( buf );
    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_avl_tree_view ===\n";
    bool all_passed = true;

    PMM_RUN( "empty_pmm_has_one_free_block", test_empty_pmm_has_one_free_block );
    PMM_RUN( "fully_allocated_has_empty_snapshot", test_fully_allocated_has_empty_snapshot );
    PMM_RUN( "snapshot_count_matches_free_count", test_snapshot_count_matches_free_count );
    PMM_RUN( "avl_parent_child_links_consistent", test_avl_parent_child_links_consistent );
    PMM_RUN( "null_mgr_no_crash", test_null_mgr_no_crash );
    PMM_RUN( "for_each_free_block_avl_count", test_for_each_free_block_avl_count );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
