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

    // Prevent allocator::realloc_grow from consuming the backing buffer's
    // physical next free block. Consume that exact free block completely so
    // append must use the relocation path (expanding the arena if necessary).
    using BlockState = pmm::BlockStateBase<Mgr::address_traits>;
    constexpr auto kHdrGranules = static_cast<Mgr::index_type>(
        ( sizeof( pmm::Block<Mgr::address_traits> ) + Mgr::address_traits::granule_size - 1 ) /
        Mgr::address_traits::granule_size );
    const auto data_idx = static_cast<Mgr::index_type>( s->_data_idx );
    REQUIRE( data_idx > kHdrGranules );
    auto* data_blk = pmm::detail::resolve_granule_ptr<Mgr::address_traits>(
        Mgr::backend().base_ptr(), static_cast<Mgr::index_type>( data_idx - kHdrGranules ) );
    REQUIRE( data_blk != nullptr );
    const auto next_idx = BlockState::get_next_offset( data_blk );
    REQUIRE( next_idx != Mgr::address_traits::no_block );
    auto* next_blk = pmm::detail::resolve_granule_ptr<Mgr::address_traits>( Mgr::backend().base_ptr(), next_idx );
    REQUIRE( next_blk != nullptr );
    REQUIRE( pmm::is_free( BlockState::get_node_type( next_blk ) ) );
    const auto next_total = BlockState::get_weight( next_blk );
    REQUIRE( next_total > kHdrGranules );
    const auto blocker_bytes = static_cast<std::size_t>( next_total - kHdrGranules ) *
                               Mgr::address_traits::granule_size;
    auto blocker = Mgr::allocate_typed<std::byte>( blocker_bytes );
    REQUIRE( !blocker.is_null() );
    REQUIRE( BlockState::get_next_offset( data_blk ) == next_idx );
    REQUIRE_FALSE( pmm::is_free( BlockState::get_node_type( next_blk ) ) );

    REQUIRE( s->append( s->data() + 1, 15 ) );
    REQUIRE( s->size() == 31 );
    REQUIRE( s->data() != before );
    const char expected[] = "0123456789ABCDEF123456789ABCDEF";
    REQUIRE( std::memcmp( s->data(), expected, sizeof( expected ) - 1 ) == 0 );
    REQUIRE( s->c_str()[s->size()] == '\0' );

    Mgr::deallocate_typed( blocker );
    destroy_string( p );
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
