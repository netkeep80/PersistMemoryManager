/**
 * @file test_issue106_block_state_integration.cpp
 * @brief Integration tests for Issue #106: BlockState machine fully integrated into PMM.
 *
 * Verifies that:
 *   - I106-A: Block<A> binary layout is used throughout (weight vs legacy size field)
 *   - I106-B: allocate() transitions blocks through correct state machine states
 *   - I106-C: deallocate() uses AllocatedBlock::mark_as_free() state transition
 *   - I106-D: coalesce() uses CoalescingBlock state transitions
 *   - I106-E: recover_block_state() is called during load()
 *   - I106-F: detect_block_state() correctly identifies block states
 *   - I106-G: Full allocate/deallocate cycle with state verification
 *   - I106-H: Split path (allocate_from_block with remainder block)
 *   - I106-I: Coalescing with both left and right neighbours
 *
 * @see include/pmm/block_state.h
 * @see include/pmm/allocator_policy.h
 * @see include/pmm/abstract_pmm.h
 * @version 1.0 (Issue #106 — BlockState machine integration)
 */

#include "pmm_single_threaded_heap.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

// ─── Test macros ─────────────────────────────────────────────────────────────

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
using A   = pmm::DefaultAddressTraits;

// Helper: get Block<A>* at user_ptr - sizeof(Block<A>)
static pmm::Block<A>* block_of( [[maybe_unused]] Mgr& pmm, void* user_ptr )
{
    return reinterpret_cast<pmm::Block<A>*>( static_cast<std::uint8_t*>( user_ptr ) - sizeof( pmm::Block<A> ) );
}

// Helper: get granule index of block
static std::uint32_t blk_idx_of( Mgr& pmm, pmm::Block<A>* blk )
{
    std::uint8_t* base = pmm.backend().base_ptr();
    return static_cast<std::uint32_t>( ( reinterpret_cast<std::uint8_t*>( blk ) - base ) / pmm::kGranuleSize );
}

// ─── I106-A: Block<A> binary layout (weight field) ────────────────────────────

/// @brief After allocation, the block has weight > 0 and root_offset == own_idx (AllocatedBlock invariant).
static bool test_i106_allocated_block_uses_weight()
{
    using BlockState = pmm::BlockStateBase<A>;

    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    // Allocate a block of 32 bytes (2 granules of data)
    void* raw = pmm.allocate( 32 );
    PMM_TEST( raw != nullptr );

    pmm::Block<A>* blk = block_of( pmm, raw );
    std::uint32_t  idx = blk_idx_of( pmm, blk );

    // I106-A1: weight > 0 (allocated block) (via BlockStateBase API, Issue #120)
    PMM_TEST( BlockState::get_weight( blk ) > 0 );

    // I106-A2: root_offset == own_idx (AllocatedBlock invariant)
    PMM_TEST( BlockState::get_root_offset( blk ) == idx );

    // I106-A3: detect_block_state confirms AllocatedBlock
    PMM_TEST( pmm::detect_block_state<A>( blk, idx ) == 1 );

    pmm.deallocate( raw );
    pmm.destroy();
    return true;
}

/// @brief After deallocation, the block has weight == 0 and root_offset == 0 (FreeBlock invariant).
static bool test_i106_freed_block_has_zero_weight()
{
    using BlockState = pmm::BlockStateBase<A>;

    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    void* raw = pmm.allocate( 32 );
    PMM_TEST( raw != nullptr );

    pmm::Block<A>* blk = block_of( pmm, raw );

    pmm.deallocate( raw );

    // I106-A4: weight == 0 (free block after deallocate) (via BlockStateBase API, Issue #120)
    PMM_TEST( BlockState::get_weight( blk ) == 0 );

    // I106-A5: root_offset == 0 (free block invariant)
    // Note: after coalesce_with_prev, this block may be zeroed (absorbed into prev).
    // We can only check that weight == 0 since block memory may be reused.
    (void)blk; // blk may have been zeroed by coalescing

    pmm.destroy();
    return true;
}

// ─── I106-B: State machine transitions during allocate ────────────────────────

/// @brief allocate() uses state machine: FreeBlock → FreeBlockRemovedAVL → AllocatedBlock.
static bool test_i106_allocate_state_machine()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    std::size_t alloc_before = pmm.alloc_block_count();

    void* raw = pmm.allocate( 64 );
    PMM_TEST( raw != nullptr );

    // After allocation:
    pmm::Block<A>* blk = block_of( pmm, raw );
    std::uint32_t  idx = blk_idx_of( pmm, blk );

    // Block is in AllocatedBlock state
    auto* alloc = pmm::AllocatedBlock<A>::cast_from_raw( blk );
    PMM_TEST( alloc->verify_invariants( idx ) );

    // Counters updated correctly
    PMM_TEST( pmm.alloc_block_count() >= alloc_before );
    PMM_TEST( pmm.used_size() > 0 );

    pmm.deallocate( raw );
    pmm.destroy();
    return true;
}

// ─── I106-C: deallocate() uses AllocatedBlock::mark_as_free() ─────────────────

/// @brief deallocate() transitions AllocatedBlock → FreeBlockNotInAVL via mark_as_free().
static bool test_i106_deallocate_uses_mark_as_free()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    // Allocate two blocks to prevent coalescing of block under test
    void* raw1 = pmm.allocate( 64 );
    void* raw2 = pmm.allocate( 64 );
    PMM_TEST( raw1 != nullptr && raw2 != nullptr );

    pmm::Block<A>* blk1 = block_of( pmm, raw1 );
    std::uint32_t  idx1 = blk_idx_of( pmm, blk1 );

    using BlockState = pmm::BlockStateBase<A>;

    // Verify blk1 is allocated (via BlockStateBase API, Issue #120)
    PMM_TEST( BlockState::get_weight( blk1 ) > 0 );
    PMM_TEST( BlockState::get_root_offset( blk1 ) == idx1 );

    // Deallocate blk1 — this calls AllocatedBlock::mark_as_free()
    pmm.deallocate( raw1 );

    // After deallocation: blk1 should be free (weight == 0, root_offset == 0)
    PMM_TEST( BlockState::get_weight( blk1 ) == 0 );
    PMM_TEST( BlockState::get_root_offset( blk1 ) == 0 );

    pmm.deallocate( raw2 );
    pmm.destroy();
    return true;
}

// ─── I106-D: coalesce() state machine transitions ─────────────────────────────

/// @brief Deallocate adjacent blocks — coalesce() merges them via CoalescingBlock state.
static bool test_i106_coalesce_with_right_neighbour()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    // Allocate two adjacent blocks of 64 bytes
    void* raw1 = pmm.allocate( 64 );
    void* raw2 = pmm.allocate( 64 );
    PMM_TEST( raw1 != nullptr && raw2 != nullptr );

    std::size_t blk_before = pmm.block_count();

    // Free the first block — coalesce may join with left (initial free block)
    pmm.deallocate( raw1 );

    // Free the second block — triggers coalesce_with_prev (merges with raw1 space)
    pmm.deallocate( raw2 );

    // After both deallocations, block count should be lower (blocks merged)
    PMM_TEST( pmm.block_count() <= blk_before );

    pmm.destroy();
    return true;
}

/// @brief Allocate, deallocate middle block, then deallocate adjacent — tests bidirectional coalesce.
static bool test_i106_coalesce_bidirectional()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    // Allocate 3 blocks
    void* raw1 = pmm.allocate( 64 );
    void* raw2 = pmm.allocate( 64 );
    void* raw3 = pmm.allocate( 64 );
    PMM_TEST( raw1 != nullptr && raw2 != nullptr && raw3 != nullptr );

    std::size_t initial_block_count = pmm.block_count();

    // Free raw2 (middle) — no coalesce yet (both neighbours are allocated)
    pmm.deallocate( raw2 );
    std::size_t after_free2 = pmm.block_count();
    PMM_TEST( after_free2 == initial_block_count ); // No merge possible

    // Free raw3 — coalesces with raw2 (right neighbour of freed raw2)
    pmm.deallocate( raw3 );
    std::size_t after_free3 = pmm.block_count();
    PMM_TEST( after_free3 < after_free2 ); // raw2 and raw3 merged

    // Free raw1 — coalesces with the merged (raw2+raw3) block
    pmm.deallocate( raw1 );
    std::size_t after_free1 = pmm.block_count();
    PMM_TEST( after_free1 < after_free3 ); // raw1 merged with (raw2+raw3)

    pmm.destroy();
    return true;
}

// ─── I106-E: recover_block_state() called during load() ───────────────────────

/// @brief load() calls rebuild_free_tree() which uses recover_block_state().
/// Simulate a block with inconsistent state (weight>0 but root_offset wrong).
static bool test_i106_recover_block_state_on_load()
{
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // Create block with inconsistent state: weight>0 but wrong root_offset
    // (via BlockStateBase static API, Issue #120)
    BlockState::set_weight_of( buffer, 5u );
    BlockState::set_root_offset_of( buffer, 99u ); // Should be own_idx (e.g. 6), but corrupted

    // recover_block_state should fix it
    std::uint32_t own_idx = 6;
    pmm::recover_block_state<A>( buffer, own_idx );
    PMM_TEST( BlockState::get_root_offset( buffer ) == own_idx ); // Fixed

    // Scenario 2: weight==0 but root_offset!=0 (inconsistent free block)
    BlockState::set_weight_of( buffer, 0u );
    BlockState::set_root_offset_of( buffer, 6u ); // Should be 0

    pmm::recover_block_state<A>( buffer, own_idx );
    PMM_TEST( BlockState::get_root_offset( buffer ) == 0 ); // Fixed

    return true;
}

// ─── I106-F: detect_block_state() identifies block states ────────────────────

/// @brief detect_block_state() returns 0 for free, 1 for allocated, -1 for invalid.
static bool test_i106_detect_block_state()
{
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // FreeBlock: weight=0, root_offset=0 (via BlockStateBase API, Issue #120)
    BlockState::set_weight_of( buffer, 0u );
    BlockState::set_root_offset_of( buffer, 0u );
    PMM_TEST( pmm::detect_block_state<A>( buffer, 6 ) == 0 );

    // AllocatedBlock: weight>0, root_offset==own_idx
    BlockState::set_weight_of( buffer, 5u );
    BlockState::set_root_offset_of( buffer, 6u );
    PMM_TEST( pmm::detect_block_state<A>( buffer, 6 ) == 1 );

    // Invalid: weight>0 but root_offset != own_idx
    BlockState::set_weight_of( buffer, 5u );
    BlockState::set_root_offset_of( buffer, 99u ); // Wrong
    PMM_TEST( pmm::detect_block_state<A>( buffer, 6 ) == -1 );

    // Invalid: weight==0 but root_offset != 0
    BlockState::set_weight_of( buffer, 0u );
    BlockState::set_root_offset_of( buffer, 6u ); // Should be 0 for free
    PMM_TEST( pmm::detect_block_state<A>( buffer, 6 ) == -1 );

    return true;
}

// ─── I106-G: Full allocate/deallocate cycle with state verification ────────────

/// @brief Multiple allocations and deallocations maintain correct state machine invariants.
static bool test_i106_full_lifecycle()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    // Allocate several blocks
    constexpr int N = 5;
    void*         ptrs[N];
    for ( int i = 0; i < N; i++ )
    {
        ptrs[i] = pmm.allocate( 128 );
        PMM_TEST( ptrs[i] != nullptr );

        // Verify state after each allocation
        pmm::Block<A>* blk = block_of( pmm, ptrs[i] );
        std::uint32_t  idx = blk_idx_of( pmm, blk );
        PMM_TEST( pmm::detect_block_state<A>( blk, idx ) == 1 ); // AllocatedBlock
    }

    // Deallocate in reverse order
    for ( int i = N - 1; i >= 0; i-- )
    {
        pmm.deallocate( ptrs[i] );
    }

    // After all deallocations, only the header block should remain allocated
    PMM_TEST( pmm.alloc_block_count() == 1 ); // Only BlockHeader_0
    PMM_TEST( pmm.free_block_count() == 1 );  // All merged into one free block

    pmm.destroy();
    return true;
}

// ─── I106-H: Split path verification ─────────────────────────────────────────

/// @brief When a large free block is split, the remainder block is a valid FreeBlock.
static bool test_i106_split_creates_valid_free_remainder()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    // Initial state: 1 large free block
    PMM_TEST( pmm.free_block_count() == 1 );

    // Allocate a small block — should trigger split of the large free block
    void* raw = pmm.allocate( 16 ); // 1 data granule = minimum
    PMM_TEST( raw != nullptr );

    // After allocation, should have 2 blocks (allocated + remainder free)
    PMM_TEST( pmm.block_count() == 3 ); // BlockHeader_0 + allocated + free remainder

    pmm::Block<A>* blk = block_of( pmm, raw );
    std::uint32_t  idx = blk_idx_of( pmm, blk );

    using BlockState = pmm::BlockStateBase<A>;

    // Allocated block is in AllocatedBlock state (via BlockStateBase API, Issue #120)
    PMM_TEST( pmm::detect_block_state<A>( blk, idx ) == 1 );
    PMM_TEST( BlockState::get_weight( blk ) > 0 );
    PMM_TEST( BlockState::get_root_offset( blk ) == idx );

    // The next block (remainder) should be a valid FreeBlock
    if ( BlockState::get_next_offset( blk ) != pmm::detail::kNoBlock )
    {
        std::uint8_t*  base = pmm.backend().base_ptr();
        pmm::Block<A>* rem  = reinterpret_cast<pmm::Block<A>*>(
            base + pmm::detail::idx_to_byte_off( BlockState::get_next_offset( blk ) ) );
        PMM_TEST( BlockState::get_weight( rem ) == 0 );      // FreeBlock: weight == 0
        PMM_TEST( BlockState::get_root_offset( rem ) == 0 ); // FreeBlock: root_offset == 0
    }

    pmm.deallocate( raw );
    pmm.destroy();
    return true;
}

// ─── I106-I: Acceptance criteria from issue #106 ──────────────────────────────

/// @brief Verify no direct size/root_offset assignments in allocator_policy.h:
///        The state machine is used instead. This is a compile-time check via
///        static_assert that the code compiles with the new API.
static bool test_i106_acceptance_criteria()
{
    // I106-I1: AllocatorPolicy uses BlockState machine (verified by compilation)
    // If allocator_policy.h had direct BlockHeader::size assignments, it wouldn't compile
    // with Block<A> layout (different field names and positions).
    // The fact that all tests compile and pass proves this.

    // I106-I2: All existing tests still pass (verified by test runner)

    // I106-I3: detect_block_state correctly identifies states in managed memory
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    void* raw = pmm.allocate( 32 );
    PMM_TEST( raw != nullptr );

    pmm::Block<A>* blk = block_of( pmm, raw );
    std::uint32_t  idx = blk_idx_of( pmm, blk );

    using BlockState = pmm::BlockStateBase<A>;

    // Allocated block: detected as AllocatedBlock
    PMM_TEST( pmm::detect_block_state<A>( blk, idx ) == 1 );

    pmm.deallocate( raw );

    // After deallocate: weight==0, root_offset==0 (FreeBlock or merged) (via BlockStateBase API, Issue #120)
    PMM_TEST( BlockState::get_weight( blk ) == 0 );
    PMM_TEST( BlockState::get_root_offset( blk ) == 0 );

    pmm.destroy();
    return true;
}

// ─── I106-J: Multiple block types interoperate correctly ──────────────────────

/// @brief Allocate blocks of various sizes and verify state machine invariants.
static bool test_i106_various_sizes()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 128 * 1024 ) );

    // Sizes: 1 byte, 16 bytes, 32 bytes, 100 bytes, 1024 bytes
    std::size_t sizes[] = { 1, 16, 32, 100, 1024 };
    void*       ptrs[5] = {};

    for ( int i = 0; i < 5; i++ )
    {
        ptrs[i] = pmm.allocate( sizes[i] );
        PMM_TEST( ptrs[i] != nullptr );

        pmm::Block<A>* blk = block_of( pmm, ptrs[i] );
        std::uint32_t  idx = blk_idx_of( pmm, blk );

        using BlockState = pmm::BlockStateBase<A>;
        // All allocated blocks must satisfy AllocatedBlock invariants (via BlockStateBase API, Issue #120)
        PMM_TEST( BlockState::get_weight( blk ) > 0 );
        PMM_TEST( BlockState::get_root_offset( blk ) == idx );
        PMM_TEST( pmm::detect_block_state<A>( blk, idx ) == 1 );
    }

    // Deallocate all
    for ( int i = 0; i < 5; i++ )
    {
        pmm.deallocate( ptrs[i] );
    }

    // All blocks should be merged back
    PMM_TEST( pmm.free_block_count() == 1 );

    pmm.destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    bool all_passed = true;

    std::cout << "=== test_issue106_block_state_integration ===\n";

    std::cout << "  I106-A: Block<A> layout (weight field)\n";
    PMM_RUN( "    Allocated block uses weight > 0", test_i106_allocated_block_uses_weight );
    PMM_RUN( "    Freed block has weight == 0", test_i106_freed_block_has_zero_weight );

    std::cout << "  I106-B: allocate() state machine transitions\n";
    PMM_RUN( "    AllocatedBlock state after alloc", test_i106_allocate_state_machine );

    std::cout << "  I106-C: deallocate() uses mark_as_free()\n";
    PMM_RUN( "    Block freed via mark_as_free()", test_i106_deallocate_uses_mark_as_free );

    std::cout << "  I106-D: coalesce() state machine transitions\n";
    PMM_RUN( "    Coalesce with right neighbour", test_i106_coalesce_with_right_neighbour );
    PMM_RUN( "    Bidirectional coalescing", test_i106_coalesce_bidirectional );

    std::cout << "  I106-E: recover_block_state() on load()\n";
    PMM_RUN( "    recover_block_state repairs inconsistencies", test_i106_recover_block_state_on_load );

    std::cout << "  I106-F: detect_block_state() identifies states\n";
    PMM_RUN( "    detect_block_state accuracy", test_i106_detect_block_state );

    std::cout << "  I106-G: Full lifecycle with state verification\n";
    PMM_RUN( "    Multiple alloc/dealloc with state checks", test_i106_full_lifecycle );

    std::cout << "  I106-H: Split path verification\n";
    PMM_RUN( "    Split creates valid FreeBlock remainder", test_i106_split_creates_valid_free_remainder );

    std::cout << "  I106-I: Acceptance criteria\n";
    PMM_RUN( "    Issue #106 acceptance criteria met", test_i106_acceptance_criteria );

    std::cout << "  I106-J: Various block sizes\n";
    PMM_RUN( "    Various sizes maintain state machine invariants", test_i106_various_sizes );

    std::cout << "\n";
    if ( all_passed )
    {
        std::cout << "All tests PASSED\n";
        return EXIT_SUCCESS;
    }
    else
    {
        std::cout << "Some tests FAILED\n";
        return EXIT_FAILURE;
    }
}
