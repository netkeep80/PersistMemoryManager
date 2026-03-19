/**
 * @file test_issue153_pmap.cpp
 * @brief Tests for pmap<_K,_V> — persistent AVL tree dictionary (Issue #153).
 *
 * Verifies the key requirements from Issue #153:
 *  1. pmap<_K,_V> implements a persistent AVL tree dictionary in PAP.
 *  2. Nodes are stored in PAP and permanently locked (cannot be freed via deallocate).
 *  3. The dictionary uses built-in TreeNode AVL fields — no separate PAP structures.
 *  4. insert() creates new nodes and updates values for existing keys.
 *  5. find() returns correct pptr for existing keys, null pptr for missing keys.
 *  6. contains() returns correct bool result.
 *  7. AVL tree self-balances during insertion.
 *  8. pmap works with various key types: int, pstringview.
 *  9. reset() clears the root for test isolation.
 *
 * Usage of the concise API (via Mgr::pmap alias):
 *  @code
 *    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 153>;
 *    Mgr::create(64 * 1024);
 *
 *    Mgr::pmap<int, int> map;
 *    map.insert(42, 100);
 *    auto p = map.find(42);
 *    assert(!p.is_null());
 *    assert(p->value == 100);
 *
 *    Mgr::destroy();
 *  @endcode
 *
 * @see include/pmm/pmap.h — pmap<_K,_V,ManagerT>
 * @see include/pmm/pstringview.h — pstringview (analogous AVL-tree type, Issue #151)
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @see include/pmm/tree_node.h — TreeNode<AT> built-in AVL fields (Issue #87, #138)
 * @version 0.1 (Issue #153 — pmap<_K,_V>)
 */

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

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 153>;

// =============================================================================
// I153-A: Basic insert and find with int keys
// =============================================================================

/// @brief insert() returns non-null pptr; find() returns the same pptr.
TEST_CASE( "    insert single key-value pair", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    auto                    p = map.insert( 42, 100 );
    REQUIRE( !p.is_null() );

    auto found = map.find( 42 );
    REQUIRE( !found.is_null() );
    REQUIRE( found == p );

    const auto* node = found.resolve();
    REQUIRE( node != nullptr );
    REQUIRE( node->key == 42 );
    REQUIRE( node->value == 100 );

    TestMgr::destroy();
}

/// @brief insert() with multiple distinct keys.
TEST_CASE( "    insert multiple distinct keys", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    auto                    p1 = map.insert( 10, 1 );
    auto                    p2 = map.insert( 20, 2 );
    auto                    p3 = map.insert( 30, 3 );

    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );
    REQUIRE( ( p1 != p2 && p2 != p3 && p1 != p3 ) );

    // Check values
    REQUIRE( map.find( 10 )->value == 1 );
    REQUIRE( map.find( 20 )->value == 2 );
    REQUIRE( map.find( 30 )->value == 3 );

    TestMgr::destroy();
}

/// @brief find() returns null pptr for missing key.
TEST_CASE( "    find returns null for missing key", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 42, 100 );

    auto not_found = map.find( 99 );
    REQUIRE( not_found.is_null() );

    TestMgr::destroy();
}

// =============================================================================
// I153-B: contains() method
// =============================================================================

/// @brief contains() returns true for existing key, false for missing key.
TEST_CASE( "    contains() returns correct result", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    map.insert( 2, 20 );
    map.insert( 3, 30 );

    REQUIRE( map.contains( 1 ) );
    REQUIRE( map.contains( 2 ) );
    REQUIRE( map.contains( 3 ) );
    REQUIRE( !map.contains( 0 ) );
    REQUIRE( !map.contains( 4 ) );
    REQUIRE( !map.contains( -1 ) );

    TestMgr::destroy();
}

// =============================================================================
// I153-C: Update value on duplicate key
// =============================================================================

/// @brief insert() with existing key updates the value (no duplicate nodes).
TEST_CASE( "    insert with existing key updates value", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    auto                    p1 = map.insert( 42, 100 );
    REQUIRE( !p1.is_null() );
    REQUIRE( map.find( 42 )->value == 100 );

    // Insert same key with new value
    auto p2 = map.insert( 42, 999 );
    REQUIRE( !p2.is_null() );

    // Must return same pptr (same node updated)
    REQUIRE( p1 == p2 );

    // Value must be updated
    REQUIRE( map.find( 42 )->value == 999 );

    TestMgr::destroy();
}

// =============================================================================
// I153-D: Block locking (Issue #155)
// =============================================================================

/// @brief After insert(), pmap node blocks are NOT permanently locked (Issue #155).
///
/// Unlike pstringview (where interning semantics require permanent lock), pmap
/// nodes are regular allocations that can be freed when removed from the tree.
TEST_CASE( "    pmap node block NOT permanently locked (Issue #155)", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    auto                    p = map.insert( 42, 100 );
    REQUIRE( !p.is_null() );

    auto* node = p.resolve();
    REQUIRE( node != nullptr );

    // pmap node blocks are NOT permanently locked (Issue #155)
    REQUIRE( TestMgr::is_permanently_locked( node ) == false );

    TestMgr::destroy();
}

// =============================================================================
// I153-E: Built-in AVL tree structure
// =============================================================================

/// @brief The dictionary uses built-in TreeNode AVL fields (not a separate hash table).
///
/// Verified by checking that after inserting several nodes, re-insertion
/// returns the same pptrs via AVL tree search.
TEST_CASE( "    AVL tree via built-in TreeNode fields", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;

    // Insert in sorted order to force AVL rotations.
    auto p1 = map.insert( 1, 10 );
    auto p2 = map.insert( 2, 20 );
    auto p3 = map.insert( 3, 30 );

    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );

    // AVL root must be non-null
    REQUIRE( map._root_idx != static_cast<TestMgr::index_type>( 0 ) );

    // All nodes accessible via AVL search
    REQUIRE( map.find( 1 ) == p1 );
    REQUIRE( map.find( 2 ) == p2 );
    REQUIRE( map.find( 3 ) == p3 );

    TestMgr::destroy();
}

/// @brief AVL tree self-balances: inserting in descending order still works.
TEST_CASE( "    AVL tree self-balances (descending order)", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 128 * 1024 ) );

    TestMgr::pmap<int, int> map;

    // Insert in descending order to force right-heavy imbalance → left rotations.
    const int N = 15;
    for ( int i = N; i >= 1; i-- )
        map.insert( i, i * 10 );

    // All nodes should be findable
    for ( int i = 1; i <= N; i++ )
    {
        auto p = map.find( i );
        REQUIRE( !p.is_null() );
        REQUIRE( p->value == i * 10 );
    }

    TestMgr::destroy();
}

/// @brief AVL tree handles many keys with correct search results.
TEST_CASE( "    20 keys with AVL balancing", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    TestMgr::pmap<int, int> map;

    const int N = 20;
    for ( int i = 0; i < N; i++ )
        map.insert( i, i * 100 );

    for ( int i = 0; i < N; i++ )
    {
        auto p = map.find( i );
        REQUIRE( !p.is_null() );
        REQUIRE( p->key == i );
        REQUIRE( p->value == i * 100 );
    }

    // Keys outside range not found
    REQUIRE( map.find( -1 ).is_null() );
    REQUIRE( map.find( N ).is_null() );

    TestMgr::destroy();
}

// =============================================================================
// I153-F: empty() method
// =============================================================================

/// @brief empty() returns true for new map, false after insert.
TEST_CASE( "    empty() returns correct state", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    REQUIRE( map.empty() );

    map.insert( 1, 10 );
    REQUIRE( !map.empty() );

    TestMgr::destroy();
}

// =============================================================================
// I153-G: reset() for test isolation
// =============================================================================

/// @brief reset() clears _root_idx; subsequent inserts create fresh tree.
TEST_CASE( "    reset() clears root for test isolation", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    REQUIRE( !map.empty() );

    map.reset();
    REQUIRE( map.empty() );
    REQUIRE( map.find( 1 ).is_null() );

    // Can insert after reset
    map.insert( 2, 20 );
    REQUIRE( !map.find( 2 ).is_null() );
    REQUIRE( map.find( 2 )->value == 20 );

    TestMgr::destroy();
}

// =============================================================================
// I153-H: pmap with pstringview keys
// =============================================================================

/// @brief pmap<pptr<pstringview>, int> works as a named persistent object dictionary.
/// Issue #184: Uses pptr<pstringview> as key (instead of copying pstringview by value)
/// because pstringview now has embedded string data that cannot be safely copied.
TEST_CASE( "    pmap<pptr<pstringview>, int> for named persistent objects", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    TestMgr::pstringview::reset();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    // Intern keys — returns pptr<pstringview>
    auto pk1 = static_cast<TestMgr::pptr<TestMgr::pstringview>>( TestMgr::pstringview( "alpha" ) );
    auto pk2 = static_cast<TestMgr::pptr<TestMgr::pstringview>>( TestMgr::pstringview( "beta" ) );
    auto pk3 = static_cast<TestMgr::pptr<TestMgr::pstringview>>( TestMgr::pstringview( "gamma" ) );

    REQUIRE( ( !pk1.is_null() && !pk2.is_null() && !pk3.is_null() ) );

    // Use pptr<pstringview> as key type (Issue #184)
    TestMgr::pmap<TestMgr::pptr<TestMgr::pstringview>, int> map;
    map.insert( pk1, 1 );
    map.insert( pk2, 2 );
    map.insert( pk3, 3 );

    // Find by pptr (uses operator< and operator== of pptr<pstringview>)
    auto found1 = map.find( pk1 );
    auto found2 = map.find( pk2 );
    auto found3 = map.find( pk3 );

    REQUIRE( ( !found1.is_null() && !found2.is_null() && !found3.is_null() ) );
    REQUIRE( found1->value == 1 );
    REQUIRE( found2->value == 2 );
    REQUIRE( found3->value == 3 );

    // Re-intern same strings → same pptr (deduplication) → same values found
    auto pk1b = static_cast<TestMgr::pptr<TestMgr::pstringview>>( TestMgr::pstringview( "alpha" ) );
    REQUIRE( pk1b == pk1 ); // same pptr (deduplication)

    auto found1b = map.find( pk1b );
    REQUIRE( !found1b.is_null() );
    REQUIRE( found1b->value == 1 );

    // Verify key comparison works correctly (pptr<pstringview>::operator< dereferences and compares)
    REQUIRE( pk1 < pk2 );      // "alpha" < "beta"
    REQUIRE( pk2 < pk3 );      // "beta" < "gamma"
    REQUIRE( !( pk2 < pk1 ) ); // not "beta" < "alpha"

    TestMgr::destroy();
    TestMgr::pstringview::reset();
}

// =============================================================================
// I153-I: pmap layout check
// =============================================================================

/// @brief pmap_node<_K,_V> has the expected layout (key then value).
TEST_CASE( "    pmap_node<int,int> size check", "[test_issue153_pmap]" )
{
    using node_t = pmm::pmap_node<int, int>;
    // node must contain key and value fields
    REQUIRE( sizeof( node_t ) >= sizeof( int ) + sizeof( int ) );
}

// =============================================================================
// I153-J: pmap alias via Mgr::pmap syntax
// =============================================================================

/// @brief Mgr::pmap<int, int> is the same type as pmm::pmap<int, int, Mgr>.
TEST_CASE( "    Mgr::pmap<K,V> is same type as pmm::pmap<K,V,Mgr>", "[test_issue153_pmap]" )
{
    using DirectMap = pmm::pmap<int, int, TestMgr>;
    using AliasMap  = TestMgr::pmap<int, int>;

    // Both types must be the same (compile-time check)
    static_assert( std::is_same<DirectMap, AliasMap>::value, "Mgr::pmap alias type mismatch" );

    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    AliasMap map;
    map.insert( 7, 77 );
    REQUIRE( map.find( 7 )->value == 77 );

    TestMgr::destroy();
}

// =============================================================================
// I153-K: Large map stress test
// =============================================================================

/// @brief Insert 50 random-order keys and verify all are found correctly.
TEST_CASE( "    50 keys in mixed order", "[test_issue153_pmap]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 1024 * 1024 ) );

    // Insert keys in a mixed order to exercise various rotation cases.
    static const int keys[] = { 50, 25, 75, 12, 37, 62, 87, 6,  18, 31, 43, 56, 68, 81, 93,
                                3,  9,  15, 21, 28, 34, 40, 46, 53, 59, 65, 71, 78, 84, 90 };
    constexpr int    N      = static_cast<int>( sizeof( keys ) / sizeof( keys[0] ) );

    TestMgr::pmap<int, int> map;
    for ( int i = 0; i < N; i++ )
        map.insert( keys[i], keys[i] * 2 );

    for ( int i = 0; i < N; i++ )
    {
        auto p = map.find( keys[i] );
        REQUIRE( !p.is_null() );
        REQUIRE( p->key == keys[i] );
        REQUIRE( p->value == keys[i] * 2 );
    }

    // Keys not in the set are not found
    REQUIRE( map.find( 0 ).is_null() );
    REQUIRE( map.find( 100 ).is_null() );
    REQUIRE( map.find( 1 ).is_null() );

    TestMgr::destroy();
}

// =============================================================================
// main
// =============================================================================
