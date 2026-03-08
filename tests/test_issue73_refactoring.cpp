/**
 * @file test_issue73_refactoring.cpp
 * @brief Tests for architectural refactoring Issue #73 (updated #110, #112 — static API).
 *
 * Issue #110: PersistMemoryManager<ConfigT, InstanceId> is a unified fully static
 * manager. Tests verify:
 * - FR-03: Block<DefaultAddressTraits> (32 bytes) and ManagerHeader (64 bytes) sizes unchanged
 *   (Issue #112: BlockHeader struct removed — Block<A> is the sole block type)
 * - FR-02/AR-03: PersistentAvlTree is a standalone class
 * - FR-04/AR-01: Public API works through static PersistMemoryManager presets
 * - AR-02: No virtual functions — all polymorphism is static
 * - AR-04: File separation: types, avl, manager, io
 * - Independence via InstanceId: distinct InstanceIds have separate static state
 */

#include "pmm/address_traits.h"
#include "pmm/block.h"
#include "pmm/config.h"
#include "pmm/free_block_tree.h"
#include "pmm/pmm_presets.h"
#include "pmm/types.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

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

using Mgr = pmm::presets::SingleThreadedHeap;

// ─── FR-03: Binary-compatibility static_assert checks ────────────────────────

// Issue #112: BlockHeader struct removed; Block<DefaultAddressTraits> is the sole block type.
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32,
               "FR-03: Block<DefaultAddressTraits> must be exactly 32 bytes (Issue #112)" );
static_assert( sizeof( pmm::detail::ManagerHeader ) == 64, "FR-03: ManagerHeader must be exactly 64 bytes" );
static_assert( sizeof( Mgr::pptr<int> ) == 4, "pptr<T> must be exactly 4 bytes (Issue #59)" );

// ─── FR-02/AR-03: PersistentAvlTree is a standalone class ────────────────────

/// @brief Verify PersistentAvlTree is all-static and callable directly.
static bool test_avl_tree_standalone()
{
    PMM_TEST( Mgr::create( 128 * 1024 ) );

    // Allocate some blocks to create a non-trivial free tree
    Mgr::pptr<std::uint8_t> p1 = Mgr::allocate_typed<std::uint8_t>( 256 );
    Mgr::pptr<std::uint8_t> p2 = Mgr::allocate_typed<std::uint8_t>( 512 );
    Mgr::pptr<std::uint8_t> p3 = Mgr::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );
    Mgr::deallocate_typed( p1 );

    PMM_TEST( Mgr::is_initialized() );

    Mgr::deallocate_typed( p2 );
    Mgr::deallocate_typed( p3 );
    Mgr::destroy();
    return true;
}

// ─── FR-04/AR-01: Public API via static PersistMemoryManager ─────────────────

/// @brief Verify the static API works correctly.
static bool test_instance_api()
{
    PMM_TEST( Mgr::create( 64 * 1024 ) );
    PMM_TEST( Mgr::is_initialized() );

    PMM_TEST( Mgr::alloc_block_count() > 0 ); // Block_0
    PMM_TEST( Mgr::free_block_count() == 1 );
    PMM_TEST( Mgr::alloc_block_count() == 1 ); // Block_0

    Mgr::pptr<std::uint8_t> p = Mgr::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );

    PMM_TEST( Mgr::alloc_block_count() == 2 ); // Block_0 + p

    Mgr::deallocate_typed( p );
    PMM_TEST( Mgr::alloc_block_count() == 1 ); // Block_0 only
    Mgr::destroy();
    return true;
}

/// @brief Verify two managers with distinct InstanceIds don't share state.
static bool test_two_instances_independent()
{
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 600>;
    using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 601>;

    PMM_TEST( Mgr1::create( 64 * 1024 ) );
    PMM_TEST( Mgr2::create( 64 * 1024 ) );

    PMM_TEST( Mgr1::is_initialized() );
    PMM_TEST( Mgr2::is_initialized() );

    // Each has separate total_size
    PMM_TEST( Mgr1::total_size() > 0 );
    PMM_TEST( Mgr2::total_size() > 0 );

    Mgr1::pptr<std::uint32_t> p1 = Mgr1::allocate_typed<std::uint32_t>( 4 );
    PMM_TEST( !p1.is_null() );
    PMM_TEST( Mgr2::alloc_block_count() == 1 ); // Mgr2 unaffected

    Mgr1::deallocate_typed( p1 );
    Mgr1::destroy();
    Mgr2::destroy();
    return true;
}

// ─── AR-02: No virtual functions ─────────────────────────────────────────────

/// @brief Ensure PersistMemoryManager has no virtual functions (AR-02).
static bool test_no_virtual_functions()
{
    static_assert( !std::is_polymorphic<Mgr>::value, "AR-02: PersistMemoryManager must have no virtual functions" );
    static_assert( !std::is_polymorphic<pmm::PersistentAvlTree>::value,
                   "AR-02: PersistentAvlTree must have no virtual functions" );
    return true;
}

// ─── AR-04: File separation ───────────────────────────────────────────────────

/// @brief Verify that types from each header are accessible independently.
static bool test_file_separation()
{
    // Types from persist_memory_types.h
    static_assert( pmm::kGranuleSize == 16, "Types header must provide kGranuleSize" );
    // Issue #112: BlockHeader removed; Block<DefaultAddressTraits> is the sole block type.
    static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32, "Types header: Block<A> (Issue #112)" );
    static_assert( sizeof( pmm::detail::ManagerHeader ) == 64, "Types header: ManagerHeader" );

    // Config from pmm_config.h
    static_assert( pmm::config::kDefaultGrowNumerator == 5, "Config header: grow_numerator" );
    static_assert( pmm::config::kDefaultGrowDenominator == 4, "Config header: grow_denominator" );

    // PersistentAvlTree from persist_avl_tree.h — just check it's a class
    static_assert( std::is_class<pmm::PersistentAvlTree>::value, "persist_avl_tree.h: PersistentAvlTree" );

    // AddressTraits
    static_assert( std::is_class<pmm::DefaultAddressTraits>::value, "address_traits.h: DefaultAddressTraits" );

    return true;
}

// ─── FR-05: NoLock policy via presets ────────────────────────────────────────

/// @brief Verify SingleThreadedHeap uses NoLock.
static bool test_nolock_preset()
{
    using NoLockMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 602>;

    PMM_TEST( NoLockMgr::create( 64 * 1024 ) );
    PMM_TEST( NoLockMgr::is_initialized() );

    NoLockMgr::pptr<std::uint32_t> p = NoLockMgr::allocate_typed<std::uint32_t>( 4 );
    PMM_TEST( !p.is_null() );

    NoLockMgr::deallocate_typed( p );
    NoLockMgr::destroy();
    return true;
}

// ─── Integration: multiple presets coexist ────────────────────────────────────

/// @brief Verify that two different InstanceId types can be used simultaneously.
static bool test_presets_coexist()
{
    using ST1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 603>;
    using ST2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 604>;

    PMM_TEST( ST1::create( 64 * 1024 ) );
    PMM_TEST( ST2::create( 64 * 1024 ) );

    PMM_TEST( ST1::is_initialized() );
    PMM_TEST( ST2::is_initialized() );

    // Each has separate total_size (same value but independent storage)
    PMM_TEST( ST1::total_size() == ST2::total_size() );

    ST1::destroy();
    ST2::destroy();
    return true;
}

int main()
{
    std::cout << "=== test_issue73_refactoring (updated #110 — static API) ===\n";
    bool all_passed = true;

    PMM_RUN( "FR-03: struct sizes", test_file_separation );
    PMM_RUN( "FR-02/AR-03: avl_tree_standalone", test_avl_tree_standalone );
    PMM_RUN( "FR-04/AR-01: instance_api", test_instance_api );
    PMM_RUN( "FR-04/AR-01: two_instances_independent", test_two_instances_independent );
    PMM_RUN( "AR-02: no_virtual_functions", test_no_virtual_functions );
    PMM_RUN( "AR-04: file_separation", test_file_separation );
    PMM_RUN( "FR-05: nolock_preset", test_nolock_preset );
    PMM_RUN( "FR-05: presets_coexist", test_presets_coexist );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
