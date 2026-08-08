#include "pmm/parray.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pstring.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

TEST_CASE( "I403: embedded pstring owner survives whole-arena relocation", "[issue403][pstring][embedded][relocation]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4031>;
    using Str = Mgr::pstring;
    struct Record
    {
        std::uint64_t marker = 0x0123456789abcdefULL;
        Str           text;
    };
    static_assert( std::is_nothrow_default_constructible_v<Record> );

    Mgr::destroy();
    REQUIRE( Mgr::create( 8 * 1024 ) );
    auto record = Mgr::create_typed<Record>();
    REQUIRE( !record.is_null() );

    auto* before = record.resolve();
    REQUIRE( before != nullptr );
    auto* embedded_before = &before->text;
    REQUIRE( pmm::pptr_from_raw<Str, Mgr>( embedded_before ).is_null() );

    char payload[32 * 1024];
    std::memset( payload, 0x5a, sizeof( payload ) );
    const auto* base_before = Mgr::backend().base_ptr();

    REQUIRE( embedded_before->assign( payload, sizeof( payload ) ) );
    REQUIRE( Mgr::backend().base_ptr() != base_before );

    auto* after = record.resolve();
    REQUIRE( after != nullptr );
    REQUIRE( after != before );
    REQUIRE( &after->text != embedded_before );
    REQUIRE( after->marker == 0x0123456789abcdefULL );
    REQUIRE( after->text.size() == sizeof( payload ) );
    REQUIRE( std::memcmp( after->text.data(), payload, sizeof( payload ) ) == 0 );
    REQUIRE( Mgr::verify().ok );

    after->text.free_data();
    Mgr::destroy_typed( record );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}

TEST_CASE( "I403: embedded parray owner survives whole-arena relocation", "[issue403][parray][embedded][relocation]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4032>;
    using Arr = Mgr::parray<std::uint64_t>;
    struct Record
    {
        std::uint64_t marker = 0xfedcba9876543210ULL;
        Arr           values;
    };
    static_assert( std::is_nothrow_default_constructible_v<Record> );

    Mgr::destroy();
    REQUIRE( Mgr::create( 8 * 1024 ) );
    auto record = Mgr::create_typed<Record>();
    REQUIRE( !record.is_null() );

    auto* before = record.resolve();
    REQUIRE( before != nullptr );
    auto* embedded_before = &before->values;
    REQUIRE( pmm::pptr_from_raw<Arr, Mgr>( embedded_before ).is_null() );

    const auto* base_before = Mgr::backend().base_ptr();
    REQUIRE( embedded_before->resize( 4096 ) );
    REQUIRE( Mgr::backend().base_ptr() != base_before );

    auto* after = record.resolve();
    REQUIRE( after != nullptr );
    REQUIRE( after != before );
    REQUIRE( &after->values != embedded_before );
    REQUIRE( after->marker == 0xfedcba9876543210ULL );
    REQUIRE( after->values.size() == 4096 );
    REQUIRE( after->values.at( 0 ) != nullptr );
    REQUIRE( *after->values.at( 0 ) == 0 );
    REQUIRE( after->values.at( 4095 ) != nullptr );
    REQUIRE( *after->values.at( 4095 ) == 0 );
    REQUIRE( Mgr::verify().ok );

    after->values.free_data();
    Mgr::destroy_typed( record );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}
