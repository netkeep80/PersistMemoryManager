/**
 * @file test_issue233_parray_insert_erase.cpp
 * @brief Tests for parray::insert(index, value) and parray::erase(index).
 *
 * Verifies the new parray methods:
 *  1. insert(index, value) inserts at the given position, shifting elements right.
 *  2. insert(size(), value) behaves like push_back().
 *  3. insert(0, value) inserts at the beginning.
 *  4. insert returns false for out-of-range index.
 *  5. erase(index) removes element at position, shifting elements left.
 *  6. erase returns false for out-of-range index.
 *  7. insert and erase work correctly together (sorted array pattern).
 *  8. insert and erase on empty/single-element arrays.
 *
 * @see include/pmm/parray.h — parray
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/parray.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

// --- Manager type alias for tests --------------------------------------------

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 233>;
using TestArr = TestMgr::parray<int>;

// =============================================================================
// I233-A: insert at various positions
// =============================================================================

TEST_CASE( "I233-A1: insert at end behaves like push_back", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->insert( 0, 10 ) );
    REQUIRE( arr->insert( 1, 20 ) );
    REQUIRE( arr->insert( 2, 30 ) );

    REQUIRE( arr->size() == 3 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 20 );
    REQUIRE( ( *arr )[2] == 30 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I233-A2: insert at beginning shifts elements", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 20 ) );
    REQUIRE( arr->push_back( 30 ) );
    REQUIRE( arr->push_back( 40 ) );

    // Insert at the beginning
    REQUIRE( arr->insert( 0, 10 ) );

    REQUIRE( arr->size() == 4 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 20 );
    REQUIRE( ( *arr )[2] == 30 );
    REQUIRE( ( *arr )[3] == 40 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I233-A3: insert in the middle", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 10 ) );
    REQUIRE( arr->push_back( 30 ) );
    REQUIRE( arr->push_back( 40 ) );

    // Insert in the middle
    REQUIRE( arr->insert( 1, 20 ) );

    REQUIRE( arr->size() == 4 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 20 );
    REQUIRE( ( *arr )[2] == 30 );
    REQUIRE( ( *arr )[3] == 40 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I233-A4: insert out of range returns false", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 10 ) );
    REQUIRE( arr->push_back( 20 ) );

    // Index 3 is out of range (size == 2, valid indices are 0..2)
    REQUIRE_FALSE( arr->insert( 3, 99 ) );
    REQUIRE( arr->size() == 2 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I233-A5: insert into empty array", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->insert( 0, 42 ) );
    REQUIRE( arr->size() == 1 );
    REQUIRE( ( *arr )[0] == 42 );

    // Out of range on empty-after-erase
    REQUIRE_FALSE( arr->insert( 2, 99 ) );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I233-B: erase at various positions
// =============================================================================

TEST_CASE( "I233-B1: erase last element", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 10 ) );
    REQUIRE( arr->push_back( 20 ) );
    REQUIRE( arr->push_back( 30 ) );

    REQUIRE( arr->erase( 2 ) );

    REQUIRE( arr->size() == 2 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 20 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I233-B2: erase first element shifts others left", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 10 ) );
    REQUIRE( arr->push_back( 20 ) );
    REQUIRE( arr->push_back( 30 ) );

    REQUIRE( arr->erase( 0 ) );

    REQUIRE( arr->size() == 2 );
    REQUIRE( ( *arr )[0] == 20 );
    REQUIRE( ( *arr )[1] == 30 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I233-B3: erase middle element", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 10 ) );
    REQUIRE( arr->push_back( 20 ) );
    REQUIRE( arr->push_back( 30 ) );

    REQUIRE( arr->erase( 1 ) );

    REQUIRE( arr->size() == 2 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 30 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I233-B4: erase out of range returns false", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 10 ) );

    REQUIRE_FALSE( arr->erase( 1 ) );
    REQUIRE_FALSE( arr->erase( 100 ) );
    REQUIRE( arr->size() == 1 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I233-B5: erase on empty array returns false", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE_FALSE( arr->erase( 0 ) );
    REQUIRE( arr->size() == 0 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I233-B6: erase single element leaves empty array", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    REQUIRE( arr->push_back( 42 ) );
    REQUIRE( arr->erase( 0 ) );

    REQUIRE( arr->empty() );
    REQUIRE( arr->size() == 0 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

// =============================================================================
// I233-C: insert + erase combined (sorted array pattern)
// =============================================================================

TEST_CASE( "I233-C1: sorted array insert pattern", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    // Build sorted array by inserting at correct position
    // Insert 30, then 10 before it, then 20 between them
    REQUIRE( arr->insert( 0, 30 ) );
    REQUIRE( arr->insert( 0, 10 ) );
    REQUIRE( arr->insert( 1, 20 ) );

    REQUIRE( arr->size() == 3 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 20 );
    REQUIRE( ( *arr )[2] == 30 );

    // Erase the middle element
    REQUIRE( arr->erase( 1 ) );
    REQUIRE( arr->size() == 2 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 30 );

    // Insert 25 between 10 and 30
    REQUIRE( arr->insert( 1, 25 ) );
    REQUIRE( arr->size() == 3 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 25 );
    REQUIRE( ( *arr )[2] == 30 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I233-C2: many inserts and erases maintain consistency", "[test_issue233_parray_insert_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pptr<TestArr> p   = TestMgr::create_typed<TestArr>();
    TestArr*               arr = p.resolve();

    // Insert 100 elements at various positions
    for ( int i = 0; i < 100; ++i )
    {
        std::size_t pos = static_cast<std::size_t>( i ) / 2; // insert in the middle
        REQUIRE( arr->insert( pos, i ) );
    }
    REQUIRE( arr->size() == 100 );

    // Erase all odd-position elements (from the end to avoid index shifting issues)
    for ( int i = 49; i >= 0; --i )
    {
        REQUIRE( arr->erase( static_cast<std::size_t>( i ) * 2 + 1 ) );
    }
    REQUIRE( arr->size() == 50 );

    // Erase remaining elements one by one from the front
    while ( !arr->empty() )
    {
        REQUIRE( arr->erase( 0 ) );
    }
    REQUIRE( arr->size() == 0 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}
