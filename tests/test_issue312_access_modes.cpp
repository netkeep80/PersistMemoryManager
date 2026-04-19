/**
 * @file test_issue312_access_modes.cpp
 * @brief Tests for explicit pptr checked/unchecked access modes.
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace
{
using Mgr      = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 312>;
using SmallMgr = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 312>;
} // namespace

TEST_CASE( "I312: valid pptr resolves through checked and unchecked paths", "[test_issue312]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint32_t> p = Mgr::allocate_typed<std::uint32_t>();
    REQUIRE( !p.is_null() );

    REQUIRE( Mgr::resolve_checked( p ) != nullptr );
    REQUIRE( Mgr::resolve_unchecked( p ) != nullptr );
    REQUIRE( Mgr::resolve( p ) == Mgr::resolve_checked( p ) );
    REQUIRE( p.resolve() == Mgr::resolve_checked( p ) );
    REQUIRE( p.resolve_unchecked() == Mgr::resolve_unchecked( p ) );

    *p = 0x312u;
    REQUIRE( *Mgr::resolve_checked( p ) == 0x312u );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}

TEST_CASE( "I312: SmallAddressTraits resolves to canonical non-aligned user pointer", "[test_issue312]" )
{
    static_assert( sizeof( pmm::Block<pmm::SmallAddressTraits> ) % pmm::SmallAddressTraits::granule_size != 0,
                   "SmallAddressTraits must exercise the non-aligned block-header path" );

    REQUIRE( SmallMgr::create() );

    SmallMgr::pptr<std::uint32_t> p = SmallMgr::allocate_typed<std::uint32_t>();
    REQUIRE( !p.is_null() );

    auto* checked   = SmallMgr::resolve_checked( p );
    auto* unchecked = SmallMgr::resolve_unchecked( p );
    REQUIRE( checked != nullptr );
    REQUIRE( unchecked != nullptr );
    REQUIRE( checked == unchecked );
    REQUIRE( p.resolve() == checked );
    REQUIRE( p.resolve_unchecked() == unchecked );

    *checked = 0x51312u;
    REQUIRE( *unchecked == 0x51312u );

    SmallMgr::deallocate_typed( p );
    SmallMgr::destroy();
}

TEST_CASE( "I312: invalid out-of-range pptr is rejected by both access modes", "[test_issue312]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint32_t> invalid( static_cast<Mgr::index_type>( Mgr::total_size() ) );

    REQUIRE( Mgr::resolve_checked( invalid ) == nullptr );
    REQUIRE( Mgr::resolve_unchecked( invalid ) == nullptr );
    REQUIRE_FALSE( Mgr::is_valid_ptr( invalid ) );

    Mgr::destroy();
}

TEST_CASE( "I312: stale pptr is rejected by checked access but explicit unchecked access remains raw",
           "[test_issue312]" )
{
    REQUIRE( Mgr::create( 64 * 1024 ) );

    Mgr::pptr<std::uint32_t> stale = Mgr::allocate_typed<std::uint32_t>();
    REQUIRE( !stale.is_null() );
    *stale = 0xDEAD312u;

    std::uint32_t* raw_before_free = Mgr::resolve_unchecked( stale );
    REQUIRE( raw_before_free != nullptr );

    Mgr::deallocate_typed( stale );

    REQUIRE( Mgr::resolve_checked( stale ) == nullptr );
    REQUIRE( Mgr::resolve( stale ) == nullptr );
    REQUIRE( stale.resolve() == nullptr );
    REQUIRE_FALSE( Mgr::is_valid_ptr( stale ) );

    REQUIRE( Mgr::resolve_unchecked( stale ) == raw_before_free );
    REQUIRE( stale.resolve_unchecked() == raw_before_free );

    Mgr::destroy();
}
