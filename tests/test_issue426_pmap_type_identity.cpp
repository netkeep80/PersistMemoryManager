#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

namespace issue426
{
struct PayloadA
{
    std::uint64_t value = 0;
};
struct PayloadB
{
    std::uint64_t value = 0;
};
struct UntaggedPayload
{
    std::uint64_t value = 0;
};
}

namespace pmm
{
template <> struct pmap_type_identity<issue426::PayloadA>
{
    static constexpr const char* tag = "issue426/payload-a/v1";
};
template <> struct pmap_type_identity<issue426::PayloadB>
{
    static constexpr const char* tag = "issue426/payload-b/v1";
};
}

TEST_CASE( "I426: typed handles require stable pointee identity", "[issue426][pmap][identity][compile]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4260>;

    static_assert( pmm::detail::pmap_has_stable_identity_v<int> );
    static_assert( pmm::detail::pmap_has_stable_identity_v<Mgr::pptr<issue426::PayloadA>> );
    static_assert( pmm::detail::pmap_has_stable_identity_v<Mgr::pptr<issue426::PayloadB>> );
    static_assert( !pmm::detail::pmap_has_stable_identity_v<Mgr::pptr<issue426::UntaggedPayload>> );
    static_assert( pmm::pmap_type_identity<Mgr::pstringview>::tag[0] != '\0' );
    static_assert( pmm::detail::pmap_type_fp<Mgr::pptr<issue426::PayloadA>>() !=
                   pmm::detail::pmap_type_fp<Mgr::pptr<issue426::PayloadB>>() );
}

TEST_CASE( "I426: distinct typed handles cannot share named or generated pmap domains",
           "[issue426][pmap][identity][domain]" )
{
    using Mgr  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4261>;
    using MapA = Mgr::pmap<int, Mgr::pptr<issue426::PayloadA>>;
    using MapB = Mgr::pmap<int, Mgr::pptr<issue426::PayloadB>>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 256 * 1024 ) );

    MapA named_a;
    MapB named_b;
    REQUIRE( named_a.bind_domain( "issue426/shared" ) );
    REQUIRE( named_b.bind_domain( "issue426/shared" ) );
    REQUIRE( std::strcmp( named_a.domain_name(), named_b.domain_name() ) != 0 );

    MapA generated_a;
    MapB generated_b;
    REQUIRE( generated_a.forest_domain_ops().root_index_ptr() != nullptr );
    REQUIRE( generated_b.forest_domain_ops().root_index_ptr() != nullptr );
    REQUIRE( std::strcmp( generated_a.domain_name(), generated_b.domain_name() ) != 0 );

    MapA named_a_again;
    REQUIRE( named_a_again.bind_domain( "issue426/shared" ) );
    REQUIRE( std::strcmp( named_a.domain_name(), named_a_again.domain_name() ) == 0 );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}

TEST_CASE( "I426: pjson-shaped typed-handle map keeps distinct key and value semantics",
           "[issue426][pmap][identity][pjson-shape]" )
{
    using Mgr        = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4262>;
    using PjsonShape = Mgr::pmap<Mgr::pptr<Mgr::pstringview>, Mgr::pptr<issue426::PayloadA>>;
    using OtherShape = Mgr::pmap<Mgr::pptr<Mgr::pstringview>, Mgr::pptr<issue426::PayloadB>>;

    static_assert( PjsonShape::domain_type_hash != OtherShape::domain_type_hash );

    Mgr::destroy();
    REQUIRE( Mgr::create( 256 * 1024 ) );
    PjsonShape pjson_shape;
    OtherShape other_shape;
    REQUIRE( pjson_shape.bind_domain( "issue426/object" ) );
    REQUIRE( other_shape.bind_domain( "issue426/object" ) );
    REQUIRE( std::strcmp( pjson_shape.domain_name(), other_shape.domain_name() ) != 0 );
    REQUIRE( Mgr::verify().ok );
    Mgr::destroy();
}

TEST_CASE( "I426: typed-handle pmap domain identity survives v3 persistence reload",
           "[issue426][pmap][identity][persistence]" )
{
    using Mgr                   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4263>;
    using MapA                  = Mgr::pmap<int, Mgr::pptr<issue426::PayloadA>>;
    constexpr const char* kFile = "test_issue426_type_identity.dat";

    Mgr::destroy();
    REQUIRE( Mgr::create( 256 * 1024 ) );
    MapA before;
    REQUIRE( before.bind_domain( "issue426/persist" ) );
    auto payload = Mgr::create_typed<issue426::PayloadA>();
    REQUIRE( !payload.is_null() );
    payload->value = 0x4263426342634263ULL;
    REQUIRE( !before.insert( 7, payload ).is_null() );
    const std::string domain_before( before.domain_name() );
    const auto        payload_offset = payload.offset();
    const auto        saved_size     = Mgr::backend().total_size();
    REQUIRE( pmm::save_manager<Mgr>( kFile ) );

    Mgr::destroy();
    REQUIRE( Mgr::create( saved_size ) );
    pmm::VerifyResult load_result;
    REQUIRE( pmm::load_manager_from_file<Mgr>( kFile, load_result ) );
    REQUIRE( load_result.ok );

    MapA after;
    REQUIRE( after.bind_domain( "issue426/persist" ) );
    REQUIRE( domain_before == after.domain_name() );
    auto found = after.find( 7 );
    REQUIRE( !found.is_null() );
    REQUIRE( found->value.offset() == payload_offset );
    REQUIRE( found->value->value == 0x4263426342634263ULL );
    REQUIRE( Mgr::verify().ok );

    Mgr::destroy();
    std::remove( kFile );
}
