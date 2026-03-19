/**
 * @file test_issue197_pvector_erase.cpp
 * @brief Tests for pvector::erase(index) — removal by index (Issue #197, Phase 3.4).
 *
 * Verifies the key requirements from Issue #197:
 *  1. erase(index) removes element at given position in O(log n).
 *  2. erase() returns false for empty vector or out-of-range index.
 *  3. Remaining elements preserve their relative order after erase.
 *  4. AVL tree remains balanced with correct weight fields after erase.
 *  5. Memory is deallocated after erase (free_size increases).
 *  6. Iterator works correctly after erase.
 *  7. push_back works correctly after erase.
 *
 * @see include/pmm/pvector.h — pvector<T,ManagerT>
 * @see include/pmm/avl_tree_mixin.h — detail::avl_remove()
 * @version 0.1 (Issue #197 — pvector::erase(index))
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

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 197>;

// =============================================================================
// I197-A: erase on empty vector
// =============================================================================

/// @brief erase(0) on empty vector returns false.
TEST_CASE( "    erase returns false for empty vector", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    REQUIRE( vec.erase( 0 ) == false );
    REQUIRE( vec.erase( 1 ) == false );
    REQUIRE( vec.erase( 100 ) == false );

    TestMgr::destroy();
}

// =============================================================================
// I197-B: erase out of range
// =============================================================================

/// @brief erase(index) returns false for index >= size().
TEST_CASE( "    erase returns false for index >= size()", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    REQUIRE( vec.erase( 3 ) == false );
    REQUIRE( vec.erase( 100 ) == false );
    REQUIRE( vec.size() == 3 );

    TestMgr::destroy();
}

// =============================================================================
// I197-C: erase first element
// =============================================================================

/// @brief erase(0) removes the first element.
TEST_CASE( "    erase(0) removes first element", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    REQUIRE( vec.erase( 0 ) == true );
    REQUIRE( vec.size() == 2 );
    REQUIRE( vec.at( 0 )->value == 20 );
    REQUIRE( vec.at( 1 )->value == 30 );

    TestMgr::destroy();
}

// =============================================================================
// I197-D: erase last element
// =============================================================================

/// @brief erase(size()-1) removes the last element.
TEST_CASE( "    erase(size()-1) removes last element", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    REQUIRE( vec.erase( 2 ) == true );
    REQUIRE( vec.size() == 2 );
    REQUIRE( vec.at( 0 )->value == 10 );
    REQUIRE( vec.at( 1 )->value == 20 );

    TestMgr::destroy();
}

// =============================================================================
// I197-E: erase middle element
// =============================================================================

/// @brief erase(1) removes the middle element, preserving order.
TEST_CASE( "    erase(1) removes middle element", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    REQUIRE( vec.erase( 1 ) == true );
    REQUIRE( vec.size() == 2 );
    REQUIRE( vec.at( 0 )->value == 10 );
    REQUIRE( vec.at( 1 )->value == 30 );

    TestMgr::destroy();
}

// =============================================================================
// I197-F: erase single element
// =============================================================================

/// @brief Erase the only element makes vector empty.
TEST_CASE( "    erase only element makes vector empty", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 42 );

    REQUIRE( vec.erase( 0 ) == true );
    REQUIRE( vec.size() == 0 );
    REQUIRE( vec.empty() );
    REQUIRE( vec.front().is_null() );
    REQUIRE( vec.back().is_null() );

    TestMgr::destroy();
}

// =============================================================================
// I197-G: erase preserves order (sequential erase)
// =============================================================================

/// @brief Multiple erases preserve element order.
TEST_CASE( "    multiple erases preserve element order", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    for ( int i = 0; i < 5; i++ )
        vec.push_back( i * 10 ); // [0, 10, 20, 30, 40]

    // Erase index 2 (value 20): [0, 10, 30, 40]
    REQUIRE( vec.erase( 2 ) == true );
    REQUIRE( vec.size() == 4 );
    REQUIRE( vec.at( 0 )->value == 0 );
    REQUIRE( vec.at( 1 )->value == 10 );
    REQUIRE( vec.at( 2 )->value == 30 );
    REQUIRE( vec.at( 3 )->value == 40 );

    // Erase index 0 (value 0): [10, 30, 40]
    REQUIRE( vec.erase( 0 ) == true );
    REQUIRE( vec.size() == 3 );
    REQUIRE( vec.at( 0 )->value == 10 );
    REQUIRE( vec.at( 1 )->value == 30 );
    REQUIRE( vec.at( 2 )->value == 40 );

    // Erase index 2 (value 40): [10, 30]
    REQUIRE( vec.erase( 2 ) == true );
    REQUIRE( vec.size() == 2 );
    REQUIRE( vec.at( 0 )->value == 10 );
    REQUIRE( vec.at( 1 )->value == 30 );

    TestMgr::destroy();
}

// =============================================================================
// I197-H: size decreases after erase
// =============================================================================

/// @brief size() decreases by 1 after each erase.
TEST_CASE( "    size() decreases by 1 after each erase", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    for ( int i = 0; i < 5; i++ )
        vec.push_back( i );

    REQUIRE( vec.size() == 5 );
    vec.erase( 0 );
    REQUIRE( vec.size() == 4 );
    vec.erase( 0 );
    REQUIRE( vec.size() == 3 );
    vec.erase( 0 );
    REQUIRE( vec.size() == 2 );
    vec.erase( 0 );
    REQUIRE( vec.size() == 1 );
    vec.erase( 0 );
    REQUIRE( vec.size() == 0 );

    TestMgr::destroy();
}

// =============================================================================
// I197-I: iterator works after erase
// =============================================================================

/// @brief Iterator traverses remaining elements in correct order after erase.
TEST_CASE( "    iterator traverses correctly after erase", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );
    vec.push_back( 40 );
    vec.push_back( 50 );

    // Erase middle element (30)
    vec.erase( 2 );

    std::vector<int> values;
    for ( auto it = vec.begin(); it != vec.end(); ++it )
    {
        auto p = *it;
        REQUIRE( !p.is_null() );
        values.push_back( p->value );
    }

    REQUIRE( values.size() == 4 );
    REQUIRE( values[0] == 10 );
    REQUIRE( values[1] == 20 );
    REQUIRE( values[2] == 40 );
    REQUIRE( values[3] == 50 );

    TestMgr::destroy();
}

// =============================================================================
// I197-J: push_back after erase
// =============================================================================

/// @brief push_back works correctly after erase.
TEST_CASE( "    push_back works after erase", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    // Erase middle
    vec.erase( 1 ); // [10, 30]

    // Push new element
    vec.push_back( 99 ); // [10, 30, 99]

    REQUIRE( vec.size() == 3 );
    REQUIRE( vec.at( 0 )->value == 10 );
    REQUIRE( vec.at( 1 )->value == 30 );
    REQUIRE( vec.at( 2 )->value == 99 );

    TestMgr::destroy();
}

// =============================================================================
// I197-K: memory deallocated after erase
// =============================================================================

/// @brief free_size increases after erase (memory is reclaimed).
TEST_CASE( "    free_size increases after erase", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    auto free_before = TestMgr::free_size();

    vec.erase( 1 );

    auto free_after = TestMgr::free_size();
    REQUIRE( free_after > free_before );

    TestMgr::destroy();
}

// =============================================================================
// I197-L: front() and back() after erase
// =============================================================================

/// @brief front() and back() return correct elements after erase.
TEST_CASE( "    front() and back() correct after erase", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    // Erase first
    vec.erase( 0 );
    REQUIRE( vec.front()->value == 20 );
    REQUIRE( vec.back()->value == 30 );

    // Erase last
    vec.erase( 1 );
    REQUIRE( vec.front()->value == 20 );
    REQUIRE( vec.back()->value == 20 );

    TestMgr::destroy();
}

// =============================================================================
// I197-M: AVL tree invariants after erase
// =============================================================================

/// @brief Recursively verify AVL tree invariants: weight == subtree size and |bf| <= 1.
static int verify_avl_node( TestMgr::pvector<int>::node_pptr p )
{
    if ( p.is_null() )
        return 0;

    using index_type                     = TestMgr::index_type;
    static constexpr index_type no_block = TestMgr::address_traits::no_block;

    auto& tn        = p.tree_node();
    auto  left_idx  = tn.get_left();
    auto  right_idx = tn.get_right();

    int left_size  = ( left_idx != no_block ) ? verify_avl_node( TestMgr::pvector<int>::node_pptr( left_idx ) ) : 0;
    int right_size = ( right_idx != no_block ) ? verify_avl_node( TestMgr::pvector<int>::node_pptr( right_idx ) ) : 0;

    if ( left_size < 0 || right_size < 0 )
        return -1;

    int expected_weight = 1 + left_size + right_size;
    if ( static_cast<int>( tn.get_weight() ) != expected_weight )
        return -1;

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

/// @brief AVL tree structure remains valid after erasing elements.
TEST_CASE( "    AVL invariants maintained after erase", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;
    const int             N = 50;

    for ( int i = 0; i < N; i++ )
        vec.push_back( i * 10 );

    REQUIRE( static_cast<int>( vec.size() ) == N );

    // Erase every other element from the middle outward
    vec.erase( 25 );             // erase middle
    vec.erase( 10 );             // erase from left area
    vec.erase( 30 );             // erase from right area
    vec.erase( 0 );              // erase first
    vec.erase( vec.size() - 1 ); // erase last

    int remaining = N - 5;
    REQUIRE( static_cast<int>( vec.size() ) == remaining );

    // Verify AVL invariants
    TestMgr::pvector<int>::node_pptr root( vec._root_idx );
    int                              total = verify_avl_node( root );
    REQUIRE( total == remaining );

    // Verify all elements are still accessible by index
    for ( int i = 0; i < remaining; i++ )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        REQUIRE( !p.is_null() );
    }

    TestMgr::destroy();
}

// =============================================================================
// I197-N: Stress test — erase all elements one by one from front
// =============================================================================

/// @brief Erase all elements from front, verifying structure at each step.
TEST_CASE( "    erase all 100 elements from front", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;
    const int             N = 100;

    for ( int i = 0; i < N; i++ )
        vec.push_back( i );

    for ( int i = 0; i < N; i++ )
    {
        REQUIRE( vec.at( 0 )->value == i );
        REQUIRE( vec.erase( 0 ) == true );
        REQUIRE( static_cast<int>( vec.size() ) == N - 1 - i );
    }

    REQUIRE( vec.empty() );

    TestMgr::destroy();
}

// =============================================================================
// I197-O: Stress test — erase all elements from back via erase (not pop_back)
// =============================================================================

/// @brief Erase all elements from back using erase(size()-1).
TEST_CASE( "    erase all 100 elements from back", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;
    const int             N = 100;

    for ( int i = 0; i < N; i++ )
        vec.push_back( i );

    for ( int i = N - 1; i >= 0; i-- )
    {
        REQUIRE( vec.at( static_cast<std::size_t>( i ) )->value == i );
        REQUIRE( vec.erase( static_cast<std::size_t>( i ) ) == true );
        REQUIRE( static_cast<int>( vec.size() ) == i );
    }

    REQUIRE( vec.empty() );

    TestMgr::destroy();
}

// =============================================================================
// I197-P: Stress test — erase random-order elements, verify AVL at end
// =============================================================================

/// @brief Insert 100 elements, erase 50, verify AVL structure.
TEST_CASE( "    insert 100, erase 50, verify AVL", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;
    const int             N = 100;

    for ( int i = 0; i < N; i++ )
        vec.push_back( i * 10 );

    // Erase every other element by always erasing from even positions
    // After each erase, the indices shift, so we erase at 0, then 1, etc.
    int erased = 0;
    for ( int i = 0; i < 50; i++ )
    {
        REQUIRE( vec.erase( static_cast<std::size_t>( i ) ) == true );
        erased++;
    }

    int remaining = N - erased;
    REQUIRE( static_cast<int>( vec.size() ) == remaining );

    // Verify AVL invariants
    TestMgr::pvector<int>::node_pptr root( vec._root_idx );
    int                              total = verify_avl_node( root );
    REQUIRE( total == remaining );

    TestMgr::destroy();
}

// =============================================================================
// I197-Q: erase + push_back interleaved
// =============================================================================

/// @brief Interleaved erase and push_back operations.
TEST_CASE( "    interleaved erase and push_back", "[test_issue197_pvector_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 1 );
    vec.push_back( 2 );
    vec.push_back( 3 );

    // Erase middle, push new
    vec.erase( 1 );     // [1, 3]
    vec.push_back( 4 ); // [1, 3, 4]
    vec.erase( 0 );     // [3, 4]
    vec.push_back( 5 ); // [3, 4, 5]
    vec.push_back( 6 ); // [3, 4, 5, 6]
    vec.erase( 2 );     // [3, 4, 6]

    REQUIRE( vec.size() == 3 );
    REQUIRE( vec.at( 0 )->value == 3 );
    REQUIRE( vec.at( 1 )->value == 4 );
    REQUIRE( vec.at( 2 )->value == 6 );

    TestMgr::destroy();
}

// =============================================================================
// main
// =============================================================================
