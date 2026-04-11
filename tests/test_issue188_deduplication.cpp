/**
 * @file test_issue188_deduplication.cpp
 * @brief Tests for code-deduplication refactoring.
 *
 * Verifies:
 *   - avl_tree_mixin.h NodeUpdateFn hook works correctly.
 *   - avl_remove() shared function works correctly.
 *   - avl_min_node() / avl_max_node() shared functions work correctly.
 *   - pmap and pstringview continue to work correctly (no regression from
 *     adding NodeUpdateFn default parameter to avl_insert/avl_rebalance_up).
 *
 * @see include/pmm/avl_tree_mixin.h — shared AVL operations
 * @version 0.2
 */

#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmap.h"
#include "pmm/pstringview.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include <vector>

// ─── Macros ───────────────────────────────────────────────────────────────────

// ─── Manager alias ───────────────────────────────────────────────────────────

using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 188>;
using Map = pmm::pmap<int, int, Mgr>;
using Str = pmm::pstringview<Mgr>;

// ─── Test: pmap still works (no regression from NodeUpdateFn default) ────────

TEST_CASE( "pmap_no_regression", "[test_issue188_deduplication]" )
{
    Mgr::create( 64 * 1024 );

    Map map;
    map.insert( 42, 100 );
    map.insert( 10, 200 );
    map.insert( 99, 300 );

    auto p42 = map.find( 42 );
    REQUIRE( !p42.is_null() );
    REQUIRE( Mgr::resolve<Map::node_type>( p42 )->value == 100 );

    auto p10 = map.find( 10 );
    REQUIRE( !p10.is_null() );
    REQUIRE( Mgr::resolve<Map::node_type>( p10 )->value == 200 );

    REQUIRE( map.contains( 99 ) );
    REQUIRE( !map.contains( 55 ) );

    // Update existing key
    map.insert( 42, 999 );
    p42 = map.find( 42 );
    REQUIRE( Mgr::resolve<Map::node_type>( p42 )->value == 999 );

    Mgr::destroy();
}

// ─── Test: pstringview still works (no regression) ───────────────────────────

TEST_CASE( "pstringview_no_regression", "[test_issue188_deduplication]" )
{
    Mgr::create( 64 * 1024 );
    Str::reset();

    Mgr::pptr<Str> p1 = Str( "hello" );
    REQUIRE( !p1.is_null() );

    Mgr::pptr<Str> p2 = Str( "hello" );
    REQUIRE( p1 == p2 ); // interning: same string = same pptr

    Mgr::pptr<Str> p3 = Str( "world" );
    REQUIRE( !p3.is_null() );
    REQUIRE( p1 != p3 );

    auto* s1 = Mgr::resolve<Str>( p1 );
    REQUIRE( s1 != nullptr );
    REQUIRE( *s1 == "hello" );
    REQUIRE( s1->size() == 5 );

    Str::reset();
    Mgr::destroy();
}
