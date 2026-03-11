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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 186>;

// =============================================================================
// I186-A: Basic push_back and at
// =============================================================================

/// @brief push_back() returns non-null pptr; at(0) returns the same value.
static bool test_i186_push_back_basic()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    auto                  p = vec.push_back( 42 );
    PMM_TEST( !p.is_null() );

    auto found = vec.at( 0 );
    PMM_TEST( !found.is_null() );
    PMM_TEST( found == p );

    const auto* node = found.resolve();
    PMM_TEST( node != nullptr );
    PMM_TEST( node->value == 42 );

    TestMgr::destroy();
    return true;
}

/// @brief push_back() with multiple elements.
static bool test_i186_push_back_multiple()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    auto                  p1 = vec.push_back( 10 );
    auto                  p2 = vec.push_back( 20 );
    auto                  p3 = vec.push_back( 30 );

    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );
    PMM_TEST( p1 != p2 && p2 != p3 && p1 != p3 );

    // Check values via at()
    PMM_TEST( vec.at( 0 )->value == 10 );
    PMM_TEST( vec.at( 1 )->value == 20 );
    PMM_TEST( vec.at( 2 )->value == 30 );

    TestMgr::destroy();
    return true;
}

/// @brief at() returns null pptr for out of range index.
static bool test_i186_at_out_of_range()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 42 );

    auto not_found = vec.at( 1 );
    PMM_TEST( not_found.is_null() );

    auto also_not_found = vec.at( 100 );
    PMM_TEST( also_not_found.is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-B: size() method
// =============================================================================

/// @brief size() returns correct count after push_back operations.
static bool test_i186_size()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    PMM_TEST( vec.size() == 0 );

    vec.push_back( 1 );
    PMM_TEST( vec.size() == 1 );

    vec.push_back( 2 );
    PMM_TEST( vec.size() == 2 );

    vec.push_back( 3 );
    PMM_TEST( vec.size() == 3 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-C: empty() method
// =============================================================================

/// @brief empty() returns true for new vector, false after push_back.
static bool test_i186_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    PMM_TEST( vec.empty() );

    vec.push_back( 1 );
    PMM_TEST( !vec.empty() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-D: front() and back()
// =============================================================================

/// @brief front() returns first element, back() returns last element.
static bool test_i186_front_back()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;

    // Empty vector
    PMM_TEST( vec.front().is_null() );
    PMM_TEST( vec.back().is_null() );

    vec.push_back( 10 );
    PMM_TEST( vec.front()->value == 10 );
    PMM_TEST( vec.back()->value == 10 );

    vec.push_back( 20 );
    PMM_TEST( vec.front()->value == 10 );
    PMM_TEST( vec.back()->value == 20 );

    vec.push_back( 30 );
    PMM_TEST( vec.front()->value == 10 );
    PMM_TEST( vec.back()->value == 30 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-E: pop_back()
// =============================================================================

/// @brief pop_back() removes the last element.
static bool test_i186_pop_back()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    PMM_TEST( vec.size() == 3 );
    PMM_TEST( vec.back()->value == 30 );

    PMM_TEST( vec.pop_back() == true );
    PMM_TEST( vec.size() == 2 );
    PMM_TEST( vec.back()->value == 20 );

    PMM_TEST( vec.pop_back() == true );
    PMM_TEST( vec.size() == 1 );
    PMM_TEST( vec.back()->value == 10 );

    PMM_TEST( vec.pop_back() == true );
    PMM_TEST( vec.size() == 0 );
    PMM_TEST( vec.empty() );

    // pop_back on empty vector returns false
    PMM_TEST( vec.pop_back() == false );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-F: clear()
// =============================================================================

/// @brief clear() removes all elements.
static bool test_i186_clear()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    PMM_TEST( vec.size() == 3 );
    PMM_TEST( !vec.empty() );

    vec.clear();
    PMM_TEST( vec.size() == 0 );
    PMM_TEST( vec.empty() );
    PMM_TEST( vec.front().is_null() );
    PMM_TEST( vec.back().is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-G: reset() for test isolation
// =============================================================================

/// @brief reset() clears indices but does not free memory.
static bool test_i186_reset()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 1 );
    PMM_TEST( !vec.empty() );

    vec.reset();
    PMM_TEST( vec.empty() );
    PMM_TEST( vec.size() == 0 );
    PMM_TEST( vec.at( 0 ).is_null() );

    // Can push_back after reset
    vec.push_back( 2 );
    PMM_TEST( !vec.empty() );
    PMM_TEST( vec.at( 0 )->value == 2 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-H: Node block not permanently locked
// =============================================================================

/// @brief pvector node blocks are NOT permanently locked.
static bool test_i186_node_block_not_permanently_locked()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    auto                  p = vec.push_back( 42 );
    PMM_TEST( !p.is_null() );

    auto* node = p.resolve();
    PMM_TEST( node != nullptr );

    // pvector node blocks are NOT permanently locked
    PMM_TEST( TestMgr::is_permanently_locked( node ) == false );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-I: Iterator
// =============================================================================

/// @brief Iterator traverses all elements in order.
static bool test_i186_iterator()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    std::vector<int> values;
    for ( auto it = vec.begin(); it != vec.end(); ++it )
    {
        auto p = *it;
        PMM_TEST( !p.is_null() );
        values.push_back( p->value );
    }

    PMM_TEST( values.size() == 3 );
    PMM_TEST( values[0] == 10 );
    PMM_TEST( values[1] == 20 );
    PMM_TEST( values[2] == 30 );

    TestMgr::destroy();
    return true;
}

/// @brief Empty vector iterator: begin() == end().
static bool test_i186_iterator_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    PMM_TEST( vec.begin() == vec.end() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-J: pvector layout check
// =============================================================================

/// @brief pvector_node<T> has the expected layout (contains value).
static bool test_i186_layout()
{
    using node_t = pmm::pvector_node<int>;
    // node must contain value field
    PMM_TEST( sizeof( node_t ) >= sizeof( int ) );
    return true;
}

// =============================================================================
// I186-K: Mgr::pvector alias
// =============================================================================

/// @brief Mgr::pvector<int> is the same type as pmm::pvector<int, Mgr>.
static bool test_i186_alias()
{
    using DirectVec = pmm::pvector<int, TestMgr>;
    using AliasVec  = TestMgr::pvector<int>;

    // Both types must be the same (compile-time check)
    static_assert( std::is_same<DirectVec, AliasVec>::value, "Mgr::pvector alias type mismatch" );

    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    AliasVec vec;
    vec.push_back( 7 );
    PMM_TEST( vec.at( 0 )->value == 7 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-L: Large vector stress test
// =============================================================================

/// @brief Insert 100 elements and verify all are accessible correctly.
static bool test_i186_stress_many_elements()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;

    const int N = 100;
    for ( int i = 0; i < N; i++ )
        vec.push_back( i * 10 );

    PMM_TEST( vec.size() == static_cast<std::size_t>( N ) );

    for ( int i = 0; i < N; i++ )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        PMM_TEST( !p.is_null() );
        PMM_TEST( p->value == i * 10 );
    }

    // Out of range
    PMM_TEST( vec.at( N ).is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I186-M: pvector with struct type
// =============================================================================

struct Point
{
    int x;
    int y;
};

/// @brief pvector works with struct types.
static bool test_i186_struct_type()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<Point> vec;

    Point p1 = { 1, 2 };
    Point p2 = { 3, 4 };
    Point p3 = { 5, 6 };

    vec.push_back( p1 );
    vec.push_back( p2 );
    vec.push_back( p3 );

    PMM_TEST( vec.size() == 3 );

    auto n0 = vec.at( 0 );
    auto n1 = vec.at( 1 );
    auto n2 = vec.at( 2 );

    PMM_TEST( !n0.is_null() && !n1.is_null() && !n2.is_null() );
    PMM_TEST( n0->value.x == 1 && n0->value.y == 2 );
    PMM_TEST( n1->value.x == 3 && n1->value.y == 4 );
    PMM_TEST( n2->value.x == 5 && n2->value.y == 6 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    bool all_passed = true;

    std::cout << "[Issue #186: pvector<T> — persistent vector]\n";

    std::cout << "  I186-A: Basic push_back and at\n";
    PMM_RUN( "    push_back single element", test_i186_push_back_basic );
    PMM_RUN( "    push_back multiple elements", test_i186_push_back_multiple );
    PMM_RUN( "    at returns null for out of range", test_i186_at_out_of_range );

    std::cout << "  I186-B: size()\n";
    PMM_RUN( "    size() returns correct count", test_i186_size );

    std::cout << "  I186-C: empty()\n";
    PMM_RUN( "    empty() returns correct state", test_i186_empty );

    std::cout << "  I186-D: front() and back()\n";
    PMM_RUN( "    front() and back() return correct elements", test_i186_front_back );

    std::cout << "  I186-E: pop_back()\n";
    PMM_RUN( "    pop_back() removes last element", test_i186_pop_back );

    std::cout << "  I186-F: clear()\n";
    PMM_RUN( "    clear() removes all elements", test_i186_clear );

    std::cout << "  I186-G: reset()\n";
    PMM_RUN( "    reset() clears indices for test isolation", test_i186_reset );

    std::cout << "  I186-H: Block locking\n";
    PMM_RUN( "    pvector node block NOT permanently locked", test_i186_node_block_not_permanently_locked );

    std::cout << "  I186-I: Iterator\n";
    PMM_RUN( "    iterator traverses all elements", test_i186_iterator );
    PMM_RUN( "    empty vector iterator", test_i186_iterator_empty );

    std::cout << "  I186-J: pvector layout\n";
    PMM_RUN( "    pvector_node<int> size check", test_i186_layout );

    std::cout << "  I186-K: Mgr::pvector alias\n";
    PMM_RUN( "    Mgr::pvector<T> is same type as pmm::pvector<T,Mgr>", test_i186_alias );

    std::cout << "  I186-L: Stress test\n";
    PMM_RUN( "    100 elements with access by index", test_i186_stress_many_elements );

    std::cout << "  I186-M: Struct type\n";
    PMM_RUN( "    pvector works with struct types", test_i186_struct_type );

    std::cout << "\n";
    if ( all_passed )
    {
        std::cout << "All Issue #186 tests PASSED.\n";
        return EXIT_SUCCESS;
    }
    else
    {
        std::cout << "Some Issue #186 tests FAILED.\n";
        return EXIT_FAILURE;
    }
}
