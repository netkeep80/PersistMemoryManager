#include "pmm/persist_memory_manager.h"
#include "pmm/pstring.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 396>;
using Str = Mgr::pstring;

void reset_manager()
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );
}

void destroy_string( Mgr::pptr<Str> p )
{
    if ( auto* s = p.resolve() )
        s->free_data();
    Mgr::destroy_typed( p );
    Mgr::destroy();
}
} // namespace

TEST_CASE( "I396: pstring assign and append preserve embedded NUL bytes", "[issue396][pstring]" )
{
    reset_manager();
    auto p = Mgr::create_typed<Str>();
    REQUIRE( !p.is_null() );
    auto* s = p.resolve();
    REQUIRE( s != nullptr );

    const char first[] = { 'a', '\0', 'b' };
    REQUIRE( s->assign( first, sizeof( first ) ) );
    REQUIRE( s->size() == sizeof( first ) );
    REQUIRE( std::memcmp( s->data(), first, sizeof( first ) ) == 0 );
    REQUIRE( s->c_str()[s->size()] == '\0' );

    const char tail[] = { '\0', 'c' };
    REQUIRE( s->append( tail, sizeof( tail ) ) );
    const char expected[] = { 'a', '\0', 'b', '\0', 'c' };
    REQUIRE( s->size() == sizeof( expected ) );
    REQUIRE( std::memcmp( s->data(), expected, sizeof( expected ) ) == 0 );
    REQUIRE( s->c_str()[s->size()] == '\0' );

    destroy_string( p );
}

TEST_CASE( "I396: pstring comparison is length-aware and binary-safe", "[issue396][pstring]" )
{
    reset_manager();
    auto pa = Mgr::create_typed<Str>();
    auto pb = Mgr::create_typed<Str>();
    REQUIRE( !pa.is_null() );
    REQUIRE( !pb.is_null() );
    auto* a = pa.resolve();
    auto* b = pb.resolve();
    REQUIRE( a != nullptr );
    REQUIRE( b != nullptr );

    const char lhs[] = { 'x', '\0', 'a' };
    const char rhs[] = { 'x', '\0', 'b' };
    REQUIRE( a->assign( lhs, sizeof( lhs ) ) );
    REQUIRE( b->assign( rhs, sizeof( rhs ) ) );
    REQUIRE( *a != *b );
    REQUIRE( *a < *b );
    REQUIRE_FALSE( *b < *a );

    REQUIRE( b->assign( lhs, 2 ) );
    REQUIRE( *b < *a );
    REQUIRE_FALSE( *a == *b );

    a->free_data();
    b->free_data();
    Mgr::destroy_typed( pa );
    Mgr::destroy_typed( pb );
    Mgr::destroy();
}

TEST_CASE( "I396: self-append survives forced backing-buffer relocation", "[issue396][pstring]" )
{
    reset_manager();
    auto p = Mgr::create_typed<Str>();
    REQUIRE( !p.is_null() );
    auto* s = p.resolve();
    REQUIRE( s != nullptr );

    const char seed[] = "0123456789ABCDEF";
    REQUIRE( s->assign( seed, sizeof( seed ) - 1 ) );
    REQUIRE( s->size() == 16 );
    const char* before = s->data();
    REQUIRE( before != nullptr );

    // Keep the pstring object itself at a stable address while forcing only
    // its backing char block to move: block in-place growth, create an exact-fit
    // destination hole, and pin both sides of that hole against coalescing.
    auto blocker = Mgr::allocate_typed<std::byte>();
    REQUIRE( !blocker.is_null() );
    auto destination = Mgr::allocate_typed<char>( 33 );
    REQUIRE( !destination.is_null() );
    auto tail_guard = Mgr::allocate_typed<std::byte>();
    REQUIRE( !tail_guard.is_null() );
    const auto destination_idx = destination.offset();
    Mgr::deallocate_typed( destination );

    REQUIRE( s->append( s->data() + 1, 15 ) );
    REQUIRE( s->size() == 31 );
    REQUIRE( s->_data_idx == destination_idx );
    REQUIRE( s->data() != before );
    const char expected[] = "0123456789ABCDEF123456789ABCDEF";
    REQUIRE( std::memcmp( s->data(), expected, sizeof( expected ) - 1 ) == 0 );
    REQUIRE( s->c_str()[s->size()] == '\0' );
    REQUIRE( Mgr::verify().ok );

    Mgr::deallocate_typed( blocker );
    Mgr::deallocate_typed( tail_guard );
    destroy_string( p );
}

TEST_CASE( "I403: pstring mutation survives whole-arena relocation", "[issue403][pstring][relocation]" )
{
    using RelocMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 403>;
    using RelocStr = RelocMgr::pstring;

    RelocMgr::destroy();
    REQUIRE( RelocMgr::create( 8 * 1024 ) );
    auto p = RelocMgr::create_typed<RelocStr>();
    REQUIRE( !p.is_null() );
    auto* before_owner = p.resolve();
    REQUIRE( before_owner != nullptr );
    REQUIRE( pmm::pptr_from_raw<RelocStr, RelocMgr>( before_owner ) == p );

    RelocStr external_owner;
    REQUIRE( pmm::pptr_from_raw<RelocStr, RelocMgr>( &external_owner ).is_null() );

    char payload[32 * 1024];
    std::memset( payload, 0x5a, sizeof( payload ) );
    const auto* before_base = RelocMgr::backend().base_ptr();

    REQUIRE( before_owner->assign( payload, sizeof( payload ) ) );
    REQUIRE( RelocMgr::backend().base_ptr() != before_base );

    auto* after_owner = p.resolve();
    REQUIRE( after_owner != nullptr );
    REQUIRE( after_owner != before_owner );
    REQUIRE( after_owner->size() == sizeof( payload ) );
    REQUIRE( std::memcmp( after_owner->data(), payload, sizeof( payload ) ) == 0 );
    REQUIRE( after_owner->c_str()[after_owner->size()] == '\0' );
    REQUIRE( RelocMgr::verify().ok );

    after_owner->free_data();
    RelocMgr::destroy_typed( p );
    RelocMgr::destroy();
}

TEST_CASE( "I396: invalid explicit lengths are rejected without mutation", "[issue396][pstring]" )
{
    reset_manager();
    auto p = Mgr::create_typed<Str>();
    REQUIRE( !p.is_null() );
    auto* s = p.resolve();
    REQUIRE( s != nullptr );
    REQUIRE( s->assign( "stable" ) );

    REQUIRE_FALSE( s->append( nullptr, 1 ) );
    REQUIRE( *s == "stable" );

    if constexpr ( sizeof( std::size_t ) > sizeof( std::uint32_t ) )
    {
        const auto too_large = static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) + 1;
        REQUIRE_FALSE( s->assign( "x", too_large ) );
        REQUIRE( *s == "stable" );
    }

    destroy_string( p );
}
