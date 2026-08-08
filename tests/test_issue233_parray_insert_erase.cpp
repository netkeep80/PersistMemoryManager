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
#include <cstring>

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

    REQUIRE( arr->insert( 0, 30 ) );
    REQUIRE( arr->insert( 0, 10 ) );
    REQUIRE( arr->insert( 1, 20 ) );

    REQUIRE( arr->size() == 3 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 20 );
    REQUIRE( ( *arr )[2] == 30 );

    REQUIRE( arr->erase( 1 ) );
    REQUIRE( arr->size() == 2 );
    REQUIRE( ( *arr )[0] == 10 );
    REQUIRE( ( *arr )[1] == 30 );

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

    for ( int i = 0; i < 100; ++i )
    {
        std::size_t pos = static_cast<std::size_t>( i ) / 2;
        REQUIRE( arr->insert( pos, i ) );
    }
    REQUIRE( arr->size() == 100 );

    for ( int i = 49; i >= 0; --i )
        REQUIRE( arr->erase( static_cast<std::size_t>( i ) * 2 + 1 ) );
    REQUIRE( arr->size() == 50 );

    while ( !arr->empty() )
        REQUIRE( arr->erase( 0 ) );
    REQUIRE( arr->size() == 0 );

    arr->free_data();
    TestMgr::destroy_typed( p );
    TestMgr::destroy();
}

TEST_CASE( "I403: parray push snapshots value and re-resolves owner across arena relocation",
           "[issue403][parray][relocation]" )
{
    struct BigValue
    {
        std::uint64_t marker;
        std::byte     bytes[2040];
    };
    static_assert( sizeof( BigValue ) == 2048 );
    static_assert( std::is_trivially_copyable_v<BigValue> );

    using RelocMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 405>;
    using RelocArr = RelocMgr::parray<BigValue>;

    RelocMgr::destroy();
    REQUIRE( RelocMgr::create( 8 * 1024 ) );

    auto source = RelocMgr::create_typed<BigValue>();
    auto array  = RelocMgr::create_typed<RelocArr>();
    REQUIRE( !source.is_null() );
    REQUIRE( !array.is_null() );

    auto* source_before = source.resolve();
    auto* array_before  = array.resolve();
    REQUIRE( source_before != nullptr );
    REQUIRE( array_before != nullptr );
    source_before->marker = 0x0123456789abcdefULL;
    std::memset( source_before->bytes, 0xa5, sizeof( source_before->bytes ) );

    const auto* base_before = RelocMgr::backend().base_ptr();
    REQUIRE( array_before->push_back( *source_before ) );
    REQUIRE( RelocMgr::backend().base_ptr() != base_before );

    auto* source_after = source.resolve();
    auto* array_after  = array.resolve();
    REQUIRE( source_after != nullptr );
    REQUIRE( array_after != nullptr );
    REQUIRE( source_after != source_before );
    REQUIRE( array_after != array_before );
    REQUIRE( array_after->size() == 1 );
    REQUIRE( array_after->at( 0 ) != nullptr );
    REQUIRE( array_after->at( 0 )->marker == 0x0123456789abcdefULL );
    for ( std::byte b : array_after->at( 0 )->bytes )
        REQUIRE( b == std::byte{ 0xa5 } );
    REQUIRE( RelocMgr::verify().ok );

    array_after->free_data();
    RelocMgr::destroy_typed( array );
    RelocMgr::destroy_typed( source );
    RelocMgr::destroy();
}
