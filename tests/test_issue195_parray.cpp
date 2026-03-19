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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

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

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 195>;
using TestArr = TestMgr::parray<int>;

// =============================================================================
// I195-A: Basic creation and push_back
// =============================================================================

/// @brief create_typed<parray<int>>() creates an empty array.
static bool test_i195_create_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p = TestMgr::create_typed<TestArr>();
    PMM_TEST( !p.is_null() );

    TestArr* arr = p.resolve();
    PMM_TEST( arr != nullptr );
    PMM_TEST( arr->empty() );
    PMM_TEST( arr->size() == 0 );
    PMM_TEST( arr->capacity() == 0 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief push_back() adds elements to the array.
static bool test_i195_push_back_basic()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    PMM_TEST( arr->push_back( 10 ) );
    PMM_TEST( arr->push_back( 20 ) );
    PMM_TEST( arr->push_back( 30 ) );

    PMM_TEST( arr->size() == 3 );
    PMM_TEST( !arr->empty() );
    PMM_TEST( arr->capacity() >= 3 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief push_back() triggers reallocation when capacity is exceeded.
static bool test_i195_push_back_realloc()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    // Push 100 elements — must trigger multiple reallocations
    for ( int i = 0; i < 100; ++i )
    {
        PMM_TEST( arr->push_back( i * 10 ) );
    }

    PMM_TEST( arr->size() == 100 );
    PMM_TEST( arr->capacity() >= 100 );

    // Verify all elements retained their values
    for ( int i = 0; i < 100; ++i )
    {
        PMM_TEST( ( *arr )[static_cast<std::size_t>( i )] == i * 10 );
    }

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-B: O(1) random access — at() and operator[]
// =============================================================================

/// @brief at(i) returns pointer to i-th element (O(1)).
static bool test_i195_at()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    for ( int i = 0; i < 5; ++i )
        PMM_TEST( arr->push_back( i + 1 ) );

    // Valid access
    PMM_TEST( arr->at( 0 ) != nullptr );
    PMM_TEST( *arr->at( 0 ) == 1 );
    PMM_TEST( *arr->at( 1 ) == 2 );
    PMM_TEST( *arr->at( 2 ) == 3 );
    PMM_TEST( *arr->at( 3 ) == 4 );
    PMM_TEST( *arr->at( 4 ) == 5 );

    // Out-of-bounds access returns nullptr
    PMM_TEST( arr->at( 5 ) == nullptr );
    PMM_TEST( arr->at( 100 ) == nullptr );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief operator[] returns element by value (O(1)).
static bool test_i195_subscript()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    PMM_TEST( arr->push_back( 100 ) );
    PMM_TEST( arr->push_back( 200 ) );
    PMM_TEST( arr->push_back( 300 ) );

    PMM_TEST( ( *arr )[0] == 100 );
    PMM_TEST( ( *arr )[1] == 200 );
    PMM_TEST( ( *arr )[2] == 300 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief at() returns const pointer on const parray.
static bool test_i195_at_const()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    PMM_TEST( arr->push_back( 42 ) );

    // Access via const reference
    const TestArr& c_arr = *arr;
    const int*     elem  = c_arr.at( 0 );
    PMM_TEST( elem != nullptr );
    PMM_TEST( *elem == 42 );

    // Out of bounds on const
    PMM_TEST( c_arr.at( 1 ) == nullptr );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-C: pop_back()
// =============================================================================

/// @brief pop_back() removes the last element.
static bool test_i195_pop_back()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    PMM_TEST( arr->push_back( 1 ) );
    PMM_TEST( arr->push_back( 2 ) );
    PMM_TEST( arr->push_back( 3 ) );
    PMM_TEST( arr->size() == 3 );

    arr->pop_back();
    PMM_TEST( arr->size() == 2 );
    PMM_TEST( ( *arr )[0] == 1 );
    PMM_TEST( ( *arr )[1] == 2 );

    arr->pop_back();
    arr->pop_back();
    PMM_TEST( arr->empty() );

    // pop_back on empty — no-op
    arr->pop_back();
    PMM_TEST( arr->empty() );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-D: set()
// =============================================================================

/// @brief set(i, value) modifies element at index.
static bool test_i195_set()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    PMM_TEST( arr->push_back( 1 ) );
    PMM_TEST( arr->push_back( 2 ) );
    PMM_TEST( arr->push_back( 3 ) );

    PMM_TEST( arr->set( 1, 42 ) );
    PMM_TEST( ( *arr )[1] == 42 );

    // Out-of-bounds set returns false
    PMM_TEST( !arr->set( 5, 99 ) );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-E: reserve()
// =============================================================================

/// @brief reserve(n) pre-allocates capacity.
static bool test_i195_reserve()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    PMM_TEST( arr->reserve( 50 ) );
    PMM_TEST( arr->capacity() >= 50 );
    PMM_TEST( arr->size() == 0 );
    PMM_TEST( arr->empty() );

    // Push elements without triggering reallocation
    for ( int i = 0; i < 50; ++i )
        PMM_TEST( arr->push_back( i ) );

    PMM_TEST( arr->size() == 50 );

    // reserve with less than current capacity — no-op
    std::size_t cap_before = arr->capacity();
    PMM_TEST( arr->reserve( 10 ) );
    PMM_TEST( arr->capacity() == cap_before );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-F: resize()
// =============================================================================

/// @brief resize(n) changes size, zero-initializes new elements.
static bool test_i195_resize()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    // resize up — new elements are zero-initialized
    PMM_TEST( arr->resize( 5 ) );
    PMM_TEST( arr->size() == 5 );
    for ( std::size_t i = 0; i < 5; ++i )
        PMM_TEST( ( *arr )[i] == 0 );

    // Set some values
    PMM_TEST( arr->set( 0, 10 ) );
    PMM_TEST( arr->set( 1, 20 ) );
    PMM_TEST( arr->set( 2, 30 ) );

    // resize down — preserves existing elements
    PMM_TEST( arr->resize( 2 ) );
    PMM_TEST( arr->size() == 2 );
    PMM_TEST( ( *arr )[0] == 10 );
    PMM_TEST( ( *arr )[1] == 20 );

    // resize to 0
    PMM_TEST( arr->resize( 0 ) );
    PMM_TEST( arr->empty() );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-G: clear() and free_data()
// =============================================================================

/// @brief clear() sets size to 0 but preserves capacity.
static bool test_i195_clear()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    PMM_TEST( arr->push_back( 1 ) );
    PMM_TEST( arr->push_back( 2 ) );
    PMM_TEST( arr->push_back( 3 ) );

    std::size_t cap = arr->capacity();
    arr->clear();
    PMM_TEST( arr->empty() );
    PMM_TEST( arr->size() == 0 );
    PMM_TEST( arr->capacity() == cap ); // capacity preserved

    // Can push again without reallocation
    PMM_TEST( arr->push_back( 99 ) );
    PMM_TEST( ( *arr )[0] == 99 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief free_data() deallocates data block and resets to empty.
static bool test_i195_free_data()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    PMM_TEST( arr->push_back( 42 ) );
    PMM_TEST( arr->push_back( 43 ) );

    std::size_t alloc_before = TestMgr::alloc_block_count();
    arr->free_data();

    PMM_TEST( arr->empty() );
    PMM_TEST( arr->size() == 0 );
    PMM_TEST( arr->capacity() == 0 );

    std::size_t alloc_after = TestMgr::alloc_block_count();
    PMM_TEST( alloc_after < alloc_before ); // data block was freed

    // Can push again after free_data (allocates new buffer)
    PMM_TEST( arr->push_back( 100 ) );
    PMM_TEST( ( *arr )[0] == 100 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-H: front() / back() / data()
// =============================================================================

/// @brief front() and back() return pointers to first/last elements.
static bool test_i195_front_back()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    // Empty array
    PMM_TEST( arr->front() == nullptr );
    PMM_TEST( arr->back() == nullptr );

    PMM_TEST( arr->push_back( 10 ) );
    PMM_TEST( arr->push_back( 20 ) );
    PMM_TEST( arr->push_back( 30 ) );

    PMM_TEST( *arr->front() == 10 );
    PMM_TEST( *arr->back() == 30 );

    // Modify via front/back
    *arr->front() = 99;
    *arr->back()  = 77;
    PMM_TEST( ( *arr )[0] == 99 );
    PMM_TEST( ( *arr )[2] == 77 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

/// @brief data() returns raw pointer to underlying block.
static bool test_i195_data_ptr()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    PMM_TEST( arr->data() == nullptr ); // empty

    PMM_TEST( arr->push_back( 1 ) );
    PMM_TEST( arr->push_back( 2 ) );

    int* d = arr->data();
    PMM_TEST( d != nullptr );
    PMM_TEST( d[0] == 1 );
    PMM_TEST( d[1] == 2 );

    // Modify via data pointer
    d[0] = 100;
    PMM_TEST( ( *arr )[0] == 100 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-I: Comparison operators
// =============================================================================

/// @brief operator== and operator!= for parray.
static bool test_i195_comparison()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    using Arr             = TestMgr::parray<int>;
    TestMgr::pptr<Arr> pa = TestMgr::create_typed<Arr>();
    TestMgr::pptr<Arr> pb = TestMgr::create_typed<Arr>();

    Arr* a = pa.resolve();
    Arr* b = pb.resolve();

    // Both empty — equal
    PMM_TEST( *a == *b );
    PMM_TEST( !( *a != *b ) );

    // Same content — equal
    a->push_back( 1 );
    a->push_back( 2 );
    b->push_back( 1 );
    b->push_back( 2 );
    PMM_TEST( *a == *b );

    // Different content — not equal
    b->set( 1, 42 );
    PMM_TEST( *a != *b );

    // Different sizes — not equal
    a->push_back( 3 );
    PMM_TEST( *a != *b );

    // Self-comparison
    PMM_TEST( *a == *a );

    a->free_data();
    b->free_data();
    TestMgr::destroy_typed( pa );
    TestMgr::destroy_typed( pb );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-J: POD structure (trivially copyable)
// =============================================================================

/// @brief parray is trivially copyable for direct serialization in PAP.
static bool test_i195_trivially_copyable()
{
    PMM_TEST( std::is_trivially_copyable_v<TestArr> );
    PMM_TEST( std::is_nothrow_constructible_v<TestArr> );
    PMM_TEST( std::is_nothrow_destructible_v<TestArr> );
    return true;
}

/// @brief parray layout: fields are at expected positions.
static bool test_i195_layout()
{
    PMM_TEST( sizeof( TestArr ) >= sizeof( std::uint32_t ) * 2 + sizeof( TestMgr::index_type ) );
    PMM_TEST( offsetof( TestArr, _size ) == 0 );
    PMM_TEST( offsetof( TestArr, _capacity ) == sizeof( std::uint32_t ) );
    return true;
}

// =============================================================================
// I195-K: Large arrays
// =============================================================================

/// @brief parray with a large number of elements (> 1000).
static bool test_i195_large_array()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    constexpr int N = 2000;
    PMM_TEST( arr->reserve( N ) );

    for ( int i = 0; i < N; ++i )
        PMM_TEST( arr->push_back( i ) );

    PMM_TEST( arr->size() == N );

    for ( int i = 0; i < N; ++i )
        PMM_TEST( ( *arr )[static_cast<std::size_t>( i )] == i );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-L: Multiple arrays in same manager
// =============================================================================

/// @brief Multiple parray instances coexist independently.
static bool test_i195_multiple_arrays()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    constexpr int          N = 5;
    TestMgr::pptr<TestArr> ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = TestMgr::create_typed<TestArr>();
        PMM_TEST( !ptrs[i].is_null() );
        for ( int j = 0; j <= i; ++j )
            PMM_TEST( ptrs[i]->push_back( i * 10 + j ) );
    }

    // Verify independence
    for ( int i = 0; i < N; ++i )
    {
        PMM_TEST( ptrs[i]->size() == static_cast<std::size_t>( i + 1 ) );
        for ( int j = 0; j <= i; ++j )
            PMM_TEST( ( *ptrs[i].resolve() )[static_cast<std::size_t>( j )] == i * 10 + j );
    }

    // Modify one — others unaffected
    PMM_TEST( ptrs[2]->set( 0, 999 ) );
    PMM_TEST( ( *ptrs[0].resolve() )[0] == 0 );
    PMM_TEST( ( *ptrs[2].resolve() )[0] == 999 );

    for ( int i = 0; i < N; ++i )
    {
        ptrs[i]->free_data();
        TestMgr::destroy_typed( ptrs[i] );
    }
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-M: Different element types
// =============================================================================

/// @brief parray works with different trivially-copyable types.
static bool test_i195_different_types()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // parray<double>
    using DblArr             = TestMgr::parray<double>;
    TestMgr::pptr<DblArr> pd = TestMgr::create_typed<DblArr>();
    PMM_TEST( !pd.is_null() );

    pd->push_back( 3.14 );
    pd->push_back( 2.71 );
    PMM_TEST( pd->size() == 2 );
    PMM_TEST( *pd->at( 0 ) == 3.14 );
    PMM_TEST( *pd->at( 1 ) == 2.71 );

    pd->free_data();
    TestMgr::destroy_typed( pd );

    // parray<std::uint8_t>
    using ByteArr             = TestMgr::parray<std::uint8_t>;
    TestMgr::pptr<ByteArr> pb = TestMgr::create_typed<ByteArr>();
    PMM_TEST( !pb.is_null() );

    for ( int i = 0; i < 256; ++i )
        PMM_TEST( pb->push_back( static_cast<std::uint8_t>( i ) ) );

    PMM_TEST( pb->size() == 256 );
    PMM_TEST( *pb->at( 0 ) == 0 );
    PMM_TEST( *pb->at( 255 ) == 255 );

    pb->free_data();
    TestMgr::destroy_typed( pb );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-N: Manager alias
// =============================================================================

/// @brief Mgr::parray<T> alias works correctly.
static bool test_i195_manager_alias()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    // Use the Mgr::parray alias directly
    TestMgr::pptr<TestMgr::parray<int>> p = TestMgr::create_typed<TestMgr::parray<int>>();
    PMM_TEST( !p.is_null() );

    p->push_back( 42 );
    PMM_TEST( ( *p.resolve() )[0] == 42 );

    p->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I195-O: Modify via at() pointer
// =============================================================================

/// @brief Elements can be modified in-place via at() pointer.
static bool test_i195_modify_via_at()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    PMM_TEST( arr->push_back( 10 ) );
    PMM_TEST( arr->push_back( 20 ) );

    int* elem = arr->at( 0 );
    PMM_TEST( elem != nullptr );
    *elem = 99;
    PMM_TEST( ( *arr )[0] == 99 );
    PMM_TEST( ( *arr )[1] == 20 ); // unchanged

    arr->free_data();
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

    std::cout << "[Issue #195: parray — persistent array with O(1) indexing (Phase 3.2)]\n";

    std::cout << "  I195-A: Basic creation and push_back\n";
    PMM_RUN( "    create empty parray", test_i195_create_empty );
    PMM_RUN( "    push_back basic", test_i195_push_back_basic );
    PMM_RUN( "    push_back with reallocation", test_i195_push_back_realloc );

    std::cout << "  I195-B: O(1) random access\n";
    PMM_RUN( "    at(i) pointer access", test_i195_at );
    PMM_RUN( "    operator[] value access", test_i195_subscript );
    PMM_RUN( "    at(i) const access", test_i195_at_const );

    std::cout << "  I195-C: pop_back()\n";
    PMM_RUN( "    pop_back removes last element", test_i195_pop_back );

    std::cout << "  I195-D: set()\n";
    PMM_RUN( "    set modifies element at index", test_i195_set );

    std::cout << "  I195-E: reserve()\n";
    PMM_RUN( "    reserve pre-allocates capacity", test_i195_reserve );

    std::cout << "  I195-F: resize()\n";
    PMM_RUN( "    resize changes size", test_i195_resize );

    std::cout << "  I195-G: clear() and free_data()\n";
    PMM_RUN( "    clear preserves capacity", test_i195_clear );
    PMM_RUN( "    free_data deallocates block", test_i195_free_data );

    std::cout << "  I195-H: front() / back() / data()\n";
    PMM_RUN( "    front and back accessors", test_i195_front_back );
    PMM_RUN( "    data() raw pointer", test_i195_data_ptr );

    std::cout << "  I195-I: Comparison operators\n";
    PMM_RUN( "    equality and inequality", test_i195_comparison );

    std::cout << "  I195-J: POD structure\n";
    PMM_RUN( "    trivially copyable", test_i195_trivially_copyable );
    PMM_RUN( "    field layout", test_i195_layout );

    std::cout << "  I195-K: Large arrays\n";
    PMM_RUN( "    large array (2000 elements)", test_i195_large_array );

    std::cout << "  I195-L: Multiple arrays\n";
    PMM_RUN( "    multiple independent arrays", test_i195_multiple_arrays );

    std::cout << "  I195-M: Different types\n";
    PMM_RUN( "    double and uint8_t arrays", test_i195_different_types );

    std::cout << "  I195-N: Manager alias\n";
    PMM_RUN( "    Mgr::parray<T> alias works", test_i195_manager_alias );

    std::cout << "  I195-O: Modify via at()\n";
    PMM_RUN( "    modify element in-place", test_i195_modify_via_at );

    std::cout << "\n";
    if ( all_passed )
    {
        std::cout << "All Issue #195 parray tests PASSED.\n";
        return EXIT_SUCCESS;
    }
    else
    {
        std::cout << "Some Issue #195 parray tests FAILED.\n";
        return EXIT_FAILURE;
    }
}
