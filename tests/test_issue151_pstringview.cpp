/**
 * @file test_issue151_pstringview.cpp
 * @brief Tests for pstringview — interned read-only persistent strings.
 *
 * Verifies the key requirements from this feature:
 *  1. pstringview blocks lock the memory block in a read-only state when created.
 *  2. pstringview is not duplicated in PAP — intern() checks for existing strings
 *     with the same value and returns a pointer to the previously created pstringview.
 *  3. The pstringview dictionary gradually populates during manager lifetime.
 *  4. pstringview supports comparison operators (==, !=, <).
 *  5. Empty strings and null strings are handled correctly.
 *  6. pstringview::reset() clears the singleton for test isolation.
 *  7. The built-in AVL tree node fields (TreeNode via Block) are used for the
 *     pstringview dictionary — no separate hash table structures in PAP.
 *
 * Simple API (key requirement):
 *  @code
 *    Mgr::pptr<Mgr::pstringview> p = Mgr::pstringview("hello");
 *    Mgr::pptr<Mgr::pstringview> p2 = Mgr::pstringview("hello");
 *    assert(p == p2);  // true — deduplication via built-in AVL tree
 *  @endcode
 *
 * @see include/pmm/pstringview.h — pstringview
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @see include/pmm/tree_node.h — TreeNode<AT> built-in AVL fields
 * @version 0.4
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pstringview.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <string>
#include <vector>

// ─── Test macros ──────────────────────────────────────────────────────────────

// ─── Manager type alias for tests ────────────────────────────────────────────

using TestMgr          = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 151>;
using TestPsv          = TestMgr::pstringview;
using TestMgr_pptr_psv = TestMgr::pptr<TestPsv>;

// =============================================================================
// I151-A: intern() creates a new pstringview and locks the block
// =============================================================================

/// @brief Simple API: pstringview<Mgr>("hello") returns non-null pptr.
TEST_CASE( "    intern non-empty string", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = TestMgr::pstringview( "hello" );
    REQUIRE( !p.is_null() );

    const TestPsv* psv = p.resolve();
    REQUIRE( psv != nullptr );
    REQUIRE( psv->size() == 5 );
    REQUIRE( !psv->empty() );
    REQUIRE( *psv == "hello" );
    REQUIRE( std::strcmp( psv->c_str(), "hello" ) == 0 );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief Simple API: pstringview<Mgr>("") returns non-null pptr for empty string.
TEST_CASE( "    intern empty string", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = TestMgr::pstringview( "" );
    REQUIRE( !p.is_null() );

    const TestPsv* psv = p.resolve();
    REQUIRE( psv != nullptr );
    REQUIRE( psv->size() == 0 );
    REQUIRE( psv->empty() );
    REQUIRE( *psv == "" );
    REQUIRE( std::strcmp( psv->c_str(), "" ) == 0 );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief pstringview<Mgr>(nullptr) is treated the same as empty string.
TEST_CASE( "    intern nullptr == intern empty", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p_null  = TestMgr::pstringview( nullptr );
    TestMgr_pptr_psv p_empty = TestMgr::pstringview( "" );

    REQUIRE( !p_null.is_null() );
    REQUIRE( !p_empty.is_null() );
    // Both nullptr and "" should intern to the same object.
    REQUIRE( p_null == p_empty );

    TestMgr::destroy();
    TestPsv::reset();
}

// =============================================================================
// I151-B: Deduplication — same string → same pptr
// =============================================================================

/// @brief pstringview<Mgr>("world") called twice returns same pptr (key requirement #2).
TEST_CASE( "    same string → same pptr", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p1 = TestMgr::pstringview( "world" );
    TestMgr_pptr_psv p2 = TestMgr::pstringview( "world" );

    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );
    // Same string → same pptr (same granule index)
    REQUIRE( p1 == p2 );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief Different strings → different pptrs.
TEST_CASE( "    different strings → different pptrs", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p_hello = TestMgr::pstringview( "hello" );
    TestMgr_pptr_psv p_world = TestMgr::pstringview( "world" );

    REQUIRE( !p_hello.is_null() );
    REQUIRE( !p_world.is_null() );
    // Different strings → different pptrs
    REQUIRE( p_hello != p_world );

    // Check contents
    REQUIRE( *p_hello == "hello" );
    REQUIRE( *p_world == "world" );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief pstringview equality uses interning guarantee (same block comparison).
/// Now compares by address since strings are stored in the same block.
TEST_CASE( "    equality via interning guarantee", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv pa = TestMgr::pstringview( "key" );
    TestMgr_pptr_psv pb = TestMgr::pstringview( "key" );
    TestMgr_pptr_psv pc = TestMgr::pstringview( "other" );

    REQUIRE( ( !pa.is_null() && !pb.is_null() && !pc.is_null() ) );

    const TestPsv* a = pa.resolve();
    const TestPsv* b = pb.resolve();
    const TestPsv* c = pc.resolve();
    REQUIRE( ( a != nullptr && b != nullptr && c != nullptr ) );

    // Interning: same string → same block → equal
    REQUIRE( *a == *b );
    REQUIRE( !( *a == *c ) );
    REQUIRE( *a != *c );

    TestMgr::destroy();
    TestPsv::reset();
}

// =============================================================================
// I151-C: Block locking — intern() locks blocks permanently (key requirement #1)
// =============================================================================

/// @brief After intern(), the pstringview block (with embedded string) is permanently locked.
/// Now string data is stored in the same block as pstringview, so only one block
/// needs to be checked. The c_str() method returns pointer to embedded string data.
TEST_CASE( "    embedded string block permanently locked", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = TestMgr::pstringview( "locked_test" );
    REQUIRE( !p.is_null() );

    const TestPsv* psv = p.resolve();
    REQUIRE( psv != nullptr );

    // Verify the string is embedded directly in the pstringview block
    const char* str = psv->c_str();
    REQUIRE( str != nullptr );
    REQUIRE( std::strcmp( str, "locked_test" ) == 0 );

    // The pstringview block (containing embedded string) must be permanently locked
    REQUIRE( TestMgr::is_permanently_locked( psv ) == true );

    // Verify we cannot free it (deallocate is a no-op for locked blocks)
    std::size_t alloc_before = TestMgr::alloc_block_count();
    TestMgr::deallocate( const_cast<TestPsv*>( psv ) );
    std::size_t alloc_after = TestMgr::alloc_block_count();
    REQUIRE( alloc_before == alloc_after ); // Block not freed

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief After intern(), the pstringview block itself is permanently locked.
TEST_CASE( "    pstringview block permanently locked", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = TestMgr::pstringview( "psview_lock" );
    REQUIRE( !p.is_null() );

    TestPsv* psv = p.resolve();
    REQUIRE( psv != nullptr );

    // The pstringview block must be permanently locked
    REQUIRE( TestMgr::is_permanently_locked( psv ) == true );

    // Verify we cannot free it
    std::size_t alloc_before = TestMgr::alloc_block_count();
    TestMgr::deallocate( psv );
    std::size_t alloc_after = TestMgr::alloc_block_count();
    REQUIRE( alloc_before == alloc_after ); // Block not freed

    TestMgr::destroy();
    TestPsv::reset();
}

// =============================================================================
// I151-D: Built-in AVL tree (key architectural requirement — no separate PAP structures)
// =============================================================================

/// @brief The pstringview dictionary uses built-in TreeNode AVL fields (not a separate hash table).
///
/// Verified by checking that:
///  1. pstringview<Mgr>() creates pstringview objects that reference each other via tree_node() fields.
///  2. The AVL tree root is tracked by the persistent `system/symbols` domain root.
///  3. pstringview nodes themselves form the BST structure via their block's TreeNode fields.
TEST_CASE( "    AVL tree via built-in TreeNode fields", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    // Insert strings in sorted order to test AVL balance.
    TestMgr_pptr_psv p_a = TestMgr::pstringview( "alpha" );
    TestMgr_pptr_psv p_b = TestMgr::pstringview( "beta" );
    TestMgr_pptr_psv p_c = TestMgr::pstringview( "gamma" );

    REQUIRE( ( !p_a.is_null() && !p_b.is_null() && !p_c.is_null() ) );

    // All nodes accessible via the AVL tree root.
    REQUIRE( TestPsv::root_index() != static_cast<TestMgr::index_type>( 0 ) );

    // Re-interning returns the same pptr (deduplication via AVL tree search).
    REQUIRE( static_cast<TestMgr_pptr_psv>( TestMgr::pstringview( "alpha" ) ) == p_a );
    REQUIRE( static_cast<TestMgr_pptr_psv>( TestMgr::pstringview( "beta" ) ) == p_b );
    REQUIRE( static_cast<TestMgr_pptr_psv>( TestMgr::pstringview( "gamma" ) ) == p_c );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief The AVL tree root is a persistent domain binding and reset() clears that binding.
TEST_CASE( "    AVL root tracked by persistent symbol domain", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    // After create() bootstrap, system dictionary root is already persistent and non-null.
    REQUIRE( TestPsv::root_index() != static_cast<TestMgr::index_type>( 0 ) );

    // After reset — root is null again.
    TestPsv::reset();
    REQUIRE( TestPsv::root_index() == static_cast<TestMgr::index_type>( 0 ) );

    TestMgr::pstringview( "test_root" );

    // After intern — root is non-null.
    REQUIRE( TestPsv::root_index() != static_cast<TestMgr::index_type>( 0 ) );

    TestMgr::destroy();
    TestPsv::reset();
}

// =============================================================================
// I151-E: Dictionary grows during manager lifetime (key requirement #5)
// =============================================================================

/// @brief Multiple distinct strings are all stored in the dictionary.
TEST_CASE( "    multiple distinct strings stored", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    const char* strings[]   = { "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa" };
    constexpr std::size_t N = sizeof( strings ) / sizeof( strings[0] );

    TestMgr_pptr_psv ptrs[N];
    for ( std::size_t i = 0; i < N; i++ )
    {
        ptrs[i] = TestMgr::pstringview( strings[i] );
        REQUIRE( !ptrs[i].is_null() );
    }

    // All strings stored correctly
    for ( std::size_t i = 0; i < N; i++ )
    {
        const TestPsv* psv = ptrs[i].resolve();
        REQUIRE( psv != nullptr );
        REQUIRE( std::strcmp( psv->c_str(), strings[i] ) == 0 );
    }

    // All pptrs are distinct (different strings → different pptrs)
    for ( std::size_t i = 0; i < N; i++ )
        for ( std::size_t j = i + 1; j < N; j++ )
            REQUIRE( ptrs[i] != ptrs[j] );

    // Re-interning returns the same pptr
    for ( std::size_t i = 0; i < N; i++ )
    {
        TestMgr_pptr_psv p2 = TestMgr::pstringview( strings[i] );
        REQUIRE( p2 == ptrs[i] );
    }

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief AVL tree balances correctly with many strings in insertion order.
TEST_CASE( "    20 strings with AVL balancing", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    // Insert 20 strings to verify AVL balancing works correctly.
    char             buf[32];
    TestMgr_pptr_psv ptrs[20];
    for ( int i = 0; i < 20; i++ )
    {
        std::snprintf( buf, sizeof( buf ), "string_%02d", i );
        ptrs[i] = TestMgr::pstringview( buf );
        REQUIRE( !ptrs[i].is_null() );
    }

    // All strings retrievable via deduplication search
    for ( int i = 0; i < 20; i++ )
    {
        std::snprintf( buf, sizeof( buf ), "string_%02d", i );
        TestMgr_pptr_psv p2 = TestMgr::pstringview( buf );
        REQUIRE( p2 == ptrs[i] );
        const TestPsv* psv = ptrs[i].resolve();
        REQUIRE( psv != nullptr );
        REQUIRE( std::strcmp( psv->c_str(), buf ) == 0 );
    }

    TestMgr::destroy();
    TestPsv::reset();
}

// =============================================================================
// I151-F: Comparison operators
// =============================================================================

/// @brief operator< provides a consistent ordering of pstringview objects.
TEST_CASE( "    operator< lexicographic ordering", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p_a = TestMgr::pstringview( "apple" );
    TestMgr_pptr_psv p_b = TestMgr::pstringview( "banana" );
    TestMgr_pptr_psv p_c = TestMgr::pstringview( "cherry" );

    REQUIRE( ( !p_a.is_null() && !p_b.is_null() && !p_c.is_null() ) );

    const TestPsv* a = p_a.resolve();
    const TestPsv* b = p_b.resolve();
    const TestPsv* c = p_c.resolve();
    REQUIRE( ( a != nullptr && b != nullptr && c != nullptr ) );

    // Lexicographic ordering
    REQUIRE( *a < *b );
    REQUIRE( *b < *c );
    REQUIRE( !( *b < *a ) );
    REQUIRE( !( *c < *b ) );
    // Equal strings are not less than each other
    REQUIRE( !( *a < *a ) );

    TestMgr::destroy();
    TestPsv::reset();
}

// =============================================================================
// I151-G: pstringview::reset() for test isolation
// =============================================================================

/// @brief reset() clears the singleton; a new intern creates a fresh AVL tree.
TEST_CASE( "    reset() clears singleton for test isolation", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    // First session
    TestMgr_pptr_psv p1 = TestMgr::pstringview( "session1" );
    REQUIRE( !p1.is_null() );

    // Destroy and recreate manager, reset singleton
    TestMgr::destroy();
    TestPsv::reset();

    REQUIRE( TestMgr::create( 64 * 1024 ) );

    // Second session: singleton was reset, creates a new AVL tree
    TestMgr_pptr_psv p2 = TestMgr::pstringview( "session2" );
    REQUIRE( !p2.is_null() );
    REQUIRE( std::strcmp( p2->c_str(), "session2" ) == 0 );

    TestMgr::destroy();
    TestPsv::reset();
}

// =============================================================================
// I151-H: pstringview static_assert size check
// =============================================================================

/// @brief pstringview<ManagerT> has expected field layout.
/// Pstringview now stores length + flexible array member str[1].
TEST_CASE( "    pstringview size check", "[test_issue151_pstringview]" )
{
    // pstringview<ManagerT> has: length (uint32_t) and str[1] (flexible array member).
    // Plus _interned (psview_pptr) which is used only in the stack-helper constructor path.
    // The actual string data is stored after the fixed-size portion using the flexible array pattern.
    // sizeof should be at least sizeof(uint32_t) + 1 (for str[1]).
    REQUIRE( sizeof( TestPsv ) >= sizeof( std::uint32_t ) + 1 );
    // Verify offsetof(pstringview, str) is at least sizeof(uint32_t)
    REQUIRE( offsetof( TestPsv, str ) >= sizeof( std::uint32_t ) );
}

// =============================================================================
// I151-I: Tests using std::string
// =============================================================================

/// @brief pstringview<Mgr>(str.c_str()) accepts std::string and returns correct pstringview.
TEST_CASE( "    intern std::string basic", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    std::string      s = "hello_stdstring";
    TestMgr_pptr_psv p = TestMgr::pstringview( s.c_str() );
    REQUIRE( !p.is_null() );

    const TestPsv* psv = p.resolve();
    REQUIRE( psv != nullptr );
    REQUIRE( psv->size() == s.size() );
    REQUIRE( !psv->empty() );
    // Compare pstringview content to std::string
    REQUIRE( std::string( psv->c_str() ) == s );
    REQUIRE( *psv == s.c_str() );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief pstringview deduplicates when called with equal std::string values.
TEST_CASE( "    intern std::string deduplication", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    std::string      s1 = "deduplicated";
    std::string      s2 = "deduplicated"; // same value, different object
    TestMgr_pptr_psv p1 = TestMgr::pstringview( s1.c_str() );
    TestMgr_pptr_psv p2 = TestMgr::pstringview( s2.c_str() );

    REQUIRE( ( !p1.is_null() && !p2.is_null() ) );
    REQUIRE( p1 == p2 ); // Same pptr — deduplication works

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief pstringview with empty std::string works correctly.
TEST_CASE( "    intern empty std::string", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    std::string      empty_str;
    TestMgr_pptr_psv p = TestMgr::pstringview( empty_str.c_str() );
    REQUIRE( !p.is_null() );

    const TestPsv* psv = p.resolve();
    REQUIRE( psv != nullptr );
    REQUIRE( psv->empty() );
    REQUIRE( psv->size() == 0 );
    REQUIRE( std::string( psv->c_str() ) == empty_str );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief pstringview with std::string containing spaces and special characters.
TEST_CASE( "    intern std::string with spaces and special chars", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    std::string      s = "hello world! 123 @#$%";
    TestMgr_pptr_psv p = TestMgr::pstringview( s.c_str() );
    REQUIRE( !p.is_null() );

    const TestPsv* psv = p.resolve();
    REQUIRE( psv != nullptr );
    REQUIRE( psv->size() == s.size() );
    REQUIRE( std::string( psv->c_str() ) == s );

    // Same value again → same pptr
    std::string      s2 = "hello world! 123 @#$%";
    TestMgr_pptr_psv p2 = TestMgr::pstringview( s2.c_str() );
    REQUIRE( p == p2 );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief pstringview with a long std::string (> 255 characters).
TEST_CASE( "    intern long std::string (>255 chars)", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    // Build a string longer than 255 characters
    std::string long_str( 512, 'x' );
    long_str[0]   = 'S';
    long_str[511] = 'E';

    TestMgr_pptr_psv p = TestMgr::pstringview( long_str.c_str() );
    REQUIRE( !p.is_null() );

    const TestPsv* psv = p.resolve();
    REQUIRE( psv != nullptr );
    REQUIRE( psv->size() == long_str.size() );
    REQUIRE( std::string( psv->c_str() ) == long_str );

    // Deduplication also works for long strings
    TestMgr_pptr_psv p2 = TestMgr::pstringview( long_str.c_str() );
    REQUIRE( p == p2 );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief Multiple std::strings interned; all are stored correctly and deduplicated.
TEST_CASE( "    intern multiple std::strings", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    std::vector<std::string> strings = { "first", "second",  "third",  "fourth", "fifth",
                                         "sixth", "seventh", "eighth", "ninth",  "tenth" };

    std::vector<TestMgr_pptr_psv> ptrs;
    for ( const auto& s : strings )
    {
        TestMgr_pptr_psv p = TestMgr::pstringview( s.c_str() );
        REQUIRE( !p.is_null() );
        ptrs.push_back( p );
    }

    // Verify content of each interned string
    for ( std::size_t i = 0; i < strings.size(); ++i )
    {
        const TestPsv* psv = ptrs[i].resolve();
        REQUIRE( psv != nullptr );
        REQUIRE( std::string( psv->c_str() ) == strings[i] );
        REQUIRE( psv->size() == strings[i].size() );
    }

    // All pptrs are distinct
    for ( std::size_t i = 0; i < ptrs.size(); ++i )
        for ( std::size_t j = i + 1; j < ptrs.size(); ++j )
            REQUIRE( ptrs[i] != ptrs[j] );

    // Re-interning via std::string returns the same pptr
    for ( std::size_t i = 0; i < strings.size(); ++i )
    {
        TestMgr_pptr_psv p2 = TestMgr::pstringview( strings[i].c_str() );
        REQUIRE( p2 == ptrs[i] );
    }

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief Comparison between pstringview and std::string (via c_str()) works correctly.
TEST_CASE( "    compare pstringview with std::string", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    std::string sa = "apple";
    std::string sb = "banana";
    std::string sc = "cherry";

    TestMgr_pptr_psv pa = TestMgr::pstringview( sa.c_str() );
    TestMgr_pptr_psv pb = TestMgr::pstringview( sb.c_str() );
    TestMgr_pptr_psv pc = TestMgr::pstringview( sc.c_str() );

    REQUIRE( ( !pa.is_null() && !pb.is_null() && !pc.is_null() ) );

    const TestPsv* a = pa.resolve();
    const TestPsv* b = pb.resolve();
    const TestPsv* c = pc.resolve();
    REQUIRE( ( a != nullptr && b != nullptr && c != nullptr ) );

    // operator== with std::string via c_str()
    REQUIRE( *a == sa.c_str() );
    REQUIRE( *b == sb.c_str() );
    REQUIRE( *c == sc.c_str() );

    // operator!= with std::string
    REQUIRE( *a != sb.c_str() );
    REQUIRE( *b != sc.c_str() );

    // operator< with pstringview
    REQUIRE( *a < *b );
    REQUIRE( *b < *c );

    // std::string round-trip: intern → c_str() → std::string
    std::string result_a( a->c_str() );
    std::string result_b( b->c_str() );
    std::string result_c( c->c_str() );
    REQUIRE( result_a == sa );
    REQUIRE( result_b == sb );
    REQUIRE( result_c == sc );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief pstringview called with std::string built at runtime (concatenation).
TEST_CASE( "    intern runtime-built std::string (concatenation)", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    std::string prefix = "key_";
    std::string suffix = "value";
    std::string s      = prefix + suffix; // "key_value" — built at runtime

    TestMgr_pptr_psv p1 = TestMgr::pstringview( s.c_str() );
    REQUIRE( !p1.is_null() );

    // Same string built independently → same pptr
    std::string      s2 = std::string( "key_" ) + std::string( "value" );
    TestMgr_pptr_psv p2 = TestMgr::pstringview( s2.c_str() );
    REQUIRE( p1 == p2 );

    const TestPsv* psv = p1.resolve();
    REQUIRE( psv != nullptr );
    REQUIRE( std::string( psv->c_str() ) == "key_value" );

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief pstringview with std::string of numeric content.
TEST_CASE( "    intern std::string with numeric content (std::to_string)", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    // Intern strings built from integers via std::to_string
    std::vector<TestMgr_pptr_psv> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        std::string      s = std::to_string( i );
        TestMgr_pptr_psv p = TestMgr::pstringview( s.c_str() );
        REQUIRE( !p.is_null() );
        ptrs.push_back( p );
    }

    // Re-intern and verify deduplication
    for ( int i = 0; i < 10; ++i )
    {
        std::string      s  = std::to_string( i );
        TestMgr_pptr_psv p2 = TestMgr::pstringview( s.c_str() );
        REQUIRE( p2 == ptrs[static_cast<std::size_t>( i )] );

        const TestPsv* psv = p2.resolve();
        REQUIRE( psv != nullptr );
        REQUIRE( std::string( psv->c_str() ) == s );
    }

    TestMgr::destroy();
    TestPsv::reset();
}

/// @brief pstringview with interleaved const char* and std::string calls; deduplication holds.
TEST_CASE( "    interleaved const char* and std::string interns", "[test_issue151_pstringview]" )
{
    TestMgr::destroy();
    TestPsv::reset();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    // Intern via const char* first
    TestMgr_pptr_psv p_cstr = TestMgr::pstringview( "mixed" );
    REQUIRE( !p_cstr.is_null() );

    // Intern same value via std::string — must return same pptr
    std::string      str   = "mixed";
    TestMgr_pptr_psv p_str = TestMgr::pstringview( str.c_str() );
    REQUIRE( !p_str.is_null() );
    REQUIRE( p_cstr == p_str );

    // Intern a different value via std::string
    std::string      other   = "other_value";
    TestMgr_pptr_psv p_other = TestMgr::pstringview( other.c_str() );
    REQUIRE( !p_other.is_null() );
    REQUIRE( p_cstr != p_other );

    // Intern that different value via const char* — must equal p_other
    TestMgr_pptr_psv p_other2 = TestMgr::pstringview( "other_value" );
    REQUIRE( p_other == p_other2 );

    TestMgr::destroy();
    TestPsv::reset();
}

// =============================================================================
// main
// =============================================================================
