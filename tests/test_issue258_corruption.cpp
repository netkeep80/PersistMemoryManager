/**
 * @file test_issue258_corruption.cpp
 * @brief Deterministic corruption tests.
 *
 * Covers test matrix group D: injection of specific corruption
 * types and verification that each is detected with the correct
 * ViolationType.
 *
 * @see docs/test_matrix.md — D1–D12
 * @see docs/diagnostics_taxonomy.md — violation types
 */

#include "pmm/forest_registry.h"
#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

using AT  = pmm::DefaultAddressTraits;
using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25830>;

static constexpr std::size_t kBlockHdrByteSize =
    ( ( sizeof( pmm::Block<AT> ) + AT::granule_size - 1 ) / AT::granule_size ) * AT::granule_size;

static pmm::detail::ManagerHeader<AT>* get_header( std::uint8_t* base ) noexcept
{
    return reinterpret_cast<pmm::detail::ManagerHeader<AT>*>( base + kBlockHdrByteSize );
}

static void setup_clean( std::size_t arena = 64 * 1024 )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( arena ) );
}

// Helper: find ViolationType in result
static bool has_violation( const pmm::VerifyResult& r, pmm::ViolationType t )
{
    for ( std::size_t i = 0; i < r.entry_count; ++i )
        if ( r.entries[i].type == t )
            return true;
    return false;
}

// ─── D1: Block root_offset corruption ───────────────────────────────────────

TEST_CASE( "corruption: wrong root_offset detected", "[issue258][corruption]" )
{
    setup_clean();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );

    auto orig = pmm::BlockStateBase<AT>::get_root_offset( blk_raw );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, orig + 111 );

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( has_violation( v, pmm::ViolationType::BlockStateInconsistent ) );

    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, orig );
    Mgr::destroy();
}

// ─── D2: Block prev_offset corruption ───────────────────────────────────────

TEST_CASE( "corruption: wrong prev_offset detected", "[issue258][corruption]" )
{
    setup_clean();

    auto p1 = Mgr::allocate_typed<std::uint32_t>( 8 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 8 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );

    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p2.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );

    auto orig = pmm::BlockStateBase<AT>::get_prev_offset( blk_raw );
    pmm::BlockStateBase<AT>::set_prev_offset_of( blk_raw, orig + 300 );

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( has_violation( v, pmm::ViolationType::PrevOffsetMismatch ) );

    pmm::BlockStateBase<AT>::set_prev_offset_of( blk_raw, orig );
    Mgr::destroy();
}

// ─── D3: Block weight mismatch (free block with weight > 0) ─────────────────

TEST_CASE( "corruption: free block with non-zero weight detected", "[issue258][corruption]" )
{
    setup_clean();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );
    Mgr::deallocate_typed( p );

    // Find a free block and corrupt its weight
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );

    auto orig_weight = pmm::BlockStateBase<AT>::get_weight( blk_raw );
    auto orig_root   = pmm::BlockStateBase<AT>::get_root_offset( blk_raw );

    // Set weight > 0 on a free block (root_offset == 0) => inconsistent
    pmm::BlockStateBase<AT>::set_weight_of( blk_raw, 42 );

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( has_violation( v, pmm::ViolationType::BlockStateInconsistent ) );

    pmm::BlockStateBase<AT>::set_weight_of( blk_raw, orig_weight );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, orig_root );
    Mgr::destroy();
}

// ─── D4: Forest registry magic corruption ───────────────────────────────────

TEST_CASE( "corruption: forest registry bad magic detected", "[issue258][corruption]" )
{
    setup_clean();

    std::uint8_t* base = Mgr::backend().base_ptr();
    auto*         hdr  = get_header( base );
    REQUIRE( hdr->root_offset != AT::no_block );

    auto* reg = reinterpret_cast<pmm::detail::ForestDomainRegistry<AT>*>(
        base + static_cast<std::size_t>( hdr->root_offset ) * AT::granule_size );

    auto orig_magic = reg->magic;
    reg->magic      = 0xBAD0CAFE;

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( has_violation( v, pmm::ViolationType::ForestRegistryMissing ) );

    reg->magic = orig_magic;
    Mgr::destroy();
}

// ─── D5: System domain name corrupted ───────────────────────────────────────

TEST_CASE( "corruption: corrupted system domain name detected", "[issue258][corruption]" )
{
    setup_clean();

    std::uint8_t* base = Mgr::backend().base_ptr();
    auto*         hdr  = get_header( base );
    auto*         reg  = reinterpret_cast<pmm::detail::ForestDomainRegistry<AT>*>(
        base + static_cast<std::size_t>( hdr->root_offset ) * AT::granule_size );

    // Find system/symbols domain and corrupt its name
    char original_name[pmm::detail::kForestDomainNameCapacity];
    bool found = false;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( std::strncmp( reg->domains[i].name, pmm::detail::kSystemDomainSymbols,
                           pmm::detail::kForestDomainNameCapacity ) == 0 )
        {
            std::memcpy( original_name, reg->domains[i].name, sizeof( original_name ) );
            std::memset( reg->domains[i].name, 0, sizeof( reg->domains[i].name ) );
            std::strncpy( reg->domains[i].name, "corrupted/name", pmm::detail::kForestDomainNameCapacity - 1 );
            found = true;
            break;
        }
    }
    REQUIRE( found );

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( has_violation( v, pmm::ViolationType::ForestDomainMissing ) );

    // Restore
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( std::strncmp( reg->domains[i].name, "corrupted/name", pmm::detail::kForestDomainNameCapacity ) == 0 )
        {
            std::memcpy( reg->domains[i].name, original_name, sizeof( original_name ) );
            break;
        }
    }
    Mgr::destroy();
}

// ─── D6: System domain flags cleared ────────────────────────────────────────

TEST_CASE( "corruption: cleared system domain flag detected", "[issue258][corruption]" )
{
    setup_clean();

    std::uint8_t* base = Mgr::backend().base_ptr();
    auto*         hdr  = get_header( base );
    auto*         reg  = reinterpret_cast<pmm::detail::ForestDomainRegistry<AT>*>(
        base + static_cast<std::size_t>( hdr->root_offset ) * AT::granule_size );

    std::uint8_t orig_flags = 0;
    bool         found      = false;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( ( reg->domains[i].flags & pmm::detail::kForestDomainFlagSystem ) != 0 )
        {
            orig_flags = reg->domains[i].flags;
            reg->domains[i].flags &= ~pmm::detail::kForestDomainFlagSystem;
            found = true;
            break;
        }
    }
    REQUIRE( found );

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( has_violation( v, pmm::ViolationType::ForestDomainFlagsMissing ) );

    // Restore
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( ( reg->domains[i].flags & pmm::detail::kForestDomainFlagSystem ) == 0 &&
             std::strncmp( reg->domains[i].name, "system/", 7 ) == 0 )
        {
            reg->domains[i].flags = orig_flags;
            break;
        }
    }
    Mgr::destroy();
}

// ─── D7: Manager header magic corruption ────────────────────────────────────

TEST_CASE( "corruption: header magic corruption detected", "[issue258][corruption]" )
{
    setup_clean();

    std::uint8_t* base = Mgr::backend().base_ptr();
    auto*         hdr  = get_header( base );
    auto          orig = hdr->magic;
    hdr->magic         = 0xDEADBEEFDEADBEEFULL;

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( has_violation( v, pmm::ViolationType::HeaderCorruption ) );

    hdr->magic = orig;
    Mgr::destroy();
}

// ─── D8: Manager header granule_size corruption ─────────────────────────────

TEST_CASE( "corruption: header granule_size corruption detected", "[issue258][corruption]" )
{
    setup_clean();

    std::uint8_t* base = Mgr::backend().base_ptr();
    auto*         hdr  = get_header( base );
    auto          orig = hdr->granule_size;
    hdr->granule_size  = 999;

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( has_violation( v, pmm::ViolationType::HeaderCorruption ) );

    hdr->granule_size = orig;
    Mgr::destroy();
}

// ─── D9: Manager header total_size corruption ───────────────────────────────

TEST_CASE( "corruption: header total_size corruption detected", "[issue258][corruption]" )
{
    setup_clean();

    std::uint8_t* base = Mgr::backend().base_ptr();
    auto*         hdr  = get_header( base );
    auto          orig = hdr->total_size;
    hdr->total_size    = orig + 8192;

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( has_violation( v, pmm::ViolationType::HeaderCorruption ) );

    hdr->total_size = orig;
    Mgr::destroy();
}

// ─── D10: Multiple simultaneous corruptions ─────────────────────────────────

TEST_CASE( "corruption: multiple simultaneous corruptions detected", "[issue258][corruption]" )
{
    setup_clean();

    auto p1 = Mgr::allocate_typed<std::uint32_t>( 8 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 8 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );

    std::uint8_t* base = Mgr::backend().base_ptr();

    // Corrupt root_offset of p1
    std::size_t usr_off1  = static_cast<std::size_t>( p1.offset() ) * AT::granule_size;
    void*       blk_raw1  = base + usr_off1 - sizeof( pmm::Block<AT> );
    auto        orig_root = pmm::BlockStateBase<AT>::get_root_offset( blk_raw1 );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw1, orig_root + 50 );

    // Corrupt prev_offset of p2
    std::size_t usr_off2  = static_cast<std::size_t>( p2.offset() ) * AT::granule_size;
    void*       blk_raw2  = base + usr_off2 - sizeof( pmm::Block<AT> );
    auto        orig_prev = pmm::BlockStateBase<AT>::get_prev_offset( blk_raw2 );
    pmm::BlockStateBase<AT>::set_prev_offset_of( blk_raw2, orig_prev + 200 );

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );
    REQUIRE( v.violation_count >= 2 );
    REQUIRE( has_violation( v, pmm::ViolationType::BlockStateInconsistent ) );
    REQUIRE( has_violation( v, pmm::ViolationType::PrevOffsetMismatch ) );

    // Restore
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw1, orig_root );
    pmm::BlockStateBase<AT>::set_prev_offset_of( blk_raw2, orig_prev );
    Mgr::destroy();
}

// ─── D11: Corruption repaired after load ────────────────────────────────────

TEST_CASE( "corruption: block state corruption repaired by load", "[issue258][corruption]" )
{
    using MgrA = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25831>;
    using MgrB = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25832>;

    const char*       kFile = "test_issue258_corr_repair.dat";
    const std::size_t arena = 64 * 1024;

    REQUIRE( MgrA::create( arena ) );
    auto p = MgrA::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt root_offset before save
    std::uint8_t* base    = MgrA::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );
    auto          blk_idx = static_cast<AT::index_type>( p.offset() - sizeof( pmm::Block<AT> ) / AT::granule_size );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, blk_idx + 500 );

    REQUIRE( pmm::save_manager<MgrA>( kFile ) );
    MgrA::destroy();

    // Load should repair
    REQUIRE( MgrB::create( arena ) );
    pmm::VerifyResult load_result;
    REQUIRE( pmm::load_manager_from_file<MgrB>( kFile, load_result ) );

    // After repair, verify should be clean
    pmm::VerifyResult post = MgrB::verify();
    REQUIRE( post.ok );

    MgrB::destroy();
    std::remove( kFile );
}

// ─── D12: Invalid pointer provenance ────────────────────────────────────────

TEST_CASE( "corruption: out-of-range block index in prev_offset", "[issue258][corruption]" )
{
    setup_clean();

    auto p1 = Mgr::allocate_typed<std::uint32_t>( 8 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 8 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );

    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p2.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );

    // Set prev to an impossibly large value
    auto orig = pmm::BlockStateBase<AT>::get_prev_offset( blk_raw );
    pmm::BlockStateBase<AT>::set_prev_offset_of( blk_raw, AT::no_block - 1 );

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE_FALSE( v.ok );

    pmm::BlockStateBase<AT>::set_prev_offset_of( blk_raw, orig );
    Mgr::destroy();
}
