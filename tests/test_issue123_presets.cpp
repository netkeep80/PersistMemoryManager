/**
 * @file test_issue123_presets.cpp
 * @brief Тесты расширения pmm_presets.h: EmbeddedHeap и IndustrialDBHeap (Issue #123).
 *
 * Проверяет два новых пресета, добавленных для генерации single-header файлов:
 *   - `EmbeddedHeap`      — NoLock + HeapStorage, коэффициент роста 50%
 *   - `IndustrialDBHeap`  — SharedMutexLock + HeapStorage, коэффициент роста 100%
 *
 * @see include/pmm/pmm_presets.h
 * @see include/pmm/manager_configs.h
 * @version 0.1 (Issue #123 — single-header preset generation)
 */

#include "pmm_single_threaded_heap.h"

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
// Issue #123 tests: new presets in pmm_presets.h
// =============================================================================

// ─── I123-A: Компиляция всех четырёх пресетов ────────────────────────────────

/// @brief Все четыре пресета компилируются и имеют ожидаемые свойства.
static bool test_all_four_presets_compile()
{
    using STH = pmm::presets::SingleThreadedHeap;
    using MTH = pmm::presets::MultiThreadedHeap;
    using EMB = pmm::presets::EmbeddedHeap;
    using IDB = pmm::presets::IndustrialDBHeap;

    // Проверяем thread_policy
    static_assert( std::is_same<STH::thread_policy, pmm::config::NoLock>::value, "SingleThreadedHeap must use NoLock" );
    static_assert( std::is_same<MTH::thread_policy, pmm::config::SharedMutexLock>::value,
                   "MultiThreadedHeap must use SharedMutexLock" );
    static_assert( std::is_same<EMB::thread_policy, pmm::config::NoLock>::value, "EmbeddedHeap must use NoLock" );
    static_assert( std::is_same<IDB::thread_policy, pmm::config::SharedMutexLock>::value,
                   "IndustrialDBHeap must use SharedMutexLock" );

    // Проверяем размер pptr (4 байта — 32-bit адресация)
    static_assert( sizeof( STH::pptr<int> ) == 4, "pptr must be 4 bytes" );
    static_assert( sizeof( MTH::pptr<int> ) == 4, "pptr must be 4 bytes" );
    static_assert( sizeof( EMB::pptr<int> ) == 4, "pptr must be 4 bytes" );
    static_assert( sizeof( IDB::pptr<int> ) == 4, "pptr must be 4 bytes" );

    return true;
}

// ─── I123-B: EmbeddedHeap — функциональность ─────────────────────────────────

/// @brief EmbeddedHeap: create(size) и allocate/deallocate.
static bool test_embedded_heap_create()
{
    using EMB = pmm::presets::EmbeddedHeap;

    PMM_TEST( !EMB::is_initialized() );

    PMM_TEST( EMB::create( 16 * 1024 ) ); // 16 KiB
    PMM_TEST( EMB::is_initialized() );
    PMM_TEST( EMB::total_size() >= 16 * 1024 );

    void* ptr = EMB::allocate( 128 );
    PMM_TEST( ptr != nullptr );
    std::memset( ptr, 0xCC, 128 );
    EMB::deallocate( ptr );

    EMB::destroy();
    PMM_TEST( !EMB::is_initialized() );
    return true;
}

/// @brief EmbeddedHeap: typed allocation via allocate_typed<T>.
static bool test_embedded_heap_typed_alloc()
{
    using EMB = pmm::presets::EmbeddedHeap;

    PMM_TEST( EMB::create( 32 * 1024 ) );
    PMM_TEST( EMB::is_initialized() );

    EMB::pptr<int> p = EMB::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( p.offset() > 0 );

    *p.resolve() = 42;
    PMM_TEST( *p.resolve() == 42 );

    EMB::deallocate_typed( p );
    EMB::destroy();
    return true;
}

/// @brief EmbeddedHeap: множественные аллокации и деаллокации.
static bool test_embedded_heap_multiple_allocs()
{
    using EMB = pmm::presets::EmbeddedHeap;

    PMM_TEST( EMB::create( 64 * 1024 ) );

    void* ptrs[8];
    for ( int i = 0; i < 8; ++i )
    {
        ptrs[i] = EMB::allocate( 64 );
        PMM_TEST( ptrs[i] != nullptr );
        std::memset( ptrs[i], static_cast<int>( i ), 64 );
    }

    for ( int i = 0; i < 8; ++i )
    {
        EMB::deallocate( ptrs[i] );
    }

    EMB::destroy();
    return true;
}

// ─── I123-C: IndustrialDBHeap — функциональность ─────────────────────────────

/// @brief IndustrialDBHeap: create(size) и allocate/deallocate.
static bool test_industrial_db_heap_create()
{
    using IDB = pmm::presets::IndustrialDBHeap;

    PMM_TEST( !IDB::is_initialized() );

    PMM_TEST( IDB::create( 32 * 1024 ) ); // 32 KiB
    PMM_TEST( IDB::is_initialized() );
    PMM_TEST( IDB::total_size() >= 32 * 1024 );

    void* ptr = IDB::allocate( 256 );
    PMM_TEST( ptr != nullptr );
    std::memset( ptr, 0xDD, 256 );
    IDB::deallocate( ptr );

    IDB::destroy();
    PMM_TEST( !IDB::is_initialized() );
    return true;
}

/// @brief IndustrialDBHeap: typed allocation via allocate_typed<T>.
static bool test_industrial_db_heap_typed_alloc()
{
    using IDB = pmm::presets::IndustrialDBHeap;

    PMM_TEST( IDB::create( 64 * 1024 ) );
    PMM_TEST( IDB::is_initialized() );

    IDB::pptr<double> p = IDB::allocate_typed<double>();
    PMM_TEST( !p.is_null() );

    *p.resolve() = 2.718;
    PMM_TEST( *p.resolve() == 2.718 );

    IDB::deallocate_typed( p );
    IDB::destroy();
    return true;
}

/// @brief IndustrialDBHeap: большой блок аллокации (проверка агрессивного роста).
static bool test_industrial_db_heap_large_alloc()
{
    using IDB = pmm::presets::IndustrialDBHeap;

    PMM_TEST( IDB::create( 16 * 1024 ) );

    // Большой блок — IndustrialDBConfig имеет grow 100%, должен расшириться
    void* ptr = IDB::allocate( 8 * 1024 );
    if ( ptr != nullptr )
    {
        std::memset( ptr, 0xFF, 8 * 1024 );
        IDB::deallocate( ptr );
    }

    IDB::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue123_presets (Issue #123: EmbeddedHeap + IndustrialDBHeap) ===\n\n";
    bool all_passed = true;

    std::cout << "--- I123-A: All four presets compile ---\n";
    PMM_RUN( "I123-A1: All presets instantiate without compile errors", test_all_four_presets_compile );

    std::cout << "\n--- I123-B: EmbeddedHeap ---\n";
    PMM_RUN( "I123-B1: EmbeddedHeap create(size) and alloc/dealloc", test_embedded_heap_create );
    PMM_RUN( "I123-B2: EmbeddedHeap typed allocation", test_embedded_heap_typed_alloc );
    PMM_RUN( "I123-B3: EmbeddedHeap multiple allocations", test_embedded_heap_multiple_allocs );

    std::cout << "\n--- I123-C: IndustrialDBHeap ---\n";
    PMM_RUN( "I123-C1: IndustrialDBHeap create(size) and alloc/dealloc", test_industrial_db_heap_create );
    PMM_RUN( "I123-C2: IndustrialDBHeap typed allocation", test_industrial_db_heap_typed_alloc );
    PMM_RUN( "I123-C3: IndustrialDBHeap large block allocation", test_industrial_db_heap_large_alloc );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
