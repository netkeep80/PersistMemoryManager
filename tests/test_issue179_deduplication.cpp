/**
 * @file test_issue179_deduplication.cpp
 * @brief Tests for code-deduplication refactoring in Issue #179.
 *
 * Verifies:
 *   - block_raw_ptr_from_pptr() and block_raw_mut_ptr_from_pptr() helpers work correctly.
 *   - All 10 public get_tree_X / set_tree_X methods function correctly after refactoring:
 *       get_tree_left_offset, get_tree_right_offset, get_tree_parent_offset,
 *       set_tree_left_offset, set_tree_right_offset, set_tree_parent_offset,
 *       get_tree_weight, set_tree_weight, get_tree_height, set_tree_height
 *   - tree_node() works correctly after refactoring.
 *   - Null pptr and uninitialized manager guard behavior is unchanged.
 *   - make_pptr_from_raw() helper: allocate_typed, allocate_typed(count), and
 *     create_typed return pptr<T> that resolves to the allocated data (Issue #179).
 *   - find_block_from_user_ptr() helper: lock_block_permanent and is_permanently_locked
 *     work correctly via the new shared prologue helper (Issue #179).
 *
 * @see include/pmm/persist_memory_manager.h
 * @version 0.2 (Issue #179 -- make_pptr_from_raw and find_block_from_user_ptr helpers)
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
// Issue #179 Tests Section E: make_pptr_from_raw helper (via typed allocation API)
// =============================================================================

/// @brief allocate_typed<T>() pptr resolves to the same address as the raw allocation.
static bool test_i179_allocate_typed_pptr_resolves_correctly()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    int* resolved = TestMgr::resolve( p );
    PMM_TEST( resolved != nullptr );

    // Write through raw pointer obtained via resolve, read back via resolve to confirm.
    *resolved = 42;
    PMM_TEST( *TestMgr::resolve( p ) == 42 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief allocate_typed<T>(count) pptr resolves to the correct base address.
static bool test_i179_allocate_typed_count_pptr_resolves_correctly()
{
    TestMgr::create( 64 * 1024 );

    const std::size_t  count = 4;
    TestMgr::pptr<int> p     = TestMgr::allocate_typed<int>( count );
    PMM_TEST( !p.is_null() );

    int* arr = TestMgr::resolve( p );
    PMM_TEST( arr != nullptr );

    for ( std::size_t i = 0; i < count; ++i )
        arr[i] = static_cast<int>( i * 10 );

    for ( std::size_t i = 0; i < count; ++i )
        PMM_TEST( TestMgr::resolve_at( p, i ) != nullptr &&
                  *TestMgr::resolve_at( p, i ) == static_cast<int>( i * 10 ) );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief create_typed<T>(args...) pptr resolves and the object was constructed.
static bool test_i179_create_typed_pptr_resolves_with_constructed_value()
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::create_typed<int>( 99 );
    PMM_TEST( !p.is_null() );

    int* resolved = TestMgr::resolve( p );
    PMM_TEST( resolved != nullptr );
    PMM_TEST( *resolved == 99 );

    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// Issue #179 Tests Section F: find_block_from_user_ptr helper (via lock API)
// =============================================================================

/// @brief lock_block_permanent and is_permanently_locked work via find_block_from_user_ptr.
static bool test_i179_lock_permanent_and_query()
{
    TestMgr::create( 64 * 1024 );

    void* ptr = TestMgr::allocate( 32 );
    PMM_TEST( ptr != nullptr );

    // Initially not locked
    PMM_TEST( !TestMgr::is_permanently_locked( ptr ) );

    // Lock it
    PMM_TEST( TestMgr::lock_block_permanent( ptr ) );

    // Now it is locked
    PMM_TEST( TestMgr::is_permanently_locked( ptr ) );

    // deallocate must be a no-op for permanently locked block
    TestMgr::deallocate( ptr );
    PMM_TEST( TestMgr::is_permanently_locked( ptr ) );

    TestMgr::destroy();
    return true;
}

/// @brief find_block_from_user_ptr: null and out-of-range pointers return safely.
static bool test_i179_find_block_null_and_invalid_ptr()
{
    TestMgr::create( 64 * 1024 );

    // Null ptr
    PMM_TEST( !TestMgr::is_permanently_locked( nullptr ) );
    PMM_TEST( !TestMgr::lock_block_permanent( nullptr ) );

    // Out-of-range ptr: should return false/nullptr, not crash
    void* bad_ptr = reinterpret_cast<void*>( static_cast<std::uintptr_t>( 1 ) );
    PMM_TEST( !TestMgr::is_permanently_locked( bad_ptr ) );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue179_deduplication (Issue #179: code deduplication helpers) ===\n\n";
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

    std::cout << "\n--- I179-E: make_pptr_from_raw helper (via typed allocation API) ---\n";
    PMM_RUN( "I179-E1: allocate_typed<T>() pptr resolves correctly", test_i179_allocate_typed_pptr_resolves_correctly );
    PMM_RUN( "I179-E2: allocate_typed<T>(count) pptr resolves correctly",
             test_i179_allocate_typed_count_pptr_resolves_correctly );
    PMM_RUN( "I179-E3: create_typed<T>(args) resolves and constructs value",
             test_i179_create_typed_pptr_resolves_with_constructed_value );

    std::cout << "\n--- I179-F: find_block_from_user_ptr helper (via lock API) ---\n";
    PMM_RUN( "I179-F1: lock_block_permanent and is_permanently_locked via helper", test_i179_lock_permanent_and_query );
    PMM_RUN( "I179-F2: null and invalid ptrs handled safely", test_i179_find_block_null_and_invalid_ptr );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
