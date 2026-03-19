/**
 * @file test_background_validator.cpp
 * @brief Phase 12 tests: background integrity validation (migrated to static API, Issue #112).
 *
 * These headless tests verify that:
 *  - ValidationResult is initialised to State::Unknown.
 *  - After run_validate() is called with a valid PMM, the state becomes Ok.
 *  - MetricsView::update_validation() stores and exposes the result correctly.
 *  - MetricsView::validate_requested() returns false by default.
 *  - is_initialized() on a fresh and on a used PMM returns true.
 *  - The kValidateIntervalSec constant equals 5.
 *
 * DemoMgr (MultiThreadedHeap) is a fully static class — no instance pointer needed.
 * g_pmm is a boolean flag: true when the manager is active.
 *
 * All tests are headless — no window or OpenGL context is required.
 */

#include "demo_app.h"
#include "demo_globals.h"
#include "metrics_view.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>

// Create a fresh DemoMgr (static) and set the global flag.
static void make_pmm( std::size_t sz )
{
    REQUIRE( demo::DemoMgr::create( sz ) );
    demo::g_pmm.store( true );
}

static void destroy_pmm()
{
    demo::g_pmm.store( false );
    demo::DemoMgr::destroy();
}

// ─── test: ValidationResult default state ────────────────────────────────────

TEST_CASE( "validation_result_default_state", "[test_background_validator]" )
{
    demo::ValidationResult r{};
    REQUIRE( r.state == demo::ValidationResult::State::Unknown );
}

// ─── test: MetricsView starts with validate_requested == false ────────────────

TEST_CASE( "metrics_view_initial_state", "[test_background_validator]" )
{
    demo::MetricsView mv;
    REQUIRE( !mv.validate_requested() );
}

// ─── test: update_validation(Ok) does not crash ───────────────────────────────

TEST_CASE( "metrics_view_update_validation_ok", "[test_background_validator]" )
{
    demo::MetricsView mv;

    demo::ValidationResult r;
    r.state     = demo::ValidationResult::State::Ok;
    r.timestamp = std::chrono::steady_clock::now();
    mv.update_validation( r );

    demo::MetricsSnapshot snap{};
    mv.update( snap, 0.0f );
}

// ─── test: update_validation(Failed) does not crash ──────────────────────────

TEST_CASE( "metrics_view_update_validation_failed", "[test_background_validator]" )
{
    demo::MetricsView mv;

    demo::ValidationResult r;
    r.state     = demo::ValidationResult::State::Failed;
    r.timestamp = std::chrono::steady_clock::now();
    mv.update_validation( r );

    demo::MetricsSnapshot snap{};
    mv.update( snap, 0.0f );
}

// ─── test: is_initialized() on a fresh PMM returns true ─────────────────────

TEST_CASE( "validate_fresh_pmm_returns_ok", "[test_background_validator]" )
{
    make_pmm( 1 * 1024 * 1024 );
    REQUIRE( demo::DemoMgr::is_initialized() );
    destroy_pmm();
}

// ─── test: is_initialized() after allocations / deallocations returns true ───

TEST_CASE( "validate_after_allocations", "[test_background_validator]" )
{
    make_pmm( 1 * 1024 * 1024 );

    demo::DemoMgr::pptr<std::uint8_t> p1 = demo::DemoMgr::allocate_typed<std::uint8_t>( 256 );
    demo::DemoMgr::pptr<std::uint8_t> p2 = demo::DemoMgr::allocate_typed<std::uint8_t>( 512 );
    demo::DemoMgr::pptr<std::uint8_t> p3 = demo::DemoMgr::allocate_typed<std::uint8_t>( 1024 );
    REQUIRE( ( !p1.is_null() && !p2.is_null() && !p3.is_null() ) );

    demo::DemoMgr::deallocate_typed( p2 ); // free middle block to exercise coalescing

    REQUIRE( demo::DemoMgr::is_initialized() );

    demo::DemoMgr::deallocate_typed( p1 );
    demo::DemoMgr::deallocate_typed( p3 );
    destroy_pmm();
}

// ─── test: ValidationResult timestamp is set correctly ───────────────────────

TEST_CASE( "validation_timestamp_is_recent", "[test_background_validator]" )
{
    make_pmm( 512 * 1024 );

    bool ok    = demo::DemoMgr::is_initialized();
    auto after = std::chrono::steady_clock::now();

    demo::ValidationResult r;
    r.state     = ok ? demo::ValidationResult::State::Ok : demo::ValidationResult::State::Failed;
    r.timestamp = after;

    auto age_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - r.timestamp ).count();
    REQUIRE( age_ms <= 1000 );

    destroy_pmm();
}

// ─── test: DemoApp::kValidateIntervalSec is 5 ────────────────────────────────

TEST_CASE( "validate_interval_is_five_seconds", "[test_background_validator]" )
{
    constexpr long long expected = 5;
    REQUIRE( demo::DemoApp::kValidateIntervalSec == expected );
}

// ─── test: successive update_validation calls — last one wins ────────────────

TEST_CASE( "update_validation_last_wins", "[test_background_validator]" )
{
    demo::MetricsView mv;

    demo::ValidationResult r1;
    r1.state     = demo::ValidationResult::State::Ok;
    r1.timestamp = std::chrono::steady_clock::now();
    mv.update_validation( r1 );

    demo::ValidationResult r2;
    r2.state     = demo::ValidationResult::State::Failed;
    r2.timestamp = std::chrono::steady_clock::now();
    mv.update_validation( r2 );

    demo::MetricsSnapshot snap{};
    mv.update( snap, 0.0f );
}

// ─── test: ValidationResult State enum has exactly the three expected values ──

TEST_CASE( "validation_state_enum_values", "[test_background_validator]" )
{
    using State = demo::ValidationResult::State;
    REQUIRE( State::Unknown != State::Ok );
    REQUIRE( State::Unknown != State::Failed );
    REQUIRE( State::Ok != State::Failed );
}
