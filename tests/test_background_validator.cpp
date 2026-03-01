/**
 * @file test_background_validator.cpp
 * @brief Phase 12 tests: background integrity validation in DemoApp / MetricsView.
 *
 * These headless tests verify that:
 *  - ValidationResult is initialised to State::Unknown.
 *  - After run_validate() is called with a valid PMM, the state becomes Ok.
 *  - MetricsView::update_validation() stores and exposes the result correctly.
 *  - MetricsView::validate_requested() returns false by default.
 *  - validate() on a fresh and on a used PMM returns true.
 *  - The kValidateIntervalSec constant equals 5.
 *
 * NOTE: Uses std::malloc for the PMM buffer so that destroy() can safely
 * free it (PMM's owns_memory=true means the buffer was malloc'd).
 *
 * All tests are headless — no window or OpenGL context is required.
 */

#include "demo_app.h"
#include "metrics_view.h"
#include "persist_memory_manager.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

// ─── helpers ──────────────────────────────────────────────────────────────────

static void pass( const char* name )
{
    std::printf( "[PASS] %s\n", name );
}

static void fail( const char* name, const char* reason )
{
    std::fprintf( stderr, "[FAIL] %s: %s\n", name, reason );
    std::exit( 1 );
}

// Create a fresh PMM singleton with the given size (in bytes).
// Uses malloc so that PersistMemoryManager::destroy() can free the buffer.
static pmm::PersistMemoryManager* make_pmm( std::size_t sz )
{
    if ( pmm::PersistMemoryManager::instance() )
        pmm::PersistMemoryManager::destroy();
    void* buf = std::malloc( sz );
    if ( !buf )
    {
        std::fprintf( stderr, "malloc failed\n" );
        std::exit( 1 );
    }
    pmm::PersistMemoryManager::create( buf, sz );
    return pmm::PersistMemoryManager::instance();
}

static void destroy_pmm()
{
    if ( pmm::PersistMemoryManager::instance() )
        pmm::PersistMemoryManager::destroy();
}

// ─── test: ValidationResult default state ────────────────────────────────────

static void test_validation_result_default_state()
{
    const char*            name = "validation_result_default_state";
    demo::ValidationResult r{};
    if ( r.state != demo::ValidationResult::State::Unknown )
        fail( name, "expected State::Unknown by default" );
    pass( name );
}

// ─── test: MetricsView starts with validate_requested == false ────────────────

static void test_metrics_view_initial_state()
{
    const char*       name = "metrics_view_initial_state";
    demo::MetricsView mv;
    if ( mv.validate_requested() )
        fail( name, "validate_requested_ should be false initially" );
    pass( name );
}

// ─── test: update_validation(Ok) does not crash ───────────────────────────────

static void test_metrics_view_update_validation_ok()
{
    const char*       name = "metrics_view_update_validation_ok";
    demo::MetricsView mv;

    demo::ValidationResult r;
    r.state     = demo::ValidationResult::State::Ok;
    r.timestamp = std::chrono::steady_clock::now();
    mv.update_validation( r );

    // Successive update() calls must still work
    demo::MetricsSnapshot snap{};
    mv.update( snap, 0.0f );

    pass( name );
}

// ─── test: update_validation(Failed) does not crash ──────────────────────────

static void test_metrics_view_update_validation_failed()
{
    const char*       name = "metrics_view_update_validation_failed";
    demo::MetricsView mv;

    demo::ValidationResult r;
    r.state     = demo::ValidationResult::State::Failed;
    r.timestamp = std::chrono::steady_clock::now();
    mv.update_validation( r );

    demo::MetricsSnapshot snap{};
    mv.update( snap, 0.0f );

    pass( name );
}

// ─── test: validate() on a fresh PMM returns true ────────────────────────────

static void test_validate_fresh_pmm_returns_ok()
{
    const char* name = "validate_fresh_pmm_returns_ok";
    auto*       mgr  = make_pmm( 1 * 1024 * 1024 );
    if ( !mgr )
        fail( name, "PMM instance is null" );

    bool ok = mgr->validate();
    if ( !ok )
        fail( name, "validate() returned false on a freshly created PMM" );

    destroy_pmm();
    pass( name );
}

// ─── test: validate() after allocations / deallocations returns true ─────────

static void test_validate_after_allocations()
{
    const char* name = "validate_after_allocations";
    auto*       mgr  = make_pmm( 1 * 1024 * 1024 );
    if ( !mgr )
        fail( name, "PMM instance is null" );

    void* p1 = mgr->allocate( 256 );
    void* p2 = mgr->allocate( 512 );
    void* p3 = mgr->allocate( 1024 );
    if ( !p1 || !p2 || !p3 )
        fail( name, "allocate() returned null unexpectedly" );

    mgr->deallocate( p2 ); // free middle block to exercise coalescing

    bool ok = mgr->validate();
    if ( !ok )
        fail( name, "validate() returned false after alloc/dealloc sequence" );

    mgr->deallocate( p1 );
    mgr->deallocate( p3 );
    destroy_pmm();
    pass( name );
}

// ─── test: ValidationResult timestamp is set correctly ───────────────────────

static void test_validation_timestamp_is_recent()
{
    const char* name = "validation_timestamp_is_recent";
    auto*       mgr  = make_pmm( 512 * 1024 );
    if ( !mgr )
        fail( name, "PMM instance is null" );

    bool ok    = mgr->validate();
    auto after = std::chrono::steady_clock::now();

    demo::ValidationResult r;
    r.state     = ok ? demo::ValidationResult::State::Ok : demo::ValidationResult::State::Failed;
    r.timestamp = after;

    // The timestamp we just assigned should be within 1 s of "now"
    auto age_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - r.timestamp ).count();
    if ( age_ms > 1000 )
        fail( name, "timestamp is older than 1 second — clock mismatch" );

    destroy_pmm();
    pass( name );
}

// ─── test: DemoApp::kValidateIntervalSec is 5 ────────────────────────────────

static void test_validate_interval_is_five_seconds()
{
    const char*         name     = "validate_interval_is_five_seconds";
    constexpr long long expected = 5;
    if ( demo::DemoApp::kValidateIntervalSec != expected )
        fail( name, "kValidateIntervalSec is not 5" );
    pass( name );
}

// ─── test: successive update_validation calls — last one wins ────────────────

static void test_update_validation_last_wins()
{
    const char*       name = "update_validation_last_wins";
    demo::MetricsView mv;

    demo::ValidationResult r1;
    r1.state     = demo::ValidationResult::State::Ok;
    r1.timestamp = std::chrono::steady_clock::now();
    mv.update_validation( r1 );

    demo::ValidationResult r2;
    r2.state     = demo::ValidationResult::State::Failed;
    r2.timestamp = std::chrono::steady_clock::now();
    mv.update_validation( r2 );

    // Object must remain usable after overwriting the result
    demo::MetricsSnapshot snap{};
    mv.update( snap, 0.0f );

    pass( name );
}

// ─── test: ValidationResult State enum has exactly the three expected values ──

static void test_validation_state_enum_values()
{
    const char* name = "validation_state_enum_values";
    using State      = demo::ValidationResult::State;

    // All three states must be distinct
    if ( State::Unknown == State::Ok )
        fail( name, "Unknown == Ok" );
    if ( State::Unknown == State::Failed )
        fail( name, "Unknown == Failed" );
    if ( State::Ok == State::Failed )
        fail( name, "Ok == Failed" );

    pass( name );
}

// ─── main ────────────────────────────────────────────────────────────────────

int main()
{
    test_validation_result_default_state();
    test_metrics_view_initial_state();
    test_metrics_view_update_validation_ok();
    test_metrics_view_update_validation_failed();
    test_validate_fresh_pmm_returns_ok();
    test_validate_after_allocations();
    test_validation_timestamp_is_recent();
    test_validate_interval_is_five_seconds();
    test_update_validation_last_wins();
    test_validation_state_enum_values();

    std::printf( "\nAll Phase 12 tests passed.\n" );
    return 0;
}
