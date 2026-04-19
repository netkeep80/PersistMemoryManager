/**
 * @file test_issue295_layout_compaction.cpp
 * @brief Structural guard for the layout include-shard repayment.
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

TEST_CASE( "issue295: layout helpers are not included as an inc shard", "[issue295][structural]" )
{
    const std::filesystem::path repo_root      = PMM_SOURCE_DIR;
    const auto                  manager_header = read_file( repo_root / "include/pmm/persist_memory_manager.h" );

    REQUIRE( manager_header.find( "#include \"pmm/layout_mixin.inc\"" ) == std::string::npos );
    REQUIRE( manager_header.find( "#include \"pmm/layout.h\"" ) != std::string::npos );
    REQUIRE( !std::filesystem::exists( repo_root / "include/pmm/layout_mixin.inc" ) );
}
