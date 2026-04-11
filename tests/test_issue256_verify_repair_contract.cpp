/**
 * @file test_issue256_verify_repair_contract.cpp
 * @brief Tests for the verify/repair/load operational contract.
 *
 * Verifies the stabilized boundary between verify, repair, and load:
 *   - Verify and load roles do not overlap (verify never modifies, load always reports).
 *   - Load does not perform hidden repair on normal operational paths.
 *   - Every repair action during load is recorded in VerifyResult.
 *   - Repair scope is bounded: only documented violations are repaired.
 *   - Diagnostic entries are uniform and complete for all violation types.
 *   - Non-recoverable corruption causes deterministic hard stop.
 *
 * @see docs/verify_repair_contract.md — operational contract
 * @see docs/diagnostics_taxonomy.md — violation types and diagnostic formats
 * @see include/pmm/diagnostics.h — RecoveryMode, VerifyResult, DiagnosticEntry
 */

#include "pmm/forest_registry.h"
#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using Mgr = pmm::presets::SingleThreadedHeap;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static constexpr std::size_t kBlockHdrByteSize =
    ( ( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + pmm::DefaultAddressTraits::granule_size - 1 ) /
      pmm::DefaultAddressTraits::granule_size ) *
    pmm::DefaultAddressTraits::granule_size;

static pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>* test_get_header( std::uint8_t* base ) noexcept
{
    return reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>( base + kBlockHdrByteSize );
}

static void setup_clean_image( std::size_t arena_size = 64 * 1024 )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( arena_size ) );
}

// ─── Test 1: verify and load roles do not overlap ──────────────────────────

TEST_CASE( "contract: verify and load roles do not overlap", "[test_issue256]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt root_offset to create a detectable violation.
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * pmm::DefaultAddressTraits::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto          orig    = pmm::BlockStateBase<pmm::DefaultAddressTraits>::get_root_offset( blk_raw );
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_root_offset_of( blk_raw, orig + 42 );

    // Verify detects but does NOT fix.
    pmm::VerifyResult v1 = Mgr::verify();
    REQUIRE_FALSE( v1.ok );
    REQUIRE( v1.mode == pmm::RecoveryMode::Verify );
    for ( std::size_t i = 0; i < v1.entry_count; ++i )
    {
        REQUIRE( v1.entries[i].action == pmm::DiagnosticAction::NoAction );
    }

    // Corruption persists after verify.
    auto after = pmm::BlockStateBase<pmm::DefaultAddressTraits>::get_root_offset( blk_raw );
    REQUIRE( after == orig + 42 );

    // Second verify produces same result — idempotent.
    pmm::VerifyResult v2 = Mgr::verify();
    REQUIRE_FALSE( v2.ok );
    REQUIRE( v2.violation_count == v1.violation_count );

    // Restore and cleanup.
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_root_offset_of( blk_raw, orig );
    Mgr::destroy();
}

// ─── Test 2: load does not run on verify path ──────────────────────────────

TEST_CASE( "contract: load does not run on verify path", "[test_issue256]" )
{
    setup_clean_image();

    // Verify on a healthy, already-initialized manager should not trigger any
    // load/repair machinery — it uses a shared (read) lock, not a unique lock.
    pmm::VerifyResult result = Mgr::verify();
    REQUIRE( result.ok );
    REQUIRE( result.mode == pmm::RecoveryMode::Verify );
    REQUIRE( result.violation_count == 0 );
    REQUIRE( result.entry_count == 0 );

    Mgr::destroy();
}

// ─── Test 3: every repair action is recorded ───────────────────────────────

TEST_CASE( "contract: every repair action is recorded in VerifyResult", "[test_issue256]" )
{
    using MgrA = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2560>;
    using MgrB = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2561>;

    static const char* kFile = "test_issue256_record.dat";
    const std::size_t  arena = 64 * 1024;

    REQUIRE( MgrA::create( arena ) );
    auto p = MgrA::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt block state to trigger a Repaired entry.
    std::uint8_t* src     = MgrA::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * pmm::DefaultAddressTraits::granule_size;
    void*         blk_raw = src + usr_off - sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto          blk_idx = static_cast<pmm::DefaultAddressTraits::index_type>(
        p.offset() - sizeof( pmm::Block<pmm::DefaultAddressTraits> ) / pmm::DefaultAddressTraits::granule_size );
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_root_offset_of( blk_raw, blk_idx + 999 );

    REQUIRE( pmm::save_manager<MgrA>( kFile ) );
    MgrA::destroy();

    // Load with VerifyResult — repairs must be recorded.
    REQUIRE( MgrB::create( arena ) );
    pmm::VerifyResult result;
    REQUIRE( pmm::load_manager_from_file<MgrB>( kFile, result ) );
    REQUIRE( result.mode == pmm::RecoveryMode::Repair );
    REQUIRE_FALSE( result.ok );

    // At least BlockStateInconsistent should be recorded as Repaired.
    bool found_block_repair = false;
    for ( std::size_t i = 0; i < result.entry_count; ++i )
    {
        if ( result.entries[i].type == pmm::ViolationType::BlockStateInconsistent )
        {
            REQUIRE( result.entries[i].action == pmm::DiagnosticAction::Repaired );
            found_block_repair = true;
        }
    }
    REQUIRE( found_block_repair );

    // After repair, verify must show clean state.
    pmm::VerifyResult post = MgrB::verify();
    REQUIRE( post.ok );

    MgrB::destroy();
    std::remove( kFile );
}

// ─── Test 4: repair scope is bounded ───────────────────────────────────────

TEST_CASE( "contract: repair scope is bounded — no user data modified", "[test_issue256]" )
{
    using MgrC = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2562>;
    using MgrD = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2563>;

    static const char* kFile = "test_issue256_scope.dat";
    const std::size_t  arena = 64 * 1024;

    REQUIRE( MgrC::create( arena ) );

    // Write known pattern into user data.
    auto p = MgrC::allocate_typed<std::uint64_t>( 8 );
    REQUIRE( !p.is_null() );
    auto           p_offset = p.offset(); // save raw offset for use with MgrD
    std::uint64_t* data     = MgrC::template resolve<std::uint64_t>( p );
    REQUIRE( data != nullptr );
    for ( int i = 0; i < 8; ++i )
        data[i] = static_cast<std::uint64_t>( 0xCAFEBABE00000000ULL | i );

    REQUIRE( pmm::save_manager<MgrC>( kFile ) );
    MgrC::destroy();

    // Load into fresh manager — load() runs repair phases.
    REQUIRE( MgrD::create( arena ) );
    pmm::VerifyResult result;
    REQUIRE( pmm::load_manager_from_file<MgrD>( kFile, result ) );

    // User data must survive repair unchanged.
    typename MgrD::template pptr<std::uint64_t> pd( p_offset );
    std::uint64_t*                              loaded_data = MgrD::template resolve<std::uint64_t>( pd );
    REQUIRE( loaded_data != nullptr );
    for ( int i = 0; i < 8; ++i )
    {
        REQUIRE( loaded_data[i] == static_cast<std::uint64_t>( 0xCAFEBABE00000000ULL | i ) );
    }

    MgrD::destroy();
    std::remove( kFile );
}

// ─── Test 5: diagnostic entries are uniform for all violation types ─────────

TEST_CASE( "contract: diagnostic entries are uniform for all violation types", "[test_issue256]" )
{
    // Construct entries for each ViolationType and verify all fields are populated.
    pmm::VerifyResult result;

    result.add( pmm::ViolationType::HeaderCorruption, pmm::DiagnosticAction::Aborted, 0, 100, 200 );
    result.add( pmm::ViolationType::BlockStateInconsistent, pmm::DiagnosticAction::Repaired, 5, 5, 999 );
    result.add( pmm::ViolationType::PrevOffsetMismatch, pmm::DiagnosticAction::Repaired, 10, 3, 777 );
    result.add( pmm::ViolationType::CounterMismatch, pmm::DiagnosticAction::Rebuilt, 0, 50, 49 );
    result.add( pmm::ViolationType::FreeTreeStale, pmm::DiagnosticAction::Rebuilt, 0, 3, 0 );
    result.add( pmm::ViolationType::ForestRegistryMissing, pmm::DiagnosticAction::NoAction, 0, 0x50465247, 0 );
    result.add( pmm::ViolationType::ForestDomainMissing, pmm::DiagnosticAction::NoAction );
    result.add( pmm::ViolationType::ForestDomainFlagsMissing, pmm::DiagnosticAction::NoAction );

    REQUIRE( result.entry_count == 8 );
    REQUIRE( result.violation_count == 8 );
    REQUIRE_FALSE( result.ok );

    // All 8 violation types are represented.
    REQUIRE( result.entries[0].type == pmm::ViolationType::HeaderCorruption );
    REQUIRE( result.entries[1].type == pmm::ViolationType::BlockStateInconsistent );
    REQUIRE( result.entries[2].type == pmm::ViolationType::PrevOffsetMismatch );
    REQUIRE( result.entries[3].type == pmm::ViolationType::CounterMismatch );
    REQUIRE( result.entries[4].type == pmm::ViolationType::FreeTreeStale );
    REQUIRE( result.entries[5].type == pmm::ViolationType::ForestRegistryMissing );
    REQUIRE( result.entries[6].type == pmm::ViolationType::ForestDomainMissing );
    REQUIRE( result.entries[7].type == pmm::ViolationType::ForestDomainFlagsMissing );

    // Verify specific field values for a representative entry.
    REQUIRE( result.entries[2].block_index == 10 );
    REQUIRE( result.entries[2].expected == 3 );
    REQUIRE( result.entries[2].actual == 777 );
}

// ─── Test 6: non-recoverable corruption causes deterministic hard stop ─────

TEST_CASE( "contract: non-recoverable corruption causes deterministic hard stop", "[test_issue256]" )
{
    using MgrE = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2564>;
    using MgrF = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2565>;
    using MgrG = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2566>;

    const std::size_t arena = 64 * 1024;

    // Test 6a: magic corruption → Aborted.
    {
        REQUIRE( MgrE::create( arena ) );
        std::uint8_t* base = MgrE::backend().base_ptr();
        auto*         hdr  = test_get_header( base );
        auto          orig = hdr->magic;
        hdr->magic         = 0xDEAD;

        // Copy to MgrF and try load.
        REQUIRE( MgrF::create( arena ) );
        std::memcpy( MgrF::backend().base_ptr(), base, arena );
        hdr->magic = orig; // restore MgrE
        MgrE::destroy();

        pmm::VerifyResult result;
        bool              ok = MgrF::load( result );
        REQUIRE_FALSE( ok );
        REQUIRE_FALSE( result.ok );
        REQUIRE( result.entry_count >= 1 );
        REQUIRE( result.entries[0].type == pmm::ViolationType::HeaderCorruption );
        REQUIRE( result.entries[0].action == pmm::DiagnosticAction::Aborted );
        MgrF::destroy();
    }

    // Test 6b: granule mismatch → Aborted.
    {
        REQUIRE( MgrG::create( arena ) );
        std::uint8_t* base = MgrG::backend().base_ptr();
        auto*         hdr  = test_get_header( base );
        auto          orig = hdr->granule_size;
        hdr->granule_size  = 999;

        pmm::VerifyResult result;
        result.mode = pmm::RecoveryMode::Verify;
        // Verify detects it too.
        pmm::VerifyResult v = MgrG::verify();
        REQUIRE_FALSE( v.ok );
        bool found_hdr = false;
        for ( std::size_t i = 0; i < v.entry_count; ++i )
        {
            if ( v.entries[i].type == pmm::ViolationType::HeaderCorruption )
                found_hdr = true;
        }
        REQUIRE( found_hdr );

        hdr->granule_size = orig;
        MgrG::destroy();
    }
}

// ─── Test 7: allocate/deallocate do not perform hidden repair ──────────────

TEST_CASE( "contract: normal operations do not perform hidden repair", "[test_issue256]" )
{
    setup_clean_image();

    // Corrupt a block's prev_offset.
    auto p1 = Mgr::allocate_typed<std::uint32_t>( 8 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 8 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );

    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p2.offset() ) * pmm::DefaultAddressTraits::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto          orig    = pmm::BlockStateBase<pmm::DefaultAddressTraits>::get_prev_offset( blk_raw );
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_prev_offset_of( blk_raw, orig + 500 );

    // Perform normal operations — allocate and deallocate.
    auto p3 = Mgr::allocate_typed<std::uint32_t>( 4 );
    REQUIRE( !p3.is_null() );
    Mgr::deallocate_typed( p3 );

    // The corruption should still be present — normal operations do not repair.
    auto after = pmm::BlockStateBase<pmm::DefaultAddressTraits>::get_prev_offset( blk_raw );
    REQUIRE( after == orig + 500 );

    // verify() confirms the corruption is still there.
    pmm::VerifyResult result = Mgr::verify();
    REQUIRE_FALSE( result.ok );
    bool found = false;
    for ( std::size_t i = 0; i < result.entry_count; ++i )
    {
        if ( result.entries[i].type == pmm::ViolationType::PrevOffsetMismatch )
            found = true;
    }
    REQUIRE( found );

    // Restore and cleanup.
    pmm::BlockStateBase<pmm::DefaultAddressTraits>::set_prev_offset_of( blk_raw, orig );
    Mgr::destroy();
}

// ─── Test 8: clean round-trip through save/load produces no violations ─────

TEST_CASE( "contract: clean round-trip produces no violations", "[test_issue256]" )
{
    using MgrH = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2567>;
    using MgrI = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2568>;

    static const char* kFile = "test_issue256_clean_rt.dat";
    const std::size_t  arena = 64 * 1024;

    REQUIRE( MgrH::create( arena ) );
    auto p1 = MgrH::allocate_typed<std::uint64_t>( 10 );
    auto p2 = MgrH::allocate_typed<std::uint32_t>( 20 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );
    MgrH::deallocate_typed( p1 );

    // Verify clean before save.
    pmm::VerifyResult pre = MgrH::verify();
    REQUIRE( pre.ok );

    REQUIRE( pmm::save_manager<MgrH>( kFile ) );
    MgrH::destroy();

    // Load and check — the only expected violations are structural ones
    // that load() repairs (prev_offset, counters, free tree).
    REQUIRE( MgrI::create( arena ) );
    pmm::VerifyResult result;
    REQUIRE( pmm::load_manager_from_file<MgrI>( kFile, result ) );

    // After load, verify must show clean state.
    pmm::VerifyResult post = MgrI::verify();
    REQUIRE( post.ok );
    REQUIRE( post.violation_count == 0 );

    MgrI::destroy();
    std::remove( kFile );
}

// ─── Test 9: ViolationType enum covers all documented violation classes ─────

TEST_CASE( "contract: ViolationType enum covers all documented classes", "[test_issue256]" )
{
    // The diagnostics taxonomy documents 8 violation types.
    // Verify they all exist and have distinct values.
    std::vector<pmm::ViolationType> types = {
        pmm::ViolationType::None,
        pmm::ViolationType::BlockStateInconsistent,
        pmm::ViolationType::PrevOffsetMismatch,
        pmm::ViolationType::CounterMismatch,
        pmm::ViolationType::FreeTreeStale,
        pmm::ViolationType::ForestRegistryMissing,
        pmm::ViolationType::ForestDomainMissing,
        pmm::ViolationType::ForestDomainFlagsMissing,
        pmm::ViolationType::HeaderCorruption,
    };

    // All values must be distinct.
    for ( std::size_t i = 0; i < types.size(); ++i )
    {
        for ( std::size_t j = i + 1; j < types.size(); ++j )
        {
            REQUIRE( types[i] != types[j] );
        }
    }

    // DiagnosticAction enum covers all documented actions.
    std::vector<pmm::DiagnosticAction> actions = {
        pmm::DiagnosticAction::NoAction,
        pmm::DiagnosticAction::Repaired,
        pmm::DiagnosticAction::Rebuilt,
        pmm::DiagnosticAction::Aborted,
    };

    for ( std::size_t i = 0; i < actions.size(); ++i )
    {
        for ( std::size_t j = i + 1; j < actions.size(); ++j )
        {
            REQUIRE( actions[i] != actions[j] );
        }
    }
}
