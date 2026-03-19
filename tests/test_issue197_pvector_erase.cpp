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

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 197>;

// =============================================================================
// I197-A: erase on empty vector
// =============================================================================

/// @brief erase(0) on empty vector returns false.
static bool test_i197_erase_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    PMM_TEST( vec.erase( 0 ) == false );
    PMM_TEST( vec.erase( 1 ) == false );
    PMM_TEST( vec.erase( 100 ) == false );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-B: erase out of range
// =============================================================================

/// @brief erase(index) returns false for index >= size().
static bool test_i197_erase_out_of_range()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    PMM_TEST( vec.erase( 3 ) == false );
    PMM_TEST( vec.erase( 100 ) == false );
    PMM_TEST( vec.size() == 3 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-C: erase first element
// =============================================================================

/// @brief erase(0) removes the first element.
static bool test_i197_erase_first()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    PMM_TEST( vec.erase( 0 ) == true );
    PMM_TEST( vec.size() == 2 );
    PMM_TEST( vec.at( 0 )->value == 20 );
    PMM_TEST( vec.at( 1 )->value == 30 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-D: erase last element
// =============================================================================

/// @brief erase(size()-1) removes the last element.
static bool test_i197_erase_last()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    PMM_TEST( vec.erase( 2 ) == true );
    PMM_TEST( vec.size() == 2 );
    PMM_TEST( vec.at( 0 )->value == 10 );
    PMM_TEST( vec.at( 1 )->value == 20 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-E: erase middle element
// =============================================================================

/// @brief erase(1) removes the middle element, preserving order.
static bool test_i197_erase_middle()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    PMM_TEST( vec.erase( 1 ) == true );
    PMM_TEST( vec.size() == 2 );
    PMM_TEST( vec.at( 0 )->value == 10 );
    PMM_TEST( vec.at( 1 )->value == 30 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-F: erase single element
// =============================================================================

/// @brief Erase the only element makes vector empty.
static bool test_i197_erase_single()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 42 );

    PMM_TEST( vec.erase( 0 ) == true );
    PMM_TEST( vec.size() == 0 );
    PMM_TEST( vec.empty() );
    PMM_TEST( vec.front().is_null() );
    PMM_TEST( vec.back().is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-G: erase preserves order (sequential erase)
// =============================================================================

/// @brief Multiple erases preserve element order.
static bool test_i197_erase_sequential()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    for ( int i = 0; i < 5; i++ )
        vec.push_back( i * 10 ); // [0, 10, 20, 30, 40]

    // Erase index 2 (value 20): [0, 10, 30, 40]
    PMM_TEST( vec.erase( 2 ) == true );
    PMM_TEST( vec.size() == 4 );
    PMM_TEST( vec.at( 0 )->value == 0 );
    PMM_TEST( vec.at( 1 )->value == 10 );
    PMM_TEST( vec.at( 2 )->value == 30 );
    PMM_TEST( vec.at( 3 )->value == 40 );

    // Erase index 0 (value 0): [10, 30, 40]
    PMM_TEST( vec.erase( 0 ) == true );
    PMM_TEST( vec.size() == 3 );
    PMM_TEST( vec.at( 0 )->value == 10 );
    PMM_TEST( vec.at( 1 )->value == 30 );
    PMM_TEST( vec.at( 2 )->value == 40 );

    // Erase index 2 (value 40): [10, 30]
    PMM_TEST( vec.erase( 2 ) == true );
    PMM_TEST( vec.size() == 2 );
    PMM_TEST( vec.at( 0 )->value == 10 );
    PMM_TEST( vec.at( 1 )->value == 30 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-H: size decreases after erase
// =============================================================================

/// @brief size() decreases by 1 after each erase.
static bool test_i197_size_after_erase()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    for ( int i = 0; i < 5; i++ )
        vec.push_back( i );

    PMM_TEST( vec.size() == 5 );
    vec.erase( 0 );
    PMM_TEST( vec.size() == 4 );
    vec.erase( 0 );
    PMM_TEST( vec.size() == 3 );
    vec.erase( 0 );
    PMM_TEST( vec.size() == 2 );
    vec.erase( 0 );
    PMM_TEST( vec.size() == 1 );
    vec.erase( 0 );
    PMM_TEST( vec.size() == 0 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-I: iterator works after erase
// =============================================================================

/// @brief Iterator traverses remaining elements in correct order after erase.
static bool test_i197_iterator_after_erase()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

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
        PMM_TEST( !p.is_null() );
        values.push_back( p->value );
    }

    PMM_TEST( values.size() == 4 );
    PMM_TEST( values[0] == 10 );
    PMM_TEST( values[1] == 20 );
    PMM_TEST( values[2] == 40 );
    PMM_TEST( values[3] == 50 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-J: push_back after erase
// =============================================================================

/// @brief push_back works correctly after erase.
static bool test_i197_push_back_after_erase()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    // Erase middle
    vec.erase( 1 ); // [10, 30]

    // Push new element
    vec.push_back( 99 ); // [10, 30, 99]

    PMM_TEST( vec.size() == 3 );
    PMM_TEST( vec.at( 0 )->value == 10 );
    PMM_TEST( vec.at( 1 )->value == 30 );
    PMM_TEST( vec.at( 2 )->value == 99 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-K: memory deallocated after erase
// =============================================================================

/// @brief free_size increases after erase (memory is reclaimed).
static bool test_i197_memory_reclaimed()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    auto free_before = TestMgr::free_size();

    vec.erase( 1 );

    auto free_after = TestMgr::free_size();
    PMM_TEST( free_after > free_before );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-L: front() and back() after erase
// =============================================================================

/// @brief front() and back() return correct elements after erase.
static bool test_i197_front_back_after_erase()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pvector<int> vec;
    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );

    // Erase first
    vec.erase( 0 );
    PMM_TEST( vec.front()->value == 20 );
    PMM_TEST( vec.back()->value == 30 );

    // Erase last
    vec.erase( 1 );
    PMM_TEST( vec.front()->value == 20 );
    PMM_TEST( vec.back()->value == 20 );

    TestMgr::destroy();
    return true;
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
static bool test_i197_avl_structure_after_erase()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;
    const int             N = 50;

    for ( int i = 0; i < N; i++ )
        vec.push_back( i * 10 );

    PMM_TEST( static_cast<int>( vec.size() ) == N );

    // Erase every other element from the middle outward
    vec.erase( 25 );             // erase middle
    vec.erase( 10 );             // erase from left area
    vec.erase( 30 );             // erase from right area
    vec.erase( 0 );              // erase first
    vec.erase( vec.size() - 1 ); // erase last

    int remaining = N - 5;
    PMM_TEST( static_cast<int>( vec.size() ) == remaining );

    // Verify AVL invariants
    TestMgr::pvector<int>::node_pptr root( vec._root_idx );
    int                              total = verify_avl_node( root );
    PMM_TEST( total == remaining );

    // Verify all elements are still accessible by index
    for ( int i = 0; i < remaining; i++ )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        PMM_TEST( !p.is_null() );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-N: Stress test — erase all elements one by one from front
// =============================================================================

/// @brief Erase all elements from front, verifying structure at each step.
static bool test_i197_stress_erase_from_front()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;
    const int             N = 100;

    for ( int i = 0; i < N; i++ )
        vec.push_back( i );

    for ( int i = 0; i < N; i++ )
    {
        PMM_TEST( vec.at( 0 )->value == i );
        PMM_TEST( vec.erase( 0 ) == true );
        PMM_TEST( static_cast<int>( vec.size() ) == N - 1 - i );
    }

    PMM_TEST( vec.empty() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-O: Stress test — erase all elements from back via erase (not pop_back)
// =============================================================================

/// @brief Erase all elements from back using erase(size()-1).
static bool test_i197_stress_erase_from_back()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;
    const int             N = 100;

    for ( int i = 0; i < N; i++ )
        vec.push_back( i );

    for ( int i = N - 1; i >= 0; i-- )
    {
        PMM_TEST( vec.at( static_cast<std::size_t>( i ) )->value == i );
        PMM_TEST( vec.erase( static_cast<std::size_t>( i ) ) == true );
        PMM_TEST( static_cast<int>( vec.size() ) == i );
    }

    PMM_TEST( vec.empty() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-P: Stress test — erase random-order elements, verify AVL at end
// =============================================================================

/// @brief Insert 100 elements, erase 50, verify AVL structure.
static bool test_i197_stress_erase_mixed()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    TestMgr::pvector<int> vec;
    const int             N = 100;

    for ( int i = 0; i < N; i++ )
        vec.push_back( i * 10 );

    // Erase every other element by always erasing from even positions
    // After each erase, the indices shift, so we erase at 0, then 1, etc.
    int erased = 0;
    for ( int i = 0; i < 50; i++ )
    {
        PMM_TEST( vec.erase( static_cast<std::size_t>( i ) ) == true );
        erased++;
    }

    int remaining = N - erased;
    PMM_TEST( static_cast<int>( vec.size() ) == remaining );

    // Verify AVL invariants
    TestMgr::pvector<int>::node_pptr root( vec._root_idx );
    int                              total = verify_avl_node( root );
    PMM_TEST( total == remaining );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I197-Q: erase + push_back interleaved
// =============================================================================

/// @brief Interleaved erase and push_back operations.
static bool test_i197_interleaved_erase_push()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

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

    PMM_TEST( vec.size() == 3 );
    PMM_TEST( vec.at( 0 )->value == 3 );
    PMM_TEST( vec.at( 1 )->value == 4 );
    PMM_TEST( vec.at( 2 )->value == 6 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    bool all_passed = true;

    std::cout << "[Issue #197: pvector::erase(index) — removal by index (Phase 3.4)]\n";

    std::cout << "  I197-A: erase on empty vector\n";
    PMM_RUN( "    erase returns false for empty vector", test_i197_erase_empty );

    std::cout << "  I197-B: erase out of range\n";
    PMM_RUN( "    erase returns false for index >= size()", test_i197_erase_out_of_range );

    std::cout << "  I197-C: erase first element\n";
    PMM_RUN( "    erase(0) removes first element", test_i197_erase_first );

    std::cout << "  I197-D: erase last element\n";
    PMM_RUN( "    erase(size()-1) removes last element", test_i197_erase_last );

    std::cout << "  I197-E: erase middle element\n";
    PMM_RUN( "    erase(1) removes middle element", test_i197_erase_middle );

    std::cout << "  I197-F: erase single element\n";
    PMM_RUN( "    erase only element makes vector empty", test_i197_erase_single );

    std::cout << "  I197-G: sequential erase preserves order\n";
    PMM_RUN( "    multiple erases preserve element order", test_i197_erase_sequential );

    std::cout << "  I197-H: size after erase\n";
    PMM_RUN( "    size() decreases by 1 after each erase", test_i197_size_after_erase );

    std::cout << "  I197-I: iterator after erase\n";
    PMM_RUN( "    iterator traverses correctly after erase", test_i197_iterator_after_erase );

    std::cout << "  I197-J: push_back after erase\n";
    PMM_RUN( "    push_back works after erase", test_i197_push_back_after_erase );

    std::cout << "  I197-K: memory reclaimed after erase\n";
    PMM_RUN( "    free_size increases after erase", test_i197_memory_reclaimed );

    std::cout << "  I197-L: front/back after erase\n";
    PMM_RUN( "    front() and back() correct after erase", test_i197_front_back_after_erase );

    std::cout << "  I197-M: AVL tree structure after erase\n";
    PMM_RUN( "    AVL invariants maintained after erase", test_i197_avl_structure_after_erase );

    std::cout << "  I197-N: Stress erase from front\n";
    PMM_RUN( "    erase all 100 elements from front", test_i197_stress_erase_from_front );

    std::cout << "  I197-O: Stress erase from back\n";
    PMM_RUN( "    erase all 100 elements from back", test_i197_stress_erase_from_back );

    std::cout << "  I197-P: Stress erase mixed\n";
    PMM_RUN( "    insert 100, erase 50, verify AVL", test_i197_stress_erase_mixed );

    std::cout << "  I197-Q: Interleaved erase + push_back\n";
    PMM_RUN( "    interleaved erase and push_back", test_i197_interleaved_erase_push );

    std::cout << "\n";
    if ( all_passed )
    {
        std::cout << "All Issue #197 tests PASSED.\n";
        return EXIT_SUCCESS;
    }
    else
    {
        std::cout << "Some Issue #197 tests FAILED.\n";
        return EXIT_FAILURE;
    }
}
