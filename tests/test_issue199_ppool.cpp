/**
 * @file test_issue199_ppool.cpp
 * @brief Tests for ppool — persistent object pool with O(1) allocate/deallocate (Issue #199, Phase 3.6).
 *
 * Verifies the key requirements from Issue #199 Phase 3.6:
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
 * @version 0.1 (Issue #199 — Phase 3.6: persistent object pool)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/ppool.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 199>;

// =============================================================================
// I199-A: Basic allocate / deallocate
// =============================================================================

/// @brief allocate() returns a valid pointer and deallocate() returns it to the pool.
static bool test_i199_basic_alloc_dealloc()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    PMM_TEST( !pool.is_null() );

    int* a = pool->allocate();
    PMM_TEST( a != nullptr );
    *a = 42;
    PMM_TEST( *a == 42 );

    PMM_TEST( pool->allocated_count() == 1 );

    pool->deallocate( a );
    PMM_TEST( pool->allocated_count() == 0 );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-B: Slot reuse after deallocation
// =============================================================================

/// @brief Deallocated slots are reused by subsequent allocations.
static bool test_i199_slot_reuse()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    int* a = pool->allocate();
    PMM_TEST( a != nullptr );
    *a = 100;

    pool->deallocate( a );

    // The next allocation should reuse the freed slot.
    int* b = pool->allocate();
    PMM_TEST( b != nullptr );
    PMM_TEST( b == a ); // Same slot reused (LIFO free-list).

    pool->deallocate( b );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-C: Multiple allocations within a single chunk
// =============================================================================

/// @brief Multiple objects can be allocated from a single chunk.
static bool test_i199_multiple_allocs()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    const int N = 10;
    int*      ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = pool->allocate();
        PMM_TEST( ptrs[i] != nullptr );
        *ptrs[i] = i * 10;
    }

    PMM_TEST( pool->allocated_count() == N );
    PMM_TEST( pool->total_capacity() >= static_cast<std::uint32_t>( N ) );

    // Verify values.
    for ( int i = 0; i < N; ++i )
        PMM_TEST( *ptrs[i] == i * 10 );

    // Verify all pointers are unique.
    for ( int i = 0; i < N; ++i )
        for ( int j = i + 1; j < N; ++j )
            PMM_TEST( ptrs[i] != ptrs[j] );

    for ( int i = 0; i < N; ++i )
        pool->deallocate( ptrs[i] );

    PMM_TEST( pool->allocated_count() == 0 );
    PMM_TEST( pool->empty() );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-D: Chunk growth (more objects than one chunk)
// =============================================================================

/// @brief Pool allocates new chunks when needed.
static bool test_i199_chunk_growth()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    pool->set_objects_per_chunk( 4 ); // Small chunks to force growth.

    const int N = 20; // More than 4 per chunk, forces multiple chunks.
    int*      ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = pool->allocate();
        PMM_TEST( ptrs[i] != nullptr );
        *ptrs[i] = i;
    }

    PMM_TEST( pool->allocated_count() == static_cast<std::uint32_t>( N ) );
    // With 4 objects per chunk, we need ceil(20/4) = 5 chunks.
    PMM_TEST( pool->total_capacity() >= static_cast<std::uint32_t>( N ) );

    // Verify values.
    for ( int i = 0; i < N; ++i )
        PMM_TEST( *ptrs[i] == i );

    for ( int i = 0; i < N; ++i )
        pool->deallocate( ptrs[i] );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-E: free_all() releases all chunks
// =============================================================================

/// @brief free_all() resets the pool to empty state.
static bool test_i199_free_all()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    for ( int i = 0; i < 10; ++i )
    {
        int* p = pool->allocate();
        PMM_TEST( p != nullptr );
        *p = i;
    }

    PMM_TEST( pool->allocated_count() == 10 );
    PMM_TEST( pool->total_capacity() > 0 );

    pool->free_all();

    PMM_TEST( pool->allocated_count() == 0 );
    PMM_TEST( pool->total_capacity() == 0 );
    PMM_TEST( pool->free_count() == 0 );
    PMM_TEST( pool->empty() );

    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-F: Statistics (allocated_count, total_capacity, free_count)
// =============================================================================

/// @brief Statistics are correctly maintained during alloc/dealloc.
static bool test_i199_statistics()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    pool->set_objects_per_chunk( 8 );

    PMM_TEST( pool->allocated_count() == 0 );
    PMM_TEST( pool->total_capacity() == 0 );
    PMM_TEST( pool->free_count() == 0 );
    PMM_TEST( pool->empty() );

    // Allocate first object — triggers chunk allocation (8 slots).
    int* a = pool->allocate();
    PMM_TEST( a != nullptr );
    PMM_TEST( pool->allocated_count() == 1 );
    PMM_TEST( pool->total_capacity() == 8 );
    PMM_TEST( pool->free_count() == 7 );

    // Allocate 7 more to fill the chunk.
    int* ptrs[7];
    for ( int i = 0; i < 7; ++i )
    {
        ptrs[i] = pool->allocate();
        PMM_TEST( ptrs[i] != nullptr );
    }

    PMM_TEST( pool->allocated_count() == 8 );
    PMM_TEST( pool->total_capacity() == 8 );
    PMM_TEST( pool->free_count() == 0 );

    // Allocate one more — triggers new chunk.
    int* extra = pool->allocate();
    PMM_TEST( extra != nullptr );
    PMM_TEST( pool->allocated_count() == 9 );
    PMM_TEST( pool->total_capacity() == 16 );
    PMM_TEST( pool->free_count() == 7 );

    // Deallocate some.
    pool->deallocate( a );
    pool->deallocate( extra );
    PMM_TEST( pool->allocated_count() == 7 );
    PMM_TEST( pool->free_count() == 9 );

    for ( int i = 0; i < 7; ++i )
        pool->deallocate( ptrs[i] );

    PMM_TEST( pool->allocated_count() == 0 );
    PMM_TEST( pool->free_count() == 16 );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-G: set_objects_per_chunk before allocation
// =============================================================================

/// @brief set_objects_per_chunk has effect before first allocation.
static bool test_i199_set_objects_per_chunk()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    pool->set_objects_per_chunk( 16 );

    int* a = pool->allocate();
    PMM_TEST( a != nullptr );
    PMM_TEST( pool->total_capacity() == 16 );

    // set_objects_per_chunk has no effect after allocation.
    pool->set_objects_per_chunk( 1000 );
    PMM_TEST( pool->total_capacity() == 16 ); // Unchanged.

    pool->deallocate( a );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
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
static bool test_i199_struct_type()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<Point199>> pool = TestMgr::create_typed<TestMgr::ppool<Point199>>();

    Point199* a = pool->allocate();
    PMM_TEST( a != nullptr );
    a->x = 1;
    a->y = 2;
    a->z = 3;

    Point199* b = pool->allocate();
    PMM_TEST( b != nullptr );
    b->x = 10;
    b->y = 20;
    b->z = 30;

    PMM_TEST( a->x == 1 && a->y == 2 && a->z == 3 );
    PMM_TEST( b->x == 10 && b->y == 20 && b->z == 30 );

    pool->deallocate( a );
    pool->deallocate( b );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-I: Pool with large struct (multiple granules per slot)
// =============================================================================

struct LargeNode199
{
    std::uint64_t data[8]; // 64 bytes — may span multiple granules.
};

/// @brief ppool works with types larger than one granule.
static bool test_i199_large_type()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<LargeNode199>> pool = TestMgr::create_typed<TestMgr::ppool<LargeNode199>>();
    pool->set_objects_per_chunk( 8 );

    LargeNode199* a = pool->allocate();
    PMM_TEST( a != nullptr );
    for ( int i = 0; i < 8; ++i )
        a->data[i] = static_cast<std::uint64_t>( i ) * 100;

    LargeNode199* b = pool->allocate();
    PMM_TEST( b != nullptr );
    for ( int i = 0; i < 8; ++i )
        b->data[i] = static_cast<std::uint64_t>( i ) * 200;

    // Verify data integrity (no overlap between a and b).
    for ( int i = 0; i < 8; ++i )
    {
        PMM_TEST( a->data[i] == static_cast<std::uint64_t>( i ) * 100 );
        PMM_TEST( b->data[i] == static_cast<std::uint64_t>( i ) * 200 );
    }

    pool->deallocate( a );
    pool->deallocate( b );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-J: Manager alias test
// =============================================================================

/// @brief ppool is accessible via Mgr::ppool<T> alias.
static bool test_i199_manager_alias()
{
    using AliasPool  = TestMgr::ppool<int>;
    using DirectPool = pmm::ppool<int, TestMgr>;

    // Both aliases refer to the same type.
    PMM_TEST( (std::is_same_v<AliasPool, DirectPool>));

    return true;
}

// =============================================================================
// I199-K: Stress test — mass allocation/deallocation
// =============================================================================

/// @brief Mass allocation/deallocation (1000 objects) maintains integrity.
static bool test_i199_stress()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 1024 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    pool->set_objects_per_chunk( 32 );

    const int         N = 1000;
    std::vector<int*> ptrs( N, nullptr );

    // Allocate all.
    for ( int i = 0; i < N; ++i )
    {
        ptrs[static_cast<std::size_t>( i )] = pool->allocate();
        PMM_TEST( ptrs[static_cast<std::size_t>( i )] != nullptr );
        *ptrs[static_cast<std::size_t>( i )] = i;
    }

    PMM_TEST( pool->allocated_count() == static_cast<std::uint32_t>( N ) );

    // Verify all values.
    for ( int i = 0; i < N; ++i )
        PMM_TEST( *ptrs[static_cast<std::size_t>( i )] == i );

    // Deallocate all.
    for ( int i = 0; i < N; ++i )
        pool->deallocate( ptrs[static_cast<std::size_t>( i )] );

    PMM_TEST( pool->allocated_count() == 0 );
    PMM_TEST( pool->empty() );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-L: Interleaved alloc/dealloc
// =============================================================================

/// @brief Interleaved allocation and deallocation maintains pool integrity.
static bool test_i199_interleaved()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

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

    PMM_TEST( pool->allocated_count() == 4 );

    // Deallocate b and c.
    pool->deallocate( b );
    pool->deallocate( c );
    PMM_TEST( pool->allocated_count() == 2 );

    // Allocate 2 more — should reuse freed slots.
    int* e = pool->allocate();
    int* f = pool->allocate();
    *e     = 5;
    *f     = 6;

    PMM_TEST( pool->allocated_count() == 4 );

    // Original values intact.
    PMM_TEST( *a == 1 );
    PMM_TEST( *d == 4 );
    PMM_TEST( *e == 5 );
    PMM_TEST( *f == 6 );

    pool->deallocate( a );
    pool->deallocate( d );
    pool->deallocate( e );
    pool->deallocate( f );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-M: Pool reuse after free_all
// =============================================================================

/// @brief Pool can be reused after free_all().
static bool test_i199_reuse_after_free()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();
    pool->set_objects_per_chunk( 8 );

    // First round.
    for ( int i = 0; i < 5; ++i )
    {
        int* p = pool->allocate();
        PMM_TEST( p != nullptr );
        *p = i;
    }
    PMM_TEST( pool->allocated_count() == 5 );

    pool->free_all();
    PMM_TEST( pool->allocated_count() == 0 );
    PMM_TEST( pool->total_capacity() == 0 );

    // Second round — pool allocates new chunks.
    for ( int i = 0; i < 5; ++i )
    {
        int* p = pool->allocate();
        PMM_TEST( p != nullptr );
        *p = i + 100;
    }
    PMM_TEST( pool->allocated_count() == 5 );
    PMM_TEST( pool->total_capacity() == 8 ); // New chunk with 8 slots.

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-N: Deallocate nullptr is safe
// =============================================================================

/// @brief deallocate(nullptr) is a no-op.
static bool test_i199_dealloc_nullptr()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    // deallocate(nullptr) should not crash or change state.
    pool->deallocate( nullptr );
    PMM_TEST( pool->allocated_count() == 0 );
    PMM_TEST( pool->empty() );

    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-O: Zero-initialization of allocated slots
// =============================================================================

/// @brief Allocated slots are zero-initialized.
static bool test_i199_zero_init()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<Point199>> pool = TestMgr::create_typed<TestMgr::ppool<Point199>>();

    Point199* p = pool->allocate();
    PMM_TEST( p != nullptr );
    PMM_TEST( p->x == 0 );
    PMM_TEST( p->y == 0 );
    PMM_TEST( p->z == 0 );

    pool->deallocate( p );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-P: Trivially copyable static_assert
// =============================================================================

/// @brief ppool<T> requires T to be trivially copyable (compile-time check).
static bool test_i199_trivially_copyable()
{
    // ppool requires T to be trivially copyable.
    PMM_TEST( (std::is_trivially_copyable_v<int>));
    PMM_TEST( (std::is_trivially_copyable_v<Point199>));
    PMM_TEST( (std::is_trivially_copyable_v<LargeNode199>));

    // Verify ppool itself is trivially copyable (POD-structure for PAP).
    PMM_TEST( (std::is_trivially_copyable_v<TestMgr::ppool<int>>));
    PMM_TEST( (std::is_trivially_copyable_v<TestMgr::ppool<Point199>>));

    return true;
}

// =============================================================================
// I199-Q: ppool with uint8_t (small type, slot padded to granule)
// =============================================================================

/// @brief ppool works with small types (uint8_t) — slot is padded to granule size.
static bool test_i199_small_type()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<std::uint8_t>> pool = TestMgr::create_typed<TestMgr::ppool<std::uint8_t>>();
    pool->set_objects_per_chunk( 8 );

    std::uint8_t* a = pool->allocate();
    PMM_TEST( a != nullptr );
    *a = 0xAB;

    std::uint8_t* b = pool->allocate();
    PMM_TEST( b != nullptr );
    *b = 0xCD;

    PMM_TEST( *a == 0xAB );
    PMM_TEST( *b == 0xCD );
    PMM_TEST( a != b );

    pool->deallocate( a );
    pool->deallocate( b );
    pool->free_all();
    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I199-R: free_all on empty pool is safe
// =============================================================================

/// @brief free_all() on an empty pool is a no-op.
static bool test_i199_free_all_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    TestMgr::pptr<TestMgr::ppool<int>> pool = TestMgr::create_typed<TestMgr::ppool<int>>();

    // free_all on empty pool should not crash.
    pool->free_all();
    PMM_TEST( pool->allocated_count() == 0 );
    PMM_TEST( pool->total_capacity() == 0 );

    TestMgr::destroy_typed( pool );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// Main — run all tests
// =============================================================================

int main()
{
    std::cout << "[Issue #199] ppool — persistent object pool (Phase 3.6)\n";

    bool all_passed = true;

    PMM_RUN( "I199-A: basic alloc/dealloc", test_i199_basic_alloc_dealloc );
    PMM_RUN( "I199-B: slot reuse", test_i199_slot_reuse );
    PMM_RUN( "I199-C: multiple allocations", test_i199_multiple_allocs );
    PMM_RUN( "I199-D: chunk growth", test_i199_chunk_growth );
    PMM_RUN( "I199-E: free_all", test_i199_free_all );
    PMM_RUN( "I199-F: statistics", test_i199_statistics );
    PMM_RUN( "I199-G: set_objects_per_chunk", test_i199_set_objects_per_chunk );
    PMM_RUN( "I199-H: struct type", test_i199_struct_type );
    PMM_RUN( "I199-I: large type (multi-granule)", test_i199_large_type );
    PMM_RUN( "I199-J: manager alias", test_i199_manager_alias );
    PMM_RUN( "I199-K: stress (1000 objects)", test_i199_stress );
    PMM_RUN( "I199-L: interleaved alloc/dealloc", test_i199_interleaved );
    PMM_RUN( "I199-M: reuse after free_all", test_i199_reuse_after_free );
    PMM_RUN( "I199-N: deallocate nullptr", test_i199_dealloc_nullptr );
    PMM_RUN( "I199-O: zero-initialization", test_i199_zero_init );
    PMM_RUN( "I199-P: trivially copyable", test_i199_trivially_copyable );
    PMM_RUN( "I199-Q: small type (uint8_t)", test_i199_small_type );
    PMM_RUN( "I199-R: free_all on empty pool", test_i199_free_all_empty );

    std::cout << "\n" << ( all_passed ? "All ppool tests PASSED." : "Some ppool tests FAILED!" ) << "\n";
    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
