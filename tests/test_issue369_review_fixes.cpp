/**
 * @file test_issue369_review_fixes.cpp
 * @brief Issue #369 review follow-up: tighten NodeType semantics and weight verification.
 *
 * Covers the four review-blocking points raised on PR #370:
 *   - NodeType::Free is no longer reported as user/PAP-deletable, and
 *     deallocate() refuses to act on a Free block (defence in depth even when
 *     the deletable bit accidentally returns true).
 *   - The reserved PMM NodeType gap stays non-allocated; application-defined
 *     values are covered by the issue #392 contract tests.
 *   - The typed allocation paths assign a logical NodeType (PStringView,
 *     PString, PArray, PMap, PPtr) instead of leaving every block as Generic.
 *   - verify catches a fabricated cached-weight corruption on a free block by
 *     comparing weight against the physical span derived from neighbours.
 */

#include "pmm/block_header.h"
#include "pmm/block_state.h"
#include "pmm/diagnostics.h"
#include "pmm/parray.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmap.h"
#include "pmm/pmm_presets.h"
#include "pmm/pptr.h"
#include "pmm/pstring.h"
#include "pmm/pstringview.h"
#include "pmm/types.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <type_traits>

using Mgr = pmm::presets::SingleThreadedHeap;
using AT  = pmm::DefaultAddressTraits;

// ─── 1. NodeType property table tightening ────────────────────────────────────

TEST_CASE( "NodeType::Free is not user/PAP-deletable", "[issue369][review]" )
{
    static_assert( !pmm::can_be_deleted_from_pap( pmm::NodeType::Free ) );
    static_assert( !pmm::is_allocated( pmm::NodeType::Free ) );
    static_assert( pmm::can_be_deleted_from_pap( pmm::NodeType::Generic ) );
    static_assert( pmm::can_be_deleted_from_pap( pmm::NodeType::PStringView ) );
    static_assert( pmm::can_be_deleted_from_pap( pmm::NodeType::PString ) );
    static_assert( pmm::can_be_deleted_from_pap( pmm::NodeType::PArray ) );
    static_assert( pmm::can_be_deleted_from_pap( pmm::NodeType::PMap ) );
    static_assert( pmm::can_be_deleted_from_pap( pmm::NodeType::PPtr ) );
    static_assert( !pmm::can_be_deleted_from_pap( pmm::NodeType::ManagerHeader ) );
    static_assert( !pmm::can_be_deleted_from_pap( pmm::NodeType::ReadOnlyLocked ) );
}

TEST_CASE( "is_allocated is closed-world: every known type maps explicitly", "[issue369][review]" )
{
    static_assert( !pmm::is_allocated( pmm::NodeType::Free ) );
    static_assert( pmm::is_allocated( pmm::NodeType::ManagerHeader ) );
    static_assert( pmm::is_allocated( pmm::NodeType::Generic ) );
    static_assert( pmm::is_allocated( pmm::NodeType::ReadOnlyLocked ) );
    static_assert( pmm::is_allocated( pmm::NodeType::PStringView ) );
    static_assert( pmm::is_allocated( pmm::NodeType::PString ) );
    static_assert( pmm::is_allocated( pmm::NodeType::PArray ) );
    static_assert( pmm::is_allocated( pmm::NodeType::PMap ) );
    static_assert( pmm::is_allocated( pmm::NodeType::PPtr ) );
}

TEST_CASE( "is_allocated rejects a reserved NodeType enum value", "[issue369][review]" )
{
    constexpr auto kReserved = static_cast<pmm::NodeType>( pmm::kApplicationNodeTypeFirst - 1 );
    REQUIRE_FALSE( pmm::is_allocated( kReserved ) );
    REQUIRE_FALSE( pmm::is_known_node_type( static_cast<std::uint8_t>( kReserved ) ) );
}

// ─── 2. deallocate() refuses to free a Free block ─────────────────────────────

TEST_CASE( "deallocate() of a Free-typed block is rejected even if it leaks past helpers",
           "[issue369][review][safety]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    void* user = Mgr::allocate( 32 );
    REQUIRE( user != nullptr );

    using BlockState  = pmm::BlockStateBase<AT>;
    auto*      base   = Mgr::backend().base_ptr();
    const auto off    = static_cast<std::size_t>( static_cast<std::uint8_t*>( user ) - base );
    void*      blk    = base + off - sizeof( pmm::Block<AT> );
    const auto weight = BlockState::get_weight( blk );

    // Forge a free-typed header on top of an allocated block. Post-#369
    // a free block has weight != 0, so the legacy `weight == 0` short-circuit
    // does not protect deallocate() any more — only the type check does.
    BlockState::set_node_type_of( blk, pmm::NodeType::Free );
    REQUIRE( BlockState::get_weight( blk ) == weight );

    // Must be a no-op: deallocate refuses to act on a non-allocated block.
    Mgr::deallocate( user );

    // The header must remain a Free record — neither freed via PAP nor
    // mutated into a free-list node by accident.
    REQUIRE( BlockState::get_node_type( blk ) == pmm::NodeType::Free );

    Mgr::destroy();
}

// ─── 3. Typed allocation paths assign logical NodeType ────────────────────────

TEST_CASE( "allocate_typed<T> tags the block with node_type_for_v<T>", "[issue369][review][typed]" )
{
    static_assert( pmm::node_type_for_v<pmm::pstring<Mgr>> == pmm::NodeType::PString );
    static_assert( pmm::node_type_for_v<pmm::pstringview<Mgr>> == pmm::NodeType::PStringView );
    static_assert( pmm::node_type_for_v<pmm::parray<int, Mgr>> == pmm::NodeType::PArray );
    static_assert( pmm::node_type_for_v<pmm::pmap<int, int, Mgr>> == pmm::NodeType::PMap );
    static_assert( pmm::node_type_for_v<pmm::pptr<int, Mgr>> == pmm::NodeType::PPtr );

    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    using BlockState = pmm::BlockStateBase<AT>;

    auto check_node_type = []( void* user, pmm::NodeType expected )
    {
        REQUIRE( user != nullptr );
        auto*      base = Mgr::backend().base_ptr();
        const auto off  = static_cast<std::size_t>( static_cast<std::uint8_t*>( user ) - base );
        void*      blk  = base + off - sizeof( pmm::Block<AT> );
        REQUIRE( BlockState::get_node_type( blk ) == expected );
    };

    auto p_str  = Mgr::allocate_typed<pmm::pstring<Mgr>>();
    auto p_view = Mgr::allocate_typed<pmm::pstringview<Mgr>>();
    auto p_arr  = Mgr::allocate_typed<pmm::parray<int, Mgr>>();
    auto p_map  = Mgr::allocate_typed<pmm::pmap<int, int, Mgr>>();
    auto p_ptr  = Mgr::allocate_typed<pmm::pptr<int, Mgr>>();

    REQUIRE_FALSE( p_str.is_null() );
    REQUIRE_FALSE( p_view.is_null() );
    REQUIRE_FALSE( p_arr.is_null() );
    REQUIRE_FALSE( p_map.is_null() );
    REQUIRE_FALSE( p_ptr.is_null() );

    check_node_type( Mgr::resolve_unchecked<pmm::pstring<Mgr>>( p_str ), pmm::NodeType::PString );
    check_node_type( Mgr::resolve_unchecked<pmm::pstringview<Mgr>>( p_view ), pmm::NodeType::PStringView );
    check_node_type( Mgr::resolve_unchecked<pmm::parray<int, Mgr>>( p_arr ), pmm::NodeType::PArray );
    check_node_type( Mgr::resolve_unchecked<pmm::pmap<int, int, Mgr>>( p_map ), pmm::NodeType::PMap );
    check_node_type( Mgr::resolve_unchecked<pmm::pptr<int, Mgr>>( p_ptr ), pmm::NodeType::PPtr );

    Mgr::destroy();
}

TEST_CASE( "create_typed<T> tags the block with node_type_for_v<T>", "[issue369][review][typed]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    using BlockState = pmm::BlockStateBase<AT>;

    auto p_str = Mgr::create_typed<pmm::pstring<Mgr>>();
    REQUIRE_FALSE( p_str.is_null() );

    void*      user = Mgr::resolve_unchecked<pmm::pstring<Mgr>>( p_str );
    auto*      base = Mgr::backend().base_ptr();
    const auto off  = static_cast<std::size_t>( static_cast<std::uint8_t*>( user ) - base );
    void*      blk  = base + off - sizeof( pmm::Block<AT> );

    REQUIRE( BlockState::get_node_type( blk ) == pmm::NodeType::PString );

    Mgr::destroy();
}

// ─── 4. verify() detects fabricated cached-weight corruption on free blocks ──

TEST_CASE( "verify reports a free block whose cached weight does not match the physical span",
           "[issue369][review][verify]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    // Create a free block in the middle of the arena: alloc, alloc-after, then
    // free the first allocation. The first allocation cannot coalesce with the
    // ManagerHeader-pinned head, but the resulting Free block has a fixed
    // physical span equal to its data + header.
    void* a = Mgr::allocate( 64 );
    void* b = Mgr::allocate( 64 );
    REQUIRE( a != nullptr );
    REQUIRE( b != nullptr );
    Mgr::deallocate( a );

    // Locate the freed block header.
    auto*      base   = Mgr::backend().base_ptr();
    const auto off_b  = static_cast<std::size_t>( static_cast<std::uint8_t*>( b ) - base );
    void*      blk_b  = base + off_b - sizeof( pmm::Block<AT> );
    using BlockState  = pmm::BlockStateBase<AT>;
    const auto prev_b = BlockState::get_prev_offset( blk_b );
    REQUIRE( prev_b != AT::no_block );
    void* free_blk = base + static_cast<std::size_t>( prev_b ) * AT::granule_size;
    REQUIRE( pmm::is_free( BlockState::get_node_type( free_blk ) ) );

    // Sanity check: verify is clean before we corrupt anything.
    {
        pmm::VerifyResult r = Mgr::verify();
        REQUIRE( r.ok );
    }

    // Corrupt the cached weight to a value that the AVL ordering can still
    // tolerate, but that diverges from the physical block span derived from
    // neighbours.
    const auto good_weight = BlockState::get_weight( free_blk );
    BlockState::set_weight_of( free_blk, static_cast<AT::index_type>( good_weight + 1 ) );

    pmm::VerifyResult r = Mgr::verify();
    REQUIRE_FALSE( r.ok );
    bool found_weight_violation = false;
    for ( std::size_t i = 0; i < r.entry_count; ++i )
    {
        if ( r.entries[i].type == pmm::ViolationType::BlockStateInconsistent &&
             r.entries[i].expected == static_cast<std::uint64_t>( good_weight ) &&
             r.entries[i].actual == static_cast<std::uint64_t>( good_weight + 1 ) )
        {
            found_weight_violation = true;
            break;
        }
    }
    REQUIRE( found_weight_violation );

    // Restore so destroy() does not trip release-mode assertions.
    BlockState::set_weight_of( free_blk, good_weight );

    Mgr::destroy();
}
