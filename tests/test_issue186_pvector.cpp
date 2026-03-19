/**
 * @file test_issue186_pvector.cpp
 * @brief Tests for pvector<T> — persistent vector container (Issue #186).
 *
 * Verifies the key requirements from Issue #186:
 *  1. pvector<T> implements a persistent vector in PAP.
 *  2. Nodes are stored in PAP and use TreeNode fields for linking.
 *  3. push_back() adds elements to the end in O(1).
 *  4. at(i) returns correct pptr for valid indices, null pptr for out of range.
 *  5. front() and back() return first/last elements.
 *  6. pop_back() removes the last element.
 *  7. clear() removes all elements.
 *  8. size() returns correct element count.
 *  9. Iteration via begin()/end() works correctly.
 * 10. Nodes are NOT permanently locked (can be freed).
 *
 * Usage of the concise API (via Mgr::pvector alias):
 *  @code
 *    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 186>;
 *    Mgr::create(64 * 1024);
 *
 *    Mgr::pvector<int> vec;
 *    vec.push_back(42);
 *    auto p = vec.at(0);
 *    assert(!p.is_null());
 *    assert(p->value == 42);
 *
 *    Mgr::destroy();
 *  @endcode
 *
 * @see include/pmm/pvector.h — pvector<T,ManagerT>
 * @see include/pmm/pmap.h — pmap (analogous AVL-tree type, Issue #153)
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @see include/pmm/tree_node.h — TreeNode<AT> built-in fields (Issue #87, #138)
 * @version 0.1 (Issue #186 — pvector<T>)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pvector.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include <string>
#include <vector>

// ─── Test macros ──────────────────────────────────────────────────────────────

// ─── Manager type alias for tests ────────────────────────────────────────────

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 186>;

// =============================================================================
// I186-A: Basic push_back and at
// =============================================================================

/// @brief push_back() returns non-null pptr; at(0) returns the same value.
TEST_CASE( "    push_back single element", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    auto                  p = vec.push_back( 42 );
    REQUIRE( !p.is_null() );

    auto found = vec.at( 0 );
    REQUIRE( !found.is_null() );
    REQUIRE( found == p );

    const auto* node = found.resolve();
    REQUIRE( node != nullptr );
    REQUIRE( node->value == 42 );

    TestMgr::destroy();
}

/// @brief push_back() with multiple elements.
TEST_CASE( "    push_back multiple elements", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    auto                  p1 = vec.push_back( 10 );
    auto                  p2 = vec.push_back( 20 );
    auto                  p3 = vec.push_back( 30 );

    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );
    REQUIRE( ( p1 != p2 && p2 != p3 && p1 != p3 ) );

    // Check values via at()
    REQUIRE( vec.at( 0 )->value == 10 );
    REQUIRE( vec.at( 1 )->value == 20 );
    REQUIRE( vec.at( 2 )->value == 30 );

    TestMgr::destroy();
}

/// @brief at() returns null pptr for out of range index.
TEST_CASE( "    at returns null for out of range", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 42 );

    auto not_found = vec.at( 1 );
    REQUIRE( not_found.is_null() );

    auto also_not_found = vec.at( 100 );
    REQUIRE( also_not_found.is_null() );

    TestMgr::destroy();
}

// =============================================================================
// I186-B: size() method
// =============================================================================

/// @brief size() returns correct count after push_back operations.
TEST_CASE( "    size() returns correct count", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    REQUIRE( vec.size() == 0 );

    vec.push_back( 1 );
    REQUIRE( vec.size() == 1 );

    vec.push_back( 2 );
    REQUIRE( vec.size() == 2 );

    vec.push_back( 3 );
    REQUIRE( vec.size() == 3 );

    TestMgr::destroy();
}

// =============================================================================
// I186-C: empty() method
// =============================================================================

/// @brief empty() returns true for new vector, false after push_back.
TEST_CASE( "    empty() returns correct state", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    REQUIRE( vec.empty() );

    vec.push_back( 1 );
    REQUIRE( !vec.empty() );

    TestMgr::destroy();
}

// =============================================================================
// I186-D: front() and back()
// =============================================================================

/// @brief front() returns first element, back() returns last element.
TEST_CASE( "    front() and back() return correct elements", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;

    // Empty vector
    REQUIRE( vec.front().is_null() );
    REQUIRE( vec.back().is_null() );

    vec.push_back( 10 );
    REQUIRE( vec.front()->value == 10 );
    REQUIRE( vec.back()->value == 10 );

    vec.push_back( 20 );
    REQUIRE( vec.front()->value == 10 );
    REQUIRE( vec.back()->value == 20 );

    vec.push_back( 30 );
    REQUIRE( vec.front()->value == 10 );
    REQUIRE( vec.back()->value == 30 );

    TestMgr::destroy();
}

// =============================================================================
// I186-E: pop_back()
// =============================================================================

/// @brief pop_back() removes the last element.
TEST_CASE( "    pop_back() removes last element", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    REQUIRE( vec.size() == 3 );
    REQUIRE( vec.back()->value == 30 );

    REQUIRE( vec.pop_back() == true );
    REQUIRE( vec.size() == 2 );
    REQUIRE( vec.back()->value == 20 );

    REQUIRE( vec.pop_back() == true );
    REQUIRE( vec.size() == 1 );
    REQUIRE( vec.back()->value == 10 );

    REQUIRE( vec.pop_back() == true );
    REQUIRE( vec.size() == 0 );
    REQUIRE( vec.empty() );

    // pop_back on empty vector returns false
    REQUIRE( vec.pop_back() == false );

    TestMgr::destroy();
}

// =============================================================================
// I186-F: clear()
// =============================================================================

/// @brief clear() removes all elements.
TEST_CASE( "    clear() removes all elements", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    REQUIRE( vec.size() == 3 );
    REQUIRE( !vec.empty() );

    vec.clear();
    REQUIRE( vec.size() == 0 );
    REQUIRE( vec.empty() );
    REQUIRE( vec.front().is_null() );
    REQUIRE( vec.back().is_null() );

    TestMgr::destroy();
}

// =============================================================================
// I186-G: reset() for test isolation
// =============================================================================

/// @brief reset() clears indices but does not free memory.
TEST_CASE( "    reset() clears indices for test isolation", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 1 );
    REQUIRE( !vec.empty() );

    vec.reset();
    REQUIRE( vec.empty() );
    REQUIRE( vec.size() == 0 );
    REQUIRE( vec.at( 0 ).is_null() );

    // Can push_back after reset
    vec.push_back( 2 );
    REQUIRE( !vec.empty() );
    REQUIRE( vec.at( 0 )->value == 2 );

    TestMgr::destroy();
}

// =============================================================================
// I186-H: Node block not permanently locked
// =============================================================================

/// @brief pvector node blocks are NOT permanently locked.
TEST_CASE( "    pvector node block NOT permanently locked", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    auto                  p = vec.push_back( 42 );
    REQUIRE( !p.is_null() );

    auto* node = p.resolve();
    REQUIRE( node != nullptr );

    // pvector node blocks are NOT permanently locked
    REQUIRE( TestMgr::is_permanently_locked( node ) == false );

    TestMgr::destroy();
}

// =============================================================================
// I186-I: Iterator
// =============================================================================

/// @brief Iterator traverses all elements in order.
TEST_CASE( "    iterator traverses all elements", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    std::vector<int> values;
    for ( auto it = vec.begin(); it != vec.end(); ++it )
    {
        auto p = *it;
        REQUIRE( !p.is_null() );
        values.push_back( p->value );
    }

    REQUIRE( values.size() == 3 );
    REQUIRE( values[0] == 10 );
    REQUIRE( values[1] == 20 );
    REQUIRE( values[2] == 30 );

    TestMgr::destroy();
}

/// @brief Empty vector iterator: begin() == end().
TEST_CASE( "    empty vector iterator", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    REQUIRE( vec.begin() == vec.end() );

    TestMgr::destroy();
}

// =============================================================================
// I186-J: pvector layout check
// =============================================================================

/// @brief pvector_node<T> has the expected layout (contains value).
TEST_CASE( "    pvector_node<int> size check", "[test_issue186_pvector]" )
{
    using node_t = pmm::pvector_node<int>;
    // node must contain value field
    REQUIRE( sizeof( node_t ) >= sizeof( int ) );
}

// =============================================================================
// I186-K: Mgr::pvector alias
// =============================================================================

/// @brief Mgr::pvector<int> is the same type as pmm::pvector<int, Mgr>.
TEST_CASE( "    Mgr::pvector<T> is same type as pmm::pvector<T,Mgr>", "[test_issue186_pvector]" )
{
    using DirectVec = pmm::pvector<int, TestMgr>;
    using AliasVec  = TestMgr::pvector<int>;

    // Both types must be the same (compile-time check)
    static_assert( std::is_same<DirectVec, AliasVec>::value, "Mgr::pvector alias type mismatch" );

    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    AliasVec vec;
    vec.push_back( 7 );
    REQUIRE( vec.at( 0 )->value == 7 );

    TestMgr::destroy();
}

// =============================================================================
// I186-L: Large vector stress test
// =============================================================================

/// @brief Insert 100 elements and verify all are accessible correctly.
TEST_CASE( "    100 elements with access by index", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;

    const int N = 100;
    for ( int i = 0; i < N; i++ )
        vec.push_back( i * 10 );

    REQUIRE( vec.size() == static_cast<std::size_t>( N ) );

    for ( int i = 0; i < N; i++ )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        REQUIRE( !p.is_null() );
        REQUIRE( p->value == i * 10 );
    }

    // Out of range
    REQUIRE( vec.at( N ).is_null() );

    TestMgr::destroy();
}

// =============================================================================
// I186-M: pvector with struct type
// =============================================================================

struct Point186
{
    int x;
    int y;
};

/// @brief pvector works with struct types.
TEST_CASE( "    pvector works with struct types", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<Point186> vec;

    Point186 p1 = { 1, 2 };
    Point186 p2 = { 3, 4 };
    Point186 p3 = { 5, 6 };

    vec.push_back( p1 );
    vec.push_back( p2 );
    vec.push_back( p3 );

    REQUIRE( vec.size() == 3 );

    auto n0 = vec.at( 0 );
    auto n1 = vec.at( 1 );
    auto n2 = vec.at( 2 );

    REQUIRE( ( !n0.is_null() && !n1.is_null() && !n2.is_null() ) );
    REQUIRE( ( n0->value.x == 1 && n0->value.y == 2 ) );
    REQUIRE( ( n1->value.x == 3 && n1->value.y == 4 ) );
    REQUIRE( ( n2->value.x == 5 && n2->value.y == 6 ) );

    TestMgr::destroy();
}

// =============================================================================
// I186-N: AVL tree structure — O(log n) height and correct weight fields
// =============================================================================

/// @brief Recursively verify AVL tree invariants: weight == subtree size and |bf| <= 1.
/// Returns the subtree size, or -1 on error.
static int verify_avl_node( TestMgr::pvector<int>::node_pptr p )
{
    if ( p.is_null() )
        return 0;

    using index_type                     = TestMgr::index_type;
    static constexpr index_type no_block = TestMgr::address_traits::no_block;

    auto& tn        = p.tree_node();
    auto  left_idx  = tn.get_left();
    auto  right_idx = tn.get_right();

    // Рекурсивно проверяем поддеревья.
    int left_size  = ( left_idx != no_block ) ? verify_avl_node( TestMgr::pvector<int>::node_pptr( left_idx ) ) : 0;
    int right_size = ( right_idx != no_block ) ? verify_avl_node( TestMgr::pvector<int>::node_pptr( right_idx ) ) : 0;

    if ( left_size < 0 || right_size < 0 )
        return -1;

    // Проверяем weight == размер поддерева.
    int expected_weight = 1 + left_size + right_size;
    if ( static_cast<int>( tn.get_weight() ) != expected_weight )
        return -1;

    // Проверяем баланс (|bf| <= 1).
    int lh = ( left_idx != no_block )
                 ? static_cast<int>( TestMgr::pvector<int>::node_pptr( left_idx ).tree_node().get_height() )
                 : 0;
    int rh = ( right_idx != no_block )
                 ? static_cast<int>( TestMgr::pvector<int>::node_pptr( right_idx ).tree_node().get_height() )
                 : 0;
    int bf = lh - rh;
    if ( bf < -1 || bf > 1 )
        return -1;

    return expected_weight;
}

/// @brief AVL tree structure: weight fields correct, balanced, height is O(log n).
TEST_CASE( "    AVL invariants: weight correct, height O(log n)", "[test_issue186_pvector]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;
    const int             N = 100;

    for ( int i = 0; i < N; i++ )
        vec.push_back( i );

    REQUIRE( static_cast<int>( vec.size() ) == N );

    // Проверяем структуру AVL-дерева.
    TestMgr::pvector<int>::node_pptr root( vec._root_idx );
    int                              total = verify_avl_node( root );
    REQUIRE( total == N );

    // Высота дерева должна быть O(log n): не более 2 * ceil(log2(N+1)).
    int height         = static_cast<int>( root.tree_node().get_height() );
    int max_avl_height = 2 * 8; // log2(100) ~ 6.6, AVL допускает ~1.44*log2(n); 16 — с запасом
    REQUIRE( height <= max_avl_height );

    // Проверяем корректность at() для всех элементов.
    for ( int i = 0; i < N; i++ )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        REQUIRE( !p.is_null() );
        REQUIRE( p->value == i );
    }

    // Проверяем структуру после нескольких pop_back.
    for ( int i = 0; i < 30; i++ )
        vec.pop_back();

    REQUIRE( static_cast<int>( vec.size() ) == N - 30 );
    TestMgr::pvector<int>::node_pptr root2( vec._root_idx );
    int                              total2 = verify_avl_node( root2 );
    REQUIRE( total2 == N - 30 );

    TestMgr::destroy();
}

// =============================================================================
// main
// =============================================================================
