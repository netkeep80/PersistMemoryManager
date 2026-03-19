/**
 * @file test_issue188_deduplication.cpp
 * @brief Tests for code-deduplication refactoring in Issue #188.
 *
 * Verifies:
 *   - avl_tree_mixin.h NodeUpdateFn hook works correctly for pvector weight updates.
 *   - avl_remove() shared function works correctly (used by pvector).
 *   - avl_min_node() / avl_max_node() shared functions work correctly.
 *   - pvector operations (push_back, pop_back, at, size, front, back, clear)
 *     continue to work correctly after deduplication refactoring.
 *   - pmap and pstringview continue to work correctly (no regression from
 *     adding NodeUpdateFn default parameter to avl_insert/avl_rebalance_up).
 *   - Order-statistic tree invariants hold after all operations.
 *
 * @see include/pmm/avl_tree_mixin.h — shared AVL operations (Issue #188)
 * @see include/pmm/pvector.h — pvector deduplication (Issue #188)
 * @version 0.1 (Issue #188 — initial deduplication tests)
 */

#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmap.h"
#include "pmm/pstringview.h"
#include "pmm/pvector.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include <vector>

// ─── Macros ───────────────────────────────────────────────────────────────────

// ─── Manager alias ───────────────────────────────────────────────────────────

using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 188>;
using Vec = pmm::pvector<int, Mgr>;
using Map = pmm::pmap<int, int, Mgr>;
using Str = pmm::pstringview<Mgr>;

// ─── Test: pvector push_back, at, size after dedup ───────────────────────────

TEST_CASE( "pvector_basic_ops", "[test_issue188_deduplication]" )
{
    Mgr::create( 64 * 1024 );

    Vec vec;
    REQUIRE( vec.empty() );
    REQUIRE( vec.size() == 0 );

    // Push back 10 elements
    for ( int i = 0; i < 10; ++i )
    {
        auto p = vec.push_back( i * 10 );
        REQUIRE( !p.is_null() );
    }
    REQUIRE( vec.size() == 10 );

    // Verify at() returns correct values
    for ( int i = 0; i < 10; ++i )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        REQUIRE( !p.is_null() );
        auto* obj = Mgr::resolve<Vec::node_type>( p );
        REQUIRE( obj != nullptr );
        REQUIRE( obj->value == i * 10 );
    }

    // Out of bounds
    REQUIRE( vec.at( 10 ).is_null() );
    REQUIRE( vec.at( 100 ).is_null() );

    Mgr::destroy();
}

// ─── Test: pvector front/back ────────────────────────────────────────────────

TEST_CASE( "pvector_front_back", "[test_issue188_deduplication]" )
{
    Mgr::create( 64 * 1024 );

    Vec vec;
    vec.push_back( 100 );
    vec.push_back( 200 );
    vec.push_back( 300 );

    auto f = vec.front();
    REQUIRE( !f.is_null() );
    REQUIRE( Mgr::resolve<Vec::node_type>( f )->value == 100 );

    auto b = vec.back();
    REQUIRE( !b.is_null() );
    REQUIRE( Mgr::resolve<Vec::node_type>( b )->value == 300 );

    Mgr::destroy();
}

// ─── Test: pvector pop_back ──────────────────────────────────────────────────

TEST_CASE( "pvector_pop_back", "[test_issue188_deduplication]" )
{
    Mgr::create( 64 * 1024 );

    Vec vec;
    for ( int i = 0; i < 5; ++i )
        vec.push_back( i );

    REQUIRE( vec.size() == 5 );

    REQUIRE( vec.pop_back() );
    REQUIRE( vec.size() == 4 );

    // Verify remaining elements
    for ( int i = 0; i < 4; ++i )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        REQUIRE( !p.is_null() );
        REQUIRE( Mgr::resolve<Vec::node_type>( p )->value == i );
    }

    // Pop all
    while ( !vec.empty() )
        vec.pop_back();
    REQUIRE( vec.empty() );
    REQUIRE( vec.size() == 0 );

    Mgr::destroy();
}

// ─── Test: pvector clear ─────────────────────────────────────────────────────

TEST_CASE( "pvector_clear", "[test_issue188_deduplication]" )
{
    Mgr::create( 64 * 1024 );

    Vec vec;
    for ( int i = 0; i < 20; ++i )
        vec.push_back( i );
    REQUIRE( vec.size() == 20 );

    vec.clear();
    REQUIRE( vec.empty() );
    REQUIRE( vec.size() == 0 );

    // Re-push after clear
    for ( int i = 0; i < 5; ++i )
        vec.push_back( i * 100 );
    REQUIRE( vec.size() == 5 );
    REQUIRE( Mgr::resolve<Vec::node_type>( vec.at( 2 ) )->value == 200 );

    Mgr::destroy();
}

// ─── Test: pvector iterator ──────────────────────────────────────────────────

TEST_CASE( "pvector_iterator", "[test_issue188_deduplication]" )
{
    Mgr::create( 64 * 1024 );

    Vec vec;
    for ( int i = 0; i < 7; ++i )
        vec.push_back( i );

    std::vector<int> collected;
    for ( auto it = vec.begin(); it != vec.end(); ++it )
    {
        auto p = *it;
        if ( !p.is_null() )
        {
            auto* obj = Mgr::resolve<Vec::node_type>( p );
            if ( obj )
                collected.push_back( obj->value );
        }
    }

    REQUIRE( collected.size() == 7 );
    for ( int i = 0; i < 7; ++i )
        REQUIRE( collected[static_cast<std::size_t>( i )] == i );

    Mgr::destroy();
}

// ─── Test: pvector large (triggers multiple rotations/rebalances) ────────────

TEST_CASE( "pvector_large", "[test_issue188_deduplication]" )
{
    Mgr::create( 256 * 1024 );

    Vec vec;
    // Insert enough elements to trigger multiple levels of AVL rebalancing
    for ( int i = 0; i < 100; ++i )
        vec.push_back( i );

    REQUIRE( vec.size() == 100 );

    // Verify all elements are accessible
    for ( int i = 0; i < 100; ++i )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        REQUIRE( !p.is_null() );
        REQUIRE( Mgr::resolve<Vec::node_type>( p )->value == i );
    }

    // Pop half
    for ( int i = 0; i < 50; ++i )
        REQUIRE( vec.pop_back() );

    REQUIRE( vec.size() == 50 );

    // Verify remaining
    for ( int i = 0; i < 50; ++i )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        REQUIRE( !p.is_null() );
        REQUIRE( Mgr::resolve<Vec::node_type>( p )->value == i );
    }

    Mgr::destroy();
}

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

// ─── Test: combined pvector + pmap (no interaction issues) ───────────────────

TEST_CASE( "combined_pvector_pmap", "[test_issue188_deduplication]" )
{
    Mgr::create( 128 * 1024 );
    Str::reset();

    // Use pvector and pmap simultaneously
    Vec vec;
    Map map;

    for ( int i = 0; i < 20; ++i )
    {
        vec.push_back( i * 10 );
        map.insert( i, i * 100 );
    }

    REQUIRE( vec.size() == 20 );
    REQUIRE( map.contains( 5 ) );
    REQUIRE( Mgr::resolve<Vec::node_type>( vec.at( 5 ) )->value == 50 );
    REQUIRE( Mgr::resolve<Map::node_type>( map.find( 5 ) )->value == 500 );

    // Pop from vector, check map unchanged
    for ( int i = 0; i < 10; ++i )
        vec.pop_back();

    REQUIRE( vec.size() == 10 );
    REQUIRE( map.contains( 15 ) ); // map unaffected

    Str::reset();
    Mgr::destroy();
}
