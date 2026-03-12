/**
 * @file test_plan_phase1_1.cpp
 * @brief Tests for Plan Phase 1, Task 1.1: Exception safety in create_typed<T>.
 *
 * This test verifies that:
 * 1. create_typed<T>(args...) works correctly with noexcept constructors
 * 2. destroy_typed<T>(p) works correctly with noexcept destructors
 * 3. The static_assert requirements are documented and enforced at compile time
 *
 * Note: static_assert failures are compile-time errors, so we cannot test them
 * at runtime. Instead, this file documents the expected behavior and provides
 * compile-time verification that the type traits work correctly.
 *
 * @see docs/plan.md Section 1.1 "Exception safety in create_typed<T>"
 * @see include/pmm/persist_memory_manager.h create_typed and destroy_typed
 * @version 1.0 (Issue #44, Plan Phase 1.1)
 */

#include "pmm_single_threaded_heap.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>

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

using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4411>;

// =============================================================================
// Test Types: noexcept constructors and destructors
// =============================================================================

/// @brief Type with noexcept default constructor — should work with create_typed<T>()
struct NoexceptDefault
{
    int value;
    NoexceptDefault() noexcept : value( 42 ) {}
    ~NoexceptDefault() noexcept = default;
};

/// @brief Type with noexcept constructor with arguments — should work with create_typed<T>(args...)
struct NoexceptWithArgs
{
    int   a;
    float b;
    NoexceptWithArgs( int x, float y ) noexcept : a( x ), b( y ) {}
    ~NoexceptWithArgs() noexcept = default;
};

/// @brief Type with noexcept copy constructor — should work with create_typed<T>(const T&)
struct NoexceptCopy
{
    int value;
    NoexceptCopy( int v ) noexcept : value( v ) {}
    NoexceptCopy( const NoexceptCopy& other ) noexcept : value( other.value ) {}
    ~NoexceptCopy() noexcept = default;
};

/// @brief Type with noexcept move constructor — should work with create_typed<T>(T&&)
struct NoexceptMove
{
    int value;
    NoexceptMove( int v ) noexcept : value( v ) {}
    NoexceptMove( NoexceptMove&& other ) noexcept : value( other.value ) { other.value = 0; }
    ~NoexceptMove() noexcept = default;
};

/// @brief Type that tracks construction and destruction
struct TrackedNoexcept
{
    int   value;
    bool* was_destroyed;
    int   magic;

    TrackedNoexcept( int v, bool* destroyed_flag ) noexcept
        : value( v ), was_destroyed( destroyed_flag ), magic( 0xDEADBEEF )
    {
    }

    ~TrackedNoexcept() noexcept
    {
        magic = 0;
        if ( was_destroyed != nullptr )
            *was_destroyed = true;
    }
};

// =============================================================================
// Compile-time verification of type traits
// =============================================================================

// These static_asserts verify that our test types have the correct noexcept properties.
// If any of these fail, the test types are incorrectly defined.

static_assert( std::is_nothrow_default_constructible_v<NoexceptDefault>,
               "NoexceptDefault must have noexcept default constructor" );
static_assert( std::is_nothrow_constructible_v<NoexceptWithArgs, int, float>,
               "NoexceptWithArgs must have noexcept constructor(int, float)" );
static_assert( std::is_nothrow_copy_constructible_v<NoexceptCopy>, "NoexceptCopy must have noexcept copy constructor" );
static_assert( std::is_nothrow_move_constructible_v<NoexceptMove>, "NoexceptMove must have noexcept move constructor" );
static_assert( std::is_nothrow_constructible_v<TrackedNoexcept, int, bool*>,
               "TrackedNoexcept must have noexcept constructor(int, bool*)" );

static_assert( std::is_nothrow_destructible_v<NoexceptDefault>, "NoexceptDefault must have noexcept destructor" );
static_assert( std::is_nothrow_destructible_v<NoexceptWithArgs>, "NoexceptWithArgs must have noexcept destructor" );
static_assert( std::is_nothrow_destructible_v<NoexceptCopy>, "NoexceptCopy must have noexcept destructor" );
static_assert( std::is_nothrow_destructible_v<NoexceptMove>, "NoexceptMove must have noexcept destructor" );
static_assert( std::is_nothrow_destructible_v<TrackedNoexcept>, "TrackedNoexcept must have noexcept destructor" );

// =============================================================================
// Runtime tests
// =============================================================================

/// @brief create_typed<T>() with noexcept default constructor works correctly
static bool test_p1_1_noexcept_default_constructor()
{
    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    auto p = Mgr::create_typed<NoexceptDefault>();
    PMM_TEST( !p.is_null() );

    NoexceptDefault* obj = Mgr::resolve( p );
    PMM_TEST( obj != nullptr );
    PMM_TEST( obj->value == 42 ); // constructor was called

    Mgr::destroy_typed( p );
    Mgr::destroy();
    return true;
}

/// @brief create_typed<T>(args...) with noexcept constructor with arguments works correctly
static bool test_p1_1_noexcept_constructor_with_args()
{
    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    auto p = Mgr::create_typed<NoexceptWithArgs>( 123, 3.14f );
    PMM_TEST( !p.is_null() );

    NoexceptWithArgs* obj = Mgr::resolve( p );
    PMM_TEST( obj != nullptr );
    PMM_TEST( obj->a == 123 );
    PMM_TEST( obj->b > 3.13f && obj->b < 3.15f ); // float comparison

    Mgr::destroy_typed( p );
    Mgr::destroy();
    return true;
}

/// @brief create_typed<T>(const T&) with noexcept copy constructor works correctly
static bool test_p1_1_noexcept_copy_constructor()
{
    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    NoexceptCopy original( 999 );
    auto         p = Mgr::create_typed<NoexceptCopy>( original );
    PMM_TEST( !p.is_null() );

    NoexceptCopy* obj = Mgr::resolve( p );
    PMM_TEST( obj != nullptr );
    PMM_TEST( obj->value == 999 ); // copy constructor was called

    Mgr::destroy_typed( p );
    Mgr::destroy();
    return true;
}

/// @brief create_typed<T>(T&&) with noexcept move constructor works correctly
static bool test_p1_1_noexcept_move_constructor()
{
    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    NoexceptMove movable( 777 );
    auto         p = Mgr::create_typed<NoexceptMove>( std::move( movable ) );
    PMM_TEST( !p.is_null() );

    NoexceptMove* obj = Mgr::resolve( p );
    PMM_TEST( obj != nullptr );
    PMM_TEST( obj->value == 777 );  // move constructor was called
    PMM_TEST( movable.value == 0 ); // original was moved from

    Mgr::destroy_typed( p );
    Mgr::destroy();
    return true;
}

/// @brief destroy_typed<T>() calls destructor correctly
static bool test_p1_1_destroy_typed_calls_destructor()
{
    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    bool was_destroyed = false;
    auto p             = Mgr::create_typed<TrackedNoexcept>( 555, &was_destroyed );
    PMM_TEST( !p.is_null() );

    TrackedNoexcept* obj = Mgr::resolve( p );
    PMM_TEST( obj != nullptr );
    PMM_TEST( obj->value == 555 );
    PMM_TEST( obj->magic == static_cast<int>( 0xDEADBEEF ) ); // constructor was called
    PMM_TEST( !was_destroyed );

    Mgr::destroy_typed( p );
    PMM_TEST( was_destroyed ); // destructor was called

    Mgr::destroy();
    return true;
}

/// @brief Primitive types (int, double, etc.) are noexcept constructible and work with create_typed
static bool test_p1_1_primitive_types()
{
    // Verify that primitive types satisfy the noexcept requirements
    static_assert( std::is_nothrow_constructible_v<int, int>, "int must be noexcept constructible" );
    static_assert( std::is_nothrow_constructible_v<double, double>, "double must be noexcept constructible" );
    static_assert( std::is_nothrow_destructible_v<int>, "int must be noexcept destructible" );
    static_assert( std::is_nothrow_destructible_v<double>, "double must be noexcept destructible" );

    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    // Test with int
    auto p_int = Mgr::create_typed<int>( 42 );
    PMM_TEST( !p_int.is_null() );
    PMM_TEST( *Mgr::resolve( p_int ) == 42 );
    Mgr::destroy_typed( p_int );

    // Test with double
    auto p_double = Mgr::create_typed<double>( 3.14159 );
    PMM_TEST( !p_double.is_null() );
    PMM_TEST( *Mgr::resolve( p_double ) > 3.14 && *Mgr::resolve( p_double ) < 3.15 );
    Mgr::destroy_typed( p_double );

    Mgr::destroy();
    return true;
}

/// @brief POD types are noexcept constructible by default and work with create_typed
static bool test_p1_1_pod_types()
{
    struct POD
    {
        int   x;
        float y;
        char  z;
    };

    static_assert( std::is_nothrow_default_constructible_v<POD>, "POD must be noexcept default constructible" );
    static_assert( std::is_nothrow_destructible_v<POD>, "POD must be noexcept destructible" );

    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    auto p = Mgr::create_typed<POD>();
    PMM_TEST( !p.is_null() );

    POD* obj = Mgr::resolve( p );
    PMM_TEST( obj != nullptr );

    // Initialize and verify
    obj->x = 100;
    obj->y = 2.5f;
    obj->z = 'A';
    PMM_TEST( obj->x == 100 );
    PMM_TEST( obj->y > 2.4f && obj->y < 2.6f );
    PMM_TEST( obj->z == 'A' );

    Mgr::destroy_typed( p );
    Mgr::destroy();
    return true;
}

// =============================================================================
// Documentation: Types that would FAIL the static_assert (compile-time error)
// =============================================================================

// The following types have throwing constructors or destructors.
// If you uncomment the test functions, they will cause a compile-time error
// due to the static_assert in create_typed and destroy_typed.
//
// This is the INTENDED behavior per Plan Phase 1, Task 1.1:
// "Add static_assert(std::is_nothrow_constructible_v<T, Args...>) in create_typed"

/*
struct ThrowingConstructor
{
    int value;
    ThrowingConstructor( int v ) : value( v )  // NOT noexcept!
    {
        if ( v < 0 )
            throw std::runtime_error( "Negative value" );
    }
};

static bool test_throwing_constructor_SHOULD_NOT_COMPILE()
{
    Mgr::destroy();
    Mgr::create( 64 * 1024 );

    // This line SHOULD NOT compile due to static_assert:
    auto p = Mgr::create_typed<ThrowingConstructor>( 42 );

    Mgr::destroy();
    return true;
}
*/

/*
struct ThrowingDestructor
{
    int value;
    ThrowingDestructor( int v ) noexcept : value( v ) {}
    ~ThrowingDestructor() noexcept(false)  // NOT noexcept!
    {
        if ( value < 0 )
            throw std::runtime_error( "Negative value on destroy" );
    }
};

static bool test_throwing_destructor_SHOULD_NOT_COMPILE()
{
    Mgr::destroy();
    Mgr::create( 64 * 1024 );

    auto p = Mgr::create_typed<ThrowingDestructor>( 42 );

    // This line SHOULD NOT compile due to static_assert:
    Mgr::destroy_typed( p );

    Mgr::destroy();
    return true;
}
*/

// =============================================================================
// Main
// =============================================================================

int main()
{
    std::cout << "Test suite: Plan Phase 1.1 — Exception safety in create_typed<T>\n";
    bool all_passed = true;

    PMM_RUN( "P1.1-A: create_typed<T>() with noexcept default constructor", test_p1_1_noexcept_default_constructor );
    PMM_RUN( "P1.1-B: create_typed<T>(args...) with noexcept constructor with args",
             test_p1_1_noexcept_constructor_with_args );
    PMM_RUN( "P1.1-C: create_typed<T>(const T&) with noexcept copy constructor", test_p1_1_noexcept_copy_constructor );
    PMM_RUN( "P1.1-D: create_typed<T>(T&&) with noexcept move constructor", test_p1_1_noexcept_move_constructor );
    PMM_RUN( "P1.1-E: destroy_typed<T>() calls destructor correctly", test_p1_1_destroy_typed_calls_destructor );
    PMM_RUN( "P1.1-F: Primitive types (int, double) work with create_typed", test_p1_1_primitive_types );
    PMM_RUN( "P1.1-G: POD types work with create_typed", test_p1_1_pod_types );

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
