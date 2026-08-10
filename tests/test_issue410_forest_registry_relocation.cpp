#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

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

template <typename Mgr> pmm::detail::ForestDomainRegistry<typename Mgr::address_traits>* registry() noexcept
{
    using AT   = typename Mgr::address_traits;
    auto* base = Mgr::backend().base_ptr();
    if ( base == nullptr )
        return nullptr;
    auto* hdr = pmm::detail::manager_header_at<AT>( base );
    if ( hdr->root_offset == AT::no_block )
        return nullptr;
    return reinterpret_cast<pmm::detail::ForestDomainRegistry<AT>*>(
        base + static_cast<std::size_t>( hdr->root_offset ) * AT::granule_size );
}

template <typename Mgr> void clear_registry_symbol_offsets() noexcept
{
    auto* reg = registry<Mgr>();
    REQUIRE( reg != nullptr );
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
        reg->domains[i].symbol_offset = 0;
}

using TightGrowthConfig =
    pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::NoLock, 1, 1, 64, pmm::logging::NoLogging>;

} // namespace

TEST_CASE( "I410: pstringview intern preserves an arena-backed source across expansion",
           "[issue410][forest-registry][relocation][symbol]" )
{
    using Mgr = pmm::PersistMemoryManager<TightGrowthConfig, 4101>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 8 * 1024 ) );

    constexpr char kText[] = "arena-backed-symbol";
    auto           source  = Mgr::template allocate_typed<char>( sizeof( kText ) );
    REQUIRE( !source.is_null() );
    std::memcpy( source.resolve(), kText, sizeof( kText ) );
    const char* source_before = source.resolve();
    const auto* base_before   = Mgr::backend().base_ptr();
    auto        filler        = consume_trailing_free_after<Mgr>( source );

    auto symbol = Mgr::pstringview::intern( source_before );
    REQUIRE( !symbol.is_null() );
    REQUIRE( Mgr::backend().base_ptr() != base_before );
    REQUIRE( source.resolve() != source_before );
    REQUIRE( symbol->size() == sizeof( kText ) - 1 );
    REQUIRE( std::string_view( symbol->c_str(), symbol->size() ) == std::string_view( kText, sizeof( kText ) - 1 ) );
    REQUIRE( Mgr::verify().ok );

    Mgr::deallocate_typed( source );
    Mgr::deallocate_typed( filler );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}

TEST_CASE( "I410: load bootstrap reacquires header and registry after relocating symbol recovery",
           "[issue410][forest-registry][relocation][reload]" )
{
    using Mgr                   = pmm::PersistMemoryManager<TightGrowthConfig, 4102>;
    constexpr const char* kFile = "test_issue410_registry_relocation.dat";

    Mgr::destroy();
    REQUIRE( Mgr::create( 16 * 1024 ) );
    REQUIRE( Mgr::register_domain( "app/recovery-a" ) );
    REQUIRE( Mgr::register_domain( "app/recovery-b" ) );

    // Make symbol metadata intentionally incomplete while keeping the domain
    // registry itself valid enough for load() to enter recovery/bootstrap.
    Mgr::pstringview::reset();
    clear_registry_symbol_offsets<Mgr>();

    auto marker = Mgr::create_typed<std::uint64_t>( 0x4102410241024102ULL );
    REQUIRE( !marker.is_null() );
    auto       filler     = consume_trailing_free_after<Mgr>( marker );
    const auto saved_size = Mgr::backend().total_size();
    REQUIRE( pmm::save_manager<Mgr>( kFile ) );

    Mgr::destroy();
    REQUIRE( Mgr::create( saved_size ) );
    const auto*       load_base_before = Mgr::backend().base_ptr();
    pmm::VerifyResult load_result;
    REQUIRE( pmm::load_manager_from_file<Mgr>( kFile, load_result ) );
    REQUIRE( load_result.ok );
    REQUIRE( Mgr::backend().base_ptr() != load_base_before );

    REQUIRE( Mgr::has_domain( pmm::detail::kSystemDomainFreeTree ) );
    REQUIRE( Mgr::has_domain( pmm::detail::kSystemDomainSymbols ) );
    REQUIRE( Mgr::has_domain( pmm::detail::kSystemDomainRegistry ) );
    REQUIRE( Mgr::has_domain( pmm::detail::kServiceNameDomainRoot ) );
    REQUIRE( Mgr::has_domain( "app/recovery-a" ) );
    REQUIRE( Mgr::has_domain( "app/recovery-b" ) );

    auto* reg = registry<Mgr>();
    REQUIRE( reg != nullptr );
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        REQUIRE( reg->domains[i].name[0] != '\0' );
        REQUIRE( reg->domains[i].symbol_offset != 0 );
        typename Mgr::template pptr<typename Mgr::pstringview> symbol( reg->domains[i].symbol_offset );
        REQUIRE( symbol.resolve() != nullptr );
        REQUIRE( std::string_view( symbol->c_str(), symbol->size() ) == std::string_view( reg->domains[i].name ) );
    }
    REQUIRE( Mgr::validate_bootstrap_invariants() );
    REQUIRE( Mgr::verify().ok );

    Mgr::destroy();
    std::remove( kFile );
}
