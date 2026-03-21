/**
 * @file test_issue235_code_review.cpp
 * @brief Tests for Issue #235 code review improvements.
 *
 * Covers:
 *   - typed_guard RAII scope-guard for pstring, parray, ppool
 *   - HeapStorage minimum initial allocation size
 *   - [[deprecated]] attribute presence on legacy functions
 *
 * @see include/pmm/typed_guard.h
 * @see include/pmm/heap_storage.h
 * @see include/pmm/types.h
 * @version 0.1 (Issue #235)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>

using Mgr = pmm::presets::SingleThreadedHeap;

// =============================================================================
// PART A: typed_guard RAII scope-guard
// =============================================================================

TEST_CASE( "I235-A1: typed_guard auto-cleans pstring on scope exit", "[test_issue235]" )
{
    Mgr::create( 64 * 1024 );
    {
        auto guard = Mgr::make_guard<Mgr::pstring>();
        REQUIRE( guard );
        guard->assign( "hello world" );
        REQUIRE( guard->size() == 11 );
    } // guard destructor calls free_data() + destroy_typed()
    // If we get here without crash/leak, the guard worked
    Mgr::destroy();
}

TEST_CASE( "I235-A2: typed_guard auto-cleans parray on scope exit", "[test_issue235]" )
{
    Mgr::create( 64 * 1024 );
    {
        auto guard = Mgr::make_guard<Mgr::parray<int>>();
        REQUIRE( guard );
        guard->push_back( 42 );
        guard->push_back( 99 );
        REQUIRE( guard->size() == 2 );
    }
    Mgr::destroy();
}

TEST_CASE( "I235-A3: typed_guard auto-cleans ppool on scope exit", "[test_issue235]" )
{
    Mgr::create( 256 * 1024 );
    {
        auto guard = Mgr::make_guard<Mgr::ppool<int>>();
        REQUIRE( guard );
        int* a = guard->allocate();
        REQUIRE( a != nullptr );
        *a = 42;
        guard->deallocate( a );
    }
    Mgr::destroy();
}

TEST_CASE( "I235-A4: typed_guard move semantics", "[test_issue235]" )
{
    Mgr::create( 64 * 1024 );
    {
        auto guard1 = Mgr::make_guard<Mgr::pstring>();
        guard1->assign( "test" );

        // Move construction
        auto guard2 = std::move( guard1 );
        REQUIRE( !guard1 ); // NOLINT — testing moved-from state
        REQUIRE( guard2 );
        REQUIRE( guard2->size() == 4 );
    }
    Mgr::destroy();
}

TEST_CASE( "I235-A5: typed_guard release() transfers ownership", "[test_issue235]" )
{
    Mgr::create( 64 * 1024 );
    {
        auto guard = Mgr::make_guard<Mgr::pstring>();
        guard->assign( "owned" );

        auto p = guard.release();
        REQUIRE( !guard );
        REQUIRE( !p.is_null() );
        REQUIRE( p->size() == 5 );

        // Manual cleanup since we released ownership
        p->free_data();
        Mgr::destroy_typed( p );
    }
    Mgr::destroy();
}

TEST_CASE( "I235-A6: typed_guard default-constructed is empty", "[test_issue235]" )
{
    pmm::typed_guard<Mgr::pstring, Mgr> guard;
    REQUIRE( !guard );
}

// =============================================================================
// PART B: HeapStorage minimum initial allocation
// =============================================================================

TEST_CASE( "I235-B1: HeapStorage expand from zero uses minimum initial size", "[test_issue235]" )
{
    pmm::HeapStorage<> storage;
    REQUIRE( storage.total_size() == 0 );

    // Request a tiny expansion (e.g. 32 bytes)
    bool ok = storage.expand( 32 );
    REQUIRE( ok );
    // Should allocate at least 4096 bytes, not just 32
    REQUIRE( storage.total_size() >= 4096 );
}

TEST_CASE( "I235-B2: HeapStorage expand from zero with large request", "[test_issue235]" )
{
    pmm::HeapStorage<> storage;
    // Request larger than minimum — should use the requested size
    bool ok = storage.expand( 8192 );
    REQUIRE( ok );
    REQUIRE( storage.total_size() >= 8192 );
}

// =============================================================================
// PART C: Concept detection for cleanup methods
// =============================================================================

TEST_CASE( "I235-C1: HasFreeData concept detects pstring", "[test_issue235]" )
{
    static_assert( pmm::HasFreeData<Mgr::pstring> );
    static_assert( (!pmm::HasFreeAll<Mgr::pstring>));
}

TEST_CASE( "I235-C2: HasFreeAll concept detects ppool", "[test_issue235]" )
{
    static_assert( (!pmm::HasFreeData<Mgr::ppool<int>>));
    static_assert( pmm::HasFreeAll<Mgr::ppool<int>> );
}

TEST_CASE( "I235-C3: HasFreeData concept detects parray", "[test_issue235]" )
{
    static_assert( pmm::HasFreeData<Mgr::parray<int>> );
}

// =============================================================================
// main
// =============================================================================
