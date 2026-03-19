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
#include "pmm_multi_threaded_heap.h"
#include "pmm_embedded_heap.h"
#include "pmm_industrial_db_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// Issue #123 tests: new presets in pmm_presets.h
// =============================================================================

// ─── I123-A: Компиляция всех четырёх пресетов ────────────────────────────────

/// @brief Все четыре пресета компилируются и имеют ожидаемые свойства.
TEST_CASE( "I123-A1: All presets instantiate without compile errors", "[test_issue123_presets]" )
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
}

// ─── I123-B: EmbeddedHeap — функциональность ─────────────────────────────────

/// @brief EmbeddedHeap: create(size) и allocate/deallocate.
TEST_CASE( "I123-B1: EmbeddedHeap create(size) and alloc/dealloc", "[test_issue123_presets]" )
{
    using EMB = pmm::presets::EmbeddedHeap;

    REQUIRE( !EMB::is_initialized() );

    REQUIRE( EMB::create( 16 * 1024 ) ); // 16 KiB
    REQUIRE( EMB::is_initialized() );
    REQUIRE( EMB::total_size() >= 16 * 1024 );

    void* ptr = EMB::allocate( 128 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xCC, 128 );
    EMB::deallocate( ptr );

    EMB::destroy();
    REQUIRE( !EMB::is_initialized() );
}

/// @brief EmbeddedHeap: typed allocation via allocate_typed<T>.
TEST_CASE( "I123-B2: EmbeddedHeap typed allocation", "[test_issue123_presets]" )
{
    using EMB = pmm::presets::EmbeddedHeap;

    REQUIRE( EMB::create( 32 * 1024 ) );
    REQUIRE( EMB::is_initialized() );

    EMB::pptr<int> p = EMB::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    REQUIRE( p.offset() > 0 );

    *p.resolve() = 42;
    REQUIRE( *p.resolve() == 42 );

    EMB::deallocate_typed( p );
    EMB::destroy();
}

/// @brief EmbeddedHeap: множественные аллокации и деаллокации.
TEST_CASE( "I123-B3: EmbeddedHeap multiple allocations", "[test_issue123_presets]" )
{
    using EMB = pmm::presets::EmbeddedHeap;

    REQUIRE( EMB::create( 64 * 1024 ) );

    void* ptrs[8];
    for ( int i = 0; i < 8; ++i )
    {
        ptrs[i] = EMB::allocate( 64 );
        REQUIRE( ptrs[i] != nullptr );
        std::memset( ptrs[i], static_cast<int>( i ), 64 );
    }

    for ( int i = 0; i < 8; ++i )
    {
        EMB::deallocate( ptrs[i] );
    }

    EMB::destroy();
}

// ─── I123-C: IndustrialDBHeap — функциональность ─────────────────────────────

/// @brief IndustrialDBHeap: create(size) и allocate/deallocate.
TEST_CASE( "I123-C1: IndustrialDBHeap create(size) and alloc/dealloc", "[test_issue123_presets]" )
{
    using IDB = pmm::presets::IndustrialDBHeap;

    REQUIRE( !IDB::is_initialized() );

    REQUIRE( IDB::create( 32 * 1024 ) ); // 32 KiB
    REQUIRE( IDB::is_initialized() );
    REQUIRE( IDB::total_size() >= 32 * 1024 );

    void* ptr = IDB::allocate( 256 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xDD, 256 );
    IDB::deallocate( ptr );

    IDB::destroy();
    REQUIRE( !IDB::is_initialized() );
}

/// @brief IndustrialDBHeap: typed allocation via allocate_typed<T>.
TEST_CASE( "I123-C2: IndustrialDBHeap typed allocation", "[test_issue123_presets]" )
{
    using IDB = pmm::presets::IndustrialDBHeap;

    REQUIRE( IDB::create( 64 * 1024 ) );
    REQUIRE( IDB::is_initialized() );

    IDB::pptr<double> p = IDB::allocate_typed<double>();
    REQUIRE( !p.is_null() );

    *p.resolve() = 2.718;
    REQUIRE( *p.resolve() == 2.718 );

    IDB::deallocate_typed( p );
    IDB::destroy();
}

/// @brief IndustrialDBHeap: большой блок аллокации (проверка агрессивного роста).
TEST_CASE( "I123-C3: IndustrialDBHeap large block allocation", "[test_issue123_presets]" )
{
    using IDB = pmm::presets::IndustrialDBHeap;

    REQUIRE( IDB::create( 16 * 1024 ) );

    // Большой блок — IndustrialDBConfig имеет grow 100%, должен расшириться
    void* ptr = IDB::allocate( 8 * 1024 );
    if ( ptr != nullptr )
    {
        std::memset( ptr, 0xFF, 8 * 1024 );
        IDB::deallocate( ptr );
    }

    IDB::destroy();
}

// =============================================================================
// main
// =============================================================================
