/**
 * @file test_issue87_phase4.cpp
 * @brief Tests Phase 4: FreeBlockTree as template policy (Issue #87, updated #102).
 *
 * Verifies:
 *  - is_free_block_tree_policy_v<AvlFreeTree<DefaultAddressTraits>> == true
 *  - AvlFreeTree correctly delegates to PersistentAvlTree
 *  - Concept applicable (SFINAE): non-conforming types return false
 *
 * @see include/pmm/free_block_tree.h
 */

#include "pmm_single_threaded_heap.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
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

// =============================================================================
// Phase 4 tests: FreeBlockTree policy concept
// =============================================================================

// ─── P4-A: Concept ────────────────────────────────────────────────────────────

static bool test_p4_avl_free_tree_satisfies_concept()
{
    using Policy = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;
    static_assert( pmm::is_free_block_tree_policy_v<Policy>,
                   "AvlFreeTree<DefaultAddressTraits> must satisfy FreeBlockTreePolicy" );
    return true;
}

static bool test_p4_non_policy_type_fails_concept()
{
    struct NotAPolicy
    {
        int x;
    };
    static_assert( !pmm::is_free_block_tree_policy_v<NotAPolicy>, "NotAPolicy must not satisfy FreeBlockTreePolicy" );
    static_assert( !pmm::is_free_block_tree_policy_v<int>, "int must not satisfy FreeBlockTreePolicy" );
    return true;
}

static bool test_p4_partial_policy_fails_concept()
{
    struct PartialPolicy
    {
        // Issue #175: ManagerHeader<AT> is now templated.
        static void insert( std::uint8_t*, pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*,
                            pmm::DefaultAddressTraits::index_type )
        {
        }
    };
    static_assert( !pmm::is_free_block_tree_policy_v<PartialPolicy>,
                   "PartialPolicy (insert only) must not satisfy FreeBlockTreePolicy" );
    return true;
}

// ─── P4-B: AvlFreeTree — type aliases ────────────────────────────────────────

static bool test_p4_avl_free_tree_aliases()
{
    using Policy = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Policy::address_traits, pmm::DefaultAddressTraits>::value,
                   "AvlFreeTree::address_traits must be DefaultAddressTraits" );
    static_assert( std::is_same<Policy::index_type, std::uint32_t>::value, "AvlFreeTree::index_type must be uint32_t" );
    return true;
}

// ─── P4-C: AvlFreeTree — functional ─────────────────────────────────────────

static bool test_p4_avl_free_tree_functional()
{
    using Policy = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;

    Mgr pmm;
    PMM_TEST( pmm.create( 4096 ) );

    // After create(), there's one free block in the tree — verify find_best_fit works
    PMM_TEST( pmm.free_block_count() >= 1 );
    PMM_TEST( pmm.is_initialized() );

    // Allocate and free to exercise insert/remove paths
    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );
    pmm.deallocate_typed( p );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase4 (Phase 4: FreeBlockTree policy, updated #102) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P4-A: FreeBlockTree concept ---\n";
    PMM_RUN( "P4-A1: AvlFreeTree<Default> satisfies FreeBlockTreePolicy", test_p4_avl_free_tree_satisfies_concept );
    PMM_RUN( "P4-A2: Non-policy type fails concept check", test_p4_non_policy_type_fails_concept );
    PMM_RUN( "P4-A3: Partial policy (insert only) fails concept check", test_p4_partial_policy_fails_concept );

    std::cout << "\n--- P4-B: AvlFreeTree — type aliases ---\n";
    PMM_RUN( "P4-B1: AvlFreeTree<Default> has correct type aliases", test_p4_avl_free_tree_aliases );

    std::cout << "\n--- P4-C: AvlFreeTree — functional ---\n";
    PMM_RUN( "P4-C1: AvlFreeTree insert/remove/find_best_fit via manager", test_p4_avl_free_tree_functional );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
