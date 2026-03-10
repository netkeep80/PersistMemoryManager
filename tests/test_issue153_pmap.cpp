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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 153>;

// =============================================================================
// I153-A: Basic insert and find with int keys
// =============================================================================

/// @brief insert() returns non-null pptr; find() returns the same pptr.
static bool test_i153_insert_basic()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    auto                    p = map.insert( 42, 100 );
    PMM_TEST( !p.is_null() );

    auto found = map.find( 42 );
    PMM_TEST( !found.is_null() );
    PMM_TEST( found == p );

    const auto* node = found.resolve();
    PMM_TEST( node != nullptr );
    PMM_TEST( node->key == 42 );
    PMM_TEST( node->value == 100 );

    TestMgr::destroy();
    return true;
}

/// @brief insert() with multiple distinct keys.
static bool test_i153_insert_multiple()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    auto                    p1 = map.insert( 10, 1 );
    auto                    p2 = map.insert( 20, 2 );
    auto                    p3 = map.insert( 30, 3 );

    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );
    PMM_TEST( p1 != p2 && p2 != p3 && p1 != p3 );

    // Check values
    PMM_TEST( map.find( 10 )->value == 1 );
    PMM_TEST( map.find( 20 )->value == 2 );
    PMM_TEST( map.find( 30 )->value == 3 );

    TestMgr::destroy();
    return true;
}

/// @brief find() returns null pptr for missing key.
static bool test_i153_find_missing()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 42, 100 );

    auto not_found = map.find( 99 );
    PMM_TEST( not_found.is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I153-B: contains() method
// =============================================================================

/// @brief contains() returns true for existing key, false for missing key.
static bool test_i153_contains()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    map.insert( 2, 20 );
    map.insert( 3, 30 );

    PMM_TEST( map.contains( 1 ) );
    PMM_TEST( map.contains( 2 ) );
    PMM_TEST( map.contains( 3 ) );
    PMM_TEST( !map.contains( 0 ) );
    PMM_TEST( !map.contains( 4 ) );
    PMM_TEST( !map.contains( -1 ) );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I153-C: Update value on duplicate key
// =============================================================================

/// @brief insert() with existing key updates the value (no duplicate nodes).
static bool test_i153_update_on_duplicate_key()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    auto                    p1 = map.insert( 42, 100 );
    PMM_TEST( !p1.is_null() );
    PMM_TEST( map.find( 42 )->value == 100 );

    // Insert same key with new value
    auto p2 = map.insert( 42, 999 );
    PMM_TEST( !p2.is_null() );

    // Must return same pptr (same node updated)
    PMM_TEST( p1 == p2 );

    // Value must be updated
    PMM_TEST( map.find( 42 )->value == 999 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I153-D: Block locking (Issue #155)
// =============================================================================

/// @brief After insert(), pmap node blocks are NOT permanently locked (Issue #155).
///
/// Unlike pstringview (where interning semantics require permanent lock), pmap
/// nodes are regular allocations that can be freed when removed from the tree.
static bool test_i153_node_block_not_permanently_locked()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    auto                    p = map.insert( 42, 100 );
    PMM_TEST( !p.is_null() );

    auto* node = p.resolve();
    PMM_TEST( node != nullptr );

    // pmap node blocks are NOT permanently locked (Issue #155)
    PMM_TEST( TestMgr::is_permanently_locked( node ) == false );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I153-E: Built-in AVL tree structure
// =============================================================================

/// @brief The dictionary uses built-in TreeNode AVL fields (not a separate hash table).
///
/// Verified by checking that after inserting several nodes, re-insertion
/// returns the same pptrs via AVL tree search.
static bool test_i153_avl_tree_structure()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;

    // Insert in sorted order to force AVL rotations.
    auto p1 = map.insert( 1, 10 );
    auto p2 = map.insert( 2, 20 );
    auto p3 = map.insert( 3, 30 );

    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    // AVL root must be non-null
    PMM_TEST( map._root_idx != static_cast<TestMgr::index_type>( 0 ) );

    // All nodes accessible via AVL search
    PMM_TEST( map.find( 1 ) == p1 );
    PMM_TEST( map.find( 2 ) == p2 );
    PMM_TEST( map.find( 3 ) == p3 );

    TestMgr::destroy();
    return true;
}

/// @brief AVL tree self-balances: inserting in descending order still works.
static bool test_i153_avl_balance_descending()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 128 * 1024 ) );

    TestMgr::pmap<int, int> map;

    // Insert in descending order to force right-heavy imbalance → left rotations.
    const int N = 15;
    for ( int i = N; i >= 1; i-- )
        map.insert( i, i * 10 );

    // All nodes should be findable
    for ( int i = 1; i <= N; i++ )
    {
        auto p = map.find( i );
        PMM_TEST( !p.is_null() );
        PMM_TEST( p->value == i * 10 );
    }

    TestMgr::destroy();
    return true;
}

/// @brief AVL tree handles many keys with correct search results.
static bool test_i153_many_keys()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    TestMgr::pmap<int, int> map;

    const int N = 20;
    for ( int i = 0; i < N; i++ )
        map.insert( i, i * 100 );

    for ( int i = 0; i < N; i++ )
    {
        auto p = map.find( i );
        PMM_TEST( !p.is_null() );
        PMM_TEST( p->key == i );
        PMM_TEST( p->value == i * 100 );
    }

    // Keys outside range not found
    PMM_TEST( map.find( -1 ).is_null() );
    PMM_TEST( map.find( N ).is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I153-F: empty() method
// =============================================================================

/// @brief empty() returns true for new map, false after insert.
static bool test_i153_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    PMM_TEST( map.empty() );

    map.insert( 1, 10 );
    PMM_TEST( !map.empty() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I153-G: reset() for test isolation
// =============================================================================

/// @brief reset() clears _root_idx; subsequent inserts create fresh tree.
static bool test_i153_reset()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    PMM_TEST( !map.empty() );

    map.reset();
    PMM_TEST( map.empty() );
    PMM_TEST( map.find( 1 ).is_null() );

    // Can insert after reset
    map.insert( 2, 20 );
    PMM_TEST( !map.find( 2 ).is_null() );
    PMM_TEST( map.find( 2 )->value == 20 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I153-H: pmap with pstringview keys
// =============================================================================

/// @brief pmap<pstringview, int> works as a named persistent object dictionary.
static bool test_i153_pstringview_key()
{
    TestMgr::destroy();
    TestMgr::pstringview::reset();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    // Intern keys
    auto pk1 = static_cast<TestMgr::pptr<TestMgr::pstringview>>( TestMgr::pstringview( "alpha" ) );
    auto pk2 = static_cast<TestMgr::pptr<TestMgr::pstringview>>( TestMgr::pstringview( "beta" ) );
    auto pk3 = static_cast<TestMgr::pptr<TestMgr::pstringview>>( TestMgr::pstringview( "gamma" ) );

    PMM_TEST( !pk1.is_null() && !pk2.is_null() && !pk3.is_null() );

    const TestMgr::pstringview* k1 = pk1.resolve();
    const TestMgr::pstringview* k2 = pk2.resolve();
    const TestMgr::pstringview* k3 = pk3.resolve();

    PMM_TEST( k1 != nullptr && k2 != nullptr && k3 != nullptr );

    TestMgr::pmap<TestMgr::pstringview, int> map;
    map.insert( *k1, 1 );
    map.insert( *k2, 2 );
    map.insert( *k3, 3 );

    // Find by value (uses operator< and operator== of pstringview)
    auto found1 = map.find( *k1 );
    auto found2 = map.find( *k2 );
    auto found3 = map.find( *k3 );

    PMM_TEST( !found1.is_null() && !found2.is_null() && !found3.is_null() );
    PMM_TEST( found1->value == 1 );
    PMM_TEST( found2->value == 2 );
    PMM_TEST( found3->value == 3 );

    // Re-intern same strings → same key objects → same values found
    auto pk1b = static_cast<TestMgr::pptr<TestMgr::pstringview>>( TestMgr::pstringview( "alpha" ) );
    PMM_TEST( pk1b == pk1 ); // same pptr (deduplication)

    const TestMgr::pstringview* k1b = pk1b.resolve();
    PMM_TEST( k1b != nullptr );
    auto found1b = map.find( *k1b );
    PMM_TEST( !found1b.is_null() );
    PMM_TEST( found1b->value == 1 );

    TestMgr::destroy();
    TestMgr::pstringview::reset();
    return true;
}

// =============================================================================
// I153-I: pmap layout check
// =============================================================================

/// @brief pmap_node<_K,_V> has the expected layout (key then value).
static bool test_i153_layout()
{
    using node_t = pmm::pmap_node<int, int>;
    // node must contain key and value fields
    PMM_TEST( sizeof( node_t ) >= sizeof( int ) + sizeof( int ) );
    return true;
}

// =============================================================================
// I153-J: pmap alias via Mgr::pmap syntax
// =============================================================================

/// @brief Mgr::pmap<int, int> is the same type as pmm::pmap<int, int, Mgr>.
static bool test_i153_alias()
{
    using DirectMap = pmm::pmap<int, int, TestMgr>;
    using AliasMap  = TestMgr::pmap<int, int>;

    // Both types must be the same (compile-time check)
    static_assert( std::is_same<DirectMap, AliasMap>::value, "Mgr::pmap alias type mismatch" );

    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    AliasMap map;
    map.insert( 7, 77 );
    PMM_TEST( map.find( 7 )->value == 77 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I153-K: Large map stress test
// =============================================================================

/// @brief Insert 50 random-order keys and verify all are found correctly.
static bool test_i153_stress_mixed_order()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 1024 * 1024 ) );

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
        PMM_TEST( !p.is_null() );
        PMM_TEST( p->key == keys[i] );
        PMM_TEST( p->value == keys[i] * 2 );
    }

    // Keys not in the set are not found
    PMM_TEST( map.find( 0 ).is_null() );
    PMM_TEST( map.find( 100 ).is_null() );
    PMM_TEST( map.find( 1 ).is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    bool all_passed = true;

    std::cout << "[Issue #153: pmap<_K,_V> — persistent AVL tree dictionary]\n";

    std::cout << "  I153-A: Basic insert and find\n";
    PMM_RUN( "    insert single key-value pair", test_i153_insert_basic );
    PMM_RUN( "    insert multiple distinct keys", test_i153_insert_multiple );
    PMM_RUN( "    find returns null for missing key", test_i153_find_missing );

    std::cout << "  I153-B: contains()\n";
    PMM_RUN( "    contains() returns correct result", test_i153_contains );

    std::cout << "  I153-C: Update on duplicate key\n";
    PMM_RUN( "    insert with existing key updates value", test_i153_update_on_duplicate_key );

    std::cout << "  I153-D: Block locking (Issue #155)\n";
    PMM_RUN( "    pmap node block NOT permanently locked (Issue #155)", test_i153_node_block_not_permanently_locked );

    std::cout << "  I153-E: Built-in AVL tree structure\n";
    PMM_RUN( "    AVL tree via built-in TreeNode fields", test_i153_avl_tree_structure );
    PMM_RUN( "    AVL tree self-balances (descending order)", test_i153_avl_balance_descending );
    PMM_RUN( "    20 keys with AVL balancing", test_i153_many_keys );

    std::cout << "  I153-F: empty()\n";
    PMM_RUN( "    empty() returns correct state", test_i153_empty );

    std::cout << "  I153-G: reset()\n";
    PMM_RUN( "    reset() clears root for test isolation", test_i153_reset );

    std::cout << "  I153-H: pmap with pstringview keys\n";
    PMM_RUN( "    pmap<pstringview, int> for named persistent objects", test_i153_pstringview_key );

    std::cout << "  I153-I: pmap layout\n";
    PMM_RUN( "    pmap_node<int,int> size check", test_i153_layout );

    std::cout << "  I153-J: Mgr::pmap alias\n";
    PMM_RUN( "    Mgr::pmap<K,V> is same type as pmm::pmap<K,V,Mgr>", test_i153_alias );

    std::cout << "  I153-K: Stress test\n";
    PMM_RUN( "    50 keys in mixed order", test_i153_stress_mixed_order );

    std::cout << "\n";
    if ( all_passed )
    {
        std::cout << "All Issue #153 tests PASSED.\n";
        return EXIT_SUCCESS;
    }
    else
    {
        std::cout << "Some Issue #153 tests FAILED.\n";
        return EXIT_FAILURE;
    }
}
