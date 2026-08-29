#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace
{

template <typename Mgr, typename T>
typename Mgr::template pptr<std::uint8_t> consume_trailing_free_after( typename Mgr::template pptr<T> last )
{
    using AT    = typename Mgr::address_traits;
    using State = pmm::BlockStateBase<AT>;

    constexpr auto kHdrGranules = pmm::detail::kBlockHeaderGranules_t<AT>;
    const auto     last_idx     = static_cast<typename Mgr::index_type>( last.offset() - kHdrGranules );
    auto*          last_blk     = pmm::detail::resolve_granule_ptr<AT>( Mgr::backend().base_ptr(), last_idx );
    REQUIRE( last_blk != nullptr );

    const auto tail_idx = State::get_next_offset( last_blk );
    REQUIRE( tail_idx != AT::no_block );
    auto* tail_blk = pmm::detail::resolve_granule_ptr<AT>( Mgr::backend().base_ptr(), tail_idx );
    REQUIRE( tail_blk != nullptr );
    REQUIRE( pmm::is_free( State::get_node_type( tail_blk ) ) );

    const auto tail_total = State::get_weight( tail_blk );
    REQUIRE( tail_total > kHdrGranules );
    const auto tail_bytes = static_cast<std::size_t>( tail_total - kHdrGranules ) * AT::granule_size;
    auto       filler     = Mgr::template allocate_typed<std::uint8_t>( tail_bytes );
    REQUIRE( !filler.is_null() );
    REQUIRE( filler.offset() == tail_idx + kHdrGranules );
    return filler;
}

using TightGrowthConfig =
    pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::NoLock, 1, 1, 64, pmm::logging::NoLogging>;

} // namespace

TEST_CASE( "I416: pstringview is persistent node representation only", "[issue416][pstringview][layout]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4160>;
    using Psv = Mgr::pstringview;

    static_assert( std::is_standard_layout_v<Psv> );
    static_assert( std::is_trivially_copyable_v<Psv> );
    static_assert( offsetof( Psv, str ) == sizeof( std::uint32_t ) );
    static_assert( !std::is_constructible_v<Psv, const char*> );
}

TEST_CASE( "I416: symbol identity is exact length plus bytes", "[issue416][pstringview][binary]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4161>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 128 * 1024 ) );

    const char a_nul_b[] = { 'a', '\0', 'b' };
    const char a_nul[]   = { 'a', '\0' };
    auto       a         = Mgr::pstringview::intern( "a" );
    auto       a0        = Mgr::pstringview::intern( std::string_view( a_nul, sizeof( a_nul ) ) );
    auto       a0b       = Mgr::pstringview::intern( std::string_view( a_nul_b, sizeof( a_nul_b ) ) );
    auto       aa        = Mgr::pstringview::intern( "aa" );
    auto       a0b_again = Mgr::pstringview::intern( std::string_view( a_nul_b, sizeof( a_nul_b ) ) );

    REQUIRE( ( !a.is_null() && !a0.is_null() && !a0b.is_null() && !aa.is_null() ) );
    REQUIRE( a != a0 );
    REQUIRE( a != a0b );
    REQUIRE( a0 != a0b );
    REQUIRE( a0b_again == a0b );
    REQUIRE( a0b->size() == 3 );
    REQUIRE( std::memcmp( a0b->c_str(), a_nul_b, 3 ) == 0 );
    REQUIRE( a0b->c_str()[3] == '\0' );

    REQUIRE( *a < *a0 );
    REQUIRE( *a0 < *a0b );
    REQUIRE( *a0b < *aa );

    auto ops = Mgr::pstringview::forest_domain_ops();
    REQUIRE( ops.find( std::string_view( a_nul_b, sizeof( a_nul_b ) ) ) == a0b );
    REQUIRE( ops.find( "a" ) == a );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}

TEST_CASE( "I416: C-string convenience delegates to explicit-length identity", "[issue416][pstringview][compat]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4162>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 128 * 1024 ) );

    constexpr char text[] = "ordinary-utf8-~-/";
    auto           cstr   = Mgr::pstringview::intern( text );
    auto           view   = Mgr::pstringview::intern( std::string_view( text, sizeof( text ) - 1 ) );
    REQUIRE( !cstr.is_null() );
    REQUIRE( cstr == view );
    REQUIRE( cstr->view() == std::string_view( text, sizeof( text ) - 1 ) );

    const char utf8[]  = { static_cast<char>( 0xd0 ), static_cast<char>( 0x9f ), static_cast<char>( 0xd1 ),
                           static_cast<char>( 0x80 ), static_cast<char>( 0xd0 ), static_cast<char>( 0xb8 ) };
    auto       unicode = Mgr::pstringview::intern( std::string_view( utf8, sizeof( utf8 ) ) );
    REQUIRE( !unicode.is_null() );
    REQUIRE( unicode->size() == sizeof( utf8 ) );
    REQUIRE( std::memcmp( unicode->c_str(), utf8, sizeof( utf8 ) ) == 0 );
    REQUIRE( unicode->c_str()[sizeof( utf8 )] == '\0' );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}

TEST_CASE( "I416: explicit-length arena source survives relocation", "[issue416][pstringview][relocation]" )
{
    using Mgr = pmm::PersistMemoryManager<TightGrowthConfig, 4163>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 8 * 1024 ) );

    constexpr char bytes[] = { 'r', '\0', 'v', 'm' };
    auto           source  = Mgr::template allocate_typed<char>( sizeof( bytes ) );
    REQUIRE( !source.is_null() );
    std::memcpy( source.resolve(), bytes, sizeof( bytes ) );
    const char* source_before = source.resolve();
    const auto* base_before   = Mgr::backend().base_ptr();
    auto        filler        = consume_trailing_free_after<Mgr>( source );

    auto symbol = Mgr::pstringview::intern( std::string_view( source_before, sizeof( bytes ) ) );
    REQUIRE( !symbol.is_null() );
    REQUIRE( Mgr::backend().base_ptr() != base_before );
    REQUIRE( source.resolve() != source_before );
    REQUIRE( symbol->size() == sizeof( bytes ) );
    REQUIRE( std::memcmp( symbol->c_str(), bytes, sizeof( bytes ) ) == 0 );
    REQUIRE( symbol->c_str()[sizeof( bytes )] == '\0' );
    REQUIRE( Mgr::verify().ok );

    Mgr::deallocate_typed( source );
    Mgr::deallocate_typed( filler );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}

TEST_CASE( "I416: binary symbol identity survives persistence reload", "[issue416][pstringview][persistence]" )
{
    using Mgr                      = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4164>;
    constexpr const char*  kFile   = "test_issue416_pstringview_binary.dat";
    const char             bytes[] = { 'p', '\0', '~', '/', 'x' };
    const std::string_view key( bytes, sizeof( bytes ) );

    Mgr::destroy();
    REQUIRE( Mgr::create( 128 * 1024 ) );
    auto before = Mgr::pstringview::intern( key );
    REQUIRE( !before.is_null() );
    const auto saved_size = Mgr::backend().total_size();
    REQUIRE( pmm::save_manager<Mgr>( kFile ) );

    Mgr::destroy();
    REQUIRE( Mgr::create( saved_size ) );
    pmm::VerifyResult load_result;
    REQUIRE( pmm::load_manager_from_file<Mgr>( kFile, load_result ) );
    REQUIRE( load_result.ok );

    auto resolved = before.resolve();
    REQUIRE( resolved != nullptr );
    REQUIRE( resolved->size() == key.size() );
    REQUIRE( std::memcmp( resolved->c_str(), key.data(), key.size() ) == 0 );
    auto after = Mgr::pstringview::intern( key );
    REQUIRE( after == before );
    REQUIRE( Mgr::verify().ok );

    Mgr::destroy();
    std::remove( kFile );
}
