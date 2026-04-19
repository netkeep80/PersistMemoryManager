/**
 * @file test_issue306_repo_guard_gitkeep.cpp
 * @brief Structural guard for the root .gitkeep repo-guard exception.
 */

#include <catch2/catch_test_macros.hpp>

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

} // namespace

TEST_CASE( "issue306: repo-guard policy allows root gitkeep as operational file", "[issue306][governance]" )
{
    const std::filesystem::path repo_root = PMM_SOURCE_DIR;
    const auto                  policy    = read_file( repo_root / "repo-policy.json" );

    REQUIRE( policy.find( "\"operational_paths\"" ) != std::string::npos );
    REQUIRE( policy.find( "\".gitkeep\"" ) != std::string::npos );
    REQUIRE( policy.find( "\"operational\": [\".gitkeep\"]" ) != std::string::npos );
    REQUIRE( policy.find( "\"allow_classes\": [\"governance\", \"operational\", \"release\"]" ) != std::string::npos );
}
