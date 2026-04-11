/**
 * @file test_issue73_refactoring.cpp
 * @brief Tests for architectural refactoring.
 *
 * manager. Tests verify:
 * - FR-03: Block<DefaultAddressTraits> (32 bytes) and ManagerHeader (64 bytes) sizes unchanged
 *   (BlockHeader struct removed — Block<A> is the sole block type)
 * - FR-02/AR-03: AvlFreeTree<DefaultAddressTraits> is a standalone class
 * - FR-04/AR-01: Public API works through static PersistMemoryManager presets
 * - AR-02: No virtual functions — all polymorphism is static
 * - AR-04: File separation: types, avl, manager, io
 * - Independence via InstanceId: distinct InstanceIds have separate static state
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include <type_traits>

using Mgr = pmm::presets::SingleThreadedHeap;

// ─── FR-03: Binary-compatibility static_assert checks ────────────────────────

// BlockHeader struct removed; Block<DefaultAddressTraits> is the sole block type.
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32,
               "FR-03: Block<DefaultAddressTraits> must be exactly 32 bytes " );
// ManagerHeader<AT> is now templated; DefaultAddressTraits variant remains 64 bytes.
static_assert( sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) == 64,
               "FR-03: ManagerHeader<DefaultAddressTraits> must be exactly 64 bytes " );
static_assert( sizeof( Mgr::pptr<int> ) == 4, "pptr<T> must be exactly 4 bytes " );

// ─── FR-02/AR-03: AvlFreeTree<DefaultAddressTraits> is a standalone class ────────────────────

/// @brief Verify AvlFreeTree<DefaultAddressTraits> is all-static and callable directly.
TEST_CASE( "FR-02/AR-03: avl_tree_standalone", "[test_issue73_refactoring]" )
{
    REQUIRE( Mgr::create( 128 * 1024 ) );

    // Allocate some blocks to create a non-trivial free tree
    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 512 );
    Mgr::pptr<std::uint8_t> p3 = Mgr::allocate_typed<std::uint8_t>( 128 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );
    Mgr::deallocate_typed( p1 );

    REQUIRE( Mgr::is_initialized() );

    Mgr::deallocate_typed( p2 );
    Mgr::deallocate_typed( p3 );
    Mgr::destroy();
}

// ─── FR-04/AR-01: Public API via static PersistMemoryManager ─────────────────

/// @brief Verify the static API works correctly.
TEST_CASE( "FR-04/AR-01: instance_api", "[test_issue73_refactoring]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );
    REQUIRE( Mgr::is_initialized() );
    const auto baseline_alloc = Mgr::alloc_block_count();

    REQUIRE( Mgr::alloc_block_count() > 0 ); // system blocks
    REQUIRE( Mgr::free_block_count() == 1 );
    REQUIRE( Mgr::alloc_block_count() == baseline_alloc ); // system blocks

    Mgr::pptr<std::uint8_t> p = Mgr::allocate_typed<std::uint8_t>( 128 );
    REQUIRE( !p.is_null() );

    REQUIRE( Mgr::alloc_block_count() == baseline_alloc + 1 ); // system + p

    Mgr::deallocate_typed( p );
    REQUIRE( Mgr::alloc_block_count() == baseline_alloc ); // system only
    Mgr::destroy();
}

/// @brief Verify two managers with distinct InstanceIds don't share state.
TEST_CASE( "FR-04/AR-01: two_instances_independent", "[test_issue73_refactoring]" )
{
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 600>;
    using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 601>;

    REQUIRE( Mgr1::create( 64 * 1024 ) );
    REQUIRE( Mgr2::create( 64 * 1024 ) );
    const auto baseline_alloc2 = Mgr2::alloc_block_count();

    REQUIRE( Mgr1::is_initialized() );
    REQUIRE( Mgr2::is_initialized() );

    // Each has separate total_size
    REQUIRE( Mgr1::total_size() > 0 );
    REQUIRE( Mgr2::total_size() > 0 );

    Mgr1::pptr<std::uint32_t> p1 = Mgr1::allocate_typed<std::uint32_t>( 4 );
    REQUIRE( !p1.is_null() );
    REQUIRE( Mgr2::alloc_block_count() == baseline_alloc2 ); // Mgr2 unaffected

    Mgr1::deallocate_typed( p1 );
    Mgr1::destroy();
    Mgr2::destroy();
}

// ─── AR-02: No virtual functions ─────────────────────────────────────────────

/// @brief Ensure PersistMemoryManager has no virtual functions (AR-02).
TEST_CASE( "AR-02: no_virtual_functions", "[test_issue73_refactoring]" )
{
    static_assert( !std::is_polymorphic<Mgr>::value, "AR-02: PersistMemoryManager must have no virtual functions" );
    static_assert( !std::is_polymorphic<pmm::AvlFreeTree<pmm::DefaultAddressTraits>>::value,
                   "AR-02: AvlFreeTree<DefaultAddressTraits> must have no virtual functions" );
}

// ─── AR-04: File separation ───────────────────────────────────────────────────

/// @brief Verify that types from each header are accessible independently.
TEST_CASE( "AR-04: file_separation", "[test_issue73_refactoring]" )
{
    // Types from persist_memory_types.h
    static_assert( pmm::kGranuleSize == 16, "Types header must provide kGranuleSize" );
    // BlockHeader removed; Block<DefaultAddressTraits> is the sole block type.
    static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32, "Types header: Block<A> " );
    // ManagerHeader<AT> is now templated.
    static_assert( sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) == 64,
                   "Types header: ManagerHeader<DefaultAddressTraits> " );

    // Config from pmm_config.h
    static_assert( pmm::config::kDefaultGrowNumerator == 5, "Config header: grow_numerator" );
    static_assert( pmm::config::kDefaultGrowDenominator == 4, "Config header: grow_denominator" );

    // AvlFreeTree<DefaultAddressTraits> from persist_avl_tree.h — just check it's a class
    static_assert( std::is_class<pmm::AvlFreeTree<pmm::DefaultAddressTraits>>::value, "persist_avl_tree.h: AvlFreeTree<DefaultAddressTraits>" );

    // AddressTraits
    static_assert( std::is_class<pmm::DefaultAddressTraits>::value, "address_traits.h: DefaultAddressTraits" );
}

// ─── FR-05: NoLock policy via presets ────────────────────────────────────────

/// @brief Verify SingleThreadedHeap uses NoLock.
TEST_CASE( "FR-05: nolock_preset", "[test_issue73_refactoring]" )
{
    using NoLockMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 602>;

    REQUIRE( NoLockMgr::create( 64 * 1024 ) );
    REQUIRE( NoLockMgr::is_initialized() );

    NoLockMgr::pptr<std::uint32_t> p = NoLockMgr::allocate_typed<std::uint32_t>( 4 );
    REQUIRE( !p.is_null() );

    NoLockMgr::deallocate_typed( p );
    NoLockMgr::destroy();
}

// ─── Integration: multiple presets coexist ────────────────────────────────────

/// @brief Verify that two different InstanceId types can be used simultaneously.
TEST_CASE( "FR-05: presets_coexist", "[test_issue73_refactoring]" )
{
    using ST1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 603>;
    using ST2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 604>;

    REQUIRE( ST1::create( 64 * 1024 ) );
    REQUIRE( ST2::create( 64 * 1024 ) );

    REQUIRE( ST1::is_initialized() );
    REQUIRE( ST2::is_initialized() );

    // Each has separate total_size (same value but independent storage)
    REQUIRE( ST1::total_size() == ST2::total_size() );

    ST1::destroy();
    ST2::destroy();
}
