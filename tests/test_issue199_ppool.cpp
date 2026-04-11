/**
 * @file test_issue199_ppool.cpp
 * @brief Tests for ppool — persistent object pool with O(1) allocate/deallocate.
 *
 * Verifies the key requirements from this feature:
 *  1. ppool allocates objects from the pool in O(1).
 *  2. ppool deallocates objects back to the pool in O(1).
 *  3. Deallocated slots are reused by subsequent allocations.
 *  4. Multiple chunks are allocated when the pool grows.
 *  5. free_all() releases all chunks.
 *  6. ppool works with different element types (int, struct, large struct).
 *  7. set_objects_per_chunk() configures chunk size before first allocation.
 *  8. allocated_count(), total_capacity(), free_count() report correct statistics.
 *  9. ppool is accessible via Mgr::ppool<T> alias.
 * 10. ppool with trivially copyable struct works correctly.
 * 11. ppool handles mass allocation/deallocation (stress test).
 * 12. Interleaved alloc/dealloc maintains pool integrity.
 * 13. ppool with custom objects_per_chunk works correctly.
 * 14. ppool supports allocate after free_all (pool reuse).
 *
 * @see include/pmm/ppool.h — ppool
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @version 0.1
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/ppool.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include <type_traits>
#include <vector>

// --- Manager type alias for tests --------------------------------------------

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 199>;

// =============================================================================
// I199-A: Basic allocate / deallocate
// =============================================================================

/// @brief allocate() returns a valid pointer and deallocate() returns it to the pool.
TEST_CASE( "I199-A: basic alloc/dealloc", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    REQUIRE( !pool.is_null() );

    int* a = pool->allocate();
    REQUIRE( a != nullptr );
    *a = 42;
    REQUIRE( *a == 42 );

    REQUIRE( pool->allocated_count() == 1 );

    pool->deallocate( a );
    REQUIRE( pool->allocated_count() == 0 );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-B: Slot reuse after deallocation
// =============================================================================

/// @brief Deallocated slots are reused by subsequent allocations.
TEST_CASE( "I199-B: slot reuse", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    int* a = pool->allocate();
    REQUIRE( a != nullptr );
    *a = 100;

    pool->deallocate( a );

    // The next allocation should reuse the freed slot.
    int* b = pool->allocate();
    REQUIRE( b != nullptr );
    REQUIRE( b == a ); // Same slot reused (LIFO free-list).

    pool->deallocate( b );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-C: Multiple allocations within a single chunk
// =============================================================================

/// @brief Multiple objects can be allocated from a single chunk.
TEST_CASE( "I199-C: multiple allocations", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    const int N = 10;
    int*      ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = pool->allocate();
        REQUIRE( ptrs[i] != nullptr );
        *ptrs[i] = i * 10;
    }

    REQUIRE( pool->allocated_count() == N );
    REQUIRE( pool->total_capacity() >= static_cast<std::uint32_t>( N ) );

    // Verify values.
    for ( int i = 0; i < N; ++i )
        REQUIRE( *ptrs[i] == i * 10 );

    // Verify all pointers are unique.
    for ( int i = 0; i < N; ++i )
        for ( int j = i + 1; j < N; ++j )
            REQUIRE( ptrs[i] != ptrs[j] );

    for ( int i = 0; i < N; ++i )
        pool->deallocate( ptrs[i] );

    REQUIRE( pool->allocated_count() == 0 );
    REQUIRE( pool->empty() );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-D: Chunk growth (more objects than one chunk)
// =============================================================================

/// @brief Pool allocates new chunks when needed.
TEST_CASE( "I199-D: chunk growth", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    pool->set_objects_per_chunk( 4 ); // Small chunks to force growth.

    const int N = 20; // More than 4 per chunk, forces multiple chunks.
    int*      ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = pool->allocate();
        REQUIRE( ptrs[i] != nullptr );
        *ptrs[i] = i;
    }

    REQUIRE( pool->allocated_count() == static_cast<std::uint32_t>( N ) );
    // With 4 objects per chunk, we need ceil(20/4) = 5 chunks.
    REQUIRE( pool->total_capacity() >= static_cast<std::uint32_t>( N ) );

    // Verify values.
    for ( int i = 0; i < N; ++i )
        REQUIRE( *ptrs[i] == i );

    for ( int i = 0; i < N; ++i )
        pool->deallocate( ptrs[i] );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-E: free_all() releases all chunks
// =============================================================================

/// @brief free_all() resets the pool to empty state.
TEST_CASE( "I199-E: free_all", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    for ( int i = 0; i < 10; ++i )
    {
        int* p = pool->allocate();
        REQUIRE( p != nullptr );
        *p = i;
    }

    REQUIRE( pool->allocated_count() == 10 );
    REQUIRE( pool->total_capacity() > 0 );

    pool->free_all();

    REQUIRE( pool->allocated_count() == 0 );
    REQUIRE( pool->total_capacity() == 0 );
    REQUIRE( pool->free_count() == 0 );
    REQUIRE( pool->empty() );

    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-F: Statistics (allocated_count, total_capacity, free_count)
// =============================================================================

/// @brief Statistics are correctly maintained during alloc/dealloc.
TEST_CASE( "I199-F: statistics", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    pool->set_objects_per_chunk( 8 );

    REQUIRE( pool->allocated_count() == 0 );
    REQUIRE( pool->total_capacity() == 0 );
    REQUIRE( pool->free_count() == 0 );
    REQUIRE( pool->empty() );

    // Allocate first object — triggers chunk allocation (8 slots).
    int* a = pool->allocate();
    REQUIRE( a != nullptr );
    REQUIRE( pool->allocated_count() == 1 );
    REQUIRE( pool->total_capacity() == 8 );
    REQUIRE( pool->free_count() == 7 );

    // Allocate 7 more to fill the chunk.
    int* ptrs[7];
    for ( int i = 0; i < 7; ++i )
    {
        ptrs[i] = pool->allocate();
        REQUIRE( ptrs[i] != nullptr );
    }

    REQUIRE( pool->allocated_count() == 8 );
    REQUIRE( pool->total_capacity() == 8 );
    REQUIRE( pool->free_count() == 0 );

    // Allocate one more — triggers new chunk.
    int* extra = pool->allocate();
    REQUIRE( extra != nullptr );
    REQUIRE( pool->allocated_count() == 9 );
    REQUIRE( pool->total_capacity() == 16 );
    REQUIRE( pool->free_count() == 7 );

    // Deallocate some.
    pool->deallocate( a );
    pool->deallocate( extra );
    REQUIRE( pool->allocated_count() == 7 );
    REQUIRE( pool->free_count() == 9 );

    for ( int i = 0; i < 7; ++i )
        pool->deallocate( ptrs[i] );

    REQUIRE( pool->allocated_count() == 0 );
    REQUIRE( pool->free_count() == 16 );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-G: set_objects_per_chunk before allocation
// =============================================================================

/// @brief set_objects_per_chunk has effect before first allocation.
TEST_CASE( "I199-G: set_objects_per_chunk", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    pool->set_objects_per_chunk( 16 );

    int* a = pool->allocate();
    REQUIRE( a != nullptr );
    REQUIRE( pool->total_capacity() == 16 );

    // set_objects_per_chunk has no effect after allocation.
    pool->set_objects_per_chunk( 1000 );
    REQUIRE( pool->total_capacity() == 16 ); // Unchanged.

    pool->deallocate( a );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-H: Pool with struct type
// =============================================================================

struct Point199
{
    int x;
    int y;
    int z;
};

/// @brief ppool works with user-defined POD struct.
TEST_CASE( "I199-H: struct type", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<Point199>> pool = TestMgr::create_typed<TestMgr::ppool<Point199>>();

    Point199* a = pool->allocate();
    REQUIRE( a != nullptr );
    a->x = 1;
    a->y = 2;
    a->z = 3;

    Point199* b = pool->allocate();
    REQUIRE( b != nullptr );
    b->x = 10;
    b->y = 20;
    b->z = 30;

    REQUIRE( ( a->x == 1 && a->y == 2 && a->z == 3 ) );
    REQUIRE( ( b->x == 10 && b->y == 20 && b->z == 30 ) );

    pool->deallocate( a );
    pool->deallocate( b );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-I: Pool with large struct (multiple granules per slot)
// =============================================================================

struct LargeNode199
{
    std::uint64_t data[8]; // 64 bytes — may span multiple granules.
};

/// @brief ppool works with types larger than one granule.
TEST_CASE( "I199-I: large type (multi-granule)", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<LargeNode199>> pool = TestMgr::create_typed<TestMgr::ppool<LargeNode199>>();
    pool->set_objects_per_chunk( 8 );

    LargeNode199* a = pool->allocate();
    REQUIRE( a != nullptr );
    for ( int i = 0; i < 8; ++i )
        a->data[i] = static_cast<std::uint64_t>( i ) * 100;

    LargeNode199* b = pool->allocate();
    REQUIRE( b != nullptr );
    for ( int i = 0; i < 8; ++i )
        b->data[i] = static_cast<std::uint64_t>( i ) * 200;

    // Verify data integrity (no overlap between a and b).
    for ( int i = 0; i < 8; ++i )
    {
        REQUIRE( a->data[i] == static_cast<std::uint64_t>( i ) * 100 );
        REQUIRE( b->data[i] == static_cast<std::uint64_t>( i ) * 200 );
    }

    pool->deallocate( a );
    pool->deallocate( b );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-J: Manager alias test
// =============================================================================

/// @brief ppool is accessible via Mgr::ppool<T> alias.
TEST_CASE( "I199-J: manager alias", "[test_issue199_ppool]" )
{
    using AliasPool  = TestMgr::ppool<int>;
    using DirectPool = pmm::ppool<int, TestMgr>;

    // Both aliases refer to the same type.
    REQUIRE( (std::is_same_v<AliasPool, DirectPool>));
}

// =============================================================================
// I199-K: Stress test — mass allocation/deallocation
// =============================================================================

/// @brief Mass allocation/deallocation (1000 objects) maintains integrity.
TEST_CASE( "I199-K: stress (1000 objects)", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 1024 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    pool->set_objects_per_chunk( 32 );

    const int         N = 1000;
    std::vector<int*> ptrs( N, nullptr );

    // Allocate all.
    for ( int i = 0; i < N; ++i )
    {
        ptrs[static_cast<std::size_t>( i )] = pool->allocate();
        REQUIRE( ptrs[static_cast<std::size_t>( i )] != nullptr );
        *ptrs[static_cast<std::size_t>( i )] = i;
    }

    REQUIRE( pool->allocated_count() == static_cast<std::uint32_t>( N ) );

    // Verify all values.
    for ( int i = 0; i < N; ++i )
        REQUIRE( *ptrs[static_cast<std::size_t>( i )] == i );

    // Deallocate all.
    for ( int i = 0; i < N; ++i )
        pool->deallocate( ptrs[static_cast<std::size_t>( i )] );

    REQUIRE( pool->allocated_count() == 0 );
    REQUIRE( pool->empty() );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-L: Interleaved alloc/dealloc
// =============================================================================

/// @brief Interleaved allocation and deallocation maintains pool integrity.
TEST_CASE( "I199-L: interleaved alloc/dealloc", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    pool->set_objects_per_chunk( 4 );

    // Allocate 4 objects.
    int* a = pool->allocate();
    int* b = pool->allocate();
    int* c = pool->allocate();
    int* d = pool->allocate();
    *a     = 1;
    *b     = 2;
    *c     = 3;
    *d     = 4;

    REQUIRE( pool->allocated_count() == 4 );

    // Deallocate b and c.
    pool->deallocate( b );
    pool->deallocate( c );
    REQUIRE( pool->allocated_count() == 2 );

    // Allocate 2 more — should reuse freed slots.
    int* e = pool->allocate();
    int* f = pool->allocate();
    *e     = 5;
    *f     = 6;

    REQUIRE( pool->allocated_count() == 4 );

    // Original values intact.
    REQUIRE( *a == 1 );
    REQUIRE( *d == 4 );
    REQUIRE( *e == 5 );
    REQUIRE( *f == 6 );

    pool->deallocate( a );
    pool->deallocate( d );
    pool->deallocate( e );
    pool->deallocate( f );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-M: Pool reuse after free_all
// =============================================================================

/// @brief Pool can be reused after free_all().
TEST_CASE( "I199-M: reuse after free_all", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    pool->set_objects_per_chunk( 8 );

    // First round.
    for ( int i = 0; i < 5; ++i )
    {
        int* p = pool->allocate();
        REQUIRE( p != nullptr );
        *p = i;
    }
    REQUIRE( pool->allocated_count() == 5 );

    pool->free_all();
    REQUIRE( pool->allocated_count() == 0 );
    REQUIRE( pool->total_capacity() == 0 );

    // Second round — pool allocates new chunks.
    for ( int i = 0; i < 5; ++i )
    {
        int* p = pool->allocate();
        REQUIRE( p != nullptr );
        *p = i + 100;
    }
    REQUIRE( pool->allocated_count() == 5 );
    REQUIRE( pool->total_capacity() == 8 ); // New chunk with 8 slots.

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-N: Deallocate nullptr is safe
// =============================================================================

/// @brief deallocate(nullptr) is a no-op.
TEST_CASE( "I199-N: deallocate nullptr", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    // deallocate(nullptr) should not crash or change state.
    pool->deallocate( nullptr );
    REQUIRE( pool->allocated_count() == 0 );
    REQUIRE( pool->empty() );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-O: Zero-initialization of allocated slots
// =============================================================================

/// @brief Allocated slots are zero-initialized.
TEST_CASE( "I199-O: zero-initialization", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<Point199>> pool = TestMgr::create_typed<TestMgr::ppool<Point199>>();

    Point199* p = pool->allocate();
    REQUIRE( p != nullptr );
    REQUIRE( p->x == 0 );
    REQUIRE( p->y == 0 );
    REQUIRE( p->z == 0 );

    pool->deallocate( p );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-P: Trivially copyable static_assert
// =============================================================================

/// @brief ppool<T> requires T to be trivially copyable (compile-time check).
TEST_CASE( "I199-P: trivially copyable", "[test_issue199_ppool]" )
{
    // ppool requires T to be trivially copyable.
    REQUIRE( (std::is_trivially_copyable_v<int>));
    REQUIRE( (std::is_trivially_copyable_v<Point199>));
    REQUIRE( (std::is_trivially_copyable_v<LargeNode199>));

    // Verify ppool itself is trivially copyable (POD-structure for PAP).
    REQUIRE( (std::is_trivially_copyable_v<TestMgr::ppool<int>>));
    REQUIRE( (std::is_trivially_copyable_v<TestMgr::ppool<Point199>>));
}

// =============================================================================
// I199-Q: ppool with uint8_t (small type, slot padded to granule)
// =============================================================================

/// @brief ppool works with small types (uint8_t) — slot is padded to granule size.
TEST_CASE( "I199-Q: small type (uint8_t)", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<std::uint8_t>> pool = TestMgr::create_typed<TestMgr::ppool<std::uint8_t>>();
    pool->set_objects_per_chunk( 8 );

    std::uint8_t* a = pool->allocate();
    REQUIRE( a != nullptr );
    *a = 0xAB;

    std::uint8_t* b = pool->allocate();
    REQUIRE( b != nullptr );
    *b = 0xCD;

    REQUIRE( *a == 0xAB );
    REQUIRE( *b == 0xCD );
    REQUIRE( a != b );

    pool->deallocate( a );
    pool->deallocate( b );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// I199-R: free_all on empty pool is safe
// =============================================================================

/// @brief free_all() on an empty pool is a no-op.
TEST_CASE( "I199-R: free_all on empty pool", "[test_issue199_ppool]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    // free_all on empty pool should not crash.
    pool->free_all();
    REQUIRE( pool->allocated_count() == 0 );
    REQUIRE( pool->total_capacity() == 0 );

    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
}

// =============================================================================
// Main — run all tests
// =============================================================================
