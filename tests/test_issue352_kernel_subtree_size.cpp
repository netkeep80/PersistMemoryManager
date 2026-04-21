#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

namespace
{

std::string read_file( const std::filesystem::path& path )
{
    std::ifstream input( path );
    REQUIRE( input.good() );
    return std::string( std::istreambuf_iterator<char>( input ), std::istreambuf_iterator<char>() );
}

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

std::string regex_capture( const std::string& text, const std::string& pattern )
{
    const std::regex expression( pattern );
    std::smatch      match;
    REQUIRE( std::regex_search( text, match, expression ) );
    REQUIRE( match.size() == 2 );
    return match[1].str();
}

struct SizeRule
{
    std::string glob;
    std::size_t max_lines;
};

SizeRule load_kernel_subtree_rule( const std::filesystem::path& repo_root )
{
    const auto policy = read_file( repo_root / "repo-policy.json" );
    const auto rule   = regex_capture( policy, R"re((\{[^{}]*"id"\s*:\s*"kernel-subtree-max-lines"[^{}]*\}))re" );

    return {
        regex_capture( rule, R"re("glob"\s*:\s*"([^"]+)")re" ),
        static_cast<std::size_t>( std::stoull( regex_capture( rule, R"re("max"\s*:\s*([0-9]+))re" ) ) ),
    };
}

std::filesystem::path directory_from_glob( const std::filesystem::path& repo_root, const std::string& glob )
{
    const std::string recursive_suffix = "/**";
    REQUIRE( glob.size() > recursive_suffix.size() );
    REQUIRE( glob.compare( glob.size() - recursive_suffix.size(), recursive_suffix.size(), recursive_suffix ) == 0 );
    return repo_root / glob.substr( 0, glob.size() - recursive_suffix.size() );
}

} // namespace

TEST_CASE( "issue352: include/pmm subtree stays below the kernel size budget", "[issue352][repo-guard]" )
{
    const std::filesystem::path repo_root = PMM_SOURCE_DIR;
    const auto                  rule      = load_kernel_subtree_rule( repo_root );
    const auto                  rule_root = directory_from_glob( repo_root, rule.glob );

    std::size_t total_lines = 0;
    for ( const auto& entry : std::filesystem::recursive_directory_iterator( rule_root ) )
    {
        if ( entry.is_regular_file() )
            total_lines += count_lines( entry.path() );
    }

    REQUIRE( total_lines <= rule.max_lines );
}
