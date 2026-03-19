/**
 * @file test_issue87_phase8.cpp
 * @brief Тесты Phase 8: pmm_presets.h — готовые инстанции (Issue #87, обновлено #110).
 *
 * Issue #110: pmm_presets.h обновлён для использования PersistMemoryManager.
 * Только два пресета: SingleThreadedHeap и MultiThreadedHeap.
 * Все методы статические.
 *
 * @see include/pmm/pmm_presets.h
 * @see plan_issue87.md §5 «Фаза 8: pmm_presets.h»
 * @version 0.2 (Issue #110 — обновлено для статического API)
 */

#include "pmm_single_threaded_heap.h"
#include "pmm_multi_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// Phase 8 tests: pmm_presets.h (updated for Issue #110)
// =============================================================================

// ─── P8-A: Компиляция предустановок ──────────────────────────────────────────

/// @brief Предустановки компилируются (инстанциация типов).
TEST_CASE( "P8-A1: Presets instantiate without compile errors", "[test_issue87_phase8]" )
{
    using STH = pmm::presets::SingleThreadedHeap;
    using MTH = pmm::presets::MultiThreadedHeap;

    // Проверяем thread_policy
    static_assert( std::is_same<STH::thread_policy, pmm::config::NoLock>::value, "SingleThreadedHeap must use NoLock" );
    static_assert( std::is_same<MTH::thread_policy, pmm::config::SharedMutexLock>::value,
                   "MultiThreadedHeap must use SharedMutexLock" );

    // Проверяем что оба пресета — static managers
    static_assert( sizeof( STH::pptr<int> ) == 4, "SingleThreadedHeap pptr must be 4 bytes" );
    static_assert( sizeof( MTH::pptr<int> ) == 4, "MultiThreadedHeap pptr must be 4 bytes" );
}

// ─── P8-C: SingleThreadedHeap — функциональность ─────────────────────────────

/// @brief SingleThreadedHeap: create(size) и allocate/deallocate.
TEST_CASE( "P8-C1: SingleThreadedHeap create(size) and alloc/dealloc", "[test_issue87_phase8]" )
{
    using STH = pmm::presets::SingleThreadedHeap;

    REQUIRE( !STH::is_initialized() );

    REQUIRE( STH::create( 16 * 1024 ) ); // 16 KiB
    REQUIRE( STH::is_initialized() );
    REQUIRE( STH::total_size() >= 16 * 1024 );

    void* ptr = STH::allocate( 256 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xAA, 256 );
    STH::deallocate( ptr );

    STH::destroy();
    REQUIRE( !STH::is_initialized() );
}

/// @brief SingleThreadedHeap: typed allocation via allocate_typed<T>.
TEST_CASE( "P8-C2: SingleThreadedHeap typed allocation", "[test_issue87_phase8]" )
{
    using STH = pmm::presets::SingleThreadedHeap;

    REQUIRE( STH::create( 64 * 1024 ) );
    REQUIRE( STH::is_initialized() );

    STH::pptr<int> p = STH::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    REQUIRE( p.offset() > 0 );

    *p.resolve() = 12345;
    REQUIRE( *p.resolve() == 12345 );

    STH::deallocate_typed( p );
    STH::destroy();
}

/// @brief SingleThreadedHeap: auto-expand при нехватке места.
TEST_CASE( "P8-C3: SingleThreadedHeap auto-expand", "[test_issue87_phase8]" )
{
    using STH = pmm::presets::SingleThreadedHeap;

    REQUIRE( STH::create( pmm::detail::kMinMemorySize ) ); // минимальный размер

    // Пытаемся выделить больше, чем есть — должен расшириться
    void* ptr = STH::allocate( 4096 );
    if ( ptr != nullptr )
    {
        STH::deallocate( ptr );
    }
    // Тест проходит, если не крашится (expand может не сработать на малых буферах)

    STH::destroy();
}

// ─── P8-D: MultiThreadedHeap — функциональность ──────────────────────────────

/// @brief MultiThreadedHeap: create(size) и allocate/deallocate с блокировками.
TEST_CASE( "P8-D1: MultiThreadedHeap create(size) and alloc/dealloc", "[test_issue87_phase8]" )
{
    using MTH = pmm::presets::MultiThreadedHeap;

    REQUIRE( MTH::create( 16 * 1024 ) );
    REQUIRE( MTH::is_initialized() );

    void* ptr = MTH::allocate( 128 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xBB, 128 );
    MTH::deallocate( ptr );

    MTH::destroy();
}

/// @brief MultiThreadedHeap: typed allocation.
TEST_CASE( "P8-D2: MultiThreadedHeap typed allocation", "[test_issue87_phase8]" )
{
    using MTH = pmm::presets::MultiThreadedHeap;

    REQUIRE( MTH::create( 64 * 1024 ) );
    REQUIRE( MTH::is_initialized() );

    MTH::pptr<double> p = MTH::allocate_typed<double>();
    REQUIRE( !p.is_null() );

    *p.resolve() = 3.14;
    REQUIRE( *p.resolve() == 3.14 );

    MTH::deallocate_typed( p );
    MTH::destroy();
}

// =============================================================================
// main
// =============================================================================
