#include "pmm/typed_manager_api.h"
#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{

std::string read_file( const std::filesystem::path& path )
{
    std::ifstream input( path );
    REQUIRE( input.good() );
    return std::string( std::istreambuf_iterator<char>( input ), std::istreambuf_iterator<char>() );
}

std::size_t count_lines( const std::string& text )
{
    if ( text.empty() )
        return 0;
    std::size_t lines = static_cast<std::size_t>( std::count( text.begin(), text.end(), '\n' ) );
    return ( text.back() == '\n' ) ? lines : lines + 1;
}

struct Issue318Record
{
    explicit Issue318Record( std::uint32_t initial ) noexcept : value( initial ) {}
    ~Issue318Record() noexcept = default;

    std::uint32_t value;
};

using Issue318Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 318>;

} // namespace

TEST_CASE( "issue318: typed API remains available through PersistMemoryManager", "[issue318][typed-api]" )
{
    Issue318Mgr::destroy();
    REQUIRE( Issue318Mgr::create( 64 * 1024 ) );

    auto object = Issue318Mgr::create_typed<Issue318Record>( 0x318u );
    REQUIRE_FALSE( object.is_null() );
    REQUIRE( Issue318Mgr::resolve_checked( object ) != nullptr );
    REQUIRE( Issue318Mgr::resolve_checked( object )->value == 0x318u );

    auto values = Issue318Mgr::allocate_typed<std::uint32_t>( 2 );
    REQUIRE_FALSE( values.is_null() );
    REQUIRE( Issue318Mgr::resolve( values ) != nullptr );
    Issue318Mgr::resolve( values )[0] = 11;
    Issue318Mgr::resolve( values )[1] = 22;

    auto grown = Issue318Mgr::reallocate_typed<std::uint32_t>( values, 2, 4 );
    REQUIRE_FALSE( grown.is_null() );
    REQUIRE( Issue318Mgr::resolve_at( grown, 0 ) != nullptr );
    REQUIRE( *Issue318Mgr::resolve_at( grown, 0 ) == 11 );
    REQUIRE( *Issue318Mgr::resolve_at( grown, 1 ) == 22 );

    auto round_trip = Issue318Mgr::pptr_from_byte_offset<std::uint32_t>( grown.byte_offset() );
    REQUIRE( round_trip == grown );
    REQUIRE( Issue318Mgr::is_valid_ptr( round_trip ) );

    Issue318Mgr::deallocate_typed( grown );
    Issue318Mgr::destroy_typed( object );
    Issue318Mgr::destroy();
}

TEST_CASE( "issue318: typed API lives in one normal header module", "[issue318][structural]" )
{
    const std::filesystem::path repo_root      = PMM_SOURCE_DIR;
    const auto                  manager_header = read_file( repo_root / "include/pmm/persist_memory_manager.h" );
    const auto                  typed_header   = read_file( repo_root / "include/pmm/typed_manager_api.h" );

    REQUIRE( manager_header.find( "#include \"pmm/typed_manager_api.h\"" ) != std::string::npos );
    REQUIRE( manager_header.find( "PersistMemoryTypedApi<PersistMemoryManager<ConfigT, InstanceId>>" ) !=
             std::string::npos );
    REQUIRE( manager_header.find( "static pptr<T> reallocate_typed" ) == std::string::npos );
    REQUIRE( manager_header.find( "static T* resolve_checked" ) == std::string::npos );

    REQUIRE( typed_header.find( "class PersistMemoryTypedApi" ) != std::string::npos );
    REQUIRE( typed_header.find( "static pmm::pptr<T, ManagerT> reallocate_typed" ) != std::string::npos );
    REQUIRE( typed_header.find( ".inc" ) == std::string::npos );
    REQUIRE( typed_header.find( ".inl" ) == std::string::npos );
    REQUIRE( typed_header.find( ".ipp" ) == std::string::npos );

    REQUIRE( count_lines( manager_header ) < 1250 );
}
