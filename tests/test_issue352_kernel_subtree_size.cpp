#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

std::size_t count_lines( const std::filesystem::path& path )
{
    std::ifstream input( path );
    REQUIRE( input.good() );

    std::size_t lines = 0;
    std::string unused;
    while ( std::getline( input, unused ) )
        ++lines;
    return lines;
}

} // namespace

TEST_CASE( "issue352: include/pmm subtree stays below the kernel size budget", "[issue352][repo-guard]" )
{
    const auto include_root = std::filesystem::path( PMM_SOURCE_DIR ) / "include" / "pmm";

    std::size_t total_lines = 0;
    for ( const auto& entry : std::filesystem::recursive_directory_iterator( include_root ) )
    {
        if ( entry.is_regular_file() )
            total_lines += count_lines( entry.path() );
    }

    REQUIRE( total_lines <= 9000 );
}
