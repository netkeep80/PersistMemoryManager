#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>

using ForestMgr        = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 240>;
using ForestPersistMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 241>;
using CanonicalRootMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 313>;

TEST_CASE( "forest registry bootstraps system domains", "[test_forest_registry]" )
{
    ForestMgr::destroy();
    REQUIRE( ForestMgr::create( 64 * 1024 ) );

    REQUIRE( ForestMgr::has_domain( pmm::detail::kSystemDomainFreeTree ) );
    REQUIRE( ForestMgr::has_domain( pmm::detail::kSystemDomainSymbols ) );
    REQUIRE( ForestMgr::has_domain( pmm::detail::kSystemDomainRegistry ) );
    REQUIRE( ForestMgr::has_domain( pmm::detail::kServiceNameLegacyRoot ) );

    auto free_id     = ForestMgr::find_domain_by_name( pmm::detail::kSystemDomainFreeTree );
    auto symbols_id  = ForestMgr::find_domain_by_name( pmm::detail::kSystemDomainSymbols );
    auto registry_id = ForestMgr::find_domain_by_name( pmm::detail::kSystemDomainRegistry );
    auto legacy_id   = ForestMgr::find_domain_by_name( pmm::detail::kServiceNameLegacyRoot );

    REQUIRE( free_id != 0 );
    REQUIRE( symbols_id != 0 );
    REQUIRE( registry_id != 0 );
    REQUIRE( legacy_id != 0 );

    REQUIRE( ForestMgr::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree ) != 0 );
    REQUIRE( ForestMgr::get_domain_root_offset( free_id ) ==
             ForestMgr::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree ) );
    REQUIRE( ForestMgr::get_domain_root_offset( pmm::detail::kSystemDomainSymbols ) != 0 );
    REQUIRE( ForestMgr::get_domain_root_offset( pmm::detail::kServiceNameLegacyRoot ) == 0 );

    ForestMgr::pptr<ForestMgr::pstringview> symbols_domain_symbol =
        ForestMgr::pstringview( pmm::detail::kSystemDomainSymbols );
    ForestMgr::pptr<ForestMgr::pstringview> free_domain_symbol =
        ForestMgr::pstringview( pmm::detail::kSystemDomainFreeTree );
    ForestMgr::pptr<ForestMgr::pstringview> registry_symbol =
        ForestMgr::pstringview( pmm::detail::kSystemDomainRegistry );
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
    {
        pmm::VerifyResult vr_;
        REQUIRE( pmm::load_manager_from_file<ForestPersistMgr>( filename, vr_ ) );
    }

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
    REQUIRE( ForestPersistMgr::get_domain_root_offset( pmm::detail::kServiceNameLegacyRoot ) == alpha_offset );

    REQUIRE( ForestPersistMgr::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree ) != 0 );
    REQUIRE( ForestPersistMgr::pstringview::root_index() != 0 );

    ForestPersistMgr::pptr<ForestPersistMgr::pstringview> registry_type_after =
        ForestPersistMgr::pstringview( pmm::detail::kSystemTypeForestRegistry );
    REQUIRE( !registry_type_after.is_null() );
    REQUIRE( ForestPersistMgr::find_domain_by_symbol(
                 ForestPersistMgr::pstringview( pmm::detail::kSystemDomainSymbols ) ) != 0 );
    REQUIRE( ForestPersistMgr::find_domain_by_symbol(
                 ForestPersistMgr::pstringview( pmm::detail::kSystemDomainRegistry ) ) != 0 );

    ForestPersistMgr::destroy();
    std::remove( filename );
}

TEST_CASE( "legacy root API is a compatibility shim over the domain registry", "[test_forest_registry][issue313]" )
{
    CanonicalRootMgr::destroy();
    REQUIRE( CanonicalRootMgr::create( 128 * 1024 ) );

    REQUIRE( CanonicalRootMgr::has_domain( pmm::detail::kServiceNameLegacyRoot ) );
    auto legacy_domain_id = CanonicalRootMgr::find_domain_by_name( pmm::detail::kServiceNameLegacyRoot );
    REQUIRE( legacy_domain_id != 0 );

    REQUIRE( CanonicalRootMgr::get_root<int>().is_null() );
    REQUIRE( CanonicalRootMgr::get_domain_root<int>( pmm::detail::kServiceNameLegacyRoot ).is_null() );
    REQUIRE( CanonicalRootMgr::get_domain_root_offset( legacy_domain_id ) == 0 );

    using AT           = CanonicalRootMgr::address_traits;
    std::uint8_t* base = CanonicalRootMgr::backend().base_ptr();
    auto*         hdr  = reinterpret_cast<pmm::detail::ManagerHeader<AT>*>( base + sizeof( pmm::Block<AT> ) );
    auto*         reg  = reinterpret_cast<pmm::detail::ForestDomainRegistry<AT>*>(
        base + static_cast<std::size_t>( hdr->root_offset ) * AT::granule_size );
    REQUIRE( reg->reserved_root_offset == 0 );

    auto legacy_value = CanonicalRootMgr::create_typed<int>( 31 );
    auto domain_value = CanonicalRootMgr::create_typed<int>( 313 );
    REQUIRE( !legacy_value.is_null() );
    REQUIRE( !domain_value.is_null() );

    CanonicalRootMgr::set_root( legacy_value );
    REQUIRE( CanonicalRootMgr::get_domain_root_offset( pmm::detail::kServiceNameLegacyRoot ) == legacy_value.offset() );
    REQUIRE( CanonicalRootMgr::get_domain_root<int>( legacy_domain_id ).offset() == legacy_value.offset() );

    REQUIRE( CanonicalRootMgr::set_domain_root( pmm::detail::kServiceNameLegacyRoot, domain_value ) );
    REQUIRE( CanonicalRootMgr::get_root<int>().offset() == domain_value.offset() );
    REQUIRE( *CanonicalRootMgr::get_root<int>() == 313 );

    reg->reserved_root_offset = legacy_value.offset();
    REQUIRE( CanonicalRootMgr::get_root<int>().offset() == domain_value.offset() );
    reg->reserved_root_offset = 0;
    REQUIRE( CanonicalRootMgr::validate_bootstrap_invariants() );

    CanonicalRootMgr::set_root( CanonicalRootMgr::pptr<int>() );
    REQUIRE( CanonicalRootMgr::get_root<int>().is_null() );
    REQUIRE( CanonicalRootMgr::get_domain_root<int>( pmm::detail::kServiceNameLegacyRoot ).is_null() );

    CanonicalRootMgr::destroy();
}
