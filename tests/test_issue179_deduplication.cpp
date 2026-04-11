/**
 * @file test_issue179_deduplication.cpp
 * @brief Tests for code-deduplication refactoring.
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
 *     create_typed return pptr<T> that resolves to the allocated data.
 *   - find_block_from_user_ptr() helper: lock_block_permanent and is_permanently_locked
 *     work correctly via the new shared prologue helper.
 *
 * @see include/pmm/persist_memory_manager.h
 * @version 0.2
 */

#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

// ─── Macros ───────────────────────────────────────────────────────────────────

// =============================================================================
// Manager types for tests
// =============================================================================

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 179>;

// =============================================================================
// Null/uninitialized guard behavior unchanged
// =============================================================================

/// @brief get_tree_X methods return 0 for null pptr (guard not changed by refactoring).
TEST_CASE( "I179-A1: get_tree_X return 0 for null pptr", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> null_p;
    REQUIRE( null_p.is_null() );

    REQUIRE( TestMgr::get_tree_left_offset( null_p ) == 0 );
    REQUIRE( TestMgr::get_tree_right_offset( null_p ) == 0 );
    REQUIRE( TestMgr::get_tree_parent_offset( null_p ) == 0 );
    REQUIRE( TestMgr::get_tree_weight( null_p ) == 0 );
    REQUIRE( TestMgr::get_tree_height( null_p ) == 0 );

    TestMgr::destroy();
}

/// @brief set_tree_X methods do nothing for null pptr (guard not changed by refactoring).
TEST_CASE( "I179-A2: set_tree_X are no-ops for null pptr", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> null_p;
    REQUIRE( null_p.is_null() );

    // These must not crash or corrupt state
    TestMgr::set_tree_left_offset( null_p, 1 );
    TestMgr::set_tree_right_offset( null_p, 1 );
    TestMgr::set_tree_parent_offset( null_p, 1 );
    TestMgr::set_tree_weight( null_p, 1 );
    TestMgr::set_tree_height( null_p, 1 );

    TestMgr::destroy();
}

// =============================================================================
// Get/set round-trip on a real allocated block
// =============================================================================

/// @brief get_tree_left_offset / set_tree_left_offset round-trip.
TEST_CASE( "I179-B1: left_offset round-trip", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    REQUIRE( !p.is_null() );

    using AT = pmm::DefaultAddressTraits;
    TestMgr::set_tree_left_offset( p, static_cast<AT::index_type>( 10 ) );
    REQUIRE( TestMgr::get_tree_left_offset( p ) == static_cast<AT::index_type>( 10 ) );

    // set 0 stores no_block internally, get returns 0
    TestMgr::set_tree_left_offset( p, 0 );
    REQUIRE( TestMgr::get_tree_left_offset( p ) == 0 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
}

/// @brief get_tree_right_offset / set_tree_right_offset round-trip.
TEST_CASE( "I179-B2: right_offset round-trip", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    REQUIRE( !p.is_null() );

    using AT = pmm::DefaultAddressTraits;
    TestMgr::set_tree_right_offset( p, static_cast<AT::index_type>( 20 ) );
    REQUIRE( TestMgr::get_tree_right_offset( p ) == static_cast<AT::index_type>( 20 ) );

    TestMgr::set_tree_right_offset( p, 0 );
    REQUIRE( TestMgr::get_tree_right_offset( p ) == 0 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
}

/// @brief get_tree_parent_offset / set_tree_parent_offset round-trip.
TEST_CASE( "I179-B3: parent_offset round-trip", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    REQUIRE( !p.is_null() );

    using AT = pmm::DefaultAddressTraits;
    TestMgr::set_tree_parent_offset( p, static_cast<AT::index_type>( 5 ) );
    REQUIRE( TestMgr::get_tree_parent_offset( p ) == static_cast<AT::index_type>( 5 ) );

    TestMgr::set_tree_parent_offset( p, 0 );
    REQUIRE( TestMgr::get_tree_parent_offset( p ) == 0 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
}

/// @brief get_tree_weight / set_tree_weight round-trip.
TEST_CASE( "I179-B4: weight round-trip", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    REQUIRE( !p.is_null() );

    using AT = pmm::DefaultAddressTraits;
    TestMgr::set_tree_weight( p, static_cast<AT::index_type>( 7 ) );
    REQUIRE( TestMgr::get_tree_weight( p ) == static_cast<AT::index_type>( 7 ) );

    TestMgr::set_tree_weight( p, 0 );
    REQUIRE( TestMgr::get_tree_weight( p ) == 0 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
}

/// @brief get_tree_height / set_tree_height round-trip.
TEST_CASE( "I179-B5: height round-trip", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    REQUIRE( !p.is_null() );

    TestMgr::set_tree_height( p, static_cast<std::int16_t>( 3 ) );
    REQUIRE( TestMgr::get_tree_height( p ) == static_cast<std::int16_t>( 3 ) );

    TestMgr::set_tree_height( p, 0 );
    REQUIRE( TestMgr::get_tree_height( p ) == 0 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// Tree_node() still works after refactoring
// =============================================================================

/// @brief tree_node() correctly gives access to block AVL fields.
TEST_CASE( "I179-C1: tree_node() and get_tree_X agree after refactoring", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>( 1 );
    REQUIRE( !p.is_null() );

    using AT = pmm::DefaultAddressTraits;

    // Use tree_node to set fields, verify via get_tree_X that both agree
    auto& tn = TestMgr::tree_node( p );
    tn.set_left( static_cast<AT::index_type>( 4 ) );
    tn.set_right( static_cast<AT::index_type>( 8 ) );

    REQUIRE( TestMgr::get_tree_left_offset( p ) == static_cast<AT::index_type>( 4 ) );
    REQUIRE( TestMgr::get_tree_right_offset( p ) == static_cast<AT::index_type>( 8 ) );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// Functional allocation / AVL tree still works
// =============================================================================

/// @brief Full allocate/deallocate cycle works after refactoring.
TEST_CASE( "I179-D1: allocate/deallocate cycle works", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    void* p1 = TestMgr::allocate( 32 );
    void* p2 = TestMgr::allocate( 64 );
    void* p3 = TestMgr::allocate( 128 );
    REQUIRE( p1 != nullptr );
    REQUIRE( p2 != nullptr );
    REQUIRE( p3 != nullptr );

    TestMgr::deallocate( p2 );
    TestMgr::deallocate( p1 );
    TestMgr::deallocate( p3 );

    void* p4 = TestMgr::allocate( 64 );
    REQUIRE( p4 != nullptr );
    TestMgr::deallocate( p4 );

    TestMgr::destroy();
}

// =============================================================================
// Make_pptr_from_raw helper (via typed allocation API)
// =============================================================================

/// @brief allocate_typed<T>() pptr resolves to the same address as the raw allocation.
TEST_CASE( "I179-E1: allocate_typed<T>() pptr resolves correctly", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    int* resolved = TestMgr::resolve( p );
    REQUIRE( resolved != nullptr );

    // Write through raw pointer obtained via resolve, read back via resolve to confirm.
    *resolved = 42;
    REQUIRE( *TestMgr::resolve( p ) == 42 );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
}

/// @brief allocate_typed<T>(count) pptr resolves to the correct base address.
TEST_CASE( "I179-E2: allocate_typed<T>(count) pptr resolves correctly", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    const std::size_t  count = 4;
    TestMgr::pptr<int> p     = TestMgr::allocate_typed<int>( count );
    REQUIRE( !p.is_null() );

    int* arr = TestMgr::resolve( p );
    REQUIRE( arr != nullptr );

    for ( std::size_t i = 0; i < count; ++i )
        arr[i] = static_cast<int>( i * 10 );

    for ( std::size_t i = 0; i < count; ++i )
        REQUIRE(
            ( TestMgr::resolve_at( p, i ) != nullptr && *TestMgr::resolve_at( p, i ) == static_cast<int>( i * 10 ) ) );

    TestMgr::deallocate_typed( p );
    TestMgr::destroy();
}

/// @brief create_typed<T>(args...) pptr resolves and the object was constructed.
TEST_CASE( "I179-E3: create_typed<T>(args) resolves and constructs value", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    TestMgr::pptr<int> p = TestMgr::create_typed<int>( 99 );
    REQUIRE( !p.is_null() );

    int* resolved = TestMgr::resolve( p );
    REQUIRE( resolved != nullptr );
    REQUIRE( *resolved == 99 );

    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// Find_block_from_user_ptr helper (via lock API)
// =============================================================================

/// @brief lock_block_permanent and is_permanently_locked work via find_block_from_user_ptr.
TEST_CASE( "I179-F1: lock_block_permanent and is_permanently_locked via helper", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    void* ptr = TestMgr::allocate( 32 );
    REQUIRE( ptr != nullptr );

    // Initially not locked
    REQUIRE( !TestMgr::is_permanently_locked( ptr ) );

    // Lock it
    REQUIRE( TestMgr::lock_block_permanent( ptr ) );

    // Now it is locked
    REQUIRE( TestMgr::is_permanently_locked( ptr ) );

    // deallocate must be a no-op for permanently locked block
    TestMgr::deallocate( ptr );
    REQUIRE( TestMgr::is_permanently_locked( ptr ) );

    TestMgr::destroy();
}

/// @brief find_block_from_user_ptr: null and out-of-range pointers return safely.
TEST_CASE( "I179-F2: null and invalid ptrs handled safely", "[test_issue179_deduplication]" )
{
    TestMgr::create( 64 * 1024 );

    // Null ptr
    REQUIRE( !TestMgr::is_permanently_locked( nullptr ) );
    REQUIRE( !TestMgr::lock_block_permanent( nullptr ) );

    // Out-of-range ptr: should return false/nullptr, not crash
    void* bad_ptr = reinterpret_cast<void*>( static_cast<std::uintptr_t>( 1 ) );
    REQUIRE( !TestMgr::is_permanently_locked( bad_ptr ) );

    TestMgr::destroy();
}

// =============================================================================
// main
// =============================================================================
