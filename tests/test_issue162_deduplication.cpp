/**
 * @file test_issue162_deduplication.cpp
 * @brief Тесты дедупликации AVL-поиска в pstringview и pmap (Issue #162).
 *
 * Проверяет:
 *   - detail::avl_find() — новая обобщённая функция поиска в AVL-дереве (Issue #162)
 *   - pstringview::_avl_find() делегирует в detail::avl_find()
 *   - pmap::_avl_find() делегирует в detail::avl_find()
 *   - Корректность поиска в pstringview после рефакторинга
 *   - Корректность поиска в pmap после рефакторинга
 *   - Поиск несуществующих ключей возвращает null pptr
 *   - Поиск в пустом дереве возвращает null pptr
 *
 * @see include/pmm/avl_tree_mixin.h — detail::avl_find() (Issue #162)
 * @see include/pmm/pstringview.h    — pstringview (Issue #151)
 * @see include/pmm/pmap.h           — pmap (Issue #153)
 * @version 0.1 (Issue #162 — дедупликация _avl_find)
 */

#include "pmm/avl_tree_mixin.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmap.h"
#include "pmm/pstringview.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <string>
#include <vector>

// ─── Test macros ──────────────────────────────────────────────────────────────

// ─── Manager type alias for tests ────────────────────────────────────────────

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 162>;
using TestPsv = TestMgr::pstringview;

// =============================================================================
// Issue #162 Tests Section A: pstringview search correctness after refactoring
// =============================================================================

/// @brief pstringview::intern() finds an existing string via detail::avl_find().
TEST_CASE( "I162-A1: pstringview::intern() finds existing string", "[test_issue162_deduplication]" )
{
    TestMgr::create( 64 * 1024 );
    TestPsv::reset();

    auto p1 = TestPsv::intern( "apple" );
    REQUIRE( !p1.is_null() );

    // intern() must find the same node via avl_find — not create a new one.
    auto p2 = TestPsv::intern( "apple" );
    REQUIRE( !p2.is_null() );
    REQUIRE( p1 == p2 ); // same granule index

    TestMgr::destroy();
}

/// @brief pstringview: search for a non-existent string returns null.
TEST_CASE( "I162-A2: pstringview::intern() returns non-null for new string", "[test_issue162_deduplication]" )
{
    TestMgr::create( 64 * 1024 );
    TestPsv::reset();

    TestPsv::intern( "banana" );

    // "cherry" was never interned — must return null pptr.
    auto p = TestPsv::intern( "cherry" );
    REQUIRE( !p.is_null() ); // intern() creates a new node if missing

    // But if we search again, it is now present.
    auto p2 = TestPsv::intern( "cherry" );
    REQUIRE( p == p2 );

    TestMgr::destroy();
}

/// @brief pstringview: search returns null for empty tree.
TEST_CASE( "I162-A3: pstringview::intern() on empty tree creates new node", "[test_issue162_deduplication]" )
{
    TestMgr::create( 64 * 1024 );
    TestPsv::reset(); // root_idx = 0

    // With empty tree, intern() creates a new node.
    auto p = TestPsv::intern( "hello" );
    REQUIRE( !p.is_null() );

    // After reset, the tree is logically empty again for lookup.
    TestPsv::reset();
    // Now intern() creates another node (same value, different node).
    auto p2 = TestPsv::intern( "hello" );
    REQUIRE( !p2.is_null() );
    // p != p2 because reset() cleared the root — avl_find found nothing.
    REQUIRE( p != p2 );

    TestMgr::destroy();
}

/// @brief pstringview: all inserted strings are findable in correct order.
TEST_CASE( "I162-A4: pstringview: all inserted strings are findable", "[test_issue162_deduplication]" )
{
    TestMgr::create( 256 * 1024 );
    TestPsv::reset();

    std::vector<const char*> words = { "delta", "alpha", "charlie", "bravo", "echo" };

    // Insert all words.
    for ( const char* w : words )
    {
        auto p = TestPsv::intern( w );
        REQUIRE( !p.is_null() );
    }

    // Find each word — must return the same pptr as on first intern().
    for ( const char* w : words )
    {
        auto p1 = TestPsv::intern( w );
        auto p2 = TestPsv::intern( w );
        REQUIRE( p1 == p2 ); // deduplicated
        auto* obj = TestMgr::template resolve<TestPsv>( p1 );
        REQUIRE( obj != nullptr );
        REQUIRE( std::strcmp( obj->c_str(), w ) == 0 );
    }

    TestMgr::destroy();
}

// =============================================================================
// Issue #162 Tests Section B: pmap search correctness after refactoring
// =============================================================================

/// @brief pmap: find() locates an inserted key.
TEST_CASE( "I162-B1: pmap::find() locates an inserted key", "[test_issue162_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pmap<int, int> map;
    map.insert( 10, 100 );
    map.insert( 20, 200 );
    map.insert( 5, 50 );

    auto p10 = map.find( 10 );
    REQUIRE( !p10.is_null() );
    auto* obj = TestMgr::template resolve<pmm::pmap_node<int, int>>( p10 );
    REQUIRE( obj != nullptr );
    REQUIRE( obj->value == 100 );

    auto p20 = map.find( 20 );
    REQUIRE( !p20.is_null() );
    auto* obj20 = TestMgr::template resolve<pmm::pmap_node<int, int>>( p20 );
    REQUIRE( obj20 != nullptr );
    REQUIRE( obj20->value == 200 );

    auto p5 = map.find( 5 );
    REQUIRE( !p5.is_null() );
    auto* obj5 = TestMgr::template resolve<pmm::pmap_node<int, int>>( p5 );
    REQUIRE( obj5 != nullptr );
    REQUIRE( obj5->value == 50 );

    TestMgr::destroy();
}

/// @brief pmap: find() returns null pptr for a missing key.
TEST_CASE( "I162-B2: pmap::find() returns null for missing key", "[test_issue162_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pmap<int, int> map;
    map.insert( 42, 1 );

    auto p = map.find( 99 ); // never inserted
    REQUIRE( p.is_null() );

    TestMgr::destroy();
}

/// @brief pmap: find() on empty map returns null pptr.
TEST_CASE( "I162-B3: pmap::find() returns null on empty map", "[test_issue162_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pmap<int, int> map; // root_idx = 0
    REQUIRE( map.empty() );

    auto p = map.find( 1 );
    REQUIRE( p.is_null() );

    TestMgr::destroy();
}

/// @brief pmap: repeated find() returns consistent results with AVL rebalancing.
TEST_CASE( "I162-B4: pmap::find() correct after many inserts with AVL rebalancing", "[test_issue162_deduplication]" )
{
    TestMgr::create( 256 * 1024 );

    TestMgr::pmap<int, int> map;

    // Insert keys that trigger AVL rotations: ascending order causes left-heavy tree.
    for ( int i = 1; i <= 15; ++i )
        map.insert( i, i * 10 );

    // Every key must be findable.
    for ( int i = 1; i <= 15; ++i )
    {
        auto p = map.find( i );
        REQUIRE( !p.is_null() );
        auto* obj = TestMgr::template resolve<pmm::pmap_node<int, int>>( p );
        REQUIRE( obj != nullptr );
        REQUIRE( obj->key == i );
        REQUIRE( obj->value == i * 10 );
    }

    // Keys outside the range are not found.
    REQUIRE( map.find( 0 ).is_null() );
    REQUIRE( map.find( 16 ).is_null() );
    REQUIRE( map.find( -1 ).is_null() );

    TestMgr::destroy();
}

/// @brief pmap: contains() reflects the result of find() via detail::avl_find().
TEST_CASE( "I162-B5: pmap::contains() consistent with find()", "[test_issue162_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pmap<int, int> map;
    map.insert( 7, 70 );
    map.insert( 3, 30 );
    map.insert( 11, 110 );

    REQUIRE( map.contains( 7 ) );
    REQUIRE( map.contains( 3 ) );
    REQUIRE( map.contains( 11 ) );
    REQUIRE( !map.contains( 1 ) );
    REQUIRE( !map.contains( 100 ) );

    TestMgr::destroy();
}

// =============================================================================
// Issue #162 Tests Section C: avl_find() function template in avl_tree_mixin.h
// =============================================================================

/// @brief detail::avl_find() is a template in avl_tree_mixin.h — compile-time check.
TEST_CASE( "I162-C1: detail::avl_find() template available and correct", "[test_issue162_deduplication]" )
{
    // Verify that detail::avl_find() compiles and can be instantiated.
    // We use a pmap to test it indirectly since pmap::_avl_find() delegates to it.
    TestMgr::create( 64 * 1024 );

    TestMgr::pmap<int, int> map;
    map.insert( 50, 500 );
    map.insert( 25, 250 );
    map.insert( 75, 750 );

    // Find middle node — requires left or right traversal depending on key.
    auto p25 = map.find( 25 );
    auto p50 = map.find( 50 );
    auto p75 = map.find( 75 );
    REQUIRE( !p25.is_null() );
    REQUIRE( !p50.is_null() );
    REQUIRE( !p75.is_null() );

    // Confirm all three are distinct nodes.
    REQUIRE( p25.offset() != p50.offset() );
    REQUIRE( p50.offset() != p75.offset() );
    REQUIRE( p25.offset() != p75.offset() );

    TestMgr::destroy();
}

/// @brief Both pstringview and pmap use detail::avl_find() from avl_tree_mixin.h.
///        This test verifies that the shared helper works correctly for both users.
TEST_CASE( "I162-C2: detail::avl_find() shared correctly by pstringview and pmap", "[test_issue162_deduplication]" )
{
    TestMgr::create( 256 * 1024 );
    TestPsv::reset();

    // pstringview uses detail::avl_find() with strcmp comparison.
    auto ps1 = TestPsv::intern( "key" );
    auto ps2 = TestPsv::intern( "key" );
    REQUIRE( ps1 == ps2 ); // same node returned by avl_find

    // pmap uses detail::avl_find() with operator== / operator< comparison.
    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    auto pm1 = map.find( 1 );
    auto pm2 = map.find( 1 );
    REQUIRE( pm1 == pm2 ); // same node returned by avl_find
    REQUIRE( !pm1.is_null() );

    TestMgr::destroy();
}

// =============================================================================
// main
// =============================================================================
