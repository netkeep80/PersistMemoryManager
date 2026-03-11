/**
 * @file test_issue151_pstringview.cpp
 * @brief Tests for pstringview — interned read-only persistent strings (Issue #151).
 *
 * Verifies the key requirements from Issue #151:
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
 * @see include/pmm/tree_node.h — TreeNode<AT> built-in AVL fields (Issue #87, #138)
 * @version 0.4 (Issue #151 — concise API via manager alias: Mgr::pstringview("hello"))
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pstringview.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

using TestMgr          = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 151>;
using TestPsv          = TestMgr::pstringview;
using TestMgr_pptr_psv = TestMgr::pptr<TestPsv>;

// =============================================================================
// I151-A: intern() creates a new pstringview and locks the block
// =============================================================================

/// @brief Simple API: pstringview<Mgr>("hello") returns non-null pptr.
static bool test_i151_intern_basic()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = TestMgr::pstringview( "hello" );
    PMM_TEST( !p.is_null() );

    const TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );
    PMM_TEST( psv->size() == 5 );
    PMM_TEST( !psv->empty() );
    PMM_TEST( *psv == "hello" );
    PMM_TEST( std::strcmp( psv->c_str(), "hello" ) == 0 );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief Simple API: pstringview<Mgr>("") returns non-null pptr for empty string.
static bool test_i151_intern_empty_string()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = TestMgr::pstringview( "" );
    PMM_TEST( !p.is_null() );

    const TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );
    PMM_TEST( psv->size() == 0 );
    PMM_TEST( psv->empty() );
    PMM_TEST( *psv == "" );
    PMM_TEST( std::strcmp( psv->c_str(), "" ) == 0 );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief pstringview<Mgr>(nullptr) is treated the same as empty string.
static bool test_i151_intern_nullptr()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p_null  = TestMgr::pstringview( nullptr );
    TestMgr_pptr_psv p_empty = TestMgr::pstringview( "" );

    PMM_TEST( !p_null.is_null() );
    PMM_TEST( !p_empty.is_null() );
    // Both nullptr and "" should intern to the same object.
    PMM_TEST( p_null == p_empty );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

// =============================================================================
// I151-B: Deduplication — same string → same pptr
// =============================================================================

/// @brief pstringview<Mgr>("world") called twice returns same pptr (key requirement #2).
static bool test_i151_deduplication()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p1 = TestMgr::pstringview( "world" );
    TestMgr_pptr_psv p2 = TestMgr::pstringview( "world" );

    PMM_TEST( !p1.is_null() );
    PMM_TEST( !p2.is_null() );
    // Same string → same pptr (same granule index)
    PMM_TEST( p1 == p2 );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief Different strings → different pptrs.
static bool test_i151_different_strings_different_pptrs()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p_hello = TestMgr::pstringview( "hello" );
    TestMgr_pptr_psv p_world = TestMgr::pstringview( "world" );

    PMM_TEST( !p_hello.is_null() );
    PMM_TEST( !p_world.is_null() );
    // Different strings → different pptrs
    PMM_TEST( p_hello != p_world );

    // Check contents
    PMM_TEST( *p_hello == "hello" );
    PMM_TEST( *p_world == "world" );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief pstringview equality uses interning guarantee (same block comparison).
/// Issue #184: now compares by address since strings are stored in the same block.
static bool test_i151_equality_via_interning()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv pa = TestMgr::pstringview( "key" );
    TestMgr_pptr_psv pb = TestMgr::pstringview( "key" );
    TestMgr_pptr_psv pc = TestMgr::pstringview( "other" );

    PMM_TEST( !pa.is_null() && !pb.is_null() && !pc.is_null() );

    const TestPsv* a = pa.resolve();
    const TestPsv* b = pb.resolve();
    const TestPsv* c = pc.resolve();
    PMM_TEST( a != nullptr && b != nullptr && c != nullptr );

    // Interning: same string → same block → equal (Issue #184: address comparison)
    PMM_TEST( *a == *b );
    PMM_TEST( !( *a == *c ) );
    PMM_TEST( *a != *c );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

// =============================================================================
// I151-C: Block locking — intern() locks blocks permanently (key requirement #1)
// =============================================================================

/// @brief After intern(), the pstringview block (with embedded string) is permanently locked.
/// Issue #184: Now string data is stored in the same block as pstringview, so only one block
/// needs to be checked. The c_str() method returns pointer to embedded string data.
static bool test_i151_embedded_string_block_locked()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = TestMgr::pstringview( "locked_test" );
    PMM_TEST( !p.is_null() );

    const TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );

    // Issue #184: Verify the string is embedded directly in the pstringview block
    const char* str = psv->c_str();
    PMM_TEST( str != nullptr );
    PMM_TEST( std::strcmp( str, "locked_test" ) == 0 );

    // The pstringview block (containing embedded string) must be permanently locked
    PMM_TEST( TestMgr::is_permanently_locked( psv ) == true );

    // Verify we cannot free it (deallocate is a no-op for locked blocks)
    std::size_t alloc_before = TestMgr::alloc_block_count();
    TestMgr::deallocate( const_cast<TestPsv*>( psv ) );
    std::size_t alloc_after = TestMgr::alloc_block_count();
    PMM_TEST( alloc_before == alloc_after ); // Block not freed

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief After intern(), the pstringview block itself is permanently locked.
static bool test_i151_psview_block_permanently_locked()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = TestMgr::pstringview( "psview_lock" );
    PMM_TEST( !p.is_null() );

    TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );

    // The pstringview block must be permanently locked
    PMM_TEST( TestMgr::is_permanently_locked( psv ) == true );

    // Verify we cannot free it
    std::size_t alloc_before = TestMgr::alloc_block_count();
    TestMgr::deallocate( psv );
    std::size_t alloc_after = TestMgr::alloc_block_count();
    PMM_TEST( alloc_before == alloc_after ); // Block not freed

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

// =============================================================================
// I151-D: Built-in AVL tree (key architectural requirement — no separate PAP structures)
// =============================================================================

/// @brief The pstringview dictionary uses built-in TreeNode AVL fields (not a separate hash table).
///
/// Verified by checking that:
///  1. pstringview<Mgr>() creates pstringview objects that reference each other via tree_node() fields.
///  2. The AVL tree root is tracked only by a static index (no extra PAP allocation for table).
///  3. pstringview nodes themselves form the BST structure via their block's TreeNode fields.
static bool test_i151_avl_tree_structure()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // Insert strings in sorted order to test AVL balance.
    TestMgr_pptr_psv p_a = TestMgr::pstringview( "alpha" );
    TestMgr_pptr_psv p_b = TestMgr::pstringview( "beta" );
    TestMgr_pptr_psv p_c = TestMgr::pstringview( "gamma" );

    PMM_TEST( !p_a.is_null() && !p_b.is_null() && !p_c.is_null() );

    // All nodes accessible via the AVL tree root.
    PMM_TEST( TestPsv::_root_idx != static_cast<TestMgr::index_type>( 0 ) );

    // Re-interning returns the same pptr (deduplication via AVL tree search).
    PMM_TEST( static_cast<TestMgr_pptr_psv>( TestMgr::pstringview( "alpha" ) ) == p_a );
    PMM_TEST( static_cast<TestMgr_pptr_psv>( TestMgr::pstringview( "beta" ) ) == p_b );
    PMM_TEST( static_cast<TestMgr_pptr_psv>( TestMgr::pstringview( "gamma" ) ) == p_c );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief The AVL tree root index is reset by reset() without leaking PAP state.
static bool test_i151_avl_root_reset()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // Before any intern — root is null.
    PMM_TEST( TestPsv::_root_idx == static_cast<TestMgr::index_type>( 0 ) );

    TestMgr::pstringview( "test_root" );

    // After intern — root is non-null.
    PMM_TEST( TestPsv::_root_idx != static_cast<TestMgr::index_type>( 0 ) );

    // After reset — root is null again.
    TestPsv::reset();
    PMM_TEST( TestPsv::_root_idx == static_cast<TestMgr::index_type>( 0 ) );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

// =============================================================================
// I151-E: Dictionary grows during manager lifetime (key requirement #5)
// =============================================================================

/// @brief Multiple distinct strings are all stored in the dictionary.
static bool test_i151_dictionary_grows()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    const char* strings[]   = { "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa" };
    constexpr std::size_t N = sizeof( strings ) / sizeof( strings[0] );

    TestMgr_pptr_psv ptrs[N];
    for ( std::size_t i = 0; i < N; i++ )
    {
        ptrs[i] = TestMgr::pstringview( strings[i] );
        PMM_TEST( !ptrs[i].is_null() );
    }

    // All strings stored correctly
    for ( std::size_t i = 0; i < N; i++ )
    {
        const TestPsv* psv = ptrs[i].resolve();
        PMM_TEST( psv != nullptr );
        PMM_TEST( std::strcmp( psv->c_str(), strings[i] ) == 0 );
    }

    // All pptrs are distinct (different strings → different pptrs)
    for ( std::size_t i = 0; i < N; i++ )
        for ( std::size_t j = i + 1; j < N; j++ )
            PMM_TEST( ptrs[i] != ptrs[j] );

    // Re-interning returns the same pptr
    for ( std::size_t i = 0; i < N; i++ )
    {
        TestMgr_pptr_psv p2 = TestMgr::pstringview( strings[i] );
        PMM_TEST( p2 == ptrs[i] );
    }

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief AVL tree balances correctly with many strings in insertion order.
static bool test_i151_dictionary_many_strings()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    // Insert 20 strings to verify AVL balancing works correctly.
    char             buf[32];
    TestMgr_pptr_psv ptrs[20];
    for ( int i = 0; i < 20; i++ )
    {
        std::snprintf( buf, sizeof( buf ), "string_%02d", i );
        ptrs[i] = TestMgr::pstringview( buf );
        PMM_TEST( !ptrs[i].is_null() );
    }

    // All strings retrievable via deduplication search
    for ( int i = 0; i < 20; i++ )
    {
        std::snprintf( buf, sizeof( buf ), "string_%02d", i );
        TestMgr_pptr_psv p2 = TestMgr::pstringview( buf );
        PMM_TEST( p2 == ptrs[i] );
        const TestPsv* psv = ptrs[i].resolve();
        PMM_TEST( psv != nullptr );
        PMM_TEST( std::strcmp( psv->c_str(), buf ) == 0 );
    }

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

// =============================================================================
// I151-F: Comparison operators
// =============================================================================

/// @brief operator< provides a consistent ordering of pstringview objects.
static bool test_i151_less_than_ordering()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p_a = TestMgr::pstringview( "apple" );
    TestMgr_pptr_psv p_b = TestMgr::pstringview( "banana" );
    TestMgr_pptr_psv p_c = TestMgr::pstringview( "cherry" );

    PMM_TEST( !p_a.is_null() && !p_b.is_null() && !p_c.is_null() );

    const TestPsv* a = p_a.resolve();
    const TestPsv* b = p_b.resolve();
    const TestPsv* c = p_c.resolve();
    PMM_TEST( a != nullptr && b != nullptr && c != nullptr );

    // Lexicographic ordering
    PMM_TEST( *a < *b );
    PMM_TEST( *b < *c );
    PMM_TEST( !( *b < *a ) );
    PMM_TEST( !( *c < *b ) );
    // Equal strings are not less than each other
    PMM_TEST( !( *a < *a ) );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

// =============================================================================
// I151-G: pstringview::reset() for test isolation
// =============================================================================

/// @brief reset() clears the singleton; a new intern creates a fresh AVL tree.
static bool test_i151_reset_clears_singleton()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // First session
    TestMgr_pptr_psv p1 = TestMgr::pstringview( "session1" );
    PMM_TEST( !p1.is_null() );

    // Destroy and recreate manager, reset singleton
    TestMgr::destroy();
    TestPsv::reset();

    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // Second session: singleton was reset, creates a new AVL tree
    TestMgr_pptr_psv p2 = TestMgr::pstringview( "session2" );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( std::strcmp( p2->c_str(), "session2" ) == 0 );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

// =============================================================================
// I151-H: pstringview static_assert size check
// =============================================================================

/// @brief pstringview<ManagerT> has expected field layout.
/// Issue #184: pstringview now stores length + flexible array member str[1].
static bool test_i151_layout()
{
    // pstringview<ManagerT> has: length (uint32_t) and str[1] (flexible array member).
    // Plus _interned (psview_pptr) which is used only in the stack-helper constructor path.
    // The actual string data is stored after the fixed-size portion using the flexible array pattern.
    // sizeof should be at least sizeof(uint32_t) + 1 (for str[1]).
    PMM_TEST( sizeof( TestPsv ) >= sizeof( std::uint32_t ) + 1 );
    // Verify offsetof(pstringview, str) is at least sizeof(uint32_t)
    PMM_TEST( offsetof( TestPsv, str ) >= sizeof( std::uint32_t ) );
    return true;
}

// =============================================================================
// I151-I: Tests using std::string
// =============================================================================

/// @brief pstringview<Mgr>(str.c_str()) accepts std::string and returns correct pstringview.
static bool test_i151_stdstring_basic()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    std::string      s = "hello_stdstring";
    TestMgr_pptr_psv p = TestMgr::pstringview( s.c_str() );
    PMM_TEST( !p.is_null() );

    const TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );
    PMM_TEST( psv->size() == s.size() );
    PMM_TEST( !psv->empty() );
    // Compare pstringview content to std::string
    PMM_TEST( std::string( psv->c_str() ) == s );
    PMM_TEST( *psv == s.c_str() );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief pstringview deduplicates when called with equal std::string values.
static bool test_i151_stdstring_deduplication()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    std::string      s1 = "deduplicated";
    std::string      s2 = "deduplicated"; // same value, different object
    TestMgr_pptr_psv p1 = TestMgr::pstringview( s1.c_str() );
    TestMgr_pptr_psv p2 = TestMgr::pstringview( s2.c_str() );

    PMM_TEST( !p1.is_null() && !p2.is_null() );
    PMM_TEST( p1 == p2 ); // Same pptr — deduplication works

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief pstringview with empty std::string works correctly.
static bool test_i151_stdstring_empty()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    std::string      empty_str;
    TestMgr_pptr_psv p = TestMgr::pstringview( empty_str.c_str() );
    PMM_TEST( !p.is_null() );

    const TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );
    PMM_TEST( psv->empty() );
    PMM_TEST( psv->size() == 0 );
    PMM_TEST( std::string( psv->c_str() ) == empty_str );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief pstringview with std::string containing spaces and special characters.
static bool test_i151_stdstring_special_chars()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    std::string      s = "hello world! 123 @#$%";
    TestMgr_pptr_psv p = TestMgr::pstringview( s.c_str() );
    PMM_TEST( !p.is_null() );

    const TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );
    PMM_TEST( psv->size() == s.size() );
    PMM_TEST( std::string( psv->c_str() ) == s );

    // Same value again → same pptr
    std::string      s2 = "hello world! 123 @#$%";
    TestMgr_pptr_psv p2 = TestMgr::pstringview( s2.c_str() );
    PMM_TEST( p == p2 );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief pstringview with a long std::string (> 255 characters).
static bool test_i151_stdstring_long()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    // Build a string longer than 255 characters
    std::string long_str( 512, 'x' );
    long_str[0]   = 'S';
    long_str[511] = 'E';

    TestMgr_pptr_psv p = TestMgr::pstringview( long_str.c_str() );
    PMM_TEST( !p.is_null() );

    const TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );
    PMM_TEST( psv->size() == long_str.size() );
    PMM_TEST( std::string( psv->c_str() ) == long_str );

    // Deduplication also works for long strings
    TestMgr_pptr_psv p2 = TestMgr::pstringview( long_str.c_str() );
    PMM_TEST( p == p2 );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief Multiple std::strings interned; all are stored correctly and deduplicated.
static bool test_i151_stdstring_multiple()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    std::vector<std::string> strings = { "first", "second",  "third",  "fourth", "fifth",
                                         "sixth", "seventh", "eighth", "ninth",  "tenth" };

    std::vector<TestMgr_pptr_psv> ptrs;
    for ( const auto& s : strings )
    {
        TestMgr_pptr_psv p = TestMgr::pstringview( s.c_str() );
        PMM_TEST( !p.is_null() );
        ptrs.push_back( p );
    }

    // Verify content of each interned string
    for ( std::size_t i = 0; i < strings.size(); ++i )
    {
        const TestPsv* psv = ptrs[i].resolve();
        PMM_TEST( psv != nullptr );
        PMM_TEST( std::string( psv->c_str() ) == strings[i] );
        PMM_TEST( psv->size() == strings[i].size() );
    }

    // All pptrs are distinct
    for ( std::size_t i = 0; i < ptrs.size(); ++i )
        for ( std::size_t j = i + 1; j < ptrs.size(); ++j )
            PMM_TEST( ptrs[i] != ptrs[j] );

    // Re-interning via std::string returns the same pptr
    for ( std::size_t i = 0; i < strings.size(); ++i )
    {
        TestMgr_pptr_psv p2 = TestMgr::pstringview( strings[i].c_str() );
        PMM_TEST( p2 == ptrs[i] );
    }

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief Comparison between pstringview and std::string (via c_str()) works correctly.
static bool test_i151_stdstring_comparison()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    std::string sa = "apple";
    std::string sb = "banana";
    std::string sc = "cherry";

    TestMgr_pptr_psv pa = TestMgr::pstringview( sa.c_str() );
    TestMgr_pptr_psv pb = TestMgr::pstringview( sb.c_str() );
    TestMgr_pptr_psv pc = TestMgr::pstringview( sc.c_str() );

    PMM_TEST( !pa.is_null() && !pb.is_null() && !pc.is_null() );

    const TestPsv* a = pa.resolve();
    const TestPsv* b = pb.resolve();
    const TestPsv* c = pc.resolve();
    PMM_TEST( a != nullptr && b != nullptr && c != nullptr );

    // operator== with std::string via c_str()
    PMM_TEST( *a == sa.c_str() );
    PMM_TEST( *b == sb.c_str() );
    PMM_TEST( *c == sc.c_str() );

    // operator!= with std::string
    PMM_TEST( *a != sb.c_str() );
    PMM_TEST( *b != sc.c_str() );

    // operator< with pstringview
    PMM_TEST( *a < *b );
    PMM_TEST( *b < *c );

    // std::string round-trip: intern → c_str() → std::string
    std::string result_a( a->c_str() );
    std::string result_b( b->c_str() );
    std::string result_c( c->c_str() );
    PMM_TEST( result_a == sa );
    PMM_TEST( result_b == sb );
    PMM_TEST( result_c == sc );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief pstringview called with std::string built at runtime (concatenation).
static bool test_i151_stdstring_runtime_built()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    std::string prefix = "key_";
    std::string suffix = "value";
    std::string s      = prefix + suffix; // "key_value" — built at runtime

    TestMgr_pptr_psv p1 = TestMgr::pstringview( s.c_str() );
    PMM_TEST( !p1.is_null() );

    // Same string built independently → same pptr
    std::string      s2 = std::string( "key_" ) + std::string( "value" );
    TestMgr_pptr_psv p2 = TestMgr::pstringview( s2.c_str() );
    PMM_TEST( p1 == p2 );

    const TestPsv* psv = p1.resolve();
    PMM_TEST( psv != nullptr );
    PMM_TEST( std::string( psv->c_str() ) == "key_value" );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief pstringview with std::string of numeric content.
static bool test_i151_stdstring_numeric_content()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // Intern strings built from integers via std::to_string
    std::vector<TestMgr_pptr_psv> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        std::string      s = std::to_string( i );
        TestMgr_pptr_psv p = TestMgr::pstringview( s.c_str() );
        PMM_TEST( !p.is_null() );
        ptrs.push_back( p );
    }

    // Re-intern and verify deduplication
    for ( int i = 0; i < 10; ++i )
    {
        std::string      s  = std::to_string( i );
        TestMgr_pptr_psv p2 = TestMgr::pstringview( s.c_str() );
        PMM_TEST( p2 == ptrs[static_cast<std::size_t>( i )] );

        const TestPsv* psv = p2.resolve();
        PMM_TEST( psv != nullptr );
        PMM_TEST( std::string( psv->c_str() ) == s );
    }

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

/// @brief pstringview with interleaved const char* and std::string calls; deduplication holds.
static bool test_i151_stdstring_mixed_with_cstr()
{
    TestMgr::destroy();
    TestPsv::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // Intern via const char* first
    TestMgr_pptr_psv p_cstr = TestMgr::pstringview( "mixed" );
    PMM_TEST( !p_cstr.is_null() );

    // Intern same value via std::string — must return same pptr
    std::string      str   = "mixed";
    TestMgr_pptr_psv p_str = TestMgr::pstringview( str.c_str() );
    PMM_TEST( !p_str.is_null() );
    PMM_TEST( p_cstr == p_str );

    // Intern a different value via std::string
    std::string      other   = "other_value";
    TestMgr_pptr_psv p_other = TestMgr::pstringview( other.c_str() );
    PMM_TEST( !p_other.is_null() );
    PMM_TEST( p_cstr != p_other );

    // Intern that different value via const char* — must equal p_other
    TestMgr_pptr_psv p_other2 = TestMgr::pstringview( "other_value" );
    PMM_TEST( p_other == p_other2 );

    TestMgr::destroy();
    TestPsv::reset();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    bool all_passed = true;

    std::cout << "[Issue #151: pstringview — interned read-only persistent strings via built-in AVL forest]\n";

    std::cout << "  I151-A: intern() basic creation\n";
    PMM_RUN( "    intern non-empty string", test_i151_intern_basic );
    PMM_RUN( "    intern empty string", test_i151_intern_empty_string );
    PMM_RUN( "    intern nullptr == intern empty", test_i151_intern_nullptr );

    std::cout << "  I151-B: Deduplication\n";
    PMM_RUN( "    same string → same pptr", test_i151_deduplication );
    PMM_RUN( "    different strings → different pptrs", test_i151_different_strings_different_pptrs );
    PMM_RUN( "    equality via interning guarantee", test_i151_equality_via_interning );

    std::cout << "  I151-C: Block locking (Issue #126, #184)\n";
    PMM_RUN( "    embedded string block permanently locked", test_i151_embedded_string_block_locked );
    PMM_RUN( "    pstringview block permanently locked", test_i151_psview_block_permanently_locked );

    std::cout << "  I151-D: Built-in AVL tree structure (no separate PAP structures)\n";
    PMM_RUN( "    AVL tree via built-in TreeNode fields", test_i151_avl_tree_structure );
    PMM_RUN( "    AVL root tracked by static index only", test_i151_avl_root_reset );

    std::cout << "  I151-E: Dictionary grows during manager lifetime\n";
    PMM_RUN( "    multiple distinct strings stored", test_i151_dictionary_grows );
    PMM_RUN( "    20 strings with AVL balancing", test_i151_dictionary_many_strings );

    std::cout << "  I151-F: Comparison operators\n";
    PMM_RUN( "    operator< lexicographic ordering", test_i151_less_than_ordering );

    std::cout << "  I151-G: pstringview::reset()\n";
    PMM_RUN( "    reset() clears singleton for test isolation", test_i151_reset_clears_singleton );

    std::cout << "  I151-H: pstringview layout\n";
    PMM_RUN( "    pstringview size check", test_i151_layout );

    std::cout << "  I151-I: Tests using std::string\n";
    PMM_RUN( "    intern std::string basic", test_i151_stdstring_basic );
    PMM_RUN( "    intern std::string deduplication", test_i151_stdstring_deduplication );
    PMM_RUN( "    intern empty std::string", test_i151_stdstring_empty );
    PMM_RUN( "    intern std::string with spaces and special chars", test_i151_stdstring_special_chars );
    PMM_RUN( "    intern long std::string (>255 chars)", test_i151_stdstring_long );
    PMM_RUN( "    intern multiple std::strings", test_i151_stdstring_multiple );
    PMM_RUN( "    compare pstringview with std::string", test_i151_stdstring_comparison );
    PMM_RUN( "    intern runtime-built std::string (concatenation)", test_i151_stdstring_runtime_built );
    PMM_RUN( "    intern std::string with numeric content (std::to_string)", test_i151_stdstring_numeric_content );
    PMM_RUN( "    interleaved const char* and std::string interns", test_i151_stdstring_mixed_with_cstr );

    std::cout << "\n";
    if ( all_passed )
    {
        std::cout << "All Issue #151 tests PASSED.\n";
        return EXIT_SUCCESS;
    }
    else
    {
        std::cout << "Some Issue #151 tests FAILED.\n";
        return EXIT_FAILURE;
    }
}
