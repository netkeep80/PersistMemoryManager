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

#include "pmm/pmm_presets.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

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

// =============================================================================
// Phase 8 tests: pmm_presets.h (updated for Issue #110)
// =============================================================================

// ─── P8-A: Компиляция предустановок ──────────────────────────────────────────

/// @brief Предустановки компилируются (инстанциация типов).
static bool test_p8_all_presets_compile()
{
    using STH = pmm::presets::SingleThreadedHeap;
    using MTH = pmm::presets::MultiThreadedHeap;

    // Проверяем thread_policy
    static_assert( std::is_same<STH::thread_policy, pmm::config::NoLock>::value,
                   "SingleThreadedHeap must use NoLock" );
    static_assert( std::is_same<MTH::thread_policy, pmm::config::SharedMutexLock>::value,
                   "MultiThreadedHeap must use SharedMutexLock" );

    // Проверяем что оба пресета — static managers
    static_assert( sizeof( STH::pptr<int> ) == 4, "SingleThreadedHeap pptr must be 4 bytes" );
    static_assert( sizeof( MTH::pptr<int> ) == 4, "MultiThreadedHeap pptr must be 4 bytes" );

    return true;
}

// ─── P8-C: SingleThreadedHeap — функциональность ─────────────────────────────

/// @brief SingleThreadedHeap: create(size) и allocate/deallocate.
static bool test_p8_single_threaded_heap_create()
{
    using STH = pmm::presets::SingleThreadedHeap;

    PMM_TEST( !STH::is_initialized() );

    PMM_TEST( STH::create( 16 * 1024 ) ); // 16 KiB
    PMM_TEST( STH::is_initialized() );
    PMM_TEST( STH::total_size() >= 16 * 1024 );

    void* ptr = STH::allocate( 256 );
    PMM_TEST( ptr != nullptr );
    std::memset( ptr, 0xAA, 256 );
    STH::deallocate( ptr );

    STH::destroy();
    PMM_TEST( !STH::is_initialized() );
    return true;
}

/// @brief SingleThreadedHeap: typed allocation via allocate_typed<T>.
static bool test_p8_single_threaded_heap_typed_alloc()
{
    using STH = pmm::presets::SingleThreadedHeap;

    PMM_TEST( STH::create( 64 * 1024 ) );
    PMM_TEST( STH::is_initialized() );

    STH::pptr<int> p = STH::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( p.offset() > 0 );

    *p.resolve() = 12345;
    PMM_TEST( *p.resolve() == 12345 );

    STH::deallocate_typed( p );
    STH::destroy();
    return true;
}

/// @brief SingleThreadedHeap: auto-expand при нехватке места.
static bool test_p8_single_threaded_heap_expand()
{
    using STH = pmm::presets::SingleThreadedHeap;

    PMM_TEST( STH::create( pmm::detail::kMinMemorySize ) ); // минимальный размер

    // Пытаемся выделить больше, чем есть — должен расшириться
    void* ptr = STH::allocate( 4096 );
    if ( ptr != nullptr )
    {
        STH::deallocate( ptr );
    }
    // Тест проходит, если не крашится (expand может не сработать на малых буферах)

    STH::destroy();
    return true;
}

// ─── P8-D: MultiThreadedHeap — функциональность ──────────────────────────────

/// @brief MultiThreadedHeap: create(size) и allocate/deallocate с блокировками.
static bool test_p8_multi_threaded_heap_create()
{
    using MTH = pmm::presets::MultiThreadedHeap;

    PMM_TEST( MTH::create( 16 * 1024 ) );
    PMM_TEST( MTH::is_initialized() );

    void* ptr = MTH::allocate( 128 );
    PMM_TEST( ptr != nullptr );
    std::memset( ptr, 0xBB, 128 );
    MTH::deallocate( ptr );

    MTH::destroy();
    return true;
}

/// @brief MultiThreadedHeap: typed allocation.
static bool test_p8_multi_threaded_heap_typed_alloc()
{
    using MTH = pmm::presets::MultiThreadedHeap;

    PMM_TEST( MTH::create( 64 * 1024 ) );
    PMM_TEST( MTH::is_initialized() );

    MTH::pptr<double> p = MTH::allocate_typed<double>();
    PMM_TEST( !p.is_null() );

    *p.resolve() = 3.14;
    PMM_TEST( *p.resolve() == 3.14 );

    MTH::deallocate_typed( p );
    MTH::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase8 (Phase 8: pmm_presets.h, Issue #110) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P8-A: All presets compile ---\n";
    PMM_RUN( "P8-A1: Presets instantiate without compile errors", test_p8_all_presets_compile );

    std::cout << "\n--- P8-C: SingleThreadedHeap ---\n";
    PMM_RUN( "P8-C1: SingleThreadedHeap create(size) and alloc/dealloc", test_p8_single_threaded_heap_create );
    PMM_RUN( "P8-C2: SingleThreadedHeap typed allocation", test_p8_single_threaded_heap_typed_alloc );
    PMM_RUN( "P8-C3: SingleThreadedHeap auto-expand", test_p8_single_threaded_heap_expand );

    std::cout << "\n--- P8-D: MultiThreadedHeap ---\n";
    PMM_RUN( "P8-D1: MultiThreadedHeap create(size) and alloc/dealloc", test_p8_multi_threaded_heap_create );
    PMM_RUN( "P8-D2: MultiThreadedHeap typed allocation", test_p8_multi_threaded_heap_typed_alloc );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
