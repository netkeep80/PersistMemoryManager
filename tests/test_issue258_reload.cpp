/**
 * @file test_issue258_reload.cpp
 * @brief Reload and relocation tests.
 *
 * Covers test matrix group B: reload at different buffer address,
 * multi-preset round-trip, user domain survival, pstring/pstringview
 * persistence across reload.
 *
 * @see docs/test_matrix.md — B7–B10
 * @see docs/core_invariants.md — D2a, C2a, C3a
 */

#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// ─── B7: Reload at different buffer address ─────────────────────────────────
//
// Both managers are kept alive simultaneously so that each holds its own
// heap-allocated backend buffer.  Because two independent malloc'd regions
// cannot overlap, the loaded image is guaranteed to reside at a different
// virtual address than the original.
// All offset-based data should still be valid because PMM uses granule indices,
// not raw pointers.

TEST_CASE( "reload: different base address via different InstanceId", "[issue258][reload]" )
{
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25810>;
    using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25811>;

    const char*       kFile = "test_issue258_reload_base.dat";
    const std::size_t arena = 64 * 1024;

    // Create Mgr1 and populate data.
    REQUIRE( Mgr1::create( arena ) );

    auto p1 = Mgr1::allocate_typed<std::uint64_t>( 8 );
    REQUIRE( !p1.is_null() );

    std::uint64_t* data1 = Mgr1::template resolve<std::uint64_t>( p1 );
    REQUIRE( data1 != nullptr );
    for ( int i = 0; i < 8; ++i )
        data1[i] = static_cast<std::uint64_t>( 0xDEAD000000000000ULL | i );

    auto saved_offset = p1.offset();

    REQUIRE( pmm::save_manager<Mgr1>( kFile ) );

    // Create Mgr2 while Mgr1 is still alive — guarantees a separate buffer.
    REQUIRE( Mgr2::create( arena ) );

    auto* base1_ptr = Mgr1::backend().base_ptr();
    auto* base2_ptr = Mgr2::backend().base_ptr();
    REQUIRE( base1_ptr != base2_ptr );

    pmm::VerifyResult result;
    REQUIRE( pmm::load_manager_from_file<Mgr2>( kFile, result ) );

    REQUIRE( Mgr2::is_initialized() );
    REQUIRE( Mgr2::validate_bootstrap_invariants() );

    typename Mgr2::template pptr<std::uint64_t> p2( saved_offset );
    std::uint64_t*                              data2 = Mgr2::template resolve<std::uint64_t>( p2 );
    REQUIRE( data2 != nullptr );
    for ( int i = 0; i < 8; ++i )
    {
        REQUIRE( data2[i] == static_cast<std::uint64_t>( 0xDEAD000000000000ULL | i ) );
    }

    // Clean up both managers.
    Mgr1::destroy();
    Mgr2::destroy();
    std::remove( kFile );
}

// ─── B8: Multiple presets save/load round-trip ──────────────────────────────

template <typename MgrSave, typename MgrLoad>
static void test_preset_roundtrip( const char* filename, std::size_t arena )
{
    MgrSave::destroy();
    MgrLoad::destroy();

    REQUIRE( MgrSave::create( arena ) );

    auto p = MgrSave::template allocate_typed<std::uint32_t>( 4 );
    REQUIRE( !p.is_null() );
    std::uint32_t* data = MgrSave::template resolve<std::uint32_t>( p );
    for ( int i = 0; i < 4; ++i )
        data[i] = static_cast<std::uint32_t>( 42 + i );

    auto saved_offset = p.offset();
    auto saved_blocks = MgrSave::block_count();

    REQUIRE( pmm::save_manager<MgrSave>( filename ) );
    MgrSave::destroy();

    REQUIRE( MgrLoad::create( arena ) );
    pmm::VerifyResult result;
    REQUIRE( pmm::load_manager_from_file<MgrLoad>( filename, result ) );

    REQUIRE( MgrLoad::is_initialized() );
    REQUIRE( MgrLoad::block_count() == saved_blocks );

    typename MgrLoad::template pptr<std::uint32_t> p2( saved_offset );
    std::uint32_t*                                 loaded = MgrLoad::template resolve<std::uint32_t>( p2 );
    REQUIRE( loaded != nullptr );
    for ( int i = 0; i < 4; ++i )
    {
        REQUIRE( loaded[i] == static_cast<std::uint32_t>( 42 + i ) );
    }

    MgrLoad::destroy();
    std::remove( filename );
}

TEST_CASE( "reload: CacheManagerConfig round-trip", "[issue258][reload]" )
{
    using Save = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25812>;
    using Load = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25813>;
    test_preset_roundtrip<Save, Load>( "test_issue258_rt_cache.dat", 64 * 1024 );
}

TEST_CASE( "reload: EmbeddedManagerConfig round-trip", "[issue258][reload]" )
{
    using Save = pmm::PersistMemoryManager<pmm::EmbeddedManagerConfig, 25814>;
    using Load = pmm::PersistMemoryManager<pmm::EmbeddedManagerConfig, 25815>;
    test_preset_roundtrip<Save, Load>( "test_issue258_rt_embedded.dat", 64 * 1024 );
}

// ─── B9: User domains survive reload ────────────────────────────────────────

TEST_CASE( "reload: user domain survives save/load", "[issue258][reload]" )
{
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25816>;
    using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25817>;

    const char*       kFile = "test_issue258_reload_domain.dat";
    const std::size_t arena = 64 * 1024;

    REQUIRE( Mgr1::create( arena ) );

    const char* domain_name = "user/test_domain";
    REQUIRE( Mgr1::register_domain( domain_name ) );
    REQUIRE( Mgr1::has_domain( domain_name ) );

    auto domain_binding = Mgr1::find_domain_by_name( domain_name );
    REQUIRE( domain_binding != 0 );

    REQUIRE( pmm::save_manager<Mgr1>( kFile ) );
    Mgr1::destroy();

    REQUIRE( Mgr2::create( arena ) );
    pmm::VerifyResult result;
    REQUIRE( pmm::load_manager_from_file<Mgr2>( kFile, result ) );

    REQUIRE( Mgr2::has_domain( domain_name ) );
    auto loaded_binding = Mgr2::find_domain_by_name( domain_name );
    REQUIRE( loaded_binding == domain_binding );

    Mgr2::destroy();
    std::remove( kFile );
}

// ─── B10: pstringview survives reload ───────────────────────────────────────

TEST_CASE( "reload: pstringview content survives save/load", "[issue258][reload]" )
{
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25818>;
    using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25819>;

    const char*       kFile = "test_issue258_reload_psv.dat";
    const std::size_t arena = 64 * 1024;

    REQUIRE( Mgr1::create( arena ) );

    Mgr1::pptr<Mgr1::pstringview> psv = Mgr1::pstringview( "test_interned_string" );
    REQUIRE( !psv.is_null() );
    auto psv_offset = psv.offset();

    REQUIRE( pmm::save_manager<Mgr1>( kFile ) );
    Mgr1::destroy();

    REQUIRE( Mgr2::create( arena ) );
    pmm::VerifyResult result;
    REQUIRE( pmm::load_manager_from_file<Mgr2>( kFile, result ) );

    Mgr2::pptr<Mgr2::pstringview> psv2 = Mgr2::pstringview( "test_interned_string" );
    REQUIRE( !psv2.is_null() );
    REQUIRE( psv2.offset() == psv_offset );

    Mgr2::destroy();
    std::remove( kFile );
}
