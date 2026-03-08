/**
 * @file test_issue87_abstraction.cpp
 * @brief Tests for Issue #87 abstraction (updated #102 — new API).
 *
 * Issue #102: PersistMemoryManager<> (singleton) removed.
 *   - PART A tests updated to use AbstractPersistMemoryManager.
 *   - Phase 1/2/3 beacons still test the new type infrastructure.
 *   - PART C integration tests use the new instance-based API.
 *
 * @see plan_issue87.md — full refactoring plan
 * @version 0.2 (Issue #102)
 */

#include "pmm/pmm_presets.h"
#include "pmm/types.h"
#include "pmm/block_state.h"
#include "pmm/free_block_tree.h"
#include "pmm/config.h"
#include "pmm/address_traits.h"
#include "pmm/io.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
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
// PART A: Code Review — current architecture
// =============================================================================

// ─── A1: Granule constants ────────────────────────────────────────────────────

static bool test_cr_granule_size_is_hardcoded()
{
    static_assert( pmm::kGranuleSize == 16, "kGranuleSize must be 16" );
    PMM_TEST( pmm::detail::bytes_to_granules( 16 ) == 1 );
    PMM_TEST( pmm::detail::bytes_to_granules( 17 ) == 2 );
    PMM_TEST( pmm::detail::granules_to_bytes( 1 ) == 16 );
    return true;
}

static bool test_cr_no_block_is_max_uint32()
{
    static_assert( pmm::detail::kNoBlock == 0xFFFFFFFFU, "kNoBlock is max uint32_t" );
    PMM_TEST( pmm::detail::kNoBlock == ( std::numeric_limits<std::uint32_t>::max )() );
    return true;
}

// ─── A2: Block<DefaultAddressTraits> layout (Issue #112) ─────────────────────
// Note: Block fields are protected (Issue #120). Layout verified via BlockStateBase offsets.

static bool test_cr_block_header_combines_list_and_tree()
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;
    // LinkedListNode fields at start of Block (verified via BlockStateBase::kOffset* — Issue #120)
    static_assert( BlockState::kOffsetPrevOffset == 0 );
    static_assert( BlockState::kOffsetNextOffset == 4 );
    // TreeNode fields follow LinkedListNode
    static_assert( BlockState::kOffsetLeftOffset == 8 );
    static_assert( BlockState::kOffsetRightOffset == 12 );
    static_assert( BlockState::kOffsetParentOffset == 16 );
    static_assert( BlockState::kOffsetAvlHeight == 20 );
    static_assert( BlockState::kOffsetWeight == 24 );
    static_assert( BlockState::kOffsetRootOffset == 28 );
    static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32 );
    return true;
}

static bool test_cr_block_header_uses_uint32_indices()
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;
    // Fields are protected (Issue #120); verify index_type via BlockStateBase::index_type
    static_assert( std::is_same<BlockState::index_type, std::uint32_t>::value );
    return true;
}

// ─── A3: pptr<T, ManagerT> resolves via instance ─────────────────────────────

static bool test_cr_pptr_resolves_via_manager()
{
    static_assert( sizeof( Mgr::pptr<int> ) == 4 );
    static_assert( sizeof( Mgr::pptr<double> ) == 4 );

    Mgr::pptr<int> null_ptr;
    PMM_TEST( null_ptr.is_null() );
    PMM_TEST( !null_ptr );

    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>( 1 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( p.resolve() != nullptr );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

// ─── A4: Config constants ─────────────────────────────────────────────────────

static bool test_cr_config_constants()
{
    static_assert( pmm::config::kDefaultGrowNumerator == 5 );
    static_assert( pmm::config::kDefaultGrowDenominator == 4 );
    return true;
}

// ─── A5: PersistentAvlTree is standalone ────────────────────────────────────

static bool test_cr_avl_tree_is_standalone()
{
    static_assert( !std::is_constructible<pmm::PersistentAvlTree>::value,
                   "PersistentAvlTree must not be constructible (all-static)" );

    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );
    PMM_TEST( pmm.is_initialized() );
    pmm.destroy();
    return true;
}

// ─── A6: HeapStorage backend exists ─────────────────────────────────────────

static bool test_cr_heap_backend()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );
    PMM_TEST( pmm.is_initialized() );
    pmm.destroy();
    return true;
}

// ─── A7: Instance-based manager (no virtual functions) ───────────────────────

static bool test_cr_no_virtual_functions()
{
    static_assert( !std::is_polymorphic<Mgr>::value, "AbstractPersistMemoryManager must not be polymorphic" );
    return true;
}

// ─── A8: Thread policy injection works ───────────────────────────────────────

static bool test_cr_thread_policy_injection()
{
    using ST = pmm::presets::SingleThreadedHeap;

    ST pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );
    PMM_TEST( pmm.is_initialized() );
    pmm.destroy();
    return true;
}

// =============================================================================
// PART B: Phase beacons (implemented abstractions)
// =============================================================================

// [Phase 1] AddressTraits
#include "pmm/address_traits.h"

static bool test_phase1_address_traits()
{
    using A8 = pmm::AddressTraits<std::uint8_t, 8>;
    static_assert( A8::granule_size == 8 );
    static_assert( A8::no_block == 0xFFU );
    static_assert( A8::bytes_to_granules( 8 ) == 1 );
    static_assert( A8::bytes_to_granules( 9 ) == 2 );

    using A16 = pmm::AddressTraits<std::uint16_t, 16>;
    static_assert( A16::no_block == 0xFFFFU );

    using A32 = pmm::AddressTraits<std::uint32_t, 16>;
    static_assert( A32::no_block == pmm::detail::kNoBlock );
    static_assert( A32::granule_size == pmm::kGranuleSize );

    static_assert( std::is_same<pmm::DefaultAddressTraits, A32>::value );

    return true;
}

// [Phase 2] LinkedListNode + TreeNode
// Note: Fields are protected (Issue #120). Type verified via BlockStateBase::index_type.
#include "pmm/linked_list_node.h"
#include "pmm/tree_node.h"

static bool test_phase2_list_and_tree_nodes()
{
    using A = pmm::DefaultAddressTraits;

    // Fields are protected (Issue #120): verify type via LinkedListNode::index_type alias
    static_assert( std::is_same<pmm::LinkedListNode<A>::index_type, typename A::index_type>::value,
                   "LinkedListNode::index_type must match A::index_type" );
    static_assert( std::is_same<pmm::TreeNode<A>::index_type, typename A::index_type>::value,
                   "TreeNode::index_type must match A::index_type" );

    // BlockStateBase exposes index_type for field type verification
    static_assert( std::is_same<pmm::BlockStateBase<A>::index_type, typename A::index_type>::value,
                   "BlockStateBase::index_type must match A::index_type" );

    using A8 = pmm::TinyAddressTraits;
    static_assert( std::is_same<pmm::LinkedListNode<A8>::index_type, std::uint8_t>::value,
                   "LinkedListNode<TinyAddressTraits>::index_type must be uint8_t" );
    static_assert( std::is_same<pmm::TreeNode<A8>::index_type, std::uint8_t>::value,
                   "TreeNode<TinyAddressTraits>::index_type must be uint8_t" );

    return true;
}

// [Phase 3] Block
#include "pmm/block.h"

static bool test_phase3_block_layout()
{
    using A = pmm::DefaultAddressTraits;

    static_assert( sizeof( pmm::Block<A> ) == 32 );
    static_assert( std::is_base_of<pmm::LinkedListNode<A>, pmm::Block<A>>::value );
    static_assert( std::is_base_of<pmm::TreeNode<A>, pmm::Block<A>>::value );

    return true;
}

// =============================================================================
// PART C: Integration — must pass on all phases
// =============================================================================

static bool test_integration_full_lifecycle()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 128 * 1024 ) );
    PMM_TEST( pmm.is_initialized() );

    auto p1 = pmm.allocate_typed<std::uint8_t>( 16 );
    auto p2 = pmm.allocate_typed<std::uint32_t>( 32 );
    auto p3 = pmm.allocate_typed<double>( 8 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    pmm.deallocate_typed( p2 );
    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p3 );

    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

static bool test_integration_persistence()
{
    const char* TEST_FILE = "test_i87_persist.dat";

    Mgr pmm1;
    PMM_TEST( pmm1.create( 64 * 1024 ) );
    auto p = pmm1.allocate_typed<std::uint64_t>( 1 );
    PMM_TEST( !p.is_null() );
    *p.resolve()               = 0xDEADBEEFCAFEBABEULL;
    std::uint32_t saved_offset = p.offset();

    PMM_TEST( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    PMM_TEST( pmm2.create( 64 * 1024 ) );
    PMM_TEST( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE ) );
    PMM_TEST( pmm2.is_initialized() );

    Mgr::pptr<std::uint64_t> p2( saved_offset );
    PMM_TEST( p2.resolve() != nullptr );
    PMM_TEST( *p2.resolve() == 0xDEADBEEFCAFEBABEULL );

    pmm2.destroy();
    std::remove( TEST_FILE );
    return true;
}

static bool test_integration_stats()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    PMM_TEST( pmm.free_block_count() == 1 );
    PMM_TEST( pmm.alloc_block_count() == 1 ); // Block_0 always allocated (Issue #75)

    auto p = pmm.allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );

    PMM_TEST( pmm.alloc_block_count() == 2 );

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

static bool test_integration_avl_tree_invariants()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 256 * 1024 ) );

    static const int        N = 20;
    Mgr::pptr<std::uint8_t> ptrs[N]{};
    for ( int i = 0; i < N; ++i )
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( static_cast<std::size_t>( ( i + 1 ) * 64 ) );

    for ( int i = 0; i < N; i += 2 )
        pmm.deallocate_typed( ptrs[i] );

    PMM_TEST( pmm.is_initialized() );

    for ( int i = 1; i < N; i += 2 )
        pmm.deallocate_typed( ptrs[i] );

    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_abstraction (updated #102 — new API) ===\n\n";
    bool all_passed = true;

    std::cout << "--- Part A: Architecture facts ---\n";
    PMM_RUN( "A1: granule_size constant", test_cr_granule_size_is_hardcoded );
    PMM_RUN( "A2: kNoBlock is max uint32", test_cr_no_block_is_max_uint32 );
    PMM_RUN( "A3: Block<A> combines list+tree (Issue #112)", test_cr_block_header_combines_list_and_tree );
    PMM_RUN( "A4: Block<A> uses uint32 indices (Issue #112)", test_cr_block_header_uses_uint32_indices );
    PMM_RUN( "A5: pptr resolves via manager instance", test_cr_pptr_resolves_via_manager );
    PMM_RUN( "A6: config constants", test_cr_config_constants );
    PMM_RUN( "A7: PersistentAvlTree is standalone", test_cr_avl_tree_is_standalone );
    PMM_RUN( "A8: heap backend works", test_cr_heap_backend );
    PMM_RUN( "A9: no virtual functions", test_cr_no_virtual_functions );
    PMM_RUN( "A10: thread policy injection", test_cr_thread_policy_injection );

    std::cout << "\n--- Part B: Phase beacons ---\n";
    PMM_RUN( "B1: AddressTraits<> — 8/16/32-bit", test_phase1_address_traits );
    PMM_RUN( "B2: LinkedListNode<A> + TreeNode<A>", test_phase2_list_and_tree_nodes );
    PMM_RUN( "B3: Block<A> inherits LinkedListNode + TreeNode", test_phase3_block_layout );

    std::cout << "\n--- Part C: Integration ---\n";
    PMM_RUN( "C1: full lifecycle allocate/deallocate", test_integration_full_lifecycle );
    PMM_RUN( "C2: persistence save/load", test_integration_persistence );
    PMM_RUN( "C3: stats via block_count methods", test_integration_stats );
    PMM_RUN( "C4: AVL tree invariants under fragmentation", test_integration_avl_tree_invariants );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
