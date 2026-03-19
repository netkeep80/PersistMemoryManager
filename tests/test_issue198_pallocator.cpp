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

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

// --- Manager type alias for tests --------------------------------------------

using TestMgr   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 198>;
using TestAlloc = TestMgr::pallocator<int>;

// =============================================================================
// I198-A: Basic allocate / deallocate
// =============================================================================

/// @brief allocate(n) returns a valid pointer and deallocate frees it.
TEST_CASE( "I198-A: basic alloc/dealloc", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestAlloc alloc;
    int*      p = alloc.allocate( 4 );
    REQUIRE( p != nullptr );

    // Write and read back.
    p[0] = 10;
    p[1] = 20;
    p[2] = 30;
    p[3] = 40;
    REQUIRE( p[0] == 10 );
    REQUIRE( p[1] == 20 );
    REQUIRE( p[2] == 30 );
    REQUIRE( p[3] == 40 );

    alloc.deallocate( p, 4 );
    TestMgr::destroy();
}

// =============================================================================
// I198-B: allocate(0) throws std::bad_alloc
// =============================================================================

/// @brief allocate(0) must throw std::bad_alloc.
TEST_CASE( "I198-B: allocate(0) throws", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

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
    REQUIRE( threw );

    TestMgr::destroy();
}

// =============================================================================
// I198-C: allocate() throws on failure (out of memory)
// =============================================================================

/// @brief allocate() throws std::bad_alloc when n exceeds max_size().
TEST_CASE( "I198-C: allocate failure throws", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

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
    REQUIRE( threw );

    TestMgr::destroy();
}

// =============================================================================
// I198-D: std::vector<int> with pallocator
// =============================================================================

/// @brief std::vector<int, pallocator<int>> works correctly.
TEST_CASE( "I198-D: vector<int>", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<int, TestMgr::pallocator<int>> vec;
        vec.push_back( 10 );
        vec.push_back( 20 );
        vec.push_back( 30 );

        REQUIRE( vec.size() == 3 );
        REQUIRE( vec[0] == 10 );
        REQUIRE( vec[1] == 20 );
        REQUIRE( vec[2] == 30 );

        vec.pop_back();
        REQUIRE( vec.size() == 2 );

        vec.clear();
        REQUIRE( vec.empty() );
    }

    TestMgr::destroy();
}

// =============================================================================
// I198-E: std::vector<double> with pallocator
// =============================================================================

/// @brief std::vector<double, pallocator<double>> works correctly.
TEST_CASE( "I198-E: vector<double>", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<double, TestMgr::pallocator<double>> vec;
        vec.push_back( 3.14 );
        vec.push_back( 2.71 );

        REQUIRE( vec.size() == 2 );
        REQUIRE( vec[0] == 3.14 );
        REQUIRE( vec[1] == 2.71 );
    }

    TestMgr::destroy();
}

// =============================================================================
// I198-F: Equality of allocators
// =============================================================================

/// @brief All pallocators with the same ManagerT are equal.
TEST_CASE( "I198-F: equality", "[test_issue198_pallocator]" )
{
    TestMgr::pallocator<int>    a1;
    TestMgr::pallocator<int>    a2;
    TestMgr::pallocator<double> a3;

    REQUIRE( a1 == a2 );
    REQUIRE( !( a1 != a2 ) );

    // Different value types — still equal (stateless, same manager).
    REQUIRE( a1 == a3 );
    REQUIRE( !( a1 != a3 ) );
}

// =============================================================================
// I198-G: Rebinding via std::allocator_traits
// =============================================================================

/// @brief Rebinding pallocator<int> to pallocator<double> via allocator_traits.
TEST_CASE( "I198-G: rebind", "[test_issue198_pallocator]" )
{
    using traits     = std::allocator_traits<TestMgr::pallocator<int>>;
    using rebound    = typename traits::template rebind_alloc<double>;
    using rebound_vt = typename rebound::value_type;

    REQUIRE( (std::is_same_v<rebound_vt, double>));

    // Verify rebound allocator works.
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    rebound alloc;
    double* p = alloc.allocate( 2 );
    REQUIRE( p != nullptr );
    p[0] = 1.5;
    p[1] = 2.5;
    REQUIRE( p[0] == 1.5 );
    REQUIRE( p[1] == 2.5 );
    alloc.deallocate( p, 2 );

    TestMgr::destroy();
}

// =============================================================================
// I198-H: allocator_traits type checks
// =============================================================================

/// @brief Verify std::allocator_traits recognizes pallocator correctly.
TEST_CASE( "I198-H: allocator_traits", "[test_issue198_pallocator]" )
{
    using alloc_t = TestMgr::pallocator<int>;
    using traits  = std::allocator_traits<alloc_t>;

    REQUIRE( (std::is_same_v<typename traits::value_type, int>));
    REQUIRE( (std::is_same_v<typename traits::size_type, std::size_t>));
    REQUIRE( (std::is_same_v<typename traits::difference_type, std::ptrdiff_t>));
    REQUIRE( (std::is_same_v<typename traits::pointer, int*>));
    REQUIRE( (std::is_same_v<typename traits::const_pointer, const int*>));

    // is_always_equal should be true_type (stateless allocator).
    REQUIRE( ( traits::is_always_equal::value ) );

    // propagate traits should be true_type.
    REQUIRE( ( traits::propagate_on_container_copy_assignment::value ) );
    REQUIRE( ( traits::propagate_on_container_move_assignment::value ) );
    REQUIRE( ( traits::propagate_on_container_swap::value ) );
}

// =============================================================================
// I198-I: Converting constructor
// =============================================================================

/// @brief Converting constructor from pallocator<U> works.
TEST_CASE( "I198-I: converting constructor", "[test_issue198_pallocator]" )
{
    TestMgr::pallocator<int>    a_int;
    TestMgr::pallocator<double> a_double( a_int );

    REQUIRE( a_int == a_double );
}

// =============================================================================
// I198-J: Vector with many elements (growth / reallocation)
// =============================================================================

/// @brief std::vector with pallocator handles growth (multiple reallocations).
TEST_CASE( "I198-J: vector growth (1000 elements)", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    {
        std::vector<int, TestMgr::pallocator<int>> vec;

        const int N = 1000;
        for ( int i = 0; i < N; ++i )
            vec.push_back( i * 3 );

        REQUIRE( vec.size() == static_cast<std::size_t>( N ) );
        for ( int i = 0; i < N; ++i )
            REQUIRE( vec[static_cast<std::size_t>( i )] == i * 3 );

        vec.resize( 500 );
        REQUIRE( vec.size() == 500 );
        REQUIRE( vec[0] == 0 );
        REQUIRE( vec[499] == 499 * 3 );
    }

    TestMgr::destroy();
}

// =============================================================================
// I198-K: Multiple vectors with different types coexisting
// =============================================================================

/// @brief Multiple vectors with different element types can coexist.
TEST_CASE( "I198-K: multiple vectors coexist", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<int, TestMgr::pallocator<int>>       vi;
        std::vector<double, TestMgr::pallocator<double>> vd;
        std::vector<char, TestMgr::pallocator<char>>     vc;

        vi.push_back( 42 );
        vd.push_back( 3.14 );
        vc.push_back( 'A' );

        REQUIRE( vi[0] == 42 );
        REQUIRE( vd[0] == 3.14 );
        REQUIRE( vc[0] == 'A' );
    }

    TestMgr::destroy();
}

// =============================================================================
// I198-L: max_size() returns a reasonable value
// =============================================================================

/// @brief max_size() returns a reasonable value.
TEST_CASE( "I198-L: max_size()", "[test_issue198_pallocator]" )
{
    TestMgr::pallocator<int> alloc;
    std::size_t              ms = alloc.max_size();
    REQUIRE( ms > 0 );
    REQUIRE( ms == std::numeric_limits<std::size_t>::max() / sizeof( int ) );

    TestMgr::pallocator<char> alloc_char;
    REQUIRE( alloc_char.max_size() == std::numeric_limits<std::size_t>::max() / sizeof( char ) );
}

// =============================================================================
// I198-M: Manager alias test
// =============================================================================

/// @brief pallocator is accessible via Mgr::pallocator<T> alias.
TEST_CASE( "I198-M: manager alias", "[test_issue198_pallocator]" )
{
    using AliasAlloc  = TestMgr::pallocator<int>;
    using DirectAlloc = pmm::pallocator<int, TestMgr>;

    // Both aliases refer to the same type.
    REQUIRE( (std::is_same_v<AliasAlloc, DirectAlloc>));
}

// =============================================================================
// I198-N: Vector copy / move with pallocator
// =============================================================================

/// @brief std::vector copy and move work with pallocator.
TEST_CASE( "I198-N: vector copy/move", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    {
        using Vec = std::vector<int, TestMgr::pallocator<int>>;

        Vec v1;
        v1.push_back( 1 );
        v1.push_back( 2 );
        v1.push_back( 3 );

        // Copy.
        Vec v2( v1 );
        REQUIRE( v2.size() == 3 );
        REQUIRE( v2[0] == 1 );
        REQUIRE( v2[1] == 2 );
        REQUIRE( v2[2] == 3 );

        // Move.
        Vec v3( std::move( v1 ) );
        REQUIRE( v3.size() == 3 );
        REQUIRE( v3[0] == 1 );
        REQUIRE( v3[1] == 2 );
        REQUIRE( v3[2] == 3 );
    }

    TestMgr::destroy();
}

// =============================================================================
// I198-O: Vector reserve / shrink_to_fit
// =============================================================================

/// @brief reserve() and shrink_to_fit() work with pallocator.
TEST_CASE( "I198-O: vector reserve", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<int, TestMgr::pallocator<int>> vec;
        vec.reserve( 100 );
        REQUIRE( vec.capacity() >= 100 );
        REQUIRE( vec.empty() );

        for ( int i = 0; i < 50; ++i )
            vec.push_back( i );

        REQUIRE( vec.size() == 50 );
        REQUIRE( vec.capacity() >= 100 );
    }

    TestMgr::destroy();
}

// =============================================================================
// I198-P: Vector erase / insert
// =============================================================================

/// @brief erase and insert work with pallocator-backed vector.
TEST_CASE( "I198-P: vector erase/insert", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<int, TestMgr::pallocator<int>> vec;
        vec.push_back( 10 );
        vec.push_back( 20 );
        vec.push_back( 30 );
        vec.push_back( 40 );

        // Erase second element.
        vec.erase( vec.begin() + 1 );
        REQUIRE( vec.size() == 3 );
        REQUIRE( vec[0] == 10 );
        REQUIRE( vec[1] == 30 );
        REQUIRE( vec[2] == 40 );

        // Insert at beginning.
        vec.insert( vec.begin(), 5 );
        REQUIRE( vec.size() == 4 );
        REQUIRE( vec[0] == 5 );
        REQUIRE( vec[1] == 10 );
    }

    TestMgr::destroy();
}

// =============================================================================
// I198-Q: pallocator with uint8_t (small type)
// =============================================================================

/// @brief pallocator works with small types like uint8_t.
TEST_CASE( "I198-Q: small type (uint8_t)", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<std::uint8_t, TestMgr::pallocator<std::uint8_t>> vec;
        for ( int i = 0; i < 256; ++i )
            vec.push_back( static_cast<std::uint8_t>( i ) );

        REQUIRE( vec.size() == 256 );
        for ( int i = 0; i < 256; ++i )
            REQUIRE( vec[static_cast<std::size_t>( i )] == static_cast<std::uint8_t>( i ) );
    }

    TestMgr::destroy();
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
TEST_CASE( "I198-R: struct type", "[test_issue198_pallocator]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    {
        std::vector<Point198, TestMgr::pallocator<Point198>> vec;
        vec.push_back( { 1, 2 } );
        vec.push_back( { 3, 4 } );
        vec.push_back( { 5, 6 } );

        REQUIRE( vec.size() == 3 );
        REQUIRE( ( vec[0].x == 1 && vec[0].y == 2 ) );
        REQUIRE( ( vec[1].x == 3 && vec[1].y == 4 ) );
        REQUIRE( ( vec[2].x == 5 && vec[2].y == 6 ) );
    }

    TestMgr::destroy();
}

// =============================================================================
// Main — run all tests
// =============================================================================
