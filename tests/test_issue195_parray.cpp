/**
 * @file test_issue195_parray.cpp
 * @brief Tests for parray — persistent dynamic array with O(1) indexing (Issue #195, Phase 3.2).
 *
 * Verifies the key requirements from Issue #195 Phase 3.2:
 *  1. parray can be created in persistent address space via create_typed<parray<T>>().
 *  2. push_back() adds elements, with reallocation when capacity is exceeded.
 *  3. at(i) returns a pointer to the i-th element with O(1) access.
 *  4. operator[] returns element by value with O(1) access.
 *  5. size(), empty(), capacity() report correct state.
 *  6. pop_back() removes the last element.
 *  7. set(i, value) modifies element at index i.
 *  8. reserve(n) pre-allocates capacity.
 *  9. resize(n) changes the size with zero-initialization.
 * 10. clear() resets size without freeing data.
 * 11. free_data() deallocates the data block.
 * 12. Equality operators work correctly.
 * 13. front()/back()/data() accessors work correctly.
 * 14. parray is trivially copyable (POD).
 * 15. parray works with the manager alias (Mgr::parray<T>).
 *
 * @see include/pmm/parray.h — parray
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @version 0.1 (Issue #195 — Phase 3.2: persistent array with O(1) indexing)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/parray.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <string>

// --- Manager type alias for tests --------------------------------------------

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 195>;
using TestArr = TestMgr::parray<int>;

// =============================================================================
// I195-A: Basic creation and push_back
// =============================================================================

/// @brief create_typed<parray<int>>() creates an empty array.
TEST_CASE( "    create empty parray", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p = TestMgr::create_typed<TestArr>();
    REQUIRE( !p.is_null() );

    TestArr* arr = p.resolve();
    REQUIRE( arr != nullptr );
    REQUIRE( arr->empty() );
    REQUIRE( arr->size() == 0 );
    REQUIRE( arr->capacity() == 0 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief push_back() adds elements to the array.
TEST_CASE( "    push_back basic", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 10 ) );
    REQUIRE( arr->push_back( 20 ) );
    REQUIRE( arr->push_back( 30 ) );

    REQUIRE( arr->size() == 3 );
    REQUIRE( !arr->empty() );
    REQUIRE( arr->capacity() >= 3 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief push_back() triggers reallocation when capacity is exceeded.
TEST_CASE( "    push_back with reallocation", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    // Push 100 elements — must trigger multiple reallocations
    for ( int i = 0; i < 100; ++i )
    {
        REQUIRE( arr->push_back( i * 10 ) );
    }

    REQUIRE( arr->size() == 100 );
    REQUIRE( arr->capacity() >= 100 );

    // Verify all elements retained their values
    for ( int i = 0; i < 100; ++i )
    {
        REQUIRE( ( *arr )[static_cast<std::size_t>( i )] == i * 10 );
    }

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I195-B: O(1) random access — at() and operator[]
// =============================================================================

/// @brief at(i) returns pointer to i-th element (O(1)).
TEST_CASE( "    at(i) pointer access", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    for ( int i = 0; i < 5; ++i )
        REQUIRE( arr->push_back( i + 1 ) );

    // Valid access
    REQUIRE( arr->at( 0 ) != nullptr );
    REQUIRE( *arr->at( 0 ) == 1 );
    REQUIRE( *arr->at( 1 ) == 2 );
    REQUIRE( *arr->at( 2 ) == 3 );
    REQUIRE( *arr->at( 3 ) == 4 );
    REQUIRE( *arr->at( 4 ) == 5 );

    // Out-of-bounds access returns nullptr
    REQUIRE( arr->at( 5 ) == nullptr );
    REQUIRE( arr->at( 100 ) == nullptr );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief operator[] returns element by value (O(1)).
TEST_CASE( "    operator[] value access", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 100 ) );
    REQUIRE( arr->push_back( 200 ) );
    REQUIRE( arr->push_back( 300 ) );

    REQUIRE( ( *arr )[0] == 100 );
    REQUIRE( ( *arr )[1] == 200 );
    REQUIRE( ( *arr )[2] == 300 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief at() returns const pointer on const parray.
TEST_CASE( "    at(i) const access", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 42 ) );

    // Access via const reference
    const TestArr& c_arr = *arr;
    const int*     elem  = c_arr.at( 0 );
    REQUIRE( elem != nullptr );
    REQUIRE( *elem == 42 );

    // Out of bounds on const
    REQUIRE( c_arr.at( 1 ) == nullptr );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I195-C: pop_back()
// =============================================================================

/// @brief pop_back() removes the last element.
TEST_CASE( "    pop_back removes last element", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 1 ) );
    REQUIRE( arr->push_back( 2 ) );
    REQUIRE( arr->push_back( 3 ) );
    REQUIRE( arr->size() == 3 );

    arr->pop_back();
    REQUIRE( arr->size() == 2 );
    REQUIRE( ( *arr )[0] == 1 );
    REQUIRE( ( *arr )[1] == 2 );

    arr->pop_back();
    arr->pop_back();
    REQUIRE( arr->empty() );

    // pop_back on empty — no-op
    arr->pop_back();
    REQUIRE( arr->empty() );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I195-D: set()
// =============================================================================

/// @brief set(i, value) modifies element at index.
TEST_CASE( "    set modifies element at index", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 1 ) );
    REQUIRE( arr->push_back( 2 ) );
    REQUIRE( arr->push_back( 3 ) );

    REQUIRE( arr->set( 1, 42 ) );
    REQUIRE( ( *arr )[1] == 42 );

    // Out-of-bounds set returns false
    REQUIRE( !arr->set( 5, 99 ) );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I195-E: reserve()
// =============================================================================

/// @brief reserve(n) pre-allocates capacity.
TEST_CASE( "    reserve pre-allocates capacity", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->reserve( 50 ) );
    REQUIRE( arr->capacity() >= 50 );
    REQUIRE( arr->size() == 0 );
    REQUIRE( arr->empty() );

    // Push elements without triggering reallocation
    for ( int i = 0; i < 50; ++i )
        REQUIRE( arr->push_back( i ) );

    REQUIRE( arr->size() == 50 );

    // reserve with less than current capacity — no-op
    std::size_t cap_before = arr->capacity();
    REQUIRE( arr->reserve( 10 ) );
    REQUIRE( arr->capacity() == cap_before );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I195-F: resize()
// =============================================================================

/// @brief resize(n) changes size, zero-initializes new elements.
TEST_CASE( "    resize changes size", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    // resize up — new elements are zero-initialized
    REQUIRE( arr->resize( 5 ) );
    REQUIRE( arr->size() == 5 );
    for ( std::size_t i = 0; i < 5; ++i )
        REQUIRE( ( *arr )[i] == 0 );

    // Set some values
    REQUIRE( arr->set( 0, 10 ) );
    REQUIRE( arr->set( 1, 20 ) );
    REQUIRE( arr->set( 2, 30 ) );

    // resize down — preserves existing elements
    REQUIRE( arr->resize( 2 ) );
    REQUIRE( arr->size() == 2 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 20 );

    // resize to 0
    REQUIRE( arr->resize( 0 ) );
    REQUIRE( arr->empty() );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I195-G: clear() and free_data()
// =============================================================================

/// @brief clear() sets size to 0 but preserves capacity.
TEST_CASE( "    clear preserves capacity", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 1 ) );
    REQUIRE( arr->push_back( 2 ) );
    REQUIRE( arr->push_back( 3 ) );

    std::size_t cap = arr->capacity();
    arr->clear();
    REQUIRE( arr->empty() );
    REQUIRE( arr->size() == 0 );
    REQUIRE( arr->capacity() == cap ); // capacity preserved

    // Can push again without reallocation
    REQUIRE( arr->push_back( 99 ) );
    REQUIRE( ( *arr )[0] == 99 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief free_data() deallocates data block and resets to empty.
TEST_CASE( "    free_data deallocates block", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 42 ) );
    REQUIRE( arr->push_back( 43 ) );

    std::size_t alloc_before = TestMgr::alloc_block_count();
    arr->free_data();

    REQUIRE( arr->empty() );
    REQUIRE( arr->size() == 0 );
    REQUIRE( arr->capacity() == 0 );

    std::size_t alloc_after = TestMgr::alloc_block_count();
    REQUIRE( alloc_after < alloc_before ); // data block was freed

    // Can push again after free_data (allocates new buffer)
    REQUIRE( arr->push_back( 100 ) );
    REQUIRE( ( *arr )[0] == 100 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I195-H: front() / back() / data()
// =============================================================================

/// @brief front() and back() return pointers to first/last elements.
TEST_CASE( "    front and back accessors", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    // Empty array
    REQUIRE( arr->front() == nullptr );
    REQUIRE( arr->back() == nullptr );

    REQUIRE( arr->push_back( 10 ) );
    REQUIRE( arr->push_back( 20 ) );
    REQUIRE( arr->push_back( 30 ) );

    REQUIRE( *arr->front() == 10 );
    REQUIRE( *arr->back() == 30 );

    // Modify via front/back
    *arr->front() = 99;
    *arr->back()  = 77;
    REQUIRE( ( *arr )[0] == 99 );
    REQUIRE( ( *arr )[2] == 77 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

/// @brief data() returns raw pointer to underlying block.
TEST_CASE( "    data() raw pointer", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->data() == nullptr ); // empty

    REQUIRE( arr->push_back( 1 ) );
    REQUIRE( arr->push_back( 2 ) );

    int* d = arr->data();
    REQUIRE( d != nullptr );
    REQUIRE( d[0] == 1 );
    REQUIRE( d[1] == 2 );

    // Modify via data pointer
    d[0] = 100;
    REQUIRE( ( *arr )[0] == 100 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I195-I: Comparison operators
// =============================================================================

/// @brief operator== and operator!= for parray.
TEST_CASE( "    equality and inequality", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    using Arr             = TestMgr::parray<int>;
    TestMgr::pptr<Arr> pa = TestMgr::create_typed<Arr>();
    TestMgr::pptr<Arr> pb = TestMgr::create_typed<Arr>();

    Arr* a = pa.resolve();
    Arr* b = pb.resolve();

    // Both empty — equal
    REQUIRE( *a == *b );
    REQUIRE( !( *a != *b ) );

    // Same content — equal
    a->push_back( 1 );
    a->push_back( 2 );
    b->push_back( 1 );
    b->push_back( 2 );
    REQUIRE( *a == *b );

    // Different content — not equal
    b->set( 1, 42 );
    REQUIRE( *a != *b );

    // Different sizes — not equal
    a->push_back( 3 );
    REQUIRE( *a != *b );

    // Self-comparison
    REQUIRE( *a == *a );

    a->free_data();
    b->free_data();
    TestMgr::destroy_typed( pa );
    TestMgr::destroy_typed( pb );
    TestMgr::destroy();
}

// =============================================================================
// I195-J: POD structure (trivially copyable)
// =============================================================================

/// @brief parray is trivially copyable for direct serialization in PAP.
TEST_CASE( "    trivially copyable", "[test_issue195_parray]" )
{
    REQUIRE( std::is_trivially_copyable_v<TestArr> );
    REQUIRE( std::is_nothrow_constructible_v<TestArr> );
    REQUIRE( std::is_nothrow_destructible_v<TestArr> );
}

/// @brief parray layout: fields are at expected positions.
TEST_CASE( "    field layout", "[test_issue195_parray]" )
{
    REQUIRE( sizeof( TestArr ) >= sizeof( std::uint32_t ) * 2 + sizeof( TestMgr::index_type ) );
    REQUIRE( offsetof( TestArr, _size ) == 0 );
    REQUIRE( offsetof( TestArr, _capacity ) == sizeof( std::uint32_t ) );
}

// =============================================================================
// I195-K: Large arrays
// =============================================================================

/// @brief parray with a large number of elements (> 1000).
TEST_CASE( "    large array (2000 elements)", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    constexpr int N = 2000;
    REQUIRE( arr->reserve( N ) );

    for ( int i = 0; i < N; ++i )
        REQUIRE( arr->push_back( i ) );

    REQUIRE( arr->size() == N );

    for ( int i = 0; i < N; ++i )
        REQUIRE( ( *arr )[static_cast<std::size_t>( i )] == i );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I195-L: Multiple arrays in same manager
// =============================================================================

/// @brief Multiple parray instances coexist independently.
TEST_CASE( "    multiple independent arrays", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    constexpr int          N = 5;
    TestMgr::pptr<TestArr> ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = TestMgr::create_typed<TestArr>();
        REQUIRE( !ptrs[i].is_null() );
        for ( int j = 0; j <= i; ++j )
            REQUIRE( ptrs[i]->push_back( i * 10 + j ) );
    }

    // Verify independence
    for ( int i = 0; i < N; ++i )
    {
        REQUIRE( ptrs[i]->size() == static_cast<std::size_t>( i + 1 ) );
        for ( int j = 0; j <= i; ++j )
            REQUIRE( ( *ptrs[i].resolve() )[static_cast<std::size_t>( j )] == i * 10 + j );
    }

    // Modify one — others unaffected
    REQUIRE( ptrs[2]->set( 0, 999 ) );
    REQUIRE( ( *ptrs[0].resolve() )[0] == 0 );
    REQUIRE( ( *ptrs[2].resolve() )[0] == 999 );

    for ( int i = 0; i < N; ++i )
    {
        ptrs[i]->free_data();
        TestMgr::destroy_typed( ptrs[i] );
    }
    TestMgr::destroy();
}

// =============================================================================
// I195-M: Different element types
// =============================================================================

/// @brief parray works with different trivially-copyable types.
TEST_CASE( "    double and uint8_t arrays", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    // parray<double>
    using DblArr             = TestMgr::parray<double>;
    TestMgr::pptr<DblArr> pd = TestMgr::create_typed<DblArr>();
    REQUIRE( !pd.is_null() );

    pd->push_back( 3.14 );
    pd->push_back( 2.71 );
    REQUIRE( pd->size() == 2 );
    REQUIRE( *pd->at( 0 ) == 3.14 );
    REQUIRE( *pd->at( 1 ) == 2.71 );

    pd->free_data();
    TestMgr::destroy_typed( pd );

    // parray<std::uint8_t>
    using ByteArr             = TestMgr::parray<std::uint8_t>;
    TestMgr::pptr<ByteArr> pb = TestMgr::create_typed<ByteArr>();
    REQUIRE( !pb.is_null() );

    for ( int i = 0; i < 256; ++i )
        REQUIRE( pb->push_back( static_cast<std::uint8_t>( i ) ) );

    REQUIRE( pb->size() == 256 );
    REQUIRE( *pb->at( 0 ) == 0 );
    REQUIRE( *pb->at( 255 ) == 255 );

    pb->free_data();
    TestMgr::destroy_typed( pb );

    TestMgr::destroy();
}

// =============================================================================
// I195-N: Manager alias
// =============================================================================

/// @brief Mgr::parray<T> alias works correctly.
TEST_CASE( "    Mgr::parray<T> alias works", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    // Use the Mgr::parray alias directly
    TestMgr::pptr<TestMgr::parray<int>> p = TestMgr::create_typed<TestMgr::parray<int>>();
    REQUIRE( !p.is_null() );

    p->push_back( 42 );
    REQUIRE( ( *p.resolve() )[0] == 42 );

    p->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I195-O: Modify via at() pointer
// =============================================================================

/// @brief Elements can be modified in-place via at() pointer.
TEST_CASE( "    modify element in-place", "[test_issue195_parray]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 10 ) );
    REQUIRE( arr->push_back( 20 ) );

    int* elem = arr->at( 0 );
    REQUIRE( elem != nullptr );
    *elem = 99;
    REQUIRE( ( *arr )[0] == 99 );
    REQUIRE( ( *arr )[1] == 20 ); // unchanged

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// main
// =============================================================================
