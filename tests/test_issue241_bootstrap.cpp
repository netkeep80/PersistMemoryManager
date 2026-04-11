#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using BootstrapMgr     = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2410>;
using BootstrapPersist = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2411>;

// ---------------------------------------------------------------------------
// Bootstrap invariants hold immediately after create()
// ---------------------------------------------------------------------------

TEST_CASE( "bootstrap invariants hold after create(size)", "[issue241]" )
{
    BootstrapMgr::destroy();
    REQUIRE( BootstrapMgr::create( 64 * 1024 ) );

    // Public invariant check
    REQUIRE( BootstrapMgr::validate_bootstrap_invariants() );

    // Detailed checks that the issue requires:

    // 1. Manager header is valid (magic, granule_size)
    REQUIRE( BootstrapMgr::is_initialized() );

    // 2. Free-tree exists (system/free_tree domain has non-zero root)
    REQUIRE( BootstrapMgr::has_domain( pmm::detail::kSystemDomainFreeTree ) );
    REQUIRE( BootstrapMgr::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree ) != 0 );

    // 3. Forest/domain registry exists (system/domain_registry domain)
    REQUIRE( BootstrapMgr::has_domain( pmm::detail::kSystemDomainRegistry ) );
    REQUIRE( BootstrapMgr::get_domain_root_offset( pmm::detail::kSystemDomainRegistry ) != 0 );

    // 4. Symbol dictionary exists (system/symbols domain with non-zero root)
    REQUIRE( BootstrapMgr::has_domain( pmm::detail::kSystemDomainSymbols ) );
    REQUIRE( BootstrapMgr::get_domain_root_offset( pmm::detail::kSystemDomainSymbols ) != 0 );

    // 5. System symbol names are interned
    BootstrapMgr::pptr<BootstrapMgr::pstringview> free_sym =
        BootstrapMgr::pstringview( pmm::detail::kSystemDomainFreeTree );
    BootstrapMgr::pptr<BootstrapMgr::pstringview> sym_sym =
        BootstrapMgr::pstringview( pmm::detail::kSystemDomainSymbols );
    BootstrapMgr::pptr<BootstrapMgr::pstringview> reg_sym =
        BootstrapMgr::pstringview( pmm::detail::kSystemDomainRegistry );
    BootstrapMgr::pptr<BootstrapMgr::pstringview> type_reg =
        BootstrapMgr::pstringview( pmm::detail::kSystemTypeForestRegistry );
    BootstrapMgr::pptr<BootstrapMgr::pstringview> type_psv =
        BootstrapMgr::pstringview( pmm::detail::kSystemTypePstringview );
    REQUIRE( !free_sym.is_null() );
    REQUIRE( !sym_sym.is_null() );
    REQUIRE( !reg_sym.is_null() );
    REQUIRE( !type_reg.is_null() );
    REQUIRE( !type_psv.is_null() );

    // 6. Domains are discoverable by interned symbol
    auto free_id     = BootstrapMgr::find_domain_by_name( pmm::detail::kSystemDomainFreeTree );
    auto symbols_id  = BootstrapMgr::find_domain_by_name( pmm::detail::kSystemDomainSymbols );
    auto registry_id = BootstrapMgr::find_domain_by_name( pmm::detail::kSystemDomainRegistry );
    REQUIRE( free_id != 0 );
    REQUIRE( symbols_id != 0 );
    REQUIRE( registry_id != 0 );

    REQUIRE( BootstrapMgr::find_domain_by_symbol( free_sym ) == free_id );
    REQUIRE( BootstrapMgr::find_domain_by_symbol( sym_sym ) == symbols_id );
    REQUIRE( BootstrapMgr::find_domain_by_symbol( reg_sym ) == registry_id );

    // 7. Bootstrap is deterministic: binding IDs are consistent
    REQUIRE( free_id != symbols_id );
    REQUIRE( free_id != registry_id );
    REQUIRE( symbols_id != registry_id );

    BootstrapMgr::destroy();
}

// ---------------------------------------------------------------------------
// Bootstrap invariants hold after save/load cycle
// ---------------------------------------------------------------------------

TEST_CASE( "bootstrap invariants hold after save/load", "[issue241]" )
{
    const char* filename = "test_issue241_bootstrap.dat";

    BootstrapPersist::destroy();
    REQUIRE( BootstrapPersist::create( 128 * 1024 ) );
    REQUIRE( BootstrapPersist::validate_bootstrap_invariants() );

    // Remember pre-save state
    auto free_root_before     = BootstrapPersist::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree );
    auto symbols_root_before  = BootstrapPersist::pstringview::root_index();
    auto registry_root_before = BootstrapPersist::get_domain_root_offset( pmm::detail::kSystemDomainRegistry );
    REQUIRE( free_root_before != 0 );
    REQUIRE( symbols_root_before != 0 );
    REQUIRE( registry_root_before != 0 );

    REQUIRE( pmm::save_manager<BootstrapPersist>( filename ) );

    BootstrapPersist::destroy();
    REQUIRE( BootstrapPersist::create( 128 * 1024 ) );
    REQUIRE( pmm::load_manager_from_file<BootstrapPersist>( filename, pmm::VerifyResult{} ) );

    // Invariants must hold after load
    REQUIRE( BootstrapPersist::validate_bootstrap_invariants() );

    // System domains survived persistence
    REQUIRE( BootstrapPersist::has_domain( pmm::detail::kSystemDomainFreeTree ) );
    REQUIRE( BootstrapPersist::has_domain( pmm::detail::kSystemDomainSymbols ) );
    REQUIRE( BootstrapPersist::has_domain( pmm::detail::kSystemDomainRegistry ) );

    // Symbol dictionary survived persistence
    REQUIRE( BootstrapPersist::pstringview::root_index() != 0 );
    BootstrapPersist::pptr<BootstrapPersist::pstringview> free_sym_after =
        BootstrapPersist::pstringview( pmm::detail::kSystemDomainFreeTree );
    BootstrapPersist::pptr<BootstrapPersist::pstringview> sym_sym_after =
        BootstrapPersist::pstringview( pmm::detail::kSystemDomainSymbols );
    BootstrapPersist::pptr<BootstrapPersist::pstringview> reg_sym_after =
        BootstrapPersist::pstringview( pmm::detail::kSystemDomainRegistry );
    REQUIRE( !free_sym_after.is_null() );
    REQUIRE( !sym_sym_after.is_null() );
    REQUIRE( !reg_sym_after.is_null() );

    // Registry root offset is consistent after load
    REQUIRE( BootstrapPersist::get_domain_root_offset( pmm::detail::kSystemDomainRegistry ) == registry_root_before );

    BootstrapPersist::destroy();
    std::remove( filename );
}

// ---------------------------------------------------------------------------
// Deterministic bootstrap — two creates produce identical layout
// ---------------------------------------------------------------------------

TEST_CASE( "bootstrap is deterministic", "[issue241]" )
{
    using Mgr1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2412>;
    using Mgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2413>;

    Mgr1::destroy();
    Mgr2::destroy();
    REQUIRE( Mgr1::create( 64 * 1024 ) );
    REQUIRE( Mgr2::create( 64 * 1024 ) );

    // Both must pass invariant check
    REQUIRE( Mgr1::validate_bootstrap_invariants() );
    REQUIRE( Mgr2::validate_bootstrap_invariants() );

    // Same number of system domains
    auto free1     = Mgr1::find_domain_by_name( pmm::detail::kSystemDomainFreeTree );
    auto symbols1  = Mgr1::find_domain_by_name( pmm::detail::kSystemDomainSymbols );
    auto registry1 = Mgr1::find_domain_by_name( pmm::detail::kSystemDomainRegistry );
    auto free2     = Mgr2::find_domain_by_name( pmm::detail::kSystemDomainFreeTree );
    auto symbols2  = Mgr2::find_domain_by_name( pmm::detail::kSystemDomainSymbols );
    auto registry2 = Mgr2::find_domain_by_name( pmm::detail::kSystemDomainRegistry );

    // Binding IDs must be identical (deterministic sequence)
    REQUIRE( free1 == free2 );
    REQUIRE( symbols1 == symbols2 );
    REQUIRE( registry1 == registry2 );

    // Domain root offsets must be identical
    REQUIRE( Mgr1::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree ) ==
             Mgr2::get_domain_root_offset( pmm::detail::kSystemDomainFreeTree ) );
    REQUIRE( Mgr1::get_domain_root_offset( pmm::detail::kSystemDomainSymbols ) ==
             Mgr2::get_domain_root_offset( pmm::detail::kSystemDomainSymbols ) );
    REQUIRE( Mgr1::get_domain_root_offset( pmm::detail::kSystemDomainRegistry ) ==
             Mgr2::get_domain_root_offset( pmm::detail::kSystemDomainRegistry ) );

    Mgr1::destroy();
    Mgr2::destroy();
}
