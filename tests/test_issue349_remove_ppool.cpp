/**
 * @file test_issue349_remove_ppool.cpp
 * @brief Regression guard for removing ppool from the public PMM surface.
 */

#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>

namespace
{

template <typename ManagerT, typename = void> struct HasManagerPpoolAlias : std::false_type
{
};

template <typename ManagerT>
struct HasManagerPpoolAlias<ManagerT, std::void_t<typename ManagerT::template ppool<int>>> : std::true_type
{
};

std::string read_file( const std::filesystem::path& path )
{
    std::ifstream input( path );
    REQUIRE( input.good() );
    return std::string( std::istreambuf_iterator<char>( input ), std::istreambuf_iterator<char>() );
}

using Issue349Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 349>;

} // namespace

TEST_CASE( "issue349: ppool is removed from public manager aliases", "[issue349][structural]" )
{
    STATIC_REQUIRE_FALSE( HasManagerPpoolAlias<Issue349Mgr>::value );
}

TEST_CASE( "issue349: ppool implementation header is not shipped", "[issue349][structural]" )
{
    const std::filesystem::path repo_root      = PMM_SOURCE_DIR;
    const auto                  manager_header = read_file( repo_root / "include/pmm/persist_memory_manager.h" );

    REQUIRE( !std::filesystem::exists( repo_root / "include/pmm/ppool.h" ) );
    REQUIRE( manager_header.find( "#include \"pmm/ppool.h\"" ) == std::string::npos );
    REQUIRE( manager_header.find( "using ppool" ) == std::string::npos );
}
