/**
 * @file test_issue258_bootstrap.cpp
 * @brief Extended bootstrap tests across preset configurations.
 *
 * Covers test matrix group A: bootstrap invariants across
 * multiple manager configurations and memory stats consistency.
 *
 * @see docs/test_matrix.md — A8, A9
 * @see docs/core_invariants.md — D1a–D1d
 */

#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

// ─── A8: Bootstrap across preset configurations ─────────────────────────────

template <typename MgrT> static void verify_bootstrap_for_preset( std::size_t arena_size )
{
    MgrT::destroy();
    REQUIRE( MgrT::create( arena_size ) );

    REQUIRE( MgrT::is_initialized() );
    REQUIRE( MgrT::validate_bootstrap_invariants() );

    REQUIRE( MgrT::has_domain( pmm::detail::kSystemDomainFreeTree ) );
    REQUIRE( MgrT::has_domain( pmm::detail::kSystemDomainSymbols ) );
    REQUIRE( MgrT::has_domain( pmm::detail::kSystemDomainRegistry ) );

    REQUIRE( MgrT::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree ) != 0 );
    REQUIRE( MgrT::get_domain_root_offset( pmm::detail::kSystemDomainSymbols ) != 0 );
    REQUIRE( MgrT::get_domain_root_offset( pmm::detail::kSystemDomainRegistry ) != 0 );

    auto free_id     = MgrT::find_domain_by_name( pmm::detail::kSystemDomainFreeTree );
    auto symbols_id  = MgrT::find_domain_by_name( pmm::detail::kSystemDomainSymbols );
    auto registry_id = MgrT::find_domain_by_name( pmm::detail::kSystemDomainRegistry );
    REQUIRE( free_id != 0 );
    REQUIRE( symbols_id != 0 );
    REQUIRE( registry_id != 0 );
    REQUIRE( free_id != symbols_id );
    REQUIRE( free_id != registry_id );
    REQUIRE( symbols_id != registry_id );

    MgrT::destroy();
}

TEST_CASE( "bootstrap: SingleThreadedHeap preset", "[issue258][bootstrap]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25801>;
    verify_bootstrap_for_preset<Mgr>( 64 * 1024 );
}

TEST_CASE( "bootstrap: EmbeddedHeap preset", "[issue258][bootstrap]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::EmbeddedManagerConfig, 25802>;
    verify_bootstrap_for_preset<Mgr>( 64 * 1024 );
}

TEST_CASE( "bootstrap: EmbeddedStaticHeap preset", "[issue258][bootstrap]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<65536>, 25803>;
    verify_bootstrap_for_preset<Mgr>( 64 * 1024 );
}

TEST_CASE( "bootstrap: SmallEmbeddedStaticHeap preset", "[issue258][bootstrap]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<32768>, 25804>;
    verify_bootstrap_for_preset<Mgr>( 32 * 1024 );
}

// ─── A9: Memory stats consistent after create ───────────────────────────────

TEST_CASE( "bootstrap: memory stats consistent after create", "[issue258][bootstrap]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25805>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    REQUIRE( Mgr::total_size() == 64 * 1024 );
    REQUIRE( Mgr::block_count() > 0 );
    REQUIRE( Mgr::free_block_count() >= 1 );
    REQUIRE( Mgr::alloc_block_count() > 0 );
    REQUIRE( Mgr::block_count() == Mgr::free_block_count() + Mgr::alloc_block_count() );
    REQUIRE( Mgr::used_size() > 0 );
    REQUIRE( Mgr::free_size() > 0 );
    REQUIRE( Mgr::used_size() + Mgr::free_size() == Mgr::total_size() );

    Mgr::destroy();
}

TEST_CASE( "bootstrap: minimal arena size still bootstraps", "[issue258][bootstrap]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25806>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 4 * 1024 ) );
    REQUIRE( Mgr::is_initialized() );
    REQUIRE( Mgr::validate_bootstrap_invariants() );

    Mgr::destroy();
}
