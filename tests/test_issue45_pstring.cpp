/**
 * @file test_issue45_pstring.cpp
 * @brief Tests for pstring — mutable persistent string (Issue #45, Phase 3.1).
 *
 * Verifies the key requirements from Issue #45 Phase 3.1:
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
 * @version 0.1 (Issue #45 — Phase 3.1: mutable persistent string)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pstring.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// ─── Test macros ──────────────────────────────────────────────────────────────

#define PMM_TEST( expr )                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( expr ) )                                                                                               \
        {                                                                                                              \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " << #expr << "\n";                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while ( false )

#define PMM_RUN( name, fn )                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "  " << name << " ... ";                                                                          \
        if ( fn() )                                                                                                    \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

// ─── Manager type alias for tests ────────────────────────────────────────────

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 45>;
using TestStr = TestMgr::pstring;

// =============================================================================
// I45-A: Basic creation and assignment
// =============================================================================

/// @brief create_typed<pstring>() creates an empty pstring.
static bool test_i45_create_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p = TestMgr::create_typed<TestStr>();
    PMM_TEST( !p.is_null() );

    TestStr* str = p.resolve();
    PMM_TEST( str != nullptr );
    PMM_TEST( str->empty() );
    PMM_TEST( str->size() == 0 );
    PMM_TEST( std::strcmp( str->c_str(), "" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief assign() sets the string content.
static bool test_i45_assign_basic()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p = TestMgr::create_typed<TestStr>();
    PMM_TEST( !p.is_null() );

    TestStr* str = p.resolve();
    PMM_TEST( str != nullptr );

    PMM_TEST( str->assign( "hello" ) );
    PMM_TEST( str->size() == 5 );
    PMM_TEST( !str->empty() );
    PMM_TEST( std::strcmp( str->c_str(), "hello" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief assign() with nullptr is treated as empty string.
static bool test_i45_assign_nullptr()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    PMM_TEST( str->assign( "test" ) );
    PMM_TEST( str->size() == 4 );

    PMM_TEST( str->assign( nullptr ) );
    PMM_TEST( str->size() == 0 );
    PMM_TEST( str->empty() );
    PMM_TEST( std::strcmp( str->c_str(), "" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief assign() with empty string.
static bool test_i45_assign_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    PMM_TEST( str->assign( "non-empty" ) );
    PMM_TEST( str->assign( "" ) );
    PMM_TEST( str->empty() );
    PMM_TEST( str->size() == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I45-B: Reallocation on assign
// =============================================================================

/// @brief assign() with a longer string triggers reallocation.
static bool test_i45_assign_realloc()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    // First assign — allocates initial buffer
    PMM_TEST( str->assign( "short" ) );
    PMM_TEST( str->size() == 5 );

    // Longer string — may trigger reallocation
    std::string long_str( 200, 'x' );
    PMM_TEST( str->assign( long_str.c_str() ) );
    PMM_TEST( str->size() == 200 );
    PMM_TEST( std::string( str->c_str() ) == long_str );

    // Even longer string — definitely triggers reallocation
    std::string very_long( 1000, 'y' );
    PMM_TEST( str->assign( very_long.c_str() ) );
    PMM_TEST( str->size() == 1000 );
    PMM_TEST( std::string( str->c_str() ) == very_long );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief Multiple reassignments work correctly.
static bool test_i45_assign_multiple()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    const char* values[] = { "alpha", "beta", "gamma", "delta", "epsilon" };
    for ( const char* v : values )
    {
        PMM_TEST( str->assign( v ) );
        PMM_TEST( std::strcmp( str->c_str(), v ) == 0 );
        PMM_TEST( str->size() == std::strlen( v ) );
    }

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I45-C: clear() and free_data()
// =============================================================================

/// @brief clear() sets length to 0 but preserves the buffer.
static bool test_i45_clear()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    PMM_TEST( str->assign( "hello" ) );
    PMM_TEST( str->size() == 5 );

    str->clear();
    PMM_TEST( str->empty() );
    PMM_TEST( str->size() == 0 );
    PMM_TEST( std::strcmp( str->c_str(), "" ) == 0 );

    // After clear, assign should reuse the buffer (no reallocation needed for small strings)
    PMM_TEST( str->assign( "world" ) );
    PMM_TEST( str->size() == 5 );
    PMM_TEST( std::strcmp( str->c_str(), "world" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief free_data() deallocates the data buffer and resets the string.
static bool test_i45_free_data()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    PMM_TEST( str->assign( "test data" ) );
    PMM_TEST( str->size() == 9 );

    std::size_t alloc_before = TestMgr::alloc_block_count();

    str->free_data();
    PMM_TEST( str->empty() );
    PMM_TEST( str->size() == 0 );
    PMM_TEST( std::strcmp( str->c_str(), "" ) == 0 );

    std::size_t alloc_after = TestMgr::alloc_block_count();
    // Data block should have been freed (alloc count decreased)
    PMM_TEST( alloc_after < alloc_before );

    // After free_data, assign should still work (allocates new buffer)
    PMM_TEST( str->assign( "new data" ) );
    PMM_TEST( str->size() == 8 );
    PMM_TEST( std::strcmp( str->c_str(), "new data" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I45-D: append()
// =============================================================================

/// @brief append() concatenates strings.
static bool test_i45_append_basic()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    PMM_TEST( str->assign( "hello" ) );
    PMM_TEST( str->append( " " ) );
    PMM_TEST( str->append( "world" ) );
    PMM_TEST( str->size() == 11 );
    PMM_TEST( std::strcmp( str->c_str(), "hello world" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief append() to empty string works like assign().
static bool test_i45_append_to_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    PMM_TEST( str->append( "first" ) );
    PMM_TEST( str->size() == 5 );
    PMM_TEST( std::strcmp( str->c_str(), "first" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief append() with nullptr is a no-op.
static bool test_i45_append_nullptr()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    PMM_TEST( str->assign( "test" ) );
    PMM_TEST( str->append( nullptr ) );
    PMM_TEST( str->size() == 4 );
    PMM_TEST( std::strcmp( str->c_str(), "test" ) == 0 );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief append() triggers reallocation when buffer is full.
static bool test_i45_append_realloc()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    // Build a long string through repeated appends
    std::string expected;
    for ( int i = 0; i < 100; ++i )
    {
        std::string chunk = "chunk" + std::to_string( i ) + "_";
        PMM_TEST( str->append( chunk.c_str() ) );
        expected += chunk;
    }

    PMM_TEST( str->size() == expected.size() );
    PMM_TEST( std::string( str->c_str() ) == expected );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I45-E: operator[]
// =============================================================================

/// @brief operator[] returns correct characters.
static bool test_i45_subscript()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    PMM_TEST( str->assign( "hello" ) );
    PMM_TEST( ( *str )[0] == 'h' );
    PMM_TEST( ( *str )[1] == 'e' );
    PMM_TEST( ( *str )[2] == 'l' );
    PMM_TEST( ( *str )[3] == 'l' );
    PMM_TEST( ( *str )[4] == 'o' );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I45-F: Comparison operators
// =============================================================================

/// @brief operator== with C-string.
static bool test_i45_eq_cstr()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    PMM_TEST( str->assign( "hello" ) );
    PMM_TEST( *str == "hello" );
    PMM_TEST( !( *str == "world" ) );
    PMM_TEST( *str != "world" );
    PMM_TEST( !( *str != "hello" ) );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief operator== and operator< between two pstrings.
static bool test_i45_compare_pstrings()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> pa = TestMgr::create_typed<TestStr>();
    TestMgr::pptr<TestStr> pb = TestMgr::create_typed<TestStr>();

    TestStr* a = pa.resolve();
    TestStr* b = pb.resolve();

    a->assign( "apple" );
    b->assign( "banana" );

    PMM_TEST( !( *a == *b ) );
    PMM_TEST( *a != *b );
    PMM_TEST( *a < *b );
    PMM_TEST( !( *b < *a ) );

    // Equal strings
    b->assign( "apple" );
    PMM_TEST( *a == *b );
    PMM_TEST( !( *a < *b ) );
    PMM_TEST( !( *b < *a ) );

    a->free_data();
    b->free_data();
    TestMgr::destroy_typed( pa );
    TestMgr::destroy_typed( pb );
    TestMgr::destroy();
    return true;
}

/// @brief Empty pstring comparison.
static bool test_i45_compare_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> pa = TestMgr::create_typed<TestStr>();
    TestMgr::pptr<TestStr> pb = TestMgr::create_typed<TestStr>();

    TestStr* a = pa.resolve();
    TestStr* b = pb.resolve();

    // Both empty
    PMM_TEST( *a == *b );
    PMM_TEST( *a == "" );
    PMM_TEST( *a == nullptr );

    // One non-empty
    a->assign( "non-empty" );
    PMM_TEST( *a != *b );
    PMM_TEST( !( *a == *b ) );

    a->free_data();
    b->free_data();
    TestMgr::destroy_typed( pa );
    TestMgr::destroy_typed( pb );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I45-G: POD structure (trivially copyable)
// =============================================================================

/// @brief pstring is trivially copyable for direct serialization in PAP.
static bool test_i45_trivially_copyable()
{
    PMM_TEST( std::is_trivially_copyable_v<TestStr> );
    PMM_TEST( std::is_nothrow_constructible_v<TestStr> );
    PMM_TEST( std::is_nothrow_destructible_v<TestStr> );
    return true;
}

/// @brief pstring layout: fields are at expected positions.
static bool test_i45_layout()
{
    PMM_TEST( sizeof( TestStr ) >= sizeof( std::uint32_t ) * 2 + sizeof( TestMgr::index_type ) );
    PMM_TEST( offsetof( TestStr, _length ) == 0 );
    PMM_TEST( offsetof( TestStr, _capacity ) == sizeof( std::uint32_t ) );
    return true;
}

// =============================================================================
// I45-H: Long strings
// =============================================================================

/// @brief pstring with a very long string (> 4KB).
static bool test_i45_long_string()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    std::string long_str( 8000, 'A' );
    long_str[0]    = 'S';
    long_str[7999] = 'E';

    PMM_TEST( str->assign( long_str.c_str() ) );
    PMM_TEST( str->size() == 8000 );
    PMM_TEST( std::string( str->c_str() ) == long_str );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I45-I: Multiple pstrings in same manager
// =============================================================================

/// @brief Multiple pstrings coexist and work independently.
static bool test_i45_multiple_strings()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    constexpr int          N = 10;
    TestMgr::pptr<TestStr> ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = TestMgr::create_typed<TestStr>();
        PMM_TEST( !ptrs[i].is_null() );
        std::string s = "string_" + std::to_string( i );
        PMM_TEST( ptrs[i]->assign( s.c_str() ) );
    }

    // Verify all strings are independent and correct
    for ( int i = 0; i < N; ++i )
    {
        std::string expected = "string_" + std::to_string( i );
        PMM_TEST( std::string( ptrs[i]->c_str() ) == expected );
    }

    // Modify one string — others should be unaffected
    PMM_TEST( ptrs[5]->assign( "modified" ) );
    for ( int i = 0; i < N; ++i )
    {
        if ( i == 5 )
        {
            PMM_TEST( std::strcmp( ptrs[i]->c_str(), "modified" ) == 0 );
        }
        else
        {
            std::string expected = "string_" + std::to_string( i );
            PMM_TEST( std::string( ptrs[i]->c_str() ) == expected );
        }
    }

    for ( int i = 0; i < N; ++i )
    {
        ptrs[i]->free_data();
        TestMgr::destroy_typed( ptrs[i] );
    }
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I45-J: pstring with std::string
// =============================================================================

/// @brief pstring works with std::string round-trips.
static bool test_i45_stdstring_roundtrip()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestStr> p   = TestMgr::create_typed<TestStr>();
    TestStr*               str = p.resolve();

    std::string input = "hello std::string!";
    PMM_TEST( str->assign( input.c_str() ) );

    std::string output( str->c_str() );
    PMM_TEST( output == input );
    PMM_TEST( str->size() == input.size() );

    str->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I45-K: Manager alias works correctly
// =============================================================================

/// @brief Mgr::pstring alias works correctly.
static bool test_i45_manager_alias()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // Use the Mgr::pstring alias directly
    TestMgr::pptr<TestMgr::pstring> p = TestMgr::create_typed<TestMgr::pstring>();
    PMM_TEST( !p.is_null() );

    p->assign( "alias test" );
    PMM_TEST( *p == "alias test" );

    p->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    bool all_passed = true;

    std::cout << "[Issue #45: pstring — mutable persistent string (Phase 3.1)]\n";

    std::cout << "  I45-A: Basic creation and assignment\n";
    PMM_RUN( "    create empty pstring", test_i45_create_empty );
    PMM_RUN( "    assign basic string", test_i45_assign_basic );
    PMM_RUN( "    assign nullptr", test_i45_assign_nullptr );
    PMM_RUN( "    assign empty string", test_i45_assign_empty );

    std::cout << "  I45-B: Reallocation on assign\n";
    PMM_RUN( "    assign triggers reallocation", test_i45_assign_realloc );
    PMM_RUN( "    multiple reassignments", test_i45_assign_multiple );

    std::cout << "  I45-C: clear() and free_data()\n";
    PMM_RUN( "    clear() preserves buffer", test_i45_clear );
    PMM_RUN( "    free_data() deallocates buffer", test_i45_free_data );

    std::cout << "  I45-D: append()\n";
    PMM_RUN( "    append basic", test_i45_append_basic );
    PMM_RUN( "    append to empty", test_i45_append_to_empty );
    PMM_RUN( "    append nullptr", test_i45_append_nullptr );
    PMM_RUN( "    append triggers reallocation", test_i45_append_realloc );

    std::cout << "  I45-E: operator[]\n";
    PMM_RUN( "    subscript operator", test_i45_subscript );

    std::cout << "  I45-F: Comparison operators\n";
    PMM_RUN( "    operator== with C-string", test_i45_eq_cstr );
    PMM_RUN( "    compare two pstrings", test_i45_compare_pstrings );
    PMM_RUN( "    compare empty pstrings", test_i45_compare_empty );

    std::cout << "  I45-G: POD structure\n";
    PMM_RUN( "    trivially copyable", test_i45_trivially_copyable );
    PMM_RUN( "    field layout", test_i45_layout );

    std::cout << "  I45-H: Long strings\n";
    PMM_RUN( "    long string (>4KB)", test_i45_long_string );

    std::cout << "  I45-I: Multiple pstrings\n";
    PMM_RUN( "    multiple independent pstrings", test_i45_multiple_strings );

    std::cout << "  I45-J: std::string interop\n";
    PMM_RUN( "    std::string round-trip", test_i45_stdstring_roundtrip );

    std::cout << "  I45-K: Manager alias\n";
    PMM_RUN( "    Mgr::pstring alias works", test_i45_manager_alias );

    std::cout << "\n";
    if ( all_passed )
    {
        std::cout << "All Issue #45 pstring tests PASSED.\n";
        return EXIT_SUCCESS;
    }
    else
    {
        std::cout << "Some Issue #45 pstring tests FAILED.\n";
        return EXIT_FAILURE;
    }
}
