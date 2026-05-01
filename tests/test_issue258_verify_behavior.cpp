/*
## test-issue258-verify-behavior
req: ac-005
*/

/**
 * @file test_issue258_verify_behavior.cpp
 * @brief Verify/repair behavior tests.
 *
 * Covers test matrix group E: diagnostics reflect real action,
 * verify is idempotent, repair is idempotent, verify clean after repair.
 *
 * @see docs/test_matrix.md — E4–E8
 * @see docs/verify_repair_contract.md — operational contract
 */

#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

using AT  = pmm::DefaultAddressTraits;
using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25840>;

static void setup_clean( std::size_t arena = 64 * 1024 )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( arena ) );
}

// ─── E4: Diagnostics reflect real action ────────────────────────────────────

TEST_CASE( "verify_behavior: diagnostics reflect verify-only action", "[issue258][verify]" )
{
    setup_clean();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt root_offset
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );
    auto          orig    = pmm::BlockStateBase<AT>::get_root_offset( blk_raw );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, orig + 77 );

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( v.mode == pmm::RecoveryMode::Verify );

    // All entries must have NoAction (verify never repairs)
    for ( std::size_t i = 0; i < v.entry_count; ++i )
    {
        REQUIRE( v.entries[i].action == pmm::DiagnosticAction::NoAction );
    }

    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, orig );
    Mgr::destroy();
}

TEST_CASE( "verify_behavior: diagnostics reflect load/repair action", "[issue258][verify]" )
{
    using MgrA = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25841>;
    using MgrB = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25842>;

    const char*       kFile = "test_issue258_vb_repair.dat";
    const std::size_t arena = 64 * 1024;

    REQUIRE( MgrA::create( arena ) );
    auto p = MgrA::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt root_offset
    std::uint8_t* base    = MgrA::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );
    auto          blk_idx = static_cast<AT::index_type>( p.offset() - sizeof( pmm::Block<AT> ) / AT::granule_size );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, blk_idx + 333 );

    REQUIRE( pmm::save_manager<MgrA>( kFile ) );
    MgrA::destroy();

    REQUIRE( MgrB::create( arena ) );
    pmm::VerifyResult result;
    REQUIRE( pmm::load_manager_from_file<MgrB>( kFile, result ) );

    REQUIRE( result.mode == pmm::RecoveryMode::Repair );

    // At least one entry should have Repaired or Rebuilt action
    bool found_repair = false;
    for ( std::size_t i = 0; i < result.entry_count; ++i )
    {
        if ( result.entries[i].action == pmm::DiagnosticAction::Repaired ||
             result.entries[i].action == pmm::DiagnosticAction::Rebuilt )
        {
            found_repair = true;
            break;
        }
    }
    REQUIRE( found_repair );

    MgrB::destroy();
    std::remove( kFile );
}

// ─── E5: Verify is idempotent ───────────────────────────────────────────────

TEST_CASE( "verify_behavior: verify is idempotent on clean image", "[issue258][verify]" )
{
    setup_clean();

    auto p = Mgr::allocate_typed<std::uint32_t>( 16 );
    REQUIRE( !p.is_null() );

    pmm::VerifyResult v1 = Mgr::verify();
    pmm::VerifyResult v2 = Mgr::verify();
    pmm::VerifyResult v3 = Mgr::verify();

    REQUIRE( v1.ok );
    REQUIRE( v2.ok );
    REQUIRE( v3.ok );
    REQUIRE( v1.violation_count == v2.violation_count );
    REQUIRE( v2.violation_count == v3.violation_count );

    Mgr::destroy();
}

TEST_CASE( "verify_behavior: verify is idempotent on corrupted image", "[issue258][verify]" )
{
    setup_clean();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt root_offset
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );
    auto          orig    = pmm::BlockStateBase<AT>::get_root_offset( blk_raw );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, orig + 55 );

    pmm::VerifyResult v1 = Mgr::verify();
    pmm::VerifyResult v2 = Mgr::verify();

    REQUIRE_FALSE( v1.ok );
    REQUIRE_FALSE( v2.ok );
    REQUIRE( v1.violation_count == v2.violation_count );
    REQUIRE( v1.entry_count == v2.entry_count );

    for ( std::size_t i = 0; i < v1.entry_count; ++i )
    {
        REQUIRE( v1.entries[i].type == v2.entries[i].type );
        REQUIRE( v1.entries[i].action == v2.entries[i].action );
    }

    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, orig );
    Mgr::destroy();
}

// ─── E6: Repair is idempotent (verify clean after repair) ───────────────────

TEST_CASE( "verify_behavior: verify clean after load repair", "[issue258][verify]" )
{
    using MgrC = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25843>;
    using MgrD = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25844>;

    const char*       kFile = "test_issue258_vb_clean.dat";
    const std::size_t arena = 64 * 1024;

    REQUIRE( MgrC::create( arena ) );

    // Create several blocks, deallocate some, create fragmentation
    auto a1 = MgrC::allocate_typed<std::uint32_t>( 10 );
    auto a2 = MgrC::allocate_typed<std::uint32_t>( 20 );
    auto a3 = MgrC::allocate_typed<std::uint32_t>( 30 );
    REQUIRE( !a1.is_null() );
    REQUIRE( !a2.is_null() );
    REQUIRE( !a3.is_null() );
    MgrC::deallocate_typed( a2 );

    REQUIRE( pmm::save_manager<MgrC>( kFile ) );
    MgrC::destroy();

    REQUIRE( MgrD::create( arena ) );
    pmm::VerifyResult load_result;
    REQUIRE( pmm::load_manager_from_file<MgrD>( kFile, load_result ) );

    // Verify once => should be clean
    pmm::VerifyResult v1 = MgrD::verify();
    REQUIRE( v1.ok );

    // Verify again => still clean (idempotent)
    pmm::VerifyResult v2 = MgrD::verify();
    REQUIRE( v2.ok );
    REQUIRE( v2.violation_count == 0 );

    MgrD::destroy();
    std::remove( kFile );
}

// ─── E8: Verify after repair shows clean state ─────────────────────────────

TEST_CASE( "verify_behavior: verify after repair shows clean state", "[issue258][verify]" )
{
    using MgrE = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25845>;
    using MgrF = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25846>;

    const char*       kFile = "test_issue258_vb_afterrepair.dat";
    const std::size_t arena = 64 * 1024;

    REQUIRE( MgrE::create( arena ) );
    auto p = MgrE::allocate_typed<std::uint64_t>( 8 );
    REQUIRE( !p.is_null() );

    // Corrupt block state before save
    std::uint8_t* base    = MgrE::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );
    auto          blk_idx = static_cast<AT::index_type>( p.offset() - sizeof( pmm::Block<AT> ) / AT::granule_size );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, blk_idx + 999 );

    REQUIRE( pmm::save_manager<MgrE>( kFile ) );
    MgrE::destroy();

    REQUIRE( MgrF::create( arena ) );
    pmm::VerifyResult load_result;
    REQUIRE( pmm::load_manager_from_file<MgrF>( kFile, load_result ) );

    // Load must have found violations
    REQUIRE_FALSE( load_result.ok );
    REQUIRE( load_result.violation_count > 0 );

    // But after repair, verify shows clean
    pmm::VerifyResult post = MgrF::verify();
    REQUIRE( post.ok );
    REQUIRE( post.violation_count == 0 );

    // Allocator still works after repair
    auto q = MgrF::allocate_typed<std::uint32_t>( 4 );
    REQUIRE( !q.is_null() );
    MgrF::deallocate_typed( q );

    MgrF::destroy();
    std::remove( kFile );
}
