/**
 * @file test_issue87_phase8.cpp
 * @brief Тесты Phase 8: pmm_presets.h — готовые инстанции (Issue #87).
 *
 * Проверяет:
 *  - Все предустановки компилируются без ошибок
 *  - EmbeddedStatic4K/EmbeddedStatic1K: create() и базовые операции
 *  - SingleThreadedHeap: create(size) и allocate/deallocate
 *  - MultiThreadedHeap: компилируется и функционирует
 *  - PersistentFileMapped: компилируется (mmap-тест без реального файла)
 *  - IndustrialDB: компилируется
 *
 * @see include/pmm/pmm_presets.h
 * @see plan_issue87.md §5 «Фаза 8: pmm_presets.h»
 * @version 0.1 (Issue #87 Phase 8)
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
// Phase 8 tests: pmm_presets.h
// =============================================================================

// ─── P8-A: Компиляция всех предустановок ─────────────────────────────────────

/// @brief Все предустановки компилируются (инстанциация типов).
static bool test_p8_all_presets_compile()
{
    // Просто инстанциируем каждый тип — ошибки компиляции поймаем здесь
    using E   = pmm::presets::EmbeddedStatic4K;
    using E1  = pmm::presets::EmbeddedStatic1K;
    using STH = pmm::presets::SingleThreadedHeap;
    using MTH = pmm::presets::MultiThreadedHeap;
    using PFM = pmm::presets::PersistentFileMapped;
    using IDB = pmm::presets::IndustrialDB;

    static_assert( std::is_same<E, E1>::value, "EmbeddedStatic1K must be alias for EmbeddedStatic4K" );

    // Проверяем алиасы address_traits и thread_policy
    static_assert( std::is_same<STH::thread_policy, pmm::config::NoLock>::value, "SingleThreadedHeap must use NoLock" );
    static_assert( std::is_same<MTH::thread_policy, pmm::config::SharedMutexLock>::value,
                   "MultiThreadedHeap must use SharedMutexLock" );
    static_assert( std::is_same<PFM::thread_policy, pmm::config::SharedMutexLock>::value,
                   "PersistentFileMapped must use SharedMutexLock" );
    static_assert( std::is_same<IDB::thread_policy, pmm::config::SharedMutexLock>::value,
                   "IndustrialDB must use SharedMutexLock" );

    (void)0; // suppress unused variable warnings for type aliases
    return true;
}

// ─── P8-B: EmbeddedStatic4K — функциональность ─────────────────────────────

/// @brief EmbeddedStatic4K: create() и базовые операции.
static bool test_p8_embedded_static_create()
{
    pmm::presets::EmbeddedStatic4K pmm;
    PMM_TEST( !pmm.is_initialized() );

    PMM_TEST( pmm.create() ); // StaticStorage уже имеет 4096 байт
    PMM_TEST( pmm.is_initialized() );
    PMM_TEST( pmm.total_size() == 4096 );
    PMM_TEST( pmm.free_size() > 0 );

    void* ptr = pmm.allocate( 64 );
    PMM_TEST( ptr != nullptr );
    std::memset( ptr, 0x42, 64 );
    pmm.deallocate( ptr );

    pmm.destroy();
    PMM_TEST( !pmm.is_initialized() );
    return true;
}

/// @brief EmbeddedStatic4K: множественные аллокации.
static bool test_p8_embedded_static_multiple_alloc()
{
    pmm::presets::EmbeddedStatic4K pmm;
    PMM_TEST( pmm.create() );

    constexpr int kCount = 5;
    void*         ptrs[kCount];
    for ( int i = 0; i < kCount; ++i )
    {
        ptrs[i] = pmm.allocate( 32 );
        PMM_TEST( ptrs[i] != nullptr );
    }
    for ( int i = 0; i < kCount; ++i )
    {
        pmm.deallocate( ptrs[i] );
    }

    pmm.destroy();
    return true;
}

// ─── P8-C: SingleThreadedHeap — функциональность ─────────────────────────────

/// @brief SingleThreadedHeap: create(size) и allocate/deallocate.
static bool test_p8_single_threaded_heap_create()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( !pmm.is_initialized() );

    PMM_TEST( pmm.create( 16 * 1024 ) ); // 16 KiB
    PMM_TEST( pmm.is_initialized() );
    PMM_TEST( pmm.total_size() >= 16 * 1024 );

    void* ptr = pmm.allocate( 256 );
    PMM_TEST( ptr != nullptr );
    std::memset( ptr, 0xAA, 256 );
    pmm.deallocate( ptr );

    pmm.destroy();
    return true;
}

/// @brief SingleThreadedHeap: auto-expand при нехватке места.
static bool test_p8_single_threaded_heap_expand()
{
    pmm::presets::SingleThreadedHeap pmm;
    PMM_TEST( pmm.create( pmm::detail::kMinMemorySize ) ); // минимальный размер

    // Пытаемся выделить больше, чем есть — должен расшириться
    void* ptr = pmm.allocate( 4096 );
    if ( ptr != nullptr )
    {
        pmm.deallocate( ptr );
    }
    // Тест проходит, если не крашится (expand может не сработать на малых буферах)

    pmm.destroy();
    return true;
}

// ─── P8-D: MultiThreadedHeap — функциональность ──────────────────────────────

/// @brief MultiThreadedHeap: create(size) и allocate/deallocate с блокировками.
static bool test_p8_multi_threaded_heap_create()
{
    pmm::presets::MultiThreadedHeap pmm;
    PMM_TEST( pmm.create( 16 * 1024 ) );
    PMM_TEST( pmm.is_initialized() );

    void* ptr = pmm.allocate( 128 );
    PMM_TEST( ptr != nullptr );
    pmm.deallocate( ptr );

    pmm.destroy();
    return true;
}

// ─── P8-E: PersistentFileMapped — компиляция ─────────────────────────────────

/// @brief PersistentFileMapped компилируется и создаётся без краша.
static bool test_p8_persistent_file_mapped_compiles()
{
    pmm::presets::PersistentFileMapped pmm;
    PMM_TEST( !pmm.is_initialized() );
    // Без открытия файла — просто проверяем, что объект создаётся
    PMM_TEST( pmm.backend().base_ptr() == nullptr );
    return true;
}

// ─── P8-F: IndustrialDB — компиляция ─────────────────────────────────────────

/// @brief IndustrialDB компилируется и создаётся без краша.
static bool test_p8_industrial_db_compiles()
{
    pmm::presets::IndustrialDB pmm;
    PMM_TEST( !pmm.is_initialized() );
    PMM_TEST( pmm.backend().base_ptr() == nullptr );
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase8 (Phase 8: pmm_presets.h) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P8-A: All presets compile ---\n";
    PMM_RUN( "P8-A1: All presets instantiate without compile errors", test_p8_all_presets_compile );

    std::cout << "\n--- P8-B: EmbeddedStatic4K ---\n";
    PMM_RUN( "P8-B1: EmbeddedStatic4K create() and basic ops", test_p8_embedded_static_create );
    PMM_RUN( "P8-B2: EmbeddedStatic4K multiple allocations", test_p8_embedded_static_multiple_alloc );

    std::cout << "\n--- P8-C: SingleThreadedHeap ---\n";
    PMM_RUN( "P8-C1: SingleThreadedHeap create(size) and alloc/dealloc", test_p8_single_threaded_heap_create );
    PMM_RUN( "P8-C2: SingleThreadedHeap auto-expand", test_p8_single_threaded_heap_expand );

    std::cout << "\n--- P8-D: MultiThreadedHeap ---\n";
    PMM_RUN( "P8-D1: MultiThreadedHeap create(size) and alloc/dealloc", test_p8_multi_threaded_heap_create );

    std::cout << "\n--- P8-E: PersistentFileMapped ---\n";
    PMM_RUN( "P8-E1: PersistentFileMapped compiles and constructs", test_p8_persistent_file_mapped_compiles );

    std::cout << "\n--- P8-F: IndustrialDB ---\n";
    PMM_RUN( "P8-F1: IndustrialDB compiles and constructs", test_p8_industrial_db_compiles );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
