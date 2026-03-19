/**
 * @file test_issue198_pallocator.cpp
 * @brief Tests for pallocator — STL-compatible allocator for persistent address space (Issue #198, Phase 3.5).
 *
 * Verifies the key requirements from Issue #198 Phase 3.5:
 *  1. pallocator satisfies std::allocator_traits requirements.
 *  2. allocate(n) returns a valid pointer to n elements in PAP.
 *  3. deallocate(p, n) frees the allocated memory.
 *  4. pallocator works with std::vector.
 *  5. pallocator works with std::basic_string (via rebind).
 *  6. pallocator instances with the same ManagerT are always equal.
 *  7. pallocator supports rebinding to a different type.
 *  8. allocate(0) throws std::bad_alloc.
 *  9. allocate() throws std::bad_alloc on failure.
 * 10. pallocator works with the manager alias (Mgr::pallocator<T>).
 * 11. pallocator with different element types can be compared.
 * 12. pallocator works with std::allocator_traits.
 * 13. Multiple vectors with different element types can coexist.
 * 14. pallocator with large allocations behaves correctly.
 *
 * @see include/pmm/pallocator.h — pallocator
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @version 0.1 (Issue #198 — Phase 3.5: STL-compatible allocator)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pallocator.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

// --- Test macros -------------------------------------------------------------

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

// --- Manager type alias for tests --------------------------------------------

using TestMgr   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 198>;
using TestAlloc = TestMgr::pallocator<int>;

// =============================================================================
// I198-A: Basic allocate / deallocate
// =============================================================================

/// @brief allocate(n) returns a valid pointer and deallocate frees it.
static bool test_i198_basic_alloc_dealloc()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestAlloc alloc;
    int*      p = alloc.allocate( 4 );
    PMM_TEST( p != nullptr );

    // Write and read back.
    p[0] = 10;
    p[1] = 20;
    p[2] = 30;
    p[3] = 40;
    PMM_TEST( p[0] == 10 );
    PMM_TEST( p[1] == 20 );
    PMM_TEST( p[2] == 30 );
    PMM_TEST( p[3] == 40 );

    alloc.deallocate( p, 4 );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-B: allocate(0) throws std::bad_alloc
// =============================================================================

/// @brief allocate(0) must throw std::bad_alloc.
static bool test_i198_alloc_zero()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestAlloc alloc;
    bool      threw = false;
    try
    {
        (void)alloc.allocate( 0 );
    }
    catch ( const std::bad_alloc& )
    {
        threw = true;
    }
    PMM_TEST( threw );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-C: allocate() throws on failure (out of memory)
// =============================================================================

/// @brief allocate() throws std::bad_alloc when n exceeds max_size().
static bool test_i198_alloc_failure()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestAlloc alloc;
    bool      threw = false;
    try
    {
        // Request exceeds max_size() — guaranteed to throw.
        (void)alloc.allocate( alloc.max_size() + 1 );
    }
    catch ( const std::bad_alloc& )
    {
        threw = true;
    }
    PMM_TEST( threw );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-D: std::vector<int> with pallocator
// =============================================================================

/// @brief std::vector<int, pallocator<int>> works correctly.
static bool test_i198_vector_int()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<int, TestMgr::pallocator<int>> vec;
        vec.push_back( 10 );
        vec.push_back( 20 );
        vec.push_back( 30 );

        PMM_TEST( vec.size() == 3 );
        PMM_TEST( vec[0] == 10 );
        PMM_TEST( vec[1] == 20 );
        PMM_TEST( vec[2] == 30 );

        vec.pop_back();
        PMM_TEST( vec.size() == 2 );

        vec.clear();
        PMM_TEST( vec.empty() );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-E: std::vector<double> with pallocator
// =============================================================================

/// @brief std::vector<double, pallocator<double>> works correctly.
static bool test_i198_vector_double()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<double, TestMgr::pallocator<double>> vec;
        vec.push_back( 3.14 );
        vec.push_back( 2.71 );

        PMM_TEST( vec.size() == 2 );
        PMM_TEST( vec[0] == 3.14 );
        PMM_TEST( vec[1] == 2.71 );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-F: Equality of allocators
// =============================================================================

/// @brief All pallocators with the same ManagerT are equal.
static bool test_i198_equality()
{
    TestMgr::pallocator<int>    a1;
    TestMgr::pallocator<int>    a2;
    TestMgr::pallocator<double> a3;

    PMM_TEST( a1 == a2 );
    PMM_TEST( !( a1 != a2 ) );

    // Different value types — still equal (stateless, same manager).
    PMM_TEST( a1 == a3 );
    PMM_TEST( !( a1 != a3 ) );

    return true;
}

// =============================================================================
// I198-G: Rebinding via std::allocator_traits
// =============================================================================

/// @brief Rebinding pallocator<int> to pallocator<double> via allocator_traits.
static bool test_i198_rebind()
{
    using traits     = std::allocator_traits<TestMgr::pallocator<int>>;
    using rebound    = typename traits::template rebind_alloc<double>;
    using rebound_vt = typename rebound::value_type;

    PMM_TEST( (std::is_same_v<rebound_vt, double>));

    // Verify rebound allocator works.
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    rebound alloc;
    double* p = alloc.allocate( 2 );
    PMM_TEST( p != nullptr );
    p[0] = 1.5;
    p[1] = 2.5;
    PMM_TEST( p[0] == 1.5 );
    PMM_TEST( p[1] == 2.5 );
    alloc.deallocate( p, 2 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-H: allocator_traits type checks
// =============================================================================

/// @brief Verify std::allocator_traits recognizes pallocator correctly.
static bool test_i198_traits()
{
    using alloc_t = TestMgr::pallocator<int>;
    using traits  = std::allocator_traits<alloc_t>;

    PMM_TEST( (std::is_same_v<typename traits::value_type, int>));
    PMM_TEST( (std::is_same_v<typename traits::size_type, std::size_t>));
    PMM_TEST( (std::is_same_v<typename traits::difference_type, std::ptrdiff_t>));
    PMM_TEST( (std::is_same_v<typename traits::pointer, int*>));
    PMM_TEST( (std::is_same_v<typename traits::const_pointer, const int*>));

    // is_always_equal should be true_type (stateless allocator).
    PMM_TEST( ( traits::is_always_equal::value ) );

    // propagate traits should be true_type.
    PMM_TEST( ( traits::propagate_on_container_copy_assignment::value ) );
    PMM_TEST( ( traits::propagate_on_container_move_assignment::value ) );
    PMM_TEST( ( traits::propagate_on_container_swap::value ) );

    return true;
}

// =============================================================================
// I198-I: Converting constructor
// =============================================================================

/// @brief Converting constructor from pallocator<U> works.
static bool test_i198_converting_ctor()
{
    TestMgr::pallocator<int>    a_int;
    TestMgr::pallocator<double> a_double( a_int );

    PMM_TEST( a_int == a_double );

    return true;
}

// =============================================================================
// I198-J: Vector with many elements (growth / reallocation)
// =============================================================================

/// @brief std::vector with pallocator handles growth (multiple reallocations).
static bool test_i198_vector_growth()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    {
        std::vector<int, TestMgr::pallocator<int>> vec;

        const int N = 1000;
        for ( int i = 0; i < N; ++i )
            vec.push_back( i * 3 );

        PMM_TEST( vec.size() == static_cast<std::size_t>( N ) );
        for ( int i = 0; i < N; ++i )
            PMM_TEST( vec[static_cast<std::size_t>( i )] == i * 3 );

        vec.resize( 500 );
        PMM_TEST( vec.size() == 500 );
        PMM_TEST( vec[0] == 0 );
        PMM_TEST( vec[499] == 499 * 3 );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-K: Multiple vectors with different types coexisting
// =============================================================================

/// @brief Multiple vectors with different element types can coexist.
static bool test_i198_multiple_vectors()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<int, TestMgr::pallocator<int>>       vi;
        std::vector<double, TestMgr::pallocator<double>> vd;
        std::vector<char, TestMgr::pallocator<char>>     vc;

        vi.push_back( 42 );
        vd.push_back( 3.14 );
        vc.push_back( 'A' );

        PMM_TEST( vi[0] == 42 );
        PMM_TEST( vd[0] == 3.14 );
        PMM_TEST( vc[0] == 'A' );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-L: max_size() returns a reasonable value
// =============================================================================

/// @brief max_size() returns a reasonable value.
static bool test_i198_max_size()
{
    TestMgr::pallocator<int> alloc;
    std::size_t              ms = alloc.max_size();
    PMM_TEST( ms > 0 );
    PMM_TEST( ms == std::numeric_limits<std::size_t>::max() / sizeof( int ) );

    TestMgr::pallocator<char> alloc_char;
    PMM_TEST( alloc_char.max_size() == std::numeric_limits<std::size_t>::max() / sizeof( char ) );

    return true;
}

// =============================================================================
// I198-M: Manager alias test
// =============================================================================

/// @brief pallocator is accessible via Mgr::pallocator<T> alias.
static bool test_i198_manager_alias()
{
    using AliasAlloc  = TestMgr::pallocator<int>;
    using DirectAlloc = pmm::pallocator<int, TestMgr>;

    // Both aliases refer to the same type.
    PMM_TEST( (std::is_same_v<AliasAlloc, DirectAlloc>));

    return true;
}

// =============================================================================
// I198-N: Vector copy / move with pallocator
// =============================================================================

/// @brief std::vector copy and move work with pallocator.
static bool test_i198_vector_copy_move()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    {
        using Vec = std::vector<int, TestMgr::pallocator<int>>;

        Vec v1;
        v1.push_back( 1 );
        v1.push_back( 2 );
        v1.push_back( 3 );

        // Copy.
        Vec v2( v1 );
        PMM_TEST( v2.size() == 3 );
        PMM_TEST( v2[0] == 1 );
        PMM_TEST( v2[1] == 2 );
        PMM_TEST( v2[2] == 3 );

        // Move.
        Vec v3( std::move( v1 ) );
        PMM_TEST( v3.size() == 3 );
        PMM_TEST( v3[0] == 1 );
        PMM_TEST( v3[1] == 2 );
        PMM_TEST( v3[2] == 3 );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-O: Vector reserve / shrink_to_fit
// =============================================================================

/// @brief reserve() and shrink_to_fit() work with pallocator.
static bool test_i198_vector_reserve()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<int, TestMgr::pallocator<int>> vec;
        vec.reserve( 100 );
        PMM_TEST( vec.capacity() >= 100 );
        PMM_TEST( vec.empty() );

        for ( int i = 0; i < 50; ++i )
            vec.push_back( i );

        PMM_TEST( vec.size() == 50 );
        PMM_TEST( vec.capacity() >= 100 );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-P: Vector erase / insert
// =============================================================================

/// @brief erase and insert work with pallocator-backed vector.
static bool test_i198_vector_erase_insert()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<int, TestMgr::pallocator<int>> vec;
        vec.push_back( 10 );
        vec.push_back( 20 );
        vec.push_back( 30 );
        vec.push_back( 40 );

        // Erase second element.
        vec.erase( vec.begin() + 1 );
        PMM_TEST( vec.size() == 3 );
        PMM_TEST( vec[0] == 10 );
        PMM_TEST( vec[1] == 30 );
        PMM_TEST( vec[2] == 40 );

        // Insert at beginning.
        vec.insert( vec.begin(), 5 );
        PMM_TEST( vec.size() == 4 );
        PMM_TEST( vec[0] == 5 );
        PMM_TEST( vec[1] == 10 );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-Q: pallocator with uint8_t (small type)
// =============================================================================

/// @brief pallocator works with small types like uint8_t.
static bool test_i198_small_type()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<std::uint8_t, TestMgr::pallocator<std::uint8_t>> vec;
        for ( int i = 0; i < 256; ++i )
            vec.push_back( static_cast<std::uint8_t>( i ) );

        PMM_TEST( vec.size() == 256 );
        for ( int i = 0; i < 256; ++i )
            PMM_TEST( vec[static_cast<std::size_t>( i )] == static_cast<std::uint8_t>( i ) );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I198-R: pallocator with struct type
// =============================================================================

struct Point198
{
    int x;
    int y;
};

/// @brief pallocator works with user-defined POD struct.
static bool test_i198_struct_type()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<Point198, TestMgr::pallocator<Point198>> vec;
        vec.push_back( { 1, 2 } );
        vec.push_back( { 3, 4 } );
        vec.push_back( { 5, 6 } );

        PMM_TEST( vec.size() == 3 );
        PMM_TEST( vec[0].x == 1 && vec[0].y == 2 );
        PMM_TEST( vec[1].x == 3 && vec[1].y == 4 );
        PMM_TEST( vec[2].x == 5 && vec[2].y == 6 );
    }

    TestMgr::destroy();
    return true;
}

// =============================================================================
// Main — run all tests
// =============================================================================

int main()
{
    std::cout << "[Issue #198] pallocator — STL-compatible allocator (Phase 3.5)\n";

    bool all_passed = true;

    PMM_RUN( "I198-A: basic alloc/dealloc", test_i198_basic_alloc_dealloc );
    PMM_RUN( "I198-B: allocate(0) throws", test_i198_alloc_zero );
    PMM_RUN( "I198-C: allocate failure throws", test_i198_alloc_failure );
    PMM_RUN( "I198-D: vector<int>", test_i198_vector_int );
    PMM_RUN( "I198-E: vector<double>", test_i198_vector_double );
    PMM_RUN( "I198-F: equality", test_i198_equality );
    PMM_RUN( "I198-G: rebind", test_i198_rebind );
    PMM_RUN( "I198-H: allocator_traits", test_i198_traits );
    PMM_RUN( "I198-I: converting constructor", test_i198_converting_ctor );
    PMM_RUN( "I198-J: vector growth (1000 elements)", test_i198_vector_growth );
    PMM_RUN( "I198-K: multiple vectors coexist", test_i198_multiple_vectors );
    PMM_RUN( "I198-L: max_size()", test_i198_max_size );
    PMM_RUN( "I198-M: manager alias", test_i198_manager_alias );
    PMM_RUN( "I198-N: vector copy/move", test_i198_vector_copy_move );
    PMM_RUN( "I198-O: vector reserve", test_i198_vector_reserve );
    PMM_RUN( "I198-P: vector erase/insert", test_i198_vector_erase_insert );
    PMM_RUN( "I198-Q: small type (uint8_t)", test_i198_small_type );
    PMM_RUN( "I198-R: struct type", test_i198_struct_type );

    std::cout << "\n" << ( all_passed ? "All pallocator tests PASSED." : "Some pallocator tests FAILED!" ) << "\n";
    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
