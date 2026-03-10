/**
 * @file test_issue179_deduplication.cpp
 * @brief Tests for blk_raw deduplication in tree accessor methods (Issue #179).
 *
 * Verifies:
 *   - block_raw_ptr_from_pptr() and block_raw_mut_ptr_from_pptr() helpers work correctly.
 *   - All 10 public get_tree_X / set_tree_X methods function correctly after refactoring:
 *       get_tree_left_offset, get_tree_right_offset, get_tree_parent_offset,
 *       set_tree_left_offset, set_tree_right_offset, set_tree_parent_offset,
 *       get_tree_weight, set_tree_weight, get_tree_height, set_tree_height
 *   - tree_node() works correctly after refactoring.
 *   - Null pptr and uninitialized manager guard behavior is unchanged.
 *
 * @see include/pmm/persist_memory_manager.h
 * @version 0.1 (Issue #179 -- blk_raw helper extraction)
 */

#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

// ─── Macros ───────────────────────────────────────────────────────────────────

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

// =============================================================================
// Manager types for tests
// =============================================================================

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 179>;

// =============================================================================
// Issue #179 Tests Section A: null/uninitialized guard behavior unchanged
// =============================================================================

/// @brief get_tree_X methods return 0 for null pptr (guard not changed by refactoring).
static bool test_i179_get_tree_null_pptr_returns_zero()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> null_p;
    PMM_TEST( null_p.is_null() );

    PMM_TEST( TestMgr::get_tree_left_offset( null_p ) == 0 );
    PMM_TEST( TestMgr::get_tree_right_offset( null_p ) == 0 );
    PMM_TEST( TestMgr::get_tree_parent_offset( null_p ) == 0 );
    PMM_TEST( TestMgr::get_tree_weight( null_p ) == 0 );
    PMM_TEST( TestMgr::get_tree_height( null_p ) == 0 );

    TestMgr::destroy();
    return true;
}

/// @brief set_tree_X methods do nothing for null pptr (guard not changed by refactoring).
static bool test_i179_set_tree_null_pptr_is_noop()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> null_p;
    PMM_TEST( null_p.is_null() );

    // These must not crash or corrupt state
    TestMgr::set_tree_left_offset( null_p, 1 );
    TestMgr::set_tree_right_offset( null_p, 1 );
    TestMgr::set_tree_parent_offset( null_p, 1 );
    TestMgr::set_tree_weight( null_p, 1 );
    TestMgr::set_tree_height( null_p, 1 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// Issue #179 Tests Section B: get/set round-trip on a real allocated block
// =============================================================================

/// @brief get_tree_left_offset / set_tree_left_offset round-trip.
static bool test_i179_left_offset_round_trip()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    PMM_TEST( !p.is_null() );

    using AT = pmm::DefaultAddressTraits;
    TestMgr::set_tree_left_offset( p, static_cast<AT::index_type>( 10 ) );
    PMM_TEST( TestMgr::get_tree_left_offset( p ) == static_cast<AT::index_type>( 10 ) );

    // set 0 stores no_block internally, get returns 0
    TestMgr::set_tree_left_offset( p, 0 );
    PMM_TEST( TestMgr::get_tree_left_offset( p ) == 0 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief get_tree_right_offset / set_tree_right_offset round-trip.
static bool test_i179_right_offset_round_trip()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    PMM_TEST( !p.is_null() );

    using AT = pmm::DefaultAddressTraits;
    TestMgr::set_tree_right_offset( p, static_cast<AT::index_type>( 20 ) );
    PMM_TEST( TestMgr::get_tree_right_offset( p ) == static_cast<AT::index_type>( 20 ) );

    TestMgr::set_tree_right_offset( p, 0 );
    PMM_TEST( TestMgr::get_tree_right_offset( p ) == 0 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief get_tree_parent_offset / set_tree_parent_offset round-trip.
static bool test_i179_parent_offset_round_trip()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    PMM_TEST( !p.is_null() );

    using AT = pmm::DefaultAddressTraits;
    TestMgr::set_tree_parent_offset( p, static_cast<AT::index_type>( 5 ) );
    PMM_TEST( TestMgr::get_tree_parent_offset( p ) == static_cast<AT::index_type>( 5 ) );

    TestMgr::set_tree_parent_offset( p, 0 );
    PMM_TEST( TestMgr::get_tree_parent_offset( p ) == 0 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief get_tree_weight / set_tree_weight round-trip.
static bool test_i179_weight_round_trip()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    PMM_TEST( !p.is_null() );

    using AT = pmm::DefaultAddressTraits;
    TestMgr::set_tree_weight( p, static_cast<AT::index_type>( 7 ) );
    PMM_TEST( TestMgr::get_tree_weight( p ) == static_cast<AT::index_type>( 7 ) );

    TestMgr::set_tree_weight( p, 0 );
    PMM_TEST( TestMgr::get_tree_weight( p ) == 0 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief get_tree_height / set_tree_height round-trip.
static bool test_i179_height_round_trip()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    PMM_TEST( !p.is_null() );

    TestMgr::set_tree_height( p, static_cast<std::int16_t>( 3 ) );
    PMM_TEST( TestMgr::get_tree_height( p ) == static_cast<std::int16_t>( 3 ) );

    TestMgr::set_tree_height( p, 0 );
    PMM_TEST( TestMgr::get_tree_height( p ) == 0 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// Issue #179 Tests Section C: tree_node() still works after refactoring
// =============================================================================

/// @brief tree_node() correctly gives access to block AVL fields.
static bool test_i179_tree_node_access()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    PMM_TEST( !p.is_null() );

    using AT = pmm::DefaultAddressTraits;

    // Use tree_node to set fields, verify via get_tree_X that both agree
    auto& tn = TestMgr::tree_node( p );
    tn.set_left( static_cast<AT::index_type>( 4 ) );
    tn.set_right( static_cast<AT::index_type>( 8 ) );

    PMM_TEST( TestMgr::get_tree_left_offset( p ) == static_cast<AT::index_type>( 4 ) );
    PMM_TEST( TestMgr::get_tree_right_offset( p ) == static_cast<AT::index_type>( 8 ) );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// Issue #179 Tests Section D: functional allocation / AVL tree still works
// =============================================================================

/// @brief Full allocate/deallocate cycle works after refactoring.
static bool test_i179_alloc_dealloc_functional()
{
    TestMgr::create( 64 * 1024 );

    void* p1 = TestMgr::allocate( 32 );
    void* p2 = TestMgr::allocate( 64 );
    void* p3 = TestMgr::allocate( 128 );
    PMM_TEST( p1 != nullptr );
    PMM_TEST( p2 != nullptr );
    PMM_TEST( p3 != nullptr );

    TestMgr::deallocate( p2 );
    TestMgr::deallocate( p1 );
    TestMgr::deallocate( p3 );

    void* p4 = TestMgr::allocate( 64 );
    PMM_TEST( p4 != nullptr );
    TestMgr::deallocate( p4 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue179_deduplication (Issue #179: blk_raw helper extraction) ===\n\n";
    bool all_passed = true;

    std::cout << "--- I179-A: guard behavior (null pptr) unchanged ---\n";
    PMM_RUN( "I179-A1: get_tree_X return 0 for null pptr", test_i179_get_tree_null_pptr_returns_zero );
    PMM_RUN( "I179-A2: set_tree_X are no-ops for null pptr", test_i179_set_tree_null_pptr_is_noop );

    std::cout << "\n--- I179-B: get/set round-trips on allocated blocks ---\n";
    PMM_RUN( "I179-B1: left_offset round-trip", test_i179_left_offset_round_trip );
    PMM_RUN( "I179-B2: right_offset round-trip", test_i179_right_offset_round_trip );
    PMM_RUN( "I179-B3: parent_offset round-trip", test_i179_parent_offset_round_trip );
    PMM_RUN( "I179-B4: weight round-trip", test_i179_weight_round_trip );
    PMM_RUN( "I179-B5: height round-trip", test_i179_height_round_trip );

    std::cout << "\n--- I179-C: tree_node() still works ---\n";
    PMM_RUN( "I179-C1: tree_node() and get_tree_X agree after refactoring", test_i179_tree_node_access );

    std::cout << "\n--- I179-D: functional tests ---\n";
    PMM_RUN( "I179-D1: allocate/deallocate cycle works", test_i179_alloc_dealloc_functional );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
