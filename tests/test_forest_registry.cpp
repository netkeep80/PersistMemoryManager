#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using ForestMgr       = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 240>;
using ForestPersistMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 241>;

TEST_CASE( "forest registry bootstraps system domains", "[test_forest_registry]" )
{
    ForestMgr::destroy();
    REQUIRE( ForestMgr::create( 64 * 1024 ) );

    REQUIRE( ForestMgr::has_domain( pmm::detail::kSystemDomainFreeTree ) );
    REQUIRE( ForestMgr::has_domain( pmm::detail::kSystemDomainSymbols ) );
    REQUIRE( ForestMgr::has_domain( pmm::detail::kSystemDomainRegistry ) );

    auto free_id     = ForestMgr::find_domain_by_name( pmm::detail::kSystemDomainFreeTree );
    auto symbols_id  = ForestMgr::find_domain_by_name( pmm::detail::kSystemDomainSymbols );
    auto registry_id = ForestMgr::find_domain_by_name( pmm::detail::kSystemDomainRegistry );

    REQUIRE( free_id != 0 );
    REQUIRE( symbols_id != 0 );
    REQUIRE( registry_id != 0 );

    REQUIRE( ForestMgr::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree ) != 0 );
    REQUIRE( ForestMgr::get_domain_root_offset( free_id ) ==
             ForestMgr::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree ) );
    REQUIRE( ForestMgr::get_domain_root_offset( pmm::detail::kSystemDomainSymbols ) != 0 );

    ForestMgr::pptr<ForestMgr::pstringview> symbols_domain_symbol = ForestMgr::pstringview( pmm::detail::kSystemDomainSymbols );
    ForestMgr::pptr<ForestMgr::pstringview> free_domain_symbol    = ForestMgr::pstringview( pmm::detail::kSystemDomainFreeTree );
    ForestMgr::pptr<ForestMgr::pstringview> registry_symbol = ForestMgr::pstringview( pmm::detail::kSystemDomainRegistry );
    REQUIRE( !symbols_domain_symbol.is_null() );
    REQUIRE( !free_domain_symbol.is_null() );
    REQUIRE( !registry_symbol.is_null() );
    REQUIRE( ForestMgr::find_domain_by_symbol( symbols_domain_symbol ) == symbols_id );
    REQUIRE( ForestMgr::find_domain_by_symbol( free_domain_symbol ) == free_id );
    REQUIRE( ForestMgr::find_domain_by_symbol( registry_symbol ) == registry_id );
    REQUIRE( ForestMgr::get_domain_root_offset( registry_symbol ) ==
             ForestMgr::get_domain_root_offset( pmm::detail::kSystemDomainRegistry ) );

    ForestMgr::destroy();
}

TEST_CASE( "forest registry persists user domains and legacy root", "[test_forest_registry]" )
{
    const char* filename = "test_forest_registry.dat";

    ForestPersistMgr::destroy();
    REQUIRE( ForestPersistMgr::create( 128 * 1024 ) );

    REQUIRE( ForestPersistMgr::register_domain( "app/alpha" ) );
    REQUIRE( ForestPersistMgr::register_domain( "app/beta" ) );

    auto alpha = ForestPersistMgr::create_typed<int>( 11 );
    auto beta  = ForestPersistMgr::create_typed<int>( 22 );
    REQUIRE( !alpha.is_null() );
    REQUIRE( !beta.is_null() );

    REQUIRE( ForestPersistMgr::set_domain_root( "app/alpha", alpha ) );
    REQUIRE( ForestPersistMgr::set_domain_root( "app/beta", beta ) );
    ForestPersistMgr::set_root( alpha );
    auto symbols_root_before = ForestPersistMgr::pstringview::root_index();
    REQUIRE( symbols_root_before != 0 );

    auto alpha_id = ForestPersistMgr::find_domain_by_name( "app/alpha" );
    auto beta_id  = ForestPersistMgr::find_domain_by_name( "app/beta" );
    REQUIRE( alpha_id != 0 );
    REQUIRE( beta_id != 0 );
    REQUIRE( alpha_id != beta_id );

    ForestPersistMgr::pptr<ForestPersistMgr::pstringview> alpha_symbol = ForestPersistMgr::pstringview( "app/alpha" );
    REQUIRE( !alpha_symbol.is_null() );
    REQUIRE( ForestPersistMgr::find_domain_by_symbol( alpha_symbol ) == alpha_id );

    auto free_root_before = ForestPersistMgr::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree );
    REQUIRE( free_root_before != 0 );
    ForestPersistMgr::pptr<ForestPersistMgr::pstringview> registry_type_before =
        ForestPersistMgr::pstringview( pmm::detail::kSystemTypeForestRegistry );
    REQUIRE( !registry_type_before.is_null() );

    REQUIRE( pmm::save_manager<ForestPersistMgr>( filename ) );
    auto alpha_offset = alpha.offset();
    auto beta_offset  = beta.offset();

    ForestPersistMgr::destroy();
    REQUIRE( ForestPersistMgr::create( 128 * 1024 ) );
    REQUIRE( pmm::load_manager_from_file<ForestPersistMgr>( filename ) );

    REQUIRE( ForestPersistMgr::has_domain( "app/alpha" ) );
    REQUIRE( ForestPersistMgr::has_domain( "app/beta" ) );

    auto alpha_after = ForestPersistMgr::get_domain_root<int>( "app/alpha" );
    auto beta_after  = ForestPersistMgr::get_domain_root<int>( beta_id );
    REQUIRE( !alpha_after.is_null() );
    REQUIRE( !beta_after.is_null() );
    REQUIRE( alpha_after.offset() == alpha_offset );
    REQUIRE( beta_after.offset() == beta_offset );
    REQUIRE( *alpha_after == 11 );
    REQUIRE( *beta_after == 22 );

    ForestPersistMgr::pptr<ForestPersistMgr::pstringview> alpha_symbol_after =
        ForestPersistMgr::pstringview( "app/alpha" );
    REQUIRE( !alpha_symbol_after.is_null() );
    REQUIRE( ForestPersistMgr::get_domain_root_offset( alpha_symbol_after ) == alpha_offset );

    auto legacy_root = ForestPersistMgr::get_root<int>();
    REQUIRE( !legacy_root.is_null() );
    REQUIRE( legacy_root.offset() == alpha_offset );
    REQUIRE( *legacy_root == 11 );

    REQUIRE( ForestPersistMgr::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree ) != 0 );
    REQUIRE( ForestPersistMgr::pstringview::root_index() != 0 );

    ForestPersistMgr::pptr<ForestPersistMgr::pstringview> registry_type_after =
        ForestPersistMgr::pstringview( pmm::detail::kSystemTypeForestRegistry );
    REQUIRE( !registry_type_after.is_null() );
    REQUIRE( ForestPersistMgr::find_domain_by_symbol( ForestPersistMgr::pstringview( pmm::detail::kSystemDomainSymbols ) ) != 0 );
    REQUIRE( ForestPersistMgr::find_domain_by_symbol( ForestPersistMgr::pstringview( pmm::detail::kSystemDomainRegistry ) ) != 0 );

    ForestPersistMgr::destroy();
    std::remove( filename );
}
