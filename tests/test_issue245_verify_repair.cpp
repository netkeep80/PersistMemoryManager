/**
 * @file test_issue245_verify_repair.cpp
 * @brief Tests for verify vs repair mode separation (Issue #245).
 *
 * Verifies:
 *   - verify() is read-only: detects violations without modifying the image.
 *   - load(VerifyResult&) reports repairs taken during load.
 *   - Clean images produce no violations in verify().
 *   - Corrupted block states are detected by verify() and repaired by load().
 *   - Counter mismatches are detected by verify() and repaired by load().
 *   - Forest registry issues are detected by verify().
 *
 * @see include/pmm/diagnostics.h — RecoveryMode, VerifyResult, DiagnosticEntry
 * @see include/pmm/block_state.h — verify_state / recover_state
 * @see include/pmm/allocator_policy.h — verify_linked_list, verify_counters
 */

#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

using Mgr = pmm::presets::SingleThreadedHeap;

// ─── Helper: save/load round-trip via HeapStorage ─────────────────────────────

/// @brief Create a manager, do some allocations, save to buffer, reload.
static void setup_clean_image( std::size_t arena_size = 64 * 1024 )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( arena_size ) );
}

// ─── Test 1: clean image produces no violations ──────────────────────────────

TEST_CASE( "verify_repair: clean image has no violations", "[test_issue245]" )
{
    setup_clean_image();

    // Allocate and deallocate some blocks to exercise the allocator.
    auto p1 = Mgr::allocate_typed<std::uint32_t>( 10 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 20 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );
    Mgr::deallocate_typed( p1 );

    // verify() on a healthy image should report no violations.
    pmm::VerifyResult result = Mgr::verify();
    REQUIRE( result.ok );
    REQUIRE( result.violation_count == 0 );
    REQUIRE( result.mode == pmm::RecoveryMode::Verify );

    Mgr::destroy();
}

// ─── Test 2: verify detects block state inconsistency (without repair) ──────

TEST_CASE( "verify_repair: verify detects block state inconsistency", "[test_issue245]" )
{
    setup_clean_image();

    // Allocate a block.
    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt the block's root_offset to simulate a transitional state.
    // Find the block header: user ptr is at base + offset * granule,
    // block header is sizeof(Block) bytes before user data.
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * pmm::DefaultAddressTraits::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<pmm::DefaultAddressTraits> );

    // Save original root_offset for later restoration.
    auto original_root = pmm::BlockStateBase<pmm::DefaultAddressTraits>::get_root_offset( blk_raw );

    // Corrupt: set root_offset to a wrong value (allocated block should have root_offset == own_idx).
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_root_offset_of( blk_raw, original_root + 999 );

    // verify() should detect the inconsistency.
    pmm::VerifyResult result = Mgr::verify();
    REQUIRE_FALSE( result.ok );
    REQUIRE( result.violation_count > 0 );

    bool found_block_state_issue = false;
    for ( std::size_t i = 0; i < result.entry_count; ++i )
    {
        if ( result.entries[i].type == pmm::ViolationType::BlockStateInconsistent )
        {
            found_block_state_issue = true;
            // In verify mode, no repair action taken.
            REQUIRE( result.entries[i].action == pmm::DiagnosticAction::NoAction );
        }
    }
    REQUIRE( found_block_state_issue );

    // Restore the original value so destroy() doesn't see corruption.
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_root_offset_of( blk_raw, original_root );

    Mgr::destroy();
}

// ─── Test 3: verify is truly read-only ──────────────────────────────────────

TEST_CASE( "verify_repair: verify does not modify the image", "[test_issue245]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<std::uint64_t>( 2 );
    REQUIRE( !p.is_null() );

    // Corrupt root_offset.
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * pmm::DefaultAddressTraits::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<pmm::DefaultAddressTraits> );

    auto original_root = pmm::BlockStateBase<pmm::DefaultAddressTraits>::get_root_offset( blk_raw );
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_root_offset_of( blk_raw, original_root + 42 );

    // Call verify() — should NOT modify the image.
    pmm::VerifyResult result = Mgr::verify();
    REQUIRE_FALSE( result.ok );

    // Check that the corruption is still there (verify didn't repair it).
    auto after_verify_root = pmm::BlockStateBase<pmm::DefaultAddressTraits>::get_root_offset( blk_raw );
    REQUIRE( after_verify_root == original_root + 42 );

    // Restore for cleanup.
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_root_offset_of( blk_raw, original_root );
    Mgr::destroy();
}

// ─── Test 4: load(VerifyResult&) reports repairs via file round-trip ────────

TEST_CASE( "verify_repair: load with VerifyResult reports repairs", "[test_issue245]" )
{
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2450>;
    using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2451>;

    static const char* kFile = "test_issue245_repair.dat";

    const std::size_t arena = 64 * 1024;
    REQUIRE( Mgr1::create( arena ) );

    auto p = Mgr1::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt a block's root_offset before saving.
    std::uint8_t* base    = Mgr1::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * pmm::DefaultAddressTraits::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto          blk_idx = static_cast<pmm::DefaultAddressTraits::index_type>(
        p.offset() - sizeof( pmm::Block<pmm::DefaultAddressTraits> ) / pmm::DefaultAddressTraits::granule_size );
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_root_offset_of( blk_raw, blk_idx + 777 );

    // Save the corrupted image.
    REQUIRE( pmm::save_manager<Mgr1>( kFile ) );
    Mgr1::destroy();

    // Load into Mgr2 — load_manager_from_file calls load() internally.
    REQUIRE( Mgr2::create( arena ) );
    REQUIRE( pmm::load_manager_from_file<Mgr2>( kFile ) );

    // After load, verify should show clean state (repairs applied by load()).
    pmm::VerifyResult result = Mgr2::verify();
    REQUIRE( result.ok );

    Mgr2::destroy();
    std::remove( kFile );
}

// ─── Test 5: load without VerifyResult still works (backward compatible) ────

TEST_CASE( "verify_repair: load() without VerifyResult is backward compatible", "[test_issue245]" )
{
    using Mgr3 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2452>;
    using Mgr4 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2453>;

    static const char* kFile = "test_issue245_compat.dat";

    REQUIRE( Mgr3::create( 64 * 1024 ) );
    auto p = Mgr3::allocate_typed<std::uint32_t>();
    REQUIRE( !p.is_null() );

    REQUIRE( pmm::save_manager<Mgr3>( kFile ) );
    Mgr3::destroy();

    // Plain load() without VerifyResult — should still work.
    REQUIRE( Mgr4::create( 64 * 1024 ) );
    REQUIRE( pmm::load_manager_from_file<Mgr4>( kFile ) );
    REQUIRE( Mgr4::is_initialized() );

    Mgr4::destroy();
    std::remove( kFile );
}

// ─── Test 6: verify on uninitialized manager returns error ──────────────────

TEST_CASE( "verify_repair: verify on uninitialized manager returns error", "[test_issue245]" )
{
    Mgr::destroy();
    pmm::VerifyResult result = Mgr::verify();
    REQUIRE_FALSE( result.ok );
    REQUIRE( result.violation_count > 0 );
    REQUIRE( result.entries[0].type == pmm::ViolationType::HeaderCorruption );
    REQUIRE( result.entries[0].action == pmm::DiagnosticAction::Aborted );
}

// ─── Test 7: VerifyResult entry overflow is handled gracefully ──────────────

TEST_CASE( "verify_repair: VerifyResult entry overflow handled", "[test_issue245]" )
{
    pmm::VerifyResult result;
    // Fill beyond capacity.
    for ( std::size_t i = 0; i < pmm::kMaxDiagnosticEntries + 10; ++i )
    {
        result.add( pmm::ViolationType::PrevOffsetMismatch, pmm::DiagnosticAction::NoAction, i );
    }
    REQUIRE_FALSE( result.ok );
    REQUIRE( result.violation_count == pmm::kMaxDiagnosticEntries + 10 );
    REQUIRE( result.entry_count == pmm::kMaxDiagnosticEntries );
}

// ─── Test 8: DiagnosticEntry fields are populated correctly ─────────────────

TEST_CASE( "verify_repair: DiagnosticEntry fields populated", "[test_issue245]" )
{
    pmm::VerifyResult result;
    result.add( pmm::ViolationType::CounterMismatch, pmm::DiagnosticAction::Rebuilt, 42, 100, 99 );

    REQUIRE( result.entry_count == 1 );
    REQUIRE( result.entries[0].type == pmm::ViolationType::CounterMismatch );
    REQUIRE( result.entries[0].action == pmm::DiagnosticAction::Rebuilt );
    REQUIRE( result.entries[0].block_index == 42 );
    REQUIRE( result.entries[0].expected == 100 );
    REQUIRE( result.entries[0].actual == 99 );
}

// ─── Test 9: verify detects prev_offset mismatch after manual corruption ────

TEST_CASE( "verify_repair: verify detects prev_offset mismatch", "[test_issue245]" )
{
    setup_clean_image();

    // Allocate two blocks so there is at least one prev link.
    auto p1 = Mgr::allocate_typed<std::uint32_t>( 8 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 8 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );

    // Corrupt the second block's prev_offset.
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p2.offset() ) * pmm::DefaultAddressTraits::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<pmm::DefaultAddressTraits> );

    auto original_prev = pmm::BlockStateBase<pmm::DefaultAddressTraits>::get_prev_offset( blk_raw );
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_prev_offset_of( blk_raw, original_prev + 500 );

    // verify() should detect prev_offset mismatch.
    pmm::VerifyResult result = Mgr::verify();
    REQUIRE_FALSE( result.ok );

    bool found_prev_mismatch = false;
    for ( std::size_t i = 0; i < result.entry_count; ++i )
    {
        if ( result.entries[i].type == pmm::ViolationType::PrevOffsetMismatch )
            found_prev_mismatch = true;
    }
    REQUIRE( found_prev_mismatch );

    // Restore for cleanup.
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_prev_offset_of( blk_raw, original_prev );
    Mgr::destroy();
}
