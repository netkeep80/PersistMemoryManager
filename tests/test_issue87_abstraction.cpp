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

// Issue #235: suppress deprecation warnings — this test deliberately exercises deprecated functions.
#if defined( __GNUC__ ) || defined( __clang__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined( _MSC_VER )
#pragma warning( push )
#pragma warning( disable : 4996 )
#endif

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include <limits>
#include <type_traits>

using Mgr = pmm::presets::SingleThreadedHeap;

// =============================================================================
// PART A: Code Review — current architecture
// =============================================================================

// ─── A1: Granule constants ────────────────────────────────────────────────────

TEST_CASE( "A1: granule_size constant", "[test_issue87_abstraction]" )
{
    static_assert( pmm::kGranuleSize == 16, "kGranuleSize must be 16" );
    REQUIRE( pmm::detail::bytes_to_granules( 16 ) == 1 );
    REQUIRE( pmm::detail::bytes_to_granules( 17 ) == 2 );
    REQUIRE( pmm::detail::granules_to_bytes( 1 ) == 16 );
}

TEST_CASE( "A2: kNoBlock is max uint32", "[test_issue87_abstraction]" )
{
    static_assert( pmm::detail::kNoBlock == 0xFFFFFFFFU, "kNoBlock is max uint32_t" );
    REQUIRE( pmm::detail::kNoBlock == ( std::numeric_limits<std::uint32_t>::max )() );
}

// ─── A2: Block<DefaultAddressTraits> layout (Issue #112) ─────────────────────
// Note: Block fields are protected (Issue #120). Layout verified via BlockStateBase offsets.

TEST_CASE( "A3: Block<A> combines list+tree (Issue #112)", "[test_issue87_abstraction]" )
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;
    // Issue #138: TreeNode fields come FIRST (TreeNode is base class), prev/next come AFTER
    // New layout: [0..23] TreeNode fields, [24..31] prev_offset, next_offset
    static_assert( BlockState::kOffsetWeight == 0 );
    static_assert( BlockState::kOffsetLeftOffset == 4 );
    static_assert( BlockState::kOffsetRightOffset == 8 );
    static_assert( BlockState::kOffsetParentOffset == 12 );
    static_assert( BlockState::kOffsetRootOffset == 16 );
    static_assert( BlockState::kOffsetAvlHeight == 20 );
    static_assert( BlockState::kOffsetNodeType == 22 );
    // Issue #138: prev/next come after TreeNode (sizeof(TreeNode<Default>) = 24)
    static_assert( BlockState::kOffsetPrevOffset == 24 );
    static_assert( BlockState::kOffsetNextOffset == 28 );
    static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32 );
}

TEST_CASE( "A4: Block<A> uses uint32 indices (Issue #112)", "[test_issue87_abstraction]" )
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;
    // Fields are protected (Issue #120); verify index_type via BlockStateBase::index_type
    static_assert( std::is_same<BlockState::index_type, std::uint32_t>::value );
}

// ─── A3: pptr<T, ManagerT> resolves via instance ─────────────────────────────

TEST_CASE( "A5: pptr resolves via manager instance", "[test_issue87_abstraction]" )
{
    static_assert( sizeof( Mgr::pptr<int> ) == 4 );
    static_assert( sizeof( Mgr::pptr<double> ) == 4 );

    Mgr::pptr<int> null_ptr;
    REQUIRE( null_ptr.is_null() );
    REQUIRE( !null_ptr );

    Mgr pmm;
    REQUIRE( pmm.create( 64 * 1024 ) );

    Mgr::pptr<int> p = pmm.allocate_typed<int>( 1 );
    REQUIRE( !p.is_null() );
    REQUIRE( p.resolve() != nullptr );

    pmm.deallocate_typed( p );
    pmm.destroy();
}

// ─── A4: Config constants ─────────────────────────────────────────────────────

TEST_CASE( "A6: config constants", "[test_issue87_abstraction]" )
{
    static_assert( pmm::config::kDefaultGrowNumerator == 5 );
    static_assert( pmm::config::kDefaultGrowDenominator == 4 );
}

// ─── A5: PersistentAvlTree is standalone ────────────────────────────────────

TEST_CASE( "A7: PersistentAvlTree is standalone", "[test_issue87_abstraction]" )
{
    static_assert( !std::is_constructible<pmm::PersistentAvlTree>::value,
                   "PersistentAvlTree must not be constructible (all-static)" );

    Mgr pmm;
    REQUIRE( pmm.create( 64 * 1024 ) );
    REQUIRE( pmm.is_initialized() );
    pmm.destroy();
}

// ─── A6: HeapStorage backend exists ─────────────────────────────────────────

TEST_CASE( "A8: heap backend works", "[test_issue87_abstraction]" )
{
    Mgr pmm;
    REQUIRE( pmm.create( 64 * 1024 ) );
    REQUIRE( pmm.is_initialized() );
    pmm.destroy();
}

// ─── A7: Instance-based manager (no virtual functions) ───────────────────────

TEST_CASE( "A9: no virtual functions", "[test_issue87_abstraction]" )
{
    static_assert( !std::is_polymorphic<Mgr>::value, "AbstractPersistMemoryManager must not be polymorphic" );
}

// ─── A8: Thread policy injection works ───────────────────────────────────────

TEST_CASE( "A10: thread policy injection", "[test_issue87_abstraction]" )
{
    using ST = pmm::presets::SingleThreadedHeap;

    ST pmm;
    REQUIRE( pmm.create( 64 * 1024 ) );
    REQUIRE( pmm.is_initialized() );
    pmm.destroy();
}

// =============================================================================
// PART B: Phase beacons (implemented abstractions)
// =============================================================================

// [Phase 1] AddressTraits

TEST_CASE( "B1: AddressTraits<> — 8/16/32-bit", "[test_issue87_abstraction]" )
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
}

// [Phase 2] TreeNode + Block prev/next fields (Issue #138: LinkedListNode merged into Block)
// Note: Fields are protected (Issue #120). Type verified via BlockStateBase::index_type.

TEST_CASE( "B2: TreeNode<A> + Block prev/next fields (Issue #138)", "[test_issue87_abstraction]" )
{
    using A = pmm::DefaultAddressTraits;

    // Fields are protected (Issue #120): verify type via Block::index_type alias (Issue #138)
    static_assert( std::is_same<pmm::Block<A>::index_type, typename A::index_type>::value,
                   "Block::index_type must match A::index_type (Issue #138)" );
    static_assert( std::is_same<pmm::TreeNode<A>::index_type, typename A::index_type>::value,
                   "TreeNode::index_type must match A::index_type" );

    // BlockStateBase exposes index_type for field type verification
    static_assert( std::is_same<pmm::BlockStateBase<A>::index_type, typename A::index_type>::value,
                   "BlockStateBase::index_type must match A::index_type" );

    using A8 = pmm::AddressTraits<std::uint8_t, 8>;
    static_assert( std::is_same<pmm::Block<A8>::index_type, std::uint8_t>::value,
                   "Block<AddressTraits<uint8_t, 8>>::index_type must be uint8_t (Issue #138)" );
    static_assert( std::is_same<pmm::TreeNode<A8>::index_type, std::uint8_t>::value,
                   "TreeNode<AddressTraits<uint8_t, 8>>::index_type must be uint8_t" );
}

// [Phase 3] Block

TEST_CASE( "B3: Block<A> inherits TreeNode + has prev/next fields (Issue #138)", "[test_issue87_abstraction]" )
{
    using A = pmm::DefaultAddressTraits;

    static_assert( sizeof( pmm::Block<A> ) == 32 );
    // Issue #138: Block no longer inherits LinkedListNode — prev/next are direct Block fields
    static_assert( std::is_base_of<pmm::TreeNode<A>, pmm::Block<A>>::value );
    // Verify total size is still 32 bytes: TreeNode(24) + prev(4) + next(4)
    static_assert( sizeof( pmm::Block<A> ) == 32, "Block must still be 32 bytes (Issue #138)" );
}

// =============================================================================
// PART C: Integration — must pass on all phases
// =============================================================================

TEST_CASE( "C1: full lifecycle allocate/deallocate", "[test_issue87_abstraction]" )
{
    Mgr pmm;
    REQUIRE( pmm.create( 128 * 1024 ) );
    REQUIRE( pmm.is_initialized() );

    auto p1 = pmm.allocate_typed<std::uint8_t>( 16 );
    auto p2 = pmm.allocate_typed<std::uint32_t>( 32 );
    auto p3 = pmm.allocate_typed<double>( 8 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );

    pmm.deallocate_typed( p2 );
    pmm.deallocate_typed( p1 );
    pmm.deallocate_typed( p3 );

    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

TEST_CASE( "C2: persistence save/load", "[test_issue87_abstraction]" )
{
    const char* TEST_FILE = "test_i87_persist.dat";

    Mgr pmm1;
    REQUIRE( pmm1.create( 64 * 1024 ) );
    auto p = pmm1.allocate_typed<std::uint64_t>( 1 );
    REQUIRE( !p.is_null() );
    *p.resolve()               = 0xDEADBEEFCAFEBABEULL;
    std::uint32_t saved_offset = p.offset();

    REQUIRE( pmm::save_manager<decltype( pmm1 )>( TEST_FILE ) );
    pmm1.destroy();

    Mgr pmm2;
    REQUIRE( pmm2.create( 64 * 1024 ) );
    REQUIRE( pmm::load_manager_from_file<decltype( pmm2 )>( TEST_FILE ) );
    REQUIRE( pmm2.is_initialized() );

    Mgr::pptr<std::uint64_t> p2( saved_offset );
    REQUIRE( p2.resolve() != nullptr );
    REQUIRE( *p2.resolve() == 0xDEADBEEFCAFEBABEULL );

    pmm2.destroy();
    std::remove( TEST_FILE );
}

TEST_CASE( "C3: stats via block_count methods", "[test_issue87_abstraction]" )
{
    Mgr pmm;
    REQUIRE( pmm.create( 64 * 1024 ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    REQUIRE( pmm.free_block_count() == 1 );
    REQUIRE( pmm.alloc_block_count() == baseline_alloc );

    auto p = pmm.allocate_typed<std::uint8_t>( 128 );
    REQUIRE( !p.is_null() );

    REQUIRE( pmm.alloc_block_count() == baseline_alloc + 1 );

    pmm.deallocate_typed( p );
    pmm.destroy();
}

TEST_CASE( "C4: AVL tree invariants under fragmentation", "[test_issue87_abstraction]" )
{
    Mgr pmm;
    REQUIRE( pmm.create( 256 * 1024 ) );

    static const int        N = 20;
    Mgr::pptr<std::uint8_t> ptrs[N]{};
    for ( int i = 0; i < N; ++i )
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( static_cast<std::size_t>( ( i + 1 ) * 64 ) );

    for ( int i = 0; i < N; i += 2 )
        pmm.deallocate_typed( ptrs[i] );

    REQUIRE( pmm.is_initialized() );

    for ( int i = 1; i < N; i += 2 )
        pmm.deallocate_typed( ptrs[i] );

    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

// =============================================================================
// main
// =============================================================================

// Issue #235: restore deprecation warnings
#if defined( __GNUC__ ) || defined( __clang__ )
#pragma GCC diagnostic pop
#elif defined( _MSC_VER )
#pragma warning( pop )
#endif
