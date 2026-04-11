/**
 * @file test_issue45_pstring.cpp
 * @brief Tests for pstring — mutable persistent string.
 *
 * Verifies the key requirements from this feature:
 *  1. pstring can be created in persistent address space via create_typed<pstring>().
 *  2. assign() sets string content, with reallocation when needed.
 *  3. c_str() returns null-terminated string data.
 *  4. size() returns the length of the string.
 *  5. clear() resets string to empty without freeing data buffer.
 *  6. append() concatenates content to the string.
 *  7. operator[] provides character access by index.
 *  8. free_data() deallocates the data buffer.
 *  9. Comparison operators work correctly.
 * 10. pstring works with different manager configurations.
 *
 * @see include/pmm/pstring.h — pstring
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @version 0.1
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pstring.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <string>

// ─── Test macros ──────────────────────────────────────────────────────────────

// ─── Manager type alias for tests ────────────────────────────────────────────

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 45>;
using TestStr = TestMgr::pstring;

// =============================================================================
// I45-A: Basic creation and assignment
// =============================================================================

/// @brief create_typed<pstring>() creates an empty pstring.
TEST_CASE( "    create empty pstring", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p = TestMgr::create_typed<TestStr>();
    REQUIRE( !p.is_null() );

    TestStr* str = p.resolve();
    REQUIRE( str != nullptr );
    REQUIRE( str->empty() );
    REQUIRE( str->size() == 0 );
    REQUIRE( std::strcmp( str->c_str(), "" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief assign() sets the string content.
TEST_CASE( "    assign basic string", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p = TestMgr::create_typed<TestStr>();
    REQUIRE( !p.is_null() );

    TestStr* str = p.resolve();
    REQUIRE( str != nullptr );

    REQUIRE( str->assign( "hello" ) );
    REQUIRE( str->size() == 5 );
    REQUIRE( !str->empty() );
    REQUIRE( std::strcmp( str->c_str(), "hello" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief assign() with nullptr is treated as empty string.
TEST_CASE( "    assign nullptr", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    REQUIRE( str->assign( "test" ) );
    REQUIRE( str->size() == 4 );

    REQUIRE( str->assign( nullptr ) );
    REQUIRE( str->size() == 0 );
    REQUIRE( str->empty() );
    REQUIRE( std::strcmp( str->c_str(), "" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief assign() with empty string.
TEST_CASE( "    assign empty string", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    REQUIRE( str->assign( "non-empty" ) );
    REQUIRE( str->assign( "" ) );
    REQUIRE( str->empty() );
    REQUIRE( str->size() == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I45-B: Reallocation on assign
// =============================================================================

/// @brief assign() with a longer string triggers reallocation.
TEST_CASE( "    assign triggers reallocation", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    // First assign — allocates initial buffer
    REQUIRE( str->assign( "short" ) );
    REQUIRE( str->size() == 5 );

    // Longer string — may trigger reallocation
    std::string long_str( 200, 'x' );
    REQUIRE( str->assign( long_str.c_str() ) );
    REQUIRE( str->size() == 200 );
    REQUIRE( std::string( str->c_str() ) == long_str );

    // Even longer string — definitely triggers reallocation
    std::string very_long( 1000, 'y' );
    REQUIRE( str->assign( very_long.c_str() ) );
    REQUIRE( str->size() == 1000 );
    REQUIRE( std::string( str->c_str() ) == very_long );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief Multiple reassignments work correctly.
TEST_CASE( "    multiple reassignments", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    const char* values[] = { "alpha", "beta", "gamma", "delta", "epsilon" };
    for ( const char* v : values )
    {
        REQUIRE( str->assign( v ) );
        REQUIRE( std::strcmp( str->c_str(), v ) == 0 );
        REQUIRE( str->size() == std::strlen( v ) );
    }

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I45-C: clear() and free_data()
// =============================================================================

/// @brief clear() sets length to 0 but preserves the buffer.
TEST_CASE( "    clear() preserves buffer", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    REQUIRE( str->assign( "hello" ) );
    REQUIRE( str->size() == 5 );

    str->clear();
    REQUIRE( str->empty() );
    REQUIRE( str->size() == 0 );
    REQUIRE( std::strcmp( str->c_str(), "" ) == 0 );

    // After clear, assign should reuse the buffer (no reallocation needed for small strings)
    REQUIRE( str->assign( "world" ) );
    REQUIRE( str->size() == 5 );
    REQUIRE( std::strcmp( str->c_str(), "world" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief free_data() deallocates the data buffer and resets the string.
TEST_CASE( "    free_data() deallocates buffer", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    REQUIRE( str->assign( "test data" ) );
    REQUIRE( str->size() == 9 );

    std::size_t alloc_before = TestMgr::alloc_block_count();

    str->free_data();
    REQUIRE( str->empty() );
    REQUIRE( str->size() == 0 );
    REQUIRE( std::strcmp( str->c_str(), "" ) == 0 );

    std::size_t alloc_after = TestMgr::alloc_block_count();
    // Data block should have been freed (alloc count decreased)
    REQUIRE( alloc_after < alloc_before );

    // After free_data, assign should still work (allocates new buffer)
    REQUIRE( str->assign( "new data" ) );
    REQUIRE( str->size() == 8 );
    REQUIRE( std::strcmp( str->c_str(), "new data" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I45-D: append()
// =============================================================================

/// @brief append() concatenates strings.
TEST_CASE( "    append basic", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    REQUIRE( str->assign( "hello" ) );
    REQUIRE( str->append( " " ) );
    REQUIRE( str->append( "world" ) );
    REQUIRE( str->size() == 11 );
    REQUIRE( std::strcmp( str->c_str(), "hello world" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief append() to empty string works like assign().
TEST_CASE( "    append to empty", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    REQUIRE( str->append( "first" ) );
    REQUIRE( str->size() == 5 );
    REQUIRE( std::strcmp( str->c_str(), "first" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief append() with nullptr is a no-op.
TEST_CASE( "    append nullptr", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    REQUIRE( str->assign( "test" ) );
    REQUIRE( str->append( nullptr ) );
    REQUIRE( str->size() == 4 );
    REQUIRE( std::strcmp( str->c_str(), "test" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief append() triggers reallocation when buffer is full.
TEST_CASE( "    append triggers reallocation", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    // Build a long string through repeated appends
    std::string expected;
    for ( int i = 0; i < 100; ++i )
    {
        std::string chunk = "chunk" + std::to_string( i ) + "_";
        REQUIRE( str->append( chunk.c_str() ) );
        expected += chunk;
    }

    REQUIRE( str->size() == expected.size() );
    REQUIRE( std::string( str->c_str() ) == expected );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I45-E: operator[]
// =============================================================================

/// @brief operator[] returns correct characters.
TEST_CASE( "    subscript operator", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    REQUIRE( str->assign( "hello" ) );
    REQUIRE( ( *str )[0] == 'h' );
    REQUIRE( ( *str )[1] == 'e' );
    REQUIRE( ( *str )[2] == 'l' );
    REQUIRE( ( *str )[3] == 'l' );
    REQUIRE( ( *str )[4] == 'o' );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I45-F: Comparison operators
// =============================================================================

/// @brief operator== with C-string.
TEST_CASE( "    operator== with C-string", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    REQUIRE( str->assign( "hello" ) );
    REQUIRE( *str == "hello" );
    REQUIRE( !( *str == "world" ) );
    REQUIRE( *str != "world" );
    REQUIRE( !( *str != "hello" ) );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief operator== and operator< between two pstrings.
TEST_CASE( "    compare two pstrings", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> pa = TestMgr::create_typed<TestStr>();
    TestMgr::pptr<TestStr> pb = TestMgr::create_typed<TestStr>();

    TestStr* a = pa.resolve();
    TestStr* b = pb.resolve();

    a->assign( "apple" );
    b->assign( "banana" );

    REQUIRE( !( *a == *b ) );
    REQUIRE( *a != *b );
    REQUIRE( *a < *b );
    REQUIRE( !( *b < *a ) );

    // Equal strings
    b->assign( "apple" );
    REQUIRE( *a == *b );
    REQUIRE( !( *a < *b ) );
    REQUIRE( !( *b < *a ) );

    a->free_data();
    b->free_data();
    TestMgr::destroy_typed( pa );
    TestMgr::destroy_typed( pb );
    TestMgr::destroy();
}

/// @brief Empty pstring comparison.
TEST_CASE( "    compare empty pstrings", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> pa = TestMgr::create_typed<TestStr>();
    TestMgr::pptr<TestStr> pb = TestMgr::create_typed<TestStr>();

    TestStr* a = pa.resolve();
    TestStr* b = pb.resolve();

    // Both empty
    REQUIRE( *a == *b );
    REQUIRE( *a == "" );
    REQUIRE( *a == nullptr );

    // One non-empty
    a->assign( "non-empty" );
    REQUIRE( *a != *b );
    REQUIRE( !( *a == *b ) );

    a->free_data();
    b->free_data();
    TestMgr::destroy_typed( pa );
    TestMgr::destroy_typed( pb );
    TestMgr::destroy();
}

// =============================================================================
// I45-G: POD structure (trivially copyable)
// =============================================================================

/// @brief pstring is trivially copyable for direct serialization in PAP.
TEST_CASE( "    trivially copyable", "[test_issue45_pstring]" )
{
    REQUIRE( std::is_trivially_copyable_v<TestStr> );
    REQUIRE( std::is_nothrow_constructible_v<TestStr> );
    REQUIRE( std::is_nothrow_destructible_v<TestStr> );
}

/// @brief pstring layout: fields are at expected positions.
TEST_CASE( "    field layout", "[test_issue45_pstring]" )
{
    REQUIRE( sizeof( TestStr ) >= sizeof( std::uint32_t ) * 2 + sizeof( TestMgr::index_type ) );
    REQUIRE( offsetof( TestStr, _length ) == 0 );
    REQUIRE( offsetof( TestStr, _capacity ) == sizeof( std::uint32_t ) );
}

// =============================================================================
// I45-H: Long strings
// =============================================================================

/// @brief pstring with a very long string (> 4KB).
TEST_CASE( "    long string (>4KB)", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    std::string long_str( 8000, 'A' );
    long_str[0]    = 'S';
    long_str[7999] = 'E';

    REQUIRE( str->assign( long_str.c_str() ) );
    REQUIRE( str->size() == 8000 );
    REQUIRE( std::string( str->c_str() ) == long_str );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I45-I: Multiple pstrings in same manager
// =============================================================================

/// @brief Multiple pstrings coexist and work independently.
TEST_CASE( "    multiple independent pstrings", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    constexpr int          N = 10;
    TestMgr::pptr<TestStr> ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = TestMgr::create_typed<TestStr>();
        REQUIRE( !ptrs[i].is_null() );
        std::string s = "string_" + std::to_string( i );
        REQUIRE( ptrs[i]->assign( s.c_str() ) );
    }

    // Verify all strings are independent and correct
    for ( int i = 0; i < N; ++i )
    {
        std::string expected = "string_" + std::to_string( i );
        REQUIRE( std::string( ptrs[i]->c_str() ) == expected );
    }

    // Modify one string — others should be unaffected
    REQUIRE( ptrs[5]->assign( "modified" ) );
    for ( int i = 0; i < N; ++i )
    {
        if ( i == 5 )
        {
            REQUIRE( std::strcmp( ptrs[i]->c_str(), "modified" ) == 0 );
        }
        else
        {
            std::string expected = "string_" + std::to_string( i );
            REQUIRE( std::string( ptrs[i]->c_str() ) == expected );
        }
    }

    for ( int i = 0; i < N; ++i )
    {
        ptrs[i]->free_data();
        TestMgr::destroy_typed( ptrs[i] );
    }
    TestMgr::destroy();
}

// =============================================================================
// I45-J: pstring with std::string
// =============================================================================

/// @brief pstring works with std::string round-trips.
TEST_CASE( "    std::string round-trip", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    std::string input = "hello std::string!";
    REQUIRE( str->assign( input.c_str() ) );

    std::string output( str->c_str() );
    REQUIRE( output == input );
    REQUIRE( str->size() == input.size() );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I45-K: Manager alias works correctly
// =============================================================================

/// @brief Mgr::pstring alias works correctly.
TEST_CASE( "    Mgr::pstring alias works", "[test_issue45_pstring]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    // Use the Mgr::pstring alias directly
    TestMgr::pptr<TestMgr::pstring> p = TestMgr::create_typed<TestMgr::pstring>();
    REQUIRE( !p.is_null() );

    p->assign( "alias test" );
    REQUIRE( *p == "alias test" );

    p->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// main
// =============================================================================
