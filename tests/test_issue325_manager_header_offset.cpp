/**
 * @file test_issue325_manager_header_offset.cpp
 * @brief Regression tests for ManagerHeader offset consistency in CRC/save/load paths.
 */

#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

template <typename MgrSave, typename MgrLoad> void require_roundtrip( const char* filename, std::size_t arena_size )
{
    MgrSave::destroy();
    MgrLoad::destroy();

    struct Cleanup
    {
        const char* filename;
        ~Cleanup()
        {
            MgrSave::destroy();
            MgrLoad::destroy();
            std::remove( filename );
        }
    } cleanup{ filename };

    REQUIRE( MgrSave::create( arena_size ) );

    typename MgrSave::template pptr<std::uint32_t> saved = MgrSave::template allocate_typed<std::uint32_t>( 4 );
    REQUIRE( !saved.is_null() );

    std::uint32_t* saved_data = MgrSave::template resolve<std::uint32_t>( saved );
    REQUIRE( saved_data != nullptr );
    for ( std::uint32_t i = 0; i < 4; ++i )
        saved_data[i] = 0x32500000U + i;

    auto saved_offset = saved.offset();
    REQUIRE( pmm::save_manager<MgrSave>( filename ) );

    using address_traits             = typename MgrSave::address_traits;
    const std::uint8_t* data         = MgrSave::backend().base_ptr();
    const auto*         hdr          = pmm::detail::manager_header_at<address_traits>( data );
    std::uint32_t       computed_crc = pmm::detail::compute_image_crc32<address_traits>( data, MgrSave::total_size() );
    INFO( "canonical_crc=" << hdr->crc32 );
    INFO( "computed_crc=" << computed_crc );
    REQUIRE( hdr->crc32 == computed_crc );

    MgrSave::destroy();
    REQUIRE( MgrLoad::create( arena_size ) );

    pmm::VerifyResult result;
    bool              load_ok = pmm::load_manager_from_file<MgrLoad>( filename, result );
    INFO( "last_error=" << static_cast<int>( MgrLoad::last_error() ) );
    REQUIRE( load_ok );

    typename MgrLoad::template pptr<std::uint32_t> loaded_ptr( saved_offset );
    const std::uint32_t*                           loaded_data = MgrLoad::template resolve<std::uint32_t>( loaded_ptr );
    REQUIRE( loaded_data != nullptr );
    for ( std::uint32_t i = 0; i < 4; ++i )
        REQUIRE( loaded_data[i] == 0x32500000U + i );
}

} // namespace

TEST_CASE( "I325: DefaultAddressTraits manager image round-trips with CRC", "[issue325]" )
{
    using Save = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32501>;
    using Load = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32502>;

    require_roundtrip<Save, Load>( "test_issue325_default.dat", 64 * 1024 );
}

TEST_CASE( "I325: SmallAddressTraits manager image round-trips with CRC", "[issue325]" )
{
    static_assert( sizeof( pmm::Block<pmm::SmallAddressTraits> ) % pmm::SmallAddressTraits::granule_size != 0,
                   "SmallAddressTraits must exercise a non-granule-aligned Block header" );

    using Save = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 32503>;
    using Load = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 32504>;

    require_roundtrip<Save, Load>( "test_issue325_small.dat", 4096 );
}
