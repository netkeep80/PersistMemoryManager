#include "pmm/persist_memory_manager.h"
#include "pmm/pmap.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
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

} // namespace

TEST_CASE( "I404: persistent root pmap rebinds owner after allocating bind", "[issue404][pmap][relocation][root]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4041>;
    using Map = Mgr::pmap<int, int>;
    static_assert( !std::is_constructible_v<Map, const char*> );

    Mgr::destroy();
    REQUIRE( Mgr::create( 8 * 1024 ) );
    auto map = Mgr::create_typed<Map>();
    REQUIRE( !map.is_null() );

    auto* before = map.resolve();
    REQUIRE( before != nullptr );
    const auto* base_before = Mgr::backend().base_ptr();
    auto        filler      = consume_trailing_free_after<Mgr>( map );

    REQUIRE( before->bind_domain( "issue404/root" ) );
    REQUIRE( Mgr::backend().base_ptr() != base_before );

    auto* after = map.resolve();
    REQUIRE( after != nullptr );
    REQUIRE( after != before );
    REQUIRE( std::string_view( after->domain_name() ).starts_with( "container/pmap/" ) );
    auto inserted = after->insert( 7, 70 );
    REQUIRE( !inserted.is_null() );
    REQUIRE( after->find( 7 ) == inserted );
    REQUIRE( inserted->value == 70 );
    REQUIRE( Mgr::verify().ok );

    after->clear();
    Mgr::destroy_typed( map );
    Mgr::deallocate_typed( filler );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}

TEST_CASE( "I404: embedded pmap rebinds owner after lazy domain allocation", "[issue404][pmap][relocation][embedded]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4042>;
    using Map = Mgr::pmap<int, int>;
    struct Record
    {
        std::uint64_t marker = 0x4042404240424042ULL;
        Map           map;
    };

    Mgr::destroy();
    REQUIRE( Mgr::create( 8 * 1024 ) );
    auto record = Mgr::create_typed<Record>();
    REQUIRE( !record.is_null() );

    auto* before = record.resolve();
    REQUIRE( before != nullptr );
    auto* embedded_before = &before->map;
    REQUIRE( pmm::pptr_from_raw<Map, Mgr>( embedded_before ).is_null() );
    const auto* base_before = Mgr::backend().base_ptr();
    auto        filler      = consume_trailing_free_after<Mgr>( record );

    // insert() performs lazy binding. Domain registration must expand the arena
    // because the trailing free block was consumed exactly.
    auto inserted = embedded_before->insert( 11, 110 );
    REQUIRE( !inserted.is_null() );
    REQUIRE( Mgr::backend().base_ptr() != base_before );

    auto* after = record.resolve();
    REQUIRE( after != nullptr );
    REQUIRE( after != before );
    REQUIRE( &after->map != embedded_before );
    REQUIRE( after->marker == 0x4042404240424042ULL );
    REQUIRE( after->map.find( 11 ) == inserted );
    REQUIRE( inserted->value == 110 );
    REQUIRE( Mgr::verify().ok );

    after->map.clear();
    Mgr::destroy_typed( record );
    Mgr::deallocate_typed( filler );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}

TEST_CASE( "I404: pmap insert rebases PMM-backed key and value inputs", "[issue404][pmap][relocation][inputs]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4043>;
    using Map = Mgr::pmap<int, int>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 8 * 1024 ) );
    Map map;
    REQUIRE( map.bind_domain( "issue404/inputs" ) );

    auto key   = Mgr::create_typed<int>();
    auto value = Mgr::create_typed<int>();
    REQUIRE( !key.is_null() );
    REQUIRE( !value.is_null() );
    *key.resolve()   = 1234567;
    *value.resolve() = -7654321;

    const int* key_before   = key.resolve();
    const int* value_before = value.resolve();
    REQUIRE( key_before != nullptr );
    REQUIRE( value_before != nullptr );
    const auto* base_before = Mgr::backend().base_ptr();
    auto        filler      = consume_trailing_free_after<Mgr>( value );

    auto inserted = map.insert( *key_before, *value_before );
    REQUIRE( !inserted.is_null() );
    REQUIRE( Mgr::backend().base_ptr() != base_before );
    REQUIRE( key.resolve() != key_before );
    REQUIRE( value.resolve() != value_before );
    REQUIRE( inserted->key == 1234567 );
    REQUIRE( inserted->value == -7654321 );
    REQUIRE( map.find( 1234567 ) == inserted );
    REQUIRE( Mgr::verify().ok );

    map.clear();
    Mgr::destroy_typed( key );
    Mgr::destroy_typed( value );
    Mgr::deallocate_typed( filler );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}
