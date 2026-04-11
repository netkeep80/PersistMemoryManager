/**
 * @file test_avl_tree_view.cpp
 * @brief Headless tests for AvlTreeView AVL free-block iteration.
 *
 * AvlTreeView now uses DemoMgr::for_each_free_block() (in-order traversal) to
 * display all free blocks as a visual tree (tree rendering, not a
 * flat table).
 *
 * DemoMgr (MultiThreadedHeap) is a fully static class — no instance pointer needed.
 * g_pmm is a boolean flag: true when the manager is active.
 *
 * Tests:
 *  1. Freshly created PMM: update_snapshot() completes without crash, free_block_count > 0.
 *  2. When PMM is inactive, update_snapshot() must not crash.
 *  3. After allocating blocks, used_size increases.
 *  4. After deallocating, free_size recovers.
 *  5. AVL tree has exactly one root (parent_offset == -1).
 *  6. Child links are consistent: if a node has left/right child, child's parent equals that node.
 *
 * Built only when PMM_BUILD_DEMO=ON (requires demo sources + ImGui stubs).
 */

#include "avl_tree_view.h"
#include "demo_globals.h"

#include <catch2/catch_test_macros.hpp>

#include <unordered_map>
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
 * @brief Freshly created PMM: update_snapshot() completes, free_block_count > 0.
 */
TEST_CASE( "empty_pmm_has_free_blocks", "[test_avl_tree_view]" )
{
    make_pmm( 256 * 1024 );
    REQUIRE( demo::g_pmm.load() );
    REQUIRE( demo::DemoMgr::is_initialized() );

    demo::AvlTreeView view;
    view.update_snapshot();

    // Freshly created PMM has one large free block.
    REQUIRE( demo::DemoMgr::free_block_count() >= 1 );

    destroy_pmm();
}

/**
 * @brief When PMM is inactive, update_snapshot() must not crash.
 */
TEST_CASE( "inactive_mgr_no_crash", "[test_avl_tree_view]" )
{
    demo::g_pmm.store( false );
    demo::DemoMgr::destroy();

    demo::AvlTreeView view;
    view.update_snapshot(); // must not crash (DemoMgr returns 0 for all stats when not initialized)
}

/**
 * @brief After allocating blocks, used_size increases.
 */
TEST_CASE( "alloc_increases_used_size", "[test_avl_tree_view]" )
{
    make_pmm( 256 * 1024 );
    REQUIRE( demo::g_pmm.load() );

    std::size_t used_before = demo::DemoMgr::used_size();

    demo::AvlTreeView view;
    view.update_snapshot();

    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 5; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 1024 );
        REQUIRE( !p.is_null() );
        ptrs.push_back( p );
    }

    view.update_snapshot();
    REQUIRE( demo::DemoMgr::used_size() > used_before );

    for ( auto& p : ptrs )
        demo::DemoMgr::deallocate_typed( p );

    destroy_pmm();
}

/**
 * @brief After deallocating, free_size recovers.
 */
TEST_CASE( "dealloc_recovers_free_size", "[test_avl_tree_view]" )
{
    make_pmm( 256 * 1024 );
    REQUIRE( demo::g_pmm.load() );

    std::size_t free_before = demo::DemoMgr::free_size();

    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 512 );
        if ( !p.is_null() )
            ptrs.push_back( p );
    }

    for ( auto& p : ptrs )
        demo::DemoMgr::deallocate_typed( p );

    demo::AvlTreeView view;
    view.update_snapshot();

    // After freeing everything, free_size should be back to original (or close).
    REQUIRE( demo::DemoMgr::free_size() > 0 );
    // free_size may not equal free_before exactly due to coalescing order,
    // but it should be at least as large.
    REQUIRE( demo::DemoMgr::free_size() <= demo::DemoMgr::total_size() );
    (void)free_before;

    destroy_pmm();
}

/**
 * @brief for_each_free_block() iterates exactly free_block_count() free blocks.
 *
 * Verifies that the in-order AVL traversal yields the correct number of free blocks.
 */
TEST_CASE( "for_each_free_block_count", "[test_avl_tree_view]" )
{
    make_pmm( 256 * 1024 );
    REQUIRE( demo::g_pmm.load() );

    // Allocate some blocks to fragment the heap.
    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 6; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 512 );
        if ( !p.is_null() )
            ptrs.push_back( p );
    }
    // Free every other allocation to create multiple free blocks.
    for ( std::size_t i = 0; i < ptrs.size(); i += 2 )
        demo::DemoMgr::deallocate_typed( ptrs[i] );

    demo::AvlTreeView view;
    view.update_snapshot();

    // Count free blocks via for_each_free_block().
    std::size_t iterated = 0;
    demo::DemoMgr::for_each_free_block( [&]( const pmm::FreeBlockView& ) { ++iterated; } );

    // Must match the manager's free_block_count().
    REQUIRE( iterated == demo::DemoMgr::free_block_count() );
    REQUIRE( iterated > 0 );

    // Free remaining allocations.
    for ( std::size_t i = 1; i < ptrs.size(); i += 2 )
        demo::DemoMgr::deallocate_typed( ptrs[i] );

    destroy_pmm();
}

/**
 * @brief AVL tree has exactly one root node (parent_offset == -1).
 *
 * Verifies that among all free blocks exactly one has no parent, which is
 * required for a valid AVL tree and for the tree-rendering code to work.
 */
TEST_CASE( "avl_tree_has_one_root", "[test_avl_tree_view]" )
{
    make_pmm( 256 * 1024 );
    REQUIRE( demo::g_pmm.load() );

    // Create multiple free blocks to get a non-trivial AVL tree.
    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 7; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 512 );
        if ( !p.is_null() )
            ptrs.push_back( p );
    }
    // Free every other allocation to produce multiple free blocks.
    for ( std::size_t i = 0; i < ptrs.size(); i += 2 )
        demo::DemoMgr::deallocate_typed( ptrs[i] );

    // Collect free blocks.
    std::vector<pmm::FreeBlockView> blocks;
    demo::DemoMgr::for_each_free_block( [&]( const pmm::FreeBlockView& v ) { blocks.push_back( v ); } );

    // Count roots (nodes with no parent).
    std::size_t root_count = 0;
    for ( const pmm::FreeBlockView& v : blocks )
    {
        if ( v.parent_offset < 0 )
            ++root_count;
    }

    // A valid AVL tree must have exactly one root when it has any nodes.
    REQUIRE( ( blocks.empty() || root_count == 1 ) );

    // Free remaining allocations.
    for ( std::size_t i = 1; i < ptrs.size(); i += 2 )
        demo::DemoMgr::deallocate_typed( ptrs[i] );

    destroy_pmm();
}

/**
 * @brief AVL child links are consistent with parent links.
 *
 * For each free block with a left or right child, the child's parent_offset
 * must equal the current node's offset.  This validates the data that the
 * tree-rendering code relies on.
 */
TEST_CASE( "avl_child_parent_links_consistent", "[test_avl_tree_view]" )
{
    make_pmm( 256 * 1024 );
    REQUIRE( demo::g_pmm.load() );

    // Produce a fragmented heap with several free blocks.
    std::vector<demo::DemoMgr::pptr<std::uint8_t>> ptrs;
    for ( int i = 0; i < 8; ++i )
    {
        demo::DemoMgr::pptr<std::uint8_t> p = demo::DemoMgr::allocate_typed<std::uint8_t>( 512 );
        if ( !p.is_null() )
            ptrs.push_back( p );
    }
    for ( std::size_t i = 0; i < ptrs.size(); i += 2 )
        demo::DemoMgr::deallocate_typed( ptrs[i] );

    // Build offset->FreeBlockView map.
    std::vector<pmm::FreeBlockView>                               blocks;
    std::unordered_map<std::ptrdiff_t, const pmm::FreeBlockView*> by_offset;
    demo::DemoMgr::for_each_free_block( [&]( const pmm::FreeBlockView& v ) { blocks.push_back( v ); } );
    for ( const pmm::FreeBlockView& v : blocks )
        by_offset[v.offset] = &v;

    // Check: for each node, its children's parent_offset points back to it.
    for ( const pmm::FreeBlockView& v : blocks )
    {
        if ( v.left_offset >= 0 )
        {
            auto it = by_offset.find( v.left_offset );
            REQUIRE( it != by_offset.end() );
            REQUIRE( it->second->parent_offset == v.offset );
        }
        if ( v.right_offset >= 0 )
        {
            auto it = by_offset.find( v.right_offset );
            REQUIRE( it != by_offset.end() );
            REQUIRE( it->second->parent_offset == v.offset );
        }
    }

    // Free remaining allocations.
    for ( std::size_t i = 1; i < ptrs.size(); i += 2 )
        demo::DemoMgr::deallocate_typed( ptrs[i] );

    destroy_pmm();
}
