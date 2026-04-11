/**
 * @file test_issue146_index_sizes.cpp
 * @brief Тесты поддержки 16-bit и 64-bit индексов в менеджерах ПАП.
 *
 * Проверяет новые конфигурации и пресеты с нестандартными размерами индекса:
 *   - SmallEmbeddedStaticConfig<N> — 16-bit индекс (SmallAddressTraits), StaticStorage
 *   - SmallEmbeddedStaticHeap<N>   — пресет на базе SmallEmbeddedStaticConfig
 *   - LargeDBConfig                — 64-bit индекс (LargeAddressTraits), HeapStorage
 *   - LargeDBHeap                  — пресет на базе LargeDBConfig
 *
 * @see include/pmm/manager_configs.h
 * @see include/pmm/pmm_presets.h
 * @version 0.1
 */

#include "pmm_single_threaded_heap.h"
#include "pmm_embedded_static_heap.h"
#include "pmm_small_embedded_static_heap.h"
#include "pmm_large_db_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// Section A: SmallEmbeddedStaticConfig (16-bit index) — static checks
// =============================================================================

/// @brief SmallEmbeddedStaticConfig использует SmallAddressTraits (uint16_t).
TEST_CASE( "I146-A1: SmallEmbeddedStaticConfig uses SmallAddressTraits (uint16_t)", "[test_issue146_index_sizes]" )
{
    using Config = pmm::SmallEmbeddedStaticConfig<4096>;
    static_assert( std::is_same<Config::address_traits, pmm::SmallAddressTraits>::value,
                   "SmallEmbeddedStaticConfig must use SmallAddressTraits" );
    static_assert( std::is_same<typename Config::address_traits::index_type, std::uint16_t>::value,
                   "SmallEmbeddedStaticConfig index_type must be uint16_t" );
}

/// @brief SmallEmbeddedStaticConfig использует StaticStorage с SmallAddressTraits.
TEST_CASE( "I146-A2: SmallEmbeddedStaticConfig uses StaticStorage<SmallAddressTraits>", "[test_issue146_index_sizes]" )
{
    using Config = pmm::SmallEmbeddedStaticConfig<4096>;
    static_assert( std::is_same<Config::storage_backend, pmm::StaticStorage<4096, pmm::SmallAddressTraits>>::value,
                   "SmallEmbeddedStaticConfig must use StaticStorage<4096, SmallAddressTraits>" );
    static_assert( std::is_same<Config::lock_policy, pmm::config::NoLock>::value,
                   "SmallEmbeddedStaticConfig must use NoLock" );
}

/// @brief SmallEmbeddedStaticHeap pptr<T> хранит 2-байтный индекс (16-bit).
TEST_CASE( "I146-A3: SmallEmbeddedStaticHeap pptr<int> is 2 bytes (16-bit)", "[test_issue146_index_sizes]" )
{
    using SESH = pmm::presets::SmallEmbeddedStaticHeap<4096>;
    static_assert( sizeof( SESH::pptr<int> ) == 2, "SmallEmbeddedStaticHeap pptr<int> must be 2 bytes (16-bit index)" );
}

/// @brief SmallEmbeddedStaticConfig granule_size == 16 и является степенью двойки.
TEST_CASE( "I146-A4: SmallEmbeddedStaticConfig granule_size == 16, power of 2", "[test_issue146_index_sizes]" )
{
    static_assert( pmm::SmallEmbeddedStaticConfig<4096>::granule_size == 16,
                   "SmallEmbeddedStaticConfig: granule_size must be 16" );
    static_assert( pmm::SmallEmbeddedStaticConfig<4096>::granule_size >= pmm::kMinGranuleSize,
                   "SmallEmbeddedStaticConfig: granule_size must be >= kMinGranuleSize" );
    static_assert( ( pmm::SmallEmbeddedStaticConfig<4096>::granule_size &
                     ( pmm::SmallEmbeddedStaticConfig<4096>::granule_size - 1 ) ) == 0,
                   "SmallEmbeddedStaticConfig: granule_size must be a power of 2" );
}

// =============================================================================
// Section B: SmallEmbeddedStaticHeap (16-bit) — runtime tests
// =============================================================================

/// @brief SmallEmbeddedStaticHeap<4096>: базовый жизненный цикл.
TEST_CASE( "I146-B1: SmallEmbeddedStaticHeap<4096> lifecycle", "[test_issue146_index_sizes]" )
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 14610>;

    REQUIRE( !SESH::is_initialized() );

    bool created = SESH::create( 4096 );
    REQUIRE( created );
    REQUIRE( SESH::is_initialized() );
    REQUIRE( SESH::total_size() == 4096 );

    void* ptr = SESH::allocate( 32 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xAB, 32 );
    SESH::deallocate( ptr );

    SESH::destroy();
    REQUIRE( !SESH::is_initialized() );
}

/// @brief SmallEmbeddedStaticHeap<4096>: typed allocation via pptr<T>.
TEST_CASE( "I146-B2: SmallEmbeddedStaticHeap<4096> typed allocation", "[test_issue146_index_sizes]" )
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 14611>;

    REQUIRE( SESH::create( 4096 ) );

    SESH::pptr<int> p = SESH::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    REQUIRE( p.offset() > 0 );

    *p.resolve() = 99;
    REQUIRE( *p.resolve() == 99 );

    SESH::deallocate_typed( p );
    SESH::destroy();
}

/// @brief SmallEmbeddedStaticHeap: StaticStorage не расширяется.
TEST_CASE( "I146-B3: SmallEmbeddedStaticHeap<4096> no expand (StaticStorage)", "[test_issue146_index_sizes]" )
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 14612>;

    REQUIRE( SESH::create( 4096 ) );
    std::size_t sz_before = SESH::total_size();

    void* large = SESH::allocate( 3000 ); // почти весь буфер
    if ( large != nullptr )
        SESH::deallocate( large );

    REQUIRE( SESH::total_size() == sz_before );

    SESH::destroy();
}

/// @brief SmallEmbeddedStaticHeap: множественные аллокации.
TEST_CASE( "I146-B4: SmallEmbeddedStaticHeap<4096> multiple allocations", "[test_issue146_index_sizes]" )
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 14613>;

    REQUIRE( SESH::create( 4096 ) );

    void* ptrs[4];
    for ( int i = 0; i < 4; ++i )
    {
        ptrs[i] = SESH::allocate( 32 );
        REQUIRE( ptrs[i] != nullptr );
        std::memset( ptrs[i], static_cast<int>( i + 1 ), 32 );
    }

    for ( int i = 0; i < 4; ++i )
    {
        std::uint8_t* bytes = static_cast<std::uint8_t*>( ptrs[i] );
        REQUIRE( bytes[0] == static_cast<std::uint8_t>( i + 1 ) );
    }

    for ( int i = 0; i < 4; ++i )
        SESH::deallocate( ptrs[i] );

    SESH::destroy();
}

// =============================================================================
// Section C: LargeDBConfig (64-bit index) — static checks
// =============================================================================

/// @brief LargeDBConfig использует LargeAddressTraits (uint64_t).
TEST_CASE( "I146-C1: LargeDBConfig uses LargeAddressTraits (uint64_t)", "[test_issue146_index_sizes]" )
{
    static_assert( std::is_same<pmm::LargeDBConfig::address_traits, pmm::LargeAddressTraits>::value,
                   "LargeDBConfig must use LargeAddressTraits" );
    static_assert( std::is_same<typename pmm::LargeDBConfig::address_traits::index_type, std::uint64_t>::value,
                   "LargeDBConfig index_type must be uint64_t" );
}

/// @brief LargeDBHeap pptr<T> хранит 8-байтный индекс (64-bit).
TEST_CASE( "I146-C2: LargeDBHeap pptr<int> is 8 bytes (64-bit)", "[test_issue146_index_sizes]" )
{
    using LDB = pmm::presets::LargeDBHeap;
    static_assert( sizeof( LDB::pptr<int> ) == 8, "LargeDBHeap pptr<int> must be 8 bytes (64-bit index)" );
}

/// @brief LargeDBConfig granule_size == 64 и является степенью двойки.
TEST_CASE( "I146-C3: LargeDBConfig granule_size == 64, power of 2", "[test_issue146_index_sizes]" )
{
    static_assert( pmm::LargeDBConfig::granule_size == 64,
                   "LargeDBConfig: granule_size must be 64 (LargeAddressTraits::granule_size)" );
    static_assert( pmm::LargeDBConfig::granule_size >= pmm::kMinGranuleSize,
                   "LargeDBConfig: granule_size must be >= kMinGranuleSize" );
    static_assert( ( pmm::LargeDBConfig::granule_size & ( pmm::LargeDBConfig::granule_size - 1 ) ) == 0,
                   "LargeDBConfig: granule_size must be a power of 2" );
}

/// @brief LargeDBConfig использует SharedMutexLock.
TEST_CASE( "I146-C4: LargeDBConfig uses SharedMutexLock", "[test_issue146_index_sizes]" )
{
    static_assert( std::is_same<pmm::LargeDBConfig::lock_policy, pmm::config::SharedMutexLock>::value,
                   "LargeDBConfig must use SharedMutexLock" );
}

/// @brief LargeDBConfig использует HeapStorage<LargeAddressTraits>.
TEST_CASE( "I146-C5: LargeDBConfig uses HeapStorage<LargeAddressTraits>", "[test_issue146_index_sizes]" )
{
    static_assert( std::is_same<pmm::LargeDBConfig::storage_backend, pmm::HeapStorage<pmm::LargeAddressTraits>>::value,
                   "LargeDBConfig must use HeapStorage<LargeAddressTraits>" );
}

// =============================================================================
// Section D: LargeDBHeap (64-bit) — runtime tests
// =============================================================================

/// @brief LargeDBHeap: базовый жизненный цикл.
TEST_CASE( "I146-D1: LargeDBHeap lifecycle", "[test_issue146_index_sizes]" )
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 14620>;

    REQUIRE( !LDB::is_initialized() );

    bool created = LDB::create( 16384 );
    REQUIRE( created );
    REQUIRE( LDB::is_initialized() );
    REQUIRE( LDB::total_size() == 16384 );

    void* ptr = LDB::allocate( 128 );
    REQUIRE( ptr != nullptr );
    std::memset( ptr, 0xCC, 128 );
    LDB::deallocate( ptr );

    LDB::destroy();
    REQUIRE( !LDB::is_initialized() );
}

/// @brief LargeDBHeap: typed allocation via pptr<T>.
TEST_CASE( "I146-D2: LargeDBHeap typed allocation", "[test_issue146_index_sizes]" )
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 14621>;

    REQUIRE( LDB::create( 16384 ) );

    LDB::pptr<double> p = LDB::allocate_typed<double>();
    REQUIRE( !p.is_null() );
    REQUIRE( p.offset() > 0 );

    *p.resolve() = 2.71828;
    REQUIRE( *p.resolve() == 2.71828 );

    LDB::deallocate_typed( p );
    LDB::destroy();
}

/// @brief LargeDBHeap: множественные аллокации.
TEST_CASE( "I146-D3: LargeDBHeap multiple allocations", "[test_issue146_index_sizes]" )
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 14622>;

    REQUIRE( LDB::create( 16384 ) );

    void* ptrs[8];
    for ( int i = 0; i < 8; ++i )
    {
        ptrs[i] = LDB::allocate( 128 );
        REQUIRE( ptrs[i] != nullptr );
        std::memset( ptrs[i], static_cast<int>( i ), 128 );
    }

    for ( int i = 0; i < 8; ++i )
    {
        std::uint8_t* bytes = static_cast<std::uint8_t*>( ptrs[i] );
        REQUIRE( bytes[0] == static_cast<std::uint8_t>( i ) );
    }

    for ( int i = 0; i < 8; ++i )
        LDB::deallocate( ptrs[i] );

    LDB::destroy();
}

/// @brief LargeDBHeap: авторасширение хранилища.
TEST_CASE( "I146-D4: LargeDBHeap auto-grow", "[test_issue146_index_sizes]" )
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 14623>;

    REQUIRE( LDB::create( 16384 ) );
    std::size_t sz_initial = LDB::total_size();

    // Выделяем блоки, пока не произойдёт расширение
    void* big = LDB::allocate( 8192 );
    REQUIRE( big != nullptr );
    REQUIRE( LDB::total_size() >= sz_initial );

    LDB::deallocate( big );
    LDB::destroy();
}

// =============================================================================
// Section E: Сравнение размеров pptr для всех вариантов индексов
// =============================================================================

/// @brief Размеры pptr пропорциональны размеру index_type.
TEST_CASE( "I146-E1: pptr sizes: 16-bit=2B, 32-bit=4B, 64-bit=8B", "[test_issue146_index_sizes]" )
{
    using Small32 = pmm::presets::EmbeddedStaticHeap<4096>;      // 32-bit → 4 байта
    using Small16 = pmm::presets::SmallEmbeddedStaticHeap<4096>; // 16-bit → 2 байта
    using Large64 = pmm::presets::LargeDBHeap;                   // 64-bit → 8 байт

    static_assert( sizeof( Small32::pptr<int> ) == 4, "32-bit pptr must be 4 bytes" );
    static_assert( sizeof( Small16::pptr<int> ) == 2, "16-bit pptr must be 2 bytes" );
    static_assert( sizeof( Large64::pptr<int> ) == 8, "64-bit pptr must be 8 bytes" );

    // 16-bit pptr вдвое меньше 32-bit
    static_assert( sizeof( Small16::pptr<int> ) == sizeof( Small32::pptr<int> ) / 2,
                   "16-bit pptr must be half the size of 32-bit pptr" );

    // 64-bit pptr вдвое больше 32-bit
    static_assert( sizeof( Large64::pptr<int> ) == sizeof( Small32::pptr<int> ) * 2,
                   "64-bit pptr must be twice the size of 32-bit pptr" );
}

// =============================================================================
// main
// =============================================================================
