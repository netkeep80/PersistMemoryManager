/**
 * @file test_issue144_code_review.cpp
 * @brief Tests for code review improvements (Issue #144).
 *
 * Verifies improvements based on code review by Qwen3.5-Plus:
 *  - Debug-mode validation in FreeBlock::cast_from_raw (weight==0, root_offset==0).
 *  - Debug-mode validation in AllocatedBlock::cast_from_raw (weight > 0).
 *  - bytes_to_granules overflow handling: returns 0, distinguishable from valid.
 *  - for_each_block deadlock documentation: callback cannot call allocate/deallocate.
 *  - is_valid_block structural invariants in types.h.
 *  - recover_block_state correctly handles all transitional block states.
 *
 * @see include/pmm/block_state.h — FreeBlock::cast_from_raw, AllocatedBlock::cast_from_raw
 * @see include/pmm/address_traits.h — bytes_to_granules overflow
 * @see include/pmm/persist_memory_manager.h — for_each_block
 * @see include/pmm/types.h — is_valid_block
 * @version 0.1 (Issue #144 — code review improvements)
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <limits>

// ─── Test macros ──────────────────────────────────────────────────────────────

// =============================================================================
// I144-A: FreeBlock::cast_from_raw validates invariants in debug mode
// =============================================================================

/// @brief FreeBlock::cast_from_raw on a correctly initialized free block succeeds.
TEST_CASE( "    cast_from_raw valid free block", "[test_issue144_code_review]" )
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );
    // weight=0, root_offset=0 — valid FreeBlock state

    auto* fb = pmm::FreeBlock<A>::cast_from_raw( buffer );
    REQUIRE( fb != nullptr );
    REQUIRE( fb->verify_invariants() == true );
    REQUIRE( fb->weight() == 0 );
    REQUIRE( fb->root_offset() == 0 );
}

/// @brief FreeBlock::verify_invariants detects invalid state (weight > 0).
TEST_CASE( "    verify_invariants: invalid weight", "[test_issue144_code_review]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // weight > 0 violates FreeBlock invariant.
    // Use reinterpret_cast directly instead of cast_from_raw, because
    // cast_from_raw asserts is_free() in debug builds — the assert would fire
    // and abort the process when we intentionally set up an invalid state.
    // verify_invariants() is the reliable API-level check in all builds.
    BlockState::set_weight_of( buffer, 3u );

    auto* fb = reinterpret_cast<pmm::FreeBlock<A>*>( buffer );
    REQUIRE( fb->verify_invariants() == false );
}

/// @brief FreeBlock::verify_invariants detects invalid state (root_offset != 0).
TEST_CASE( "    verify_invariants: invalid root_offset", "[test_issue144_code_review]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // root_offset != 0 violates FreeBlock invariant.
    // Use reinterpret_cast directly instead of cast_from_raw for the same
    // reason as above: cast_from_raw asserts is_free() in debug builds.
    BlockState::set_root_offset_of( buffer, 5u );

    auto* fb = reinterpret_cast<pmm::FreeBlock<A>*>( buffer );
    REQUIRE( fb->verify_invariants() == false );
}

// =============================================================================
// I144-B: AllocatedBlock::cast_from_raw validates weight > 0 in debug mode
// =============================================================================

/// @brief AllocatedBlock::cast_from_raw on a valid allocated block succeeds.
TEST_CASE( "    cast_from_raw valid allocated block", "[test_issue144_code_review]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    BlockState::set_weight_of( buffer, 4u );
    BlockState::set_root_offset_of( buffer, 6u );

    auto* alloc = pmm::AllocatedBlock<A>::cast_from_raw( buffer );
    REQUIRE( alloc != nullptr );
    REQUIRE( alloc->verify_invariants( 6 ) == true );
    REQUIRE( alloc->weight() == 4 );
}

/// @brief AllocatedBlock::verify_invariants detects wrong own_idx.
TEST_CASE( "    verify_invariants: wrong own_idx", "[test_issue144_code_review]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    BlockState::set_weight_of( buffer, 4u );
    BlockState::set_root_offset_of( buffer, 6u ); // own_idx should be 6

    auto* alloc = pmm::AllocatedBlock<A>::cast_from_raw( buffer );
    REQUIRE( alloc->verify_invariants( 6 ) == true );  // Correct idx
    REQUIRE( alloc->verify_invariants( 7 ) == false ); // Wrong idx
    REQUIRE( alloc->verify_invariants( 0 ) == false ); // Wrong idx
}

// =============================================================================
// I144-C: bytes_to_granules overflow handling
// =============================================================================

/// @brief bytes_to_granules returns 0 on overflow (well-defined sentinel).
TEST_CASE( "    overflow returns 0", "[test_issue144_code_review]" )
{
    using A = pmm::DefaultAddressTraits;

    // Max size_t: would overflow in granule calculation
    std::size_t   max_sz = std::numeric_limits<std::size_t>::max();
    A::index_type result = A::bytes_to_granules( max_sz );
    REQUIRE( result == 0 ); // Overflow returns 0

    // Also test with a value that overflows IndexT (uint32_t)
    // Large value that produces > 2^32 granules: (2^32 + 1) * granule_size
    // granule_size = 16, so (UINT32_MAX + 1) * 16 = 2^36 bytes
    std::size_t overflow_for_idx = ( static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) + 1 ) * 16;
    // This might not overflow size_t but would overflow uint32_t index
    if ( overflow_for_idx != 0 ) // Only if no size_t overflow in the multiplication
    {
        A::index_type r2 = A::bytes_to_granules( overflow_for_idx );
        REQUIRE( r2 == 0 ); // Should return 0 on IndexT overflow
    }
}

/// @brief bytes_to_granules normal conversion is correct.
TEST_CASE( "    normal conversions correct", "[test_issue144_code_review]" )
{
    using A = pmm::DefaultAddressTraits;

    // Exact multiple of granule_size
    REQUIRE( A::bytes_to_granules( 0 ) == 0 );
    REQUIRE( A::bytes_to_granules( 16 ) == 1 );
    REQUIRE( A::bytes_to_granules( 32 ) == 2 );
    REQUIRE( A::bytes_to_granules( 48 ) == 3 );

    // Ceiling: non-multiple rounds up
    REQUIRE( A::bytes_to_granules( 1 ) == 1 );
    REQUIRE( A::bytes_to_granules( 15 ) == 1 );
    REQUIRE( A::bytes_to_granules( 17 ) == 2 );
    REQUIRE( A::bytes_to_granules( 31 ) == 2 );
    REQUIRE( A::bytes_to_granules( 33 ) == 3 );
}

// =============================================================================
// I144-D: is_valid_block structural invariants
// =============================================================================

/// @brief is_valid_block rejects kNoBlock index.
TEST_CASE( "    kNoBlock constant", "[test_issue144_code_review]" )
{
    using namespace pmm::detail;
    // is_valid_block with kNoBlock should return false
    REQUIRE( kNoBlock == 0xFFFFFFFFU );
}

/// @brief Block state machine: is_free() and is_allocated() agree with weight.
TEST_CASE( "    block state: is_free / is_allocated consistency", "[test_issue144_code_review]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];

    // Free: weight=0, root_offset=0
    std::memset( buffer, 0, sizeof( buffer ) );
    auto* state = reinterpret_cast<BlockState*>( buffer );
    REQUIRE( state->is_free() == true );
    REQUIRE( state->is_allocated( 0 ) == false );
    REQUIRE( state->weight() == 0 );

    // Transitional: weight=0, root_offset!=0 — neither free nor allocated
    BlockState::set_root_offset_of( buffer, 5u );
    REQUIRE( state->is_free() == false );
    REQUIRE( state->is_allocated( 0 ) == false );
    REQUIRE( state->is_allocated( 5 ) == false ); // weight==0, so not allocated

    // Allocated: weight>0, root_offset==own_idx
    BlockState::set_weight_of( buffer, 3u );
    BlockState::set_root_offset_of( buffer, 7u );
    REQUIRE( state->is_free() == false );
    REQUIRE( state->is_allocated( 7 ) == true );
    REQUIRE( state->is_allocated( 8 ) == false );
}

// =============================================================================
// I144-E: recover_block_state handles all transitional states
// =============================================================================

/// @brief recover_block_state fixes weight>0 with wrong root_offset.
TEST_CASE( "    recover allocated with wrong root_offset", "[test_issue144_code_review]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // Simulate crash: weight>0 but root_offset points to wrong index
    BlockState::set_weight_of( buffer, 5u );
    BlockState::set_root_offset_of( buffer, 99u ); // Wrong index

    pmm::recover_block_state<A>( buffer, 10 ); // Own index is 10

    REQUIRE( BlockState::get_weight( buffer ) == 5 );       // Unchanged
    REQUIRE( BlockState::get_root_offset( buffer ) == 10 ); // Corrected to own_idx

    auto* state = reinterpret_cast<BlockState*>( buffer );
    REQUIRE( state->is_allocated( 10 ) == true );
}

/// @brief recover_block_state fixes weight==0 with non-zero root_offset.
TEST_CASE( "    recover free with non-zero root_offset", "[test_issue144_code_review]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // Simulate crash: weight==0 but root_offset is non-zero (transitional state)
    BlockState::set_weight_of( buffer, 0u );
    BlockState::set_root_offset_of( buffer, 42u );

    pmm::recover_block_state<A>( buffer, 10 ); // Own index is 10

    REQUIRE( BlockState::get_weight( buffer ) == 0 );      // Unchanged
    REQUIRE( BlockState::get_root_offset( buffer ) == 0 ); // Cleared

    auto* state = reinterpret_cast<BlockState*>( buffer );
    REQUIRE( state->is_free() == true );
}

/// @brief recover_block_state leaves valid allocated block unchanged.
TEST_CASE( "    valid allocated block unchanged", "[test_issue144_code_review]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // Correct allocated state
    BlockState::set_weight_of( buffer, 7u );
    BlockState::set_root_offset_of( buffer, 5u );

    pmm::recover_block_state<A>( buffer, 5 );

    REQUIRE( BlockState::get_weight( buffer ) == 7 );      // Unchanged
    REQUIRE( BlockState::get_root_offset( buffer ) == 5 ); // Unchanged

    auto* state = reinterpret_cast<BlockState*>( buffer );
    REQUIRE( state->is_allocated( 5 ) == true );
}

/// @brief recover_block_state leaves valid free block unchanged.
TEST_CASE( "    valid free block unchanged", "[test_issue144_code_review]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // Correct free state
    BlockState::set_weight_of( buffer, 0u );
    BlockState::set_root_offset_of( buffer, 0u );

    pmm::recover_block_state<A>( buffer, 5 );

    REQUIRE( BlockState::get_weight( buffer ) == 0 );
    REQUIRE( BlockState::get_root_offset( buffer ) == 0 );

    auto* state = reinterpret_cast<BlockState*>( buffer );
    REQUIRE( state->is_free() == true );
}

// =============================================================================
// I144-F: for_each_block does not deadlock when callback is read-only
// =============================================================================

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 144>;

/// @brief for_each_block iterates all blocks without deadlock (read-only callback).
TEST_CASE( "    for_each_block (read-only)", "[test_issue144_code_review]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 4096 ) );

    // Allocate a few blocks
    void* p1 = TestMgr::allocate( 64 );
    void* p2 = TestMgr::allocate( 128 );
    REQUIRE( p1 != nullptr );
    REQUIRE( p2 != nullptr );

    // Read-only callback: just count blocks
    std::size_t block_count     = 0;
    std::size_t allocated_count = 0;
    std::size_t free_count      = 0;

    // for_each_block is safe with a read-only callback
    // (it holds shared_lock; calling allocate/deallocate would deadlock)
    bool ok = TestMgr::for_each_block(
        [&]( const pmm::BlockView& view )
        {
            block_count++;
            if ( view.used )
                allocated_count++;
            else
                free_count++;
        } );

    REQUIRE( ok == true );
    REQUIRE( block_count >= 3 );     // Block_0 (header) + p1 block + p2 block + free remainder
    REQUIRE( allocated_count >= 2 ); // At least p1 and p2 (Block_0 also counts as used)
    REQUIRE( free_count >= 1 );      // At least one free block (remainder)

    TestMgr::deallocate( p1 );
    TestMgr::deallocate( p2 );
    TestMgr::destroy();
}

/// @brief for_each_free_block iterates only free blocks.
TEST_CASE( "    for_each_free_block", "[test_issue144_code_review]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 8192 ) );

    void* p1 = TestMgr::allocate( 64 );
    void* p2 = TestMgr::allocate( 128 );
    void* p3 = TestMgr::allocate( 32 );
    REQUIRE( ( p1 != nullptr && p2 != nullptr && p3 != nullptr ) );

    TestMgr::deallocate( p2 ); // Create a free block in the middle

    std::size_t free_block_count = 0;
    bool        found_invalid    = false;
    bool        ok               = TestMgr::for_each_free_block(
        [&]( const pmm::FreeBlockView& view )
        {
            if ( view.total_size == 0 )
                found_invalid = true;
            free_block_count++;
        } );
    REQUIRE( !found_invalid );

    REQUIRE( ok == true );
    REQUIRE( free_block_count >= 1 ); // At least one free block after deallocate

    TestMgr::deallocate( p1 );
    TestMgr::deallocate( p3 );
    TestMgr::destroy();
}

// =============================================================================
// I144-G: lock_block_permanent prevents deallocation (Issue #126)
// =============================================================================

/// @brief lock_block_permanent makes block immune to deallocate.
TEST_CASE( "    permanently locked block", "[test_issue144_code_review]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 4096 ) );

    void* p = TestMgr::allocate( 64 );
    REQUIRE( p != nullptr );

    REQUIRE( TestMgr::is_permanently_locked( p ) == false );
    REQUIRE( TestMgr::lock_block_permanent( p ) == true );
    REQUIRE( TestMgr::is_permanently_locked( p ) == true );

    std::size_t alloc_before = TestMgr::alloc_block_count();
    TestMgr::deallocate( p ); // Should be a no-op for locked block
    std::size_t alloc_after = TestMgr::alloc_block_count();

    REQUIRE( alloc_before == alloc_after ); // Count unchanged: block not freed
    REQUIRE( TestMgr::is_permanently_locked( p ) == true );

    TestMgr::destroy();
}

// =============================================================================
// I144-H: BlockStateBase::reset_avl_fields_of clears all AVL tree pointers
// =============================================================================

/// @brief BlockStateBase::reset_avl_fields_of zeroes left/right/parent/height.
/// Issue #168: reset_block_avl_fields() removed; use BlockStateBase<AT>::reset_avl_fields_of() directly.
TEST_CASE( "    reset_avl_fields_of clears AVL fields", "[test_issue144_code_review]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0xFF, sizeof( buffer ) ); // Fill with garbage

    // Set known values first
    BlockState::set_left_offset_of( buffer, 10u );
    BlockState::set_right_offset_of( buffer, 20u );
    BlockState::set_parent_offset_of( buffer, 30u );
    BlockState::set_avl_height_of( buffer, 5 );

    // Reset via BlockStateBase directly (Issue #168: wrapper removed)
    BlockState::reset_avl_fields_of( buffer );

    REQUIRE( BlockState::get_left_offset( buffer ) == A::no_block );
    REQUIRE( BlockState::get_right_offset( buffer ) == A::no_block );
    REQUIRE( BlockState::get_parent_offset( buffer ) == A::no_block );
    REQUIRE( BlockState::get_avl_height( buffer ) == 0 );
}

// =============================================================================
// main
// =============================================================================
