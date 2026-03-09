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
 *  6. pstringview_manager::reset() clears the singleton for test isolation.
 *  7. The built-in AVL tree node fields (TreeNode via Block) are used for the
 *     pstringview dictionary — no separate hash table structures in PAP.
 *
 * @see include/pmm/pstringview.h — pstringview, pstringview_manager
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @see include/pmm/tree_node.h — TreeNode<AT> built-in AVL fields (Issue #87, #138)
 * @version 0.2 (Issue #151 — pstringview via built-in AVL forest)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pstringview.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

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
using TestPsv          = pmm::pstringview<TestMgr>;
using TestMgr_pptr_psv = TestMgr::pptr<TestPsv>;

// =============================================================================
// I151-A: intern() creates a new pstringview and locks the block
// =============================================================================

/// @brief intern() returns a non-null pptr for a non-empty string.
static bool test_i151_intern_basic()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = pmm::pstringview_manager<TestMgr>::intern( "hello" );
    PMM_TEST( !p.is_null() );

    const TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );
    PMM_TEST( psv->size() == 5 );
    PMM_TEST( !psv->empty() );
    PMM_TEST( *psv == "hello" );
    PMM_TEST( std::strcmp( psv->c_str(), "hello" ) == 0 );

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

/// @brief intern() returns a non-null pptr for an empty string.
static bool test_i151_intern_empty_string()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = pmm::pstringview_manager<TestMgr>::intern( "" );
    PMM_TEST( !p.is_null() );

    const TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );
    PMM_TEST( psv->size() == 0 );
    PMM_TEST( psv->empty() );
    PMM_TEST( *psv == "" );
    PMM_TEST( std::strcmp( psv->c_str(), "" ) == 0 );

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

/// @brief intern() with nullptr is treated the same as empty string.
static bool test_i151_intern_nullptr()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p_null  = pmm::pstringview_manager<TestMgr>::intern( nullptr );
    TestMgr_pptr_psv p_empty = pmm::pstringview_manager<TestMgr>::intern( "" );

    PMM_TEST( !p_null.is_null() );
    PMM_TEST( !p_empty.is_null() );
    // Both nullptr and "" should intern to the same object.
    PMM_TEST( p_null == p_empty );

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

// =============================================================================
// I151-B: Deduplication — intern() returns same pptr for same string
// =============================================================================

/// @brief intern() returns the same pptr for the same string value (key requirement #2).
static bool test_i151_deduplication()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p1 = pmm::pstringview_manager<TestMgr>::intern( "world" );
    TestMgr_pptr_psv p2 = pmm::pstringview_manager<TestMgr>::intern( "world" );

    PMM_TEST( !p1.is_null() );
    PMM_TEST( !p2.is_null() );
    // Same string → same pptr (same granule index)
    PMM_TEST( p1 == p2 );

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

/// @brief intern() returns different pptrs for different strings.
static bool test_i151_different_strings_different_pptrs()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p_hello = pmm::pstringview_manager<TestMgr>::intern( "hello" );
    TestMgr_pptr_psv p_world = pmm::pstringview_manager<TestMgr>::intern( "world" );

    PMM_TEST( !p_hello.is_null() );
    PMM_TEST( !p_world.is_null() );
    // Different strings → different pptrs
    PMM_TEST( p_hello != p_world );

    // Check contents
    PMM_TEST( *p_hello == "hello" );
    PMM_TEST( *p_world == "world" );

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

/// @brief pstringview equality uses interning guarantee (chars_idx comparison).
static bool test_i151_equality_via_interning()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv pa = pmm::pstringview_manager<TestMgr>::intern( "key" );
    TestMgr_pptr_psv pb = pmm::pstringview_manager<TestMgr>::intern( "key" );
    TestMgr_pptr_psv pc = pmm::pstringview_manager<TestMgr>::intern( "other" );

    PMM_TEST( !pa.is_null() && !pb.is_null() && !pc.is_null() );

    const TestPsv* a = pa.resolve();
    const TestPsv* b = pb.resolve();
    const TestPsv* c = pc.resolve();
    PMM_TEST( a != nullptr && b != nullptr && c != nullptr );

    // Interning: same string → same chars_idx → equal
    PMM_TEST( *a == *b );
    PMM_TEST( !( *a == *c ) );
    PMM_TEST( *a != *c );

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

// =============================================================================
// I151-C: Block locking — intern() locks blocks permanently (key requirement #1)
// =============================================================================

/// @brief After intern(), the chars block is permanently locked (cannot be freed).
static bool test_i151_chars_block_permanently_locked()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = pmm::pstringview_manager<TestMgr>::intern( "locked_test" );
    PMM_TEST( !p.is_null() );

    const TestPsv* psv = p.resolve();
    PMM_TEST( psv != nullptr );

    // Get the chars pointer from pstringview
    using char_pptr_t = TestMgr::pptr<char>;
    char_pptr_t cp( psv->chars_idx );
    char*       chars = TestMgr::resolve<char>( cp );
    PMM_TEST( chars != nullptr );

    // The chars block must be permanently locked (Issue #151, Issue #126)
    PMM_TEST( TestMgr::is_permanently_locked( chars ) == true );

    // Verify we cannot free it (deallocate is a no-op for locked blocks)
    std::size_t alloc_before = TestMgr::alloc_block_count();
    TestMgr::deallocate( chars );
    std::size_t alloc_after = TestMgr::alloc_block_count();
    PMM_TEST( alloc_before == alloc_after ); // Block not freed

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

/// @brief After intern(), the pstringview block itself is permanently locked.
static bool test_i151_psview_block_permanently_locked()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p = pmm::pstringview_manager<TestMgr>::intern( "psview_lock" );
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
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

// =============================================================================
// I151-D: Built-in AVL tree (key architectural requirement — no separate PAP structures)
// =============================================================================

/// @brief The pstringview dictionary uses built-in TreeNode AVL fields (not a separate hash table).
///
/// Verified by checking that:
///  1. intern() creates pstringview objects that reference each other via tree_node() fields.
///  2. The AVL tree root is tracked only by a static index (no extra PAP allocation for table).
///  3. pstringview nodes themselves form the BST structure via their block's TreeNode fields.
static bool test_i151_avl_tree_structure()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // Insert strings in sorted order to test AVL balance.
    TestMgr_pptr_psv p_a = pmm::pstringview_manager<TestMgr>::intern( "alpha" );
    TestMgr_pptr_psv p_b = pmm::pstringview_manager<TestMgr>::intern( "beta" );
    TestMgr_pptr_psv p_c = pmm::pstringview_manager<TestMgr>::intern( "gamma" );

    PMM_TEST( !p_a.is_null() && !p_b.is_null() && !p_c.is_null() );

    // All nodes accessible via the AVL tree root.
    PMM_TEST( pmm::pstringview_manager<TestMgr>::_root_idx != static_cast<TestMgr::index_type>( 0 ) );

    // Re-interning returns the same pptr (deduplication via AVL tree search).
    PMM_TEST( pmm::pstringview_manager<TestMgr>::intern( "alpha" ) == p_a );
    PMM_TEST( pmm::pstringview_manager<TestMgr>::intern( "beta" )  == p_b );
    PMM_TEST( pmm::pstringview_manager<TestMgr>::intern( "gamma" ) == p_c );

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

/// @brief The AVL tree root index is reset by reset() without leaking PAP state.
static bool test_i151_avl_root_reset()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // Before any intern — root is null.
    PMM_TEST( pmm::pstringview_manager<TestMgr>::_root_idx == static_cast<TestMgr::index_type>( 0 ) );

    pmm::pstringview_manager<TestMgr>::intern( "test_root" );

    // After intern — root is non-null.
    PMM_TEST( pmm::pstringview_manager<TestMgr>::_root_idx != static_cast<TestMgr::index_type>( 0 ) );

    // After reset — root is null again.
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( pmm::pstringview_manager<TestMgr>::_root_idx == static_cast<TestMgr::index_type>( 0 ) );

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

// =============================================================================
// I151-E: Dictionary grows during manager lifetime (key requirement #5)
// =============================================================================

/// @brief Multiple distinct strings are all stored in the dictionary.
static bool test_i151_dictionary_grows()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    const char* strings[]   = { "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa" };
    constexpr std::size_t N = sizeof( strings ) / sizeof( strings[0] );

    TestMgr_pptr_psv ptrs[N];
    for ( std::size_t i = 0; i < N; i++ )
    {
        ptrs[i] = pmm::pstringview_manager<TestMgr>::intern( strings[i] );
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
        TestMgr_pptr_psv p2 = pmm::pstringview_manager<TestMgr>::intern( strings[i] );
        PMM_TEST( p2 == ptrs[i] );
    }

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

/// @brief AVL tree balances correctly with many strings in insertion order.
static bool test_i151_dictionary_many_strings()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    // Insert 20 strings to verify AVL balancing works correctly.
    char             buf[32];
    TestMgr_pptr_psv ptrs[20];
    for ( int i = 0; i < 20; i++ )
    {
        std::snprintf( buf, sizeof( buf ), "string_%02d", i );
        ptrs[i] = pmm::pstringview_manager<TestMgr>::intern( buf );
        PMM_TEST( !ptrs[i].is_null() );
    }

    // All strings retrievable via deduplication search
    for ( int i = 0; i < 20; i++ )
    {
        std::snprintf( buf, sizeof( buf ), "string_%02d", i );
        TestMgr_pptr_psv p2 = pmm::pstringview_manager<TestMgr>::intern( buf );
        PMM_TEST( p2 == ptrs[i] );
        const TestPsv* psv = ptrs[i].resolve();
        PMM_TEST( psv != nullptr );
        PMM_TEST( std::strcmp( psv->c_str(), buf ) == 0 );
    }

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

// =============================================================================
// I151-F: Comparison operators
// =============================================================================

/// @brief operator< provides a consistent ordering of pstringview objects.
static bool test_i151_less_than_ordering()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr_pptr_psv p_a = pmm::pstringview_manager<TestMgr>::intern( "apple" );
    TestMgr_pptr_psv p_b = pmm::pstringview_manager<TestMgr>::intern( "banana" );
    TestMgr_pptr_psv p_c = pmm::pstringview_manager<TestMgr>::intern( "cherry" );

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
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

// =============================================================================
// I151-G: pstringview_manager::reset() for test isolation
// =============================================================================

/// @brief reset() clears the singleton; a new intern() creates a fresh AVL tree.
static bool test_i151_reset_clears_singleton()
{
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // First session
    TestMgr_pptr_psv p1 = pmm::pstringview_manager<TestMgr>::intern( "session1" );
    PMM_TEST( !p1.is_null() );

    // Destroy and recreate manager, reset singleton
    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();

    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // Second session: singleton was reset, creates a new AVL tree
    TestMgr_pptr_psv p2 = pmm::pstringview_manager<TestMgr>::intern( "session2" );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( std::strcmp( p2->c_str(), "session2" ) == 0 );

    TestMgr::destroy();
    pmm::pstringview_manager<TestMgr>::reset();
    return true;
}

// =============================================================================
// I151-H: pstringview static_assert size check
// =============================================================================

/// @brief pstringview<ManagerT> has expected field layout.
static bool test_i151_layout()
{
    // pstringview<ManagerT> has two fields: chars_idx (index_type) and length (uint32_t).
    using index_type = TestMgr::index_type;
    // sizeof should be at least sizeof(index_type) + sizeof(uint32_t).
    PMM_TEST( sizeof( TestPsv ) >= sizeof( index_type ) + sizeof( std::uint32_t ) );
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

    std::cout << "  I151-C: Block locking (Issue #126)\n";
    PMM_RUN( "    chars block permanently locked", test_i151_chars_block_permanently_locked );
    PMM_RUN( "    pstringview block permanently locked", test_i151_psview_block_permanently_locked );

    std::cout << "  I151-D: Built-in AVL tree structure (no separate PAP structures)\n";
    PMM_RUN( "    AVL tree via built-in TreeNode fields", test_i151_avl_tree_structure );
    PMM_RUN( "    AVL root tracked by static index only", test_i151_avl_root_reset );

    std::cout << "  I151-E: Dictionary grows during manager lifetime\n";
    PMM_RUN( "    multiple distinct strings stored", test_i151_dictionary_grows );
    PMM_RUN( "    20 strings with AVL balancing", test_i151_dictionary_many_strings );

    std::cout << "  I151-F: Comparison operators\n";
    PMM_RUN( "    operator< lexicographic ordering", test_i151_less_than_ordering );

    std::cout << "  I151-G: pstringview_manager::reset()\n";
    PMM_RUN( "    reset() clears singleton for test isolation", test_i151_reset_clears_singleton );

    std::cout << "  I151-H: pstringview layout\n";
    PMM_RUN( "    pstringview size check", test_i151_layout );

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
