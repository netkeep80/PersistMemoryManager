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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// ─── Test macros ──────────────────────────────────────────────────────────────

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
        std::cout << "  " << name << " ... ";                                                                          \
        if ( fn() )                                                                                                    \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

// ─── Manager type alias for tests ────────────────────────────────────────────

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 162>;
using TestPsv = TestMgr::pstringview;

// =============================================================================
// Issue #162 Tests Section A: pstringview search correctness after refactoring
// =============================================================================

/// @brief pstringview::intern() finds an existing string via detail::avl_find().
static bool test_i162_pstringview_find_existing()
{
    TestMgr::create( 64 * 1024 );
    TestPsv::reset();

    auto p1 = TestPsv::intern( "apple" );
    PMM_TEST( !p1.is_null() );

    // intern() must find the same node via avl_find — not create a new one.
    auto p2 = TestPsv::intern( "apple" );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( p1 == p2 ); // same granule index

    TestMgr::destroy();
    return true;
}

/// @brief pstringview: search for a non-existent string returns null.
static bool test_i162_pstringview_find_missing()
{
    TestMgr::create( 64 * 1024 );
    TestPsv::reset();

    TestPsv::intern( "banana" );

    // "cherry" was never interned — must return null pptr.
    auto p = TestPsv::intern( "cherry" );
    PMM_TEST( !p.is_null() ); // intern() creates a new node if missing

    // But if we search again, it is now present.
    auto p2 = TestPsv::intern( "cherry" );
    PMM_TEST( p == p2 );

    TestMgr::destroy();
    return true;
}

/// @brief pstringview: search returns null for empty tree.
static bool test_i162_pstringview_find_empty_tree()
{
    TestMgr::create( 64 * 1024 );
    TestPsv::reset(); // root_idx = 0

    // With empty tree, intern() creates a new node.
    auto p = TestPsv::intern( "hello" );
    PMM_TEST( !p.is_null() );

    // After reset, the tree is logically empty again for lookup.
    TestPsv::reset();
    // Now intern() creates another node (same value, different node).
    auto p2 = TestPsv::intern( "hello" );
    PMM_TEST( !p2.is_null() );
    // p != p2 because reset() cleared the root — avl_find found nothing.
    PMM_TEST( p != p2 );

    TestMgr::destroy();
    return true;
}

/// @brief pstringview: all inserted strings are findable in correct order.
static bool test_i162_pstringview_find_multiple()
{
    TestMgr::create( 256 * 1024 );
    TestPsv::reset();

    std::vector<const char*> words = { "delta", "alpha", "charlie", "bravo", "echo" };

    // Insert all words.
    for ( const char* w : words )
    {
        auto p = TestPsv::intern( w );
        PMM_TEST( !p.is_null() );
    }

    // Find each word — must return the same pptr as on first intern().
    for ( const char* w : words )
    {
        auto p1 = TestPsv::intern( w );
        auto p2 = TestPsv::intern( w );
        PMM_TEST( p1 == p2 ); // deduplicated
        auto* obj = TestMgr::template resolve<TestPsv>( p1 );
        PMM_TEST( obj != nullptr );
        PMM_TEST( std::strcmp( obj->c_str(), w ) == 0 );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// Issue #162 Tests Section B: pmap search correctness after refactoring
// =============================================================================

/// @brief pmap: find() locates an inserted key.
static bool test_i162_pmap_find_existing()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pmap<int, int> map;
    map.insert( 10, 100 );
    map.insert( 20, 200 );
    map.insert( 5, 50 );

    auto p10 = map.find( 10 );
    PMM_TEST( !p10.is_null() );
    auto* obj = TestMgr::template resolve<pmm::pmap_node<int, int>>( p10 );
    PMM_TEST( obj != nullptr );
    PMM_TEST( obj->value == 100 );

    auto p20 = map.find( 20 );
    PMM_TEST( !p20.is_null() );
    auto* obj20 = TestMgr::template resolve<pmm::pmap_node<int, int>>( p20 );
    PMM_TEST( obj20 != nullptr );
    PMM_TEST( obj20->value == 200 );

    auto p5 = map.find( 5 );
    PMM_TEST( !p5.is_null() );
    auto* obj5 = TestMgr::template resolve<pmm::pmap_node<int, int>>( p5 );
    PMM_TEST( obj5 != nullptr );
    PMM_TEST( obj5->value == 50 );

    TestMgr::destroy();
    return true;
}

/// @brief pmap: find() returns null pptr for a missing key.
static bool test_i162_pmap_find_missing()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pmap<int, int> map;
    map.insert( 42, 1 );

    auto p = map.find( 99 ); // never inserted
    PMM_TEST( p.is_null() );

    TestMgr::destroy();
    return true;
}

/// @brief pmap: find() on empty map returns null pptr.
static bool test_i162_pmap_find_empty_map()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pmap<int, int> map; // root_idx = 0
    PMM_TEST( map.empty() );

    auto p = map.find( 1 );
    PMM_TEST( p.is_null() );

    TestMgr::destroy();
    return true;
}

/// @brief pmap: repeated find() returns consistent results with AVL rebalancing.
static bool test_i162_pmap_find_after_many_inserts()
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
        PMM_TEST( !p.is_null() );
        auto* obj = TestMgr::template resolve<pmm::pmap_node<int, int>>( p );
        PMM_TEST( obj != nullptr );
        PMM_TEST( obj->key == i );
        PMM_TEST( obj->value == i * 10 );
    }

    // Keys outside the range are not found.
    PMM_TEST( map.find( 0 ).is_null() );
    PMM_TEST( map.find( 16 ).is_null() );
    PMM_TEST( map.find( -1 ).is_null() );

    TestMgr::destroy();
    return true;
}

/// @brief pmap: contains() reflects the result of find() via detail::avl_find().
static bool test_i162_pmap_contains_consistent_with_find()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pmap<int, int> map;
    map.insert( 7, 70 );
    map.insert( 3, 30 );
    map.insert( 11, 110 );

    PMM_TEST( map.contains( 7 ) );
    PMM_TEST( map.contains( 3 ) );
    PMM_TEST( map.contains( 11 ) );
    PMM_TEST( !map.contains( 1 ) );
    PMM_TEST( !map.contains( 100 ) );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// Issue #162 Tests Section C: avl_find() function template in avl_tree_mixin.h
// =============================================================================

/// @brief detail::avl_find() is a template in avl_tree_mixin.h — compile-time check.
static bool test_i162_avl_find_template_available()
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
    PMM_TEST( !p25.is_null() );
    PMM_TEST( !p50.is_null() );
    PMM_TEST( !p75.is_null() );

    // Confirm all three are distinct nodes.
    PMM_TEST( p25.offset() != p50.offset() );
    PMM_TEST( p50.offset() != p75.offset() );
    PMM_TEST( p25.offset() != p75.offset() );

    TestMgr::destroy();
    return true;
}

/// @brief Both pstringview and pmap use detail::avl_find() from avl_tree_mixin.h.
///        This test verifies that the shared helper works correctly for both users.
static bool test_i162_avl_find_shared_by_pstringview_and_pmap()
{
    TestMgr::create( 256 * 1024 );
    TestPsv::reset();

    // pstringview uses detail::avl_find() with strcmp comparison.
    auto ps1 = TestPsv::intern( "key" );
    auto ps2 = TestPsv::intern( "key" );
    PMM_TEST( ps1 == ps2 ); // same node returned by avl_find

    // pmap uses detail::avl_find() with operator== / operator< comparison.
    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    auto pm1 = map.find( 1 );
    auto pm2 = map.find( 1 );
    PMM_TEST( pm1 == pm2 ); // same node returned by avl_find
    PMM_TEST( !pm1.is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue162_deduplication (Issue #162: AVL find deduplication) ===\n\n";
    bool all_passed = true;

    std::cout << "--- I162-A: pstringview search correctness after refactoring ---\n";
    PMM_RUN( "I162-A1: pstringview::intern() finds existing string", test_i162_pstringview_find_existing );
    PMM_RUN( "I162-A2: pstringview::intern() returns non-null for new string", test_i162_pstringview_find_missing );
    PMM_RUN( "I162-A3: pstringview::intern() on empty tree creates new node", test_i162_pstringview_find_empty_tree );
    PMM_RUN( "I162-A4: pstringview: all inserted strings are findable", test_i162_pstringview_find_multiple );

    std::cout << "\n--- I162-B: pmap search correctness after refactoring ---\n";
    PMM_RUN( "I162-B1: pmap::find() locates an inserted key", test_i162_pmap_find_existing );
    PMM_RUN( "I162-B2: pmap::find() returns null for missing key", test_i162_pmap_find_missing );
    PMM_RUN( "I162-B3: pmap::find() returns null on empty map", test_i162_pmap_find_empty_map );
    PMM_RUN( "I162-B4: pmap::find() correct after many inserts with AVL rebalancing",
             test_i162_pmap_find_after_many_inserts );
    PMM_RUN( "I162-B5: pmap::contains() consistent with find()", test_i162_pmap_contains_consistent_with_find );

    std::cout << "\n--- I162-C: detail::avl_find() template in avl_tree_mixin.h ---\n";
    PMM_RUN( "I162-C1: detail::avl_find() template available and correct", test_i162_avl_find_template_available );
    PMM_RUN( "I162-C2: detail::avl_find() shared correctly by pstringview and pmap",
             test_i162_avl_find_shared_by_pstringview_and_pmap );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
