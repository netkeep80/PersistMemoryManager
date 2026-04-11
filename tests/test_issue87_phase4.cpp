/**
 * @file test_issue87_phase4.cpp
 * @brief Tests Phase 4: FreeBlockTree as template policy.
 *
 * Verifies:
 *  - FreeBlockTreePolicyForTraitsConcept<AvlFreeTree<DefaultAddressTraits>> == true
 *  - AvlFreeTree correctly delegates to AvlFreeTree<DefaultAddressTraits>
 *  - Concept applicable (SFINAE): non-conforming types return false
 *
 * @see include/pmm/free_block_tree.h
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

using Mgr = pmm::presets::SingleThreadedHeap;

// =============================================================================
// Phase 4 tests: FreeBlockTree policy concept
// =============================================================================

// ─── P4-A: Concept ────────────────────────────────────────────────────────────

TEST_CASE( "P4-A1: AvlFreeTree<Default> satisfies FreeBlockTreePolicy", "[test_issue87_phase4]" )
{
    using Policy = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;
    static_assert( pmm::FreeBlockTreePolicyForTraitsConcept<Policy, pmm::DefaultAddressTraits>,
                   "AvlFreeTree<DefaultAddressTraits> must satisfy FreeBlockTreePolicy" );
}

TEST_CASE( "P4-A2: Non-policy type fails concept check", "[test_issue87_phase4]" )
{
    struct NotAPolicy
    {
        int x;
    };
    static_assert( !pmm::FreeBlockTreePolicyForTraitsConcept<NotAPolicy, pmm::DefaultAddressTraits>, "NotAPolicy must not satisfy FreeBlockTreePolicy" );
    static_assert( !pmm::FreeBlockTreePolicyForTraitsConcept<int, pmm::DefaultAddressTraits>, "int must not satisfy FreeBlockTreePolicy" );
}

TEST_CASE( "P4-A3: Partial policy (insert only) fails concept check", "[test_issue87_phase4]" )
{
    struct PartialPolicy
    {
        // ManagerHeader<AT> is now templated.
        static void insert( std::uint8_t*, pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*,
                            pmm::DefaultAddressTraits::index_type )
        {
        }
    };
    static_assert( !pmm::FreeBlockTreePolicyForTraitsConcept<PartialPolicy, pmm::DefaultAddressTraits>,
                   "PartialPolicy (insert only) must not satisfy FreeBlockTreePolicy" );
}

// ─── P4-B: AvlFreeTree — type aliases ────────────────────────────────────────

TEST_CASE( "P4-B1: AvlFreeTree<Default> has correct type aliases", "[test_issue87_phase4]" )
{
    using Policy = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Policy::address_traits, pmm::DefaultAddressTraits>::value,
                   "AvlFreeTree::address_traits must be DefaultAddressTraits" );
    static_assert( std::is_same<Policy::index_type, std::uint32_t>::value, "AvlFreeTree::index_type must be uint32_t" );
}

// ─── P4-C: AvlFreeTree — functional ─────────────────────────────────────────

TEST_CASE( "P4-C1: AvlFreeTree insert/remove/find_best_fit via manager", "[test_issue87_phase4]" )
{
    using Policy = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;

    Mgr pmm;
    REQUIRE( pmm.create( 4096 ) );

    // After create(), there's one free block in the tree — verify find_best_fit works
    REQUIRE( pmm.free_block_count() >= 1 );
    REQUIRE( pmm.is_initialized() );

    // Allocate and free to exercise insert/remove paths
    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 64 );
    REQUIRE( !p.is_null() );
    pmm.deallocate_typed( p );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

// =============================================================================
// main
// =============================================================================
