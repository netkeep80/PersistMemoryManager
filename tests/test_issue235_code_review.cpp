/**
 * @file test_issue235_code_review.cpp
 * @brief Tests: code review improvements.
 *
 * Covers:
 *   - typed_guard RAII scope-guard for persistent objects with cleanup methods
 *   - HeapStorage minimum initial allocation size
 *   - [[deprecated]] attribute presence on legacy functions
 *
 * @see include/pmm/typed_guard.h
 * @see include/pmm/heap_storage.h
 * @see include/pmm/types.h
 * @version 0.1
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <type_traits>

using Mgr = pmm::presets::SingleThreadedHeap;

namespace
{

struct Issue235FreeAllProbe
{
    Issue235FreeAllProbe() noexcept  = default;
    ~Issue235FreeAllProbe() noexcept = default;

    void free_all() noexcept { ++cleanup_calls; }

    static inline int cleanup_calls = 0;
};

} // namespace

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

TEST_CASE( "I235-A3: typed_guard supports free_all cleanup on scope exit", "[test_issue235]" )
{
    Issue235FreeAllProbe::cleanup_calls = 0;
    Mgr::create( 64 * 1024 );
    {
        auto guard = Mgr::make_guard<Issue235FreeAllProbe>();
        REQUIRE( guard );
    }
    REQUIRE( Issue235FreeAllProbe::cleanup_calls == 1 );
    Mgr::destroy();
}

TEST_CASE( "I235-A4: typed_guard move semantics", "[test_issue235]" )
{
    Mgr::create( 64 * 1024 );
    {
        auto guard1 = Mgr::make_guard<Mgr::pstring>();
        guard1->assign( "test" );

        // Move construction — guard1 becomes empty, guard2 owns the object.
        auto guard2 = std::move( guard1 );
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

TEST_CASE( "I235-C2: HasFreeAll concept detects free_all cleanup", "[test_issue235]" )
{
    static_assert( (!pmm::HasFreeData<Issue235FreeAllProbe>));
    static_assert( pmm::HasFreeAll<Issue235FreeAllProbe> );
}

TEST_CASE( "I235-C3: HasFreeData concept detects parray", "[test_issue235]" )
{
    static_assert( pmm::HasFreeData<Mgr::parray<int>> );
}

// =============================================================================
// main
// =============================================================================
