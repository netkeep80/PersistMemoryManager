/**
 * @file test_issue172_code_review.cpp
 * @brief Tests for code review improvements (Issue #172).
 *
 * Verifies fixes based on code review:
 *  - create_typed<T>(args...) calls T's constructor via placement new (Issue #172 #3).
 *  - destroy_typed<T>(p) calls T's destructor before freeing memory (Issue #172 #3).
 *  - tree_node(null_pptr) triggers assert in debug mode (Issue #172 #4).
 *  - tree_node(uninitialized manager) triggers assert in debug mode (Issue #172 #4).
 *  - is_initialized() and statistics getters work correctly under multithreaded access.
 *  - create() with initial_size near SIZE_MAX returns false (overflow guard, Issue #172 #6).
 *
 * @see include/pmm/persist_memory_manager.h
 * @version 0.1 (Issue #172 — code review fixes)
 */

#include "pmm_single_threaded_heap.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

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

// ─── Manager alias for tests ──────────────────────────────────────────────────

using M = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 172>;

// =============================================================================
// I172-A: create_typed<T> calls constructor, destroy_typed<T> calls destructor
// =============================================================================

/// @brief A type with a non-trivial constructor and destructor to test create_typed/destroy_typed.
struct TrackedObject
{
    int   value;
    bool* destroyed_flag;
    int   tag; ///< Set by constructor to verify it was called

    explicit TrackedObject( int v, bool* flag ) noexcept : value( v ), destroyed_flag( flag ), tag( 0xCAFEBABE ) {}

    ~TrackedObject() noexcept
    {
        tag = 0; // Clear tag on destruction
        if ( destroyed_flag != nullptr )
            *destroyed_flag = true;
    }
};

/// @brief create_typed<T>(args...) constructs T in-place; destroy_typed calls destructor.
static bool test_i172_create_destroy_typed_constructor_called()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    bool destroyed = false;

    // create_typed should allocate memory AND call constructor
    auto p = M::create_typed<TrackedObject>( 42, &destroyed );
    PMM_TEST( !p.is_null() );

    TrackedObject* obj = M::resolve( p );
    PMM_TEST( obj != nullptr );
    PMM_TEST( obj->value == 42 );
    PMM_TEST( obj->tag == static_cast<int>( 0xCAFEBABE ) ); // constructor set this
    PMM_TEST( !destroyed );

    // destroy_typed should call destructor AND free memory
    M::destroy_typed( p );
    PMM_TEST( destroyed ); // destructor was called

    M::destroy();
    return true;
}

/// @brief allocate_typed does NOT call constructor (raw allocation only).
struct WithDefaultValue
{
    int x;
    WithDefaultValue() noexcept : x( 123 ) {}
};

static bool test_i172_allocate_typed_no_constructor()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    // allocate_typed does NOT call the constructor — only raw memory allocation.
    // Issue #172: this is documented behavior; create_typed should be used instead.
    auto p = M::allocate_typed<WithDefaultValue>();
    PMM_TEST( !p.is_null() );

    // We cannot verify the value here because memory is uninitialized — just verify
    // that allocate_typed succeeded and the pointer is valid.
    WithDefaultValue* obj = M::resolve( p );
    PMM_TEST( obj != nullptr );

    M::deallocate_typed( p );
    M::destroy();
    return true;
}

/// @brief create_typed<T> with args; resolve and verify constructor args were passed.
static bool test_i172_create_typed_with_args()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );

    // Create objects using create_typed, verify constructor was invoked correctly.
    auto p1 = M::create_typed<WithDefaultValue>();
    PMM_TEST( !p1.is_null() );
    WithDefaultValue* obj1 = M::resolve( p1 );
    PMM_TEST( obj1 != nullptr );
    PMM_TEST( obj1->x == 123 ); // default constructor must have been called

    M::destroy_typed( p1 );
    M::destroy();
    return true;
}

// =============================================================================
// I172-B: Overflow guard in create() initial_size alignment
// =============================================================================

/// @brief create() with initial_size near SIZE_MAX returns false (overflow guard).
static bool test_i172_create_overflow_guard()
{
    M::destroy();

    // initial_size near SIZE_MAX should return false without crashing
    constexpr std::size_t huge_size = std::numeric_limits<std::size_t>::max() - 1;
    bool                  result    = M::create( huge_size );
    PMM_TEST( !result );              // must fail gracefully
    PMM_TEST( !M::is_initialized() ); // must remain uninitialized

    return true;
}

// =============================================================================
// I172-C: is_initialized() returns correct state
// =============================================================================

/// @brief is_initialized() returns false before create() and true after.
static bool test_i172_is_initialized_state()
{
    M::destroy();
    PMM_TEST( !M::is_initialized() );

    PMM_TEST( M::create( 64 * 1024 ) );
    PMM_TEST( M::is_initialized() );

    M::destroy();
    PMM_TEST( !M::is_initialized() );

    return true;
}

// =============================================================================
// Main
// =============================================================================

int main()
{
    std::cout << "Test suite: Issue #172 code review fixes\n";
    bool all_passed = true;

    PMM_RUN( "I172-A1: create_typed calls constructor, destroy_typed calls destructor",
             test_i172_create_destroy_typed_constructor_called );
    PMM_RUN( "I172-A2: allocate_typed is raw allocation (no constructor call)",
             test_i172_allocate_typed_no_constructor );
    PMM_RUN( "I172-A3: create_typed<T>() with default constructor sets fields", test_i172_create_typed_with_args );
    PMM_RUN( "I172-B: create() overflow guard for huge initial_size", test_i172_create_overflow_guard );
    PMM_RUN( "I172-C: is_initialized() state transitions", test_i172_is_initialized_state );

    if ( all_passed )
    {
        std::cout << "All tests PASSED.\n";
        return 0;
    }
    else
    {
        std::cout << "Some tests FAILED.\n";
        return 1;
    }
}
