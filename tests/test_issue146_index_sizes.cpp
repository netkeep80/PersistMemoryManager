/**
 * @file test_issue146_index_sizes.cpp
 * @brief Тесты поддержки 16-bit и 64-bit индексов в менеджерах ПАП (Issue #146).
 *
 * Проверяет новые конфигурации и пресеты с нестандартными размерами индекса:
 *   - SmallEmbeddedStaticConfig<N> — 16-bit индекс (SmallAddressTraits), StaticStorage
 *   - SmallEmbeddedStaticHeap<N>   — пресет на базе SmallEmbeddedStaticConfig
 *   - LargeDBConfig                — 64-bit индекс (LargeAddressTraits), HeapStorage
 *   - LargeDBHeap                  — пресет на базе LargeDBConfig
 *
 * @see include/pmm/manager_configs.h
 * @see include/pmm/pmm_presets.h
 * @version 0.1 (Issue #146 — поддержка 16-bit и 64-bit индексов)
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
// Section A: SmallEmbeddedStaticConfig (16-bit index) — static checks
// =============================================================================

/// @brief SmallEmbeddedStaticConfig использует SmallAddressTraits (uint16_t).
static bool test_i146_small_config_address_traits()
{
    using Config = pmm::SmallEmbeddedStaticConfig<1024>;
    static_assert( std::is_same<Config::address_traits, pmm::SmallAddressTraits>::value,
                   "SmallEmbeddedStaticConfig must use SmallAddressTraits" );
    static_assert( std::is_same<typename Config::address_traits::index_type, std::uint16_t>::value,
                   "SmallEmbeddedStaticConfig index_type must be uint16_t" );
    return true;
}

/// @brief SmallEmbeddedStaticConfig использует StaticStorage с SmallAddressTraits.
static bool test_i146_small_config_storage_backend()
{
    using Config = pmm::SmallEmbeddedStaticConfig<1024>;
    static_assert( std::is_same<Config::storage_backend, pmm::StaticStorage<1024, pmm::SmallAddressTraits>>::value,
                   "SmallEmbeddedStaticConfig must use StaticStorage<1024, SmallAddressTraits>" );
    static_assert( std::is_same<Config::lock_policy, pmm::config::NoLock>::value,
                   "SmallEmbeddedStaticConfig must use NoLock" );
    return true;
}

/// @brief SmallEmbeddedStaticHeap pptr<T> хранит 2-байтный индекс (16-bit).
static bool test_i146_small_pptr_size()
{
    using SESH = pmm::presets::SmallEmbeddedStaticHeap<1024>;
    static_assert( sizeof( SESH::pptr<int> ) == 2, "SmallEmbeddedStaticHeap pptr<int> must be 2 bytes (16-bit index)" );
    return true;
}

/// @brief SmallEmbeddedStaticConfig granule_size == 16 и является степенью двойки.
static bool test_i146_small_config_granule_size()
{
    static_assert( pmm::SmallEmbeddedStaticConfig<1024>::granule_size == 16,
                   "SmallEmbeddedStaticConfig: granule_size must be 16" );
    static_assert( pmm::SmallEmbeddedStaticConfig<1024>::granule_size >= pmm::kMinGranuleSize,
                   "SmallEmbeddedStaticConfig: granule_size must be >= kMinGranuleSize" );
    static_assert( ( pmm::SmallEmbeddedStaticConfig<1024>::granule_size &
                     ( pmm::SmallEmbeddedStaticConfig<1024>::granule_size - 1 ) ) == 0,
                   "SmallEmbeddedStaticConfig: granule_size must be a power of 2" );
    return true;
}

// =============================================================================
// Section B: SmallEmbeddedStaticHeap (16-bit) — runtime tests
// =============================================================================

/// @brief SmallEmbeddedStaticHeap<1024>: базовый жизненный цикл.
static bool test_i146_small_heap_lifecycle()
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<1024>, 14610>;

    PMM_TEST( !SESH::is_initialized() );

    bool created = SESH::create( 1024 );
    PMM_TEST( created );
    PMM_TEST( SESH::is_initialized() );
    PMM_TEST( SESH::total_size() == 1024 );

    void* ptr = SESH::allocate( 32 );
    PMM_TEST( ptr != nullptr );
    std::memset( ptr, 0xAB, 32 );
    SESH::deallocate( ptr );

    SESH::destroy();
    PMM_TEST( !SESH::is_initialized() );
    return true;
}

/// @brief SmallEmbeddedStaticHeap<1024>: typed allocation via pptr<T>.
static bool test_i146_small_heap_typed_alloc()
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<1024>, 14611>;

    PMM_TEST( SESH::create( 1024 ) );

    SESH::pptr<int> p = SESH::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( p.offset() > 0 );

    *p.resolve() = 99;
    PMM_TEST( *p.resolve() == 99 );

    SESH::deallocate_typed( p );
    SESH::destroy();
    return true;
}

/// @brief SmallEmbeddedStaticHeap: StaticStorage не расширяется.
static bool test_i146_small_heap_no_expand()
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<1024>, 14612>;

    PMM_TEST( SESH::create( 1024 ) );
    std::size_t sz_before = SESH::total_size();

    void* large = SESH::allocate( 1008 ); // почти весь буфер
    if ( large != nullptr )
        SESH::deallocate( large );

    PMM_TEST( SESH::total_size() == sz_before );

    SESH::destroy();
    return true;
}

/// @brief SmallEmbeddedStaticHeap: множественные аллокации.
static bool test_i146_small_heap_multiple_allocs()
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<1024>, 14613>;

    PMM_TEST( SESH::create( 1024 ) );

    void* ptrs[4];
    for ( int i = 0; i < 4; ++i )
    {
        ptrs[i] = SESH::allocate( 32 );
        PMM_TEST( ptrs[i] != nullptr );
        std::memset( ptrs[i], static_cast<int>( i + 1 ), 32 );
    }

    for ( int i = 0; i < 4; ++i )
    {
        std::uint8_t* bytes = static_cast<std::uint8_t*>( ptrs[i] );
        PMM_TEST( bytes[0] == static_cast<std::uint8_t>( i + 1 ) );
    }

    for ( int i = 0; i < 4; ++i )
        SESH::deallocate( ptrs[i] );

    SESH::destroy();
    return true;
}

// =============================================================================
// Section C: LargeDBConfig (64-bit index) — static checks
// =============================================================================

/// @brief LargeDBConfig использует LargeAddressTraits (uint64_t).
static bool test_i146_large_config_address_traits()
{
    static_assert( std::is_same<pmm::LargeDBConfig::address_traits, pmm::LargeAddressTraits>::value,
                   "LargeDBConfig must use LargeAddressTraits" );
    static_assert( std::is_same<typename pmm::LargeDBConfig::address_traits::index_type, std::uint64_t>::value,
                   "LargeDBConfig index_type must be uint64_t" );
    return true;
}

/// @brief LargeDBHeap pptr<T> хранит 8-байтный индекс (64-bit).
static bool test_i146_large_pptr_size()
{
    using LDB = pmm::presets::LargeDBHeap;
    static_assert( sizeof( LDB::pptr<int> ) == 8, "LargeDBHeap pptr<int> must be 8 bytes (64-bit index)" );
    return true;
}

/// @brief LargeDBConfig granule_size == 64 и является степенью двойки.
static bool test_i146_large_config_granule_size()
{
    static_assert( pmm::LargeDBConfig::granule_size == 64,
                   "LargeDBConfig: granule_size must be 64 (LargeAddressTraits::granule_size)" );
    static_assert( pmm::LargeDBConfig::granule_size >= pmm::kMinGranuleSize,
                   "LargeDBConfig: granule_size must be >= kMinGranuleSize" );
    static_assert( ( pmm::LargeDBConfig::granule_size & ( pmm::LargeDBConfig::granule_size - 1 ) ) == 0,
                   "LargeDBConfig: granule_size must be a power of 2" );
    return true;
}

/// @brief LargeDBConfig использует SharedMutexLock.
static bool test_i146_large_config_lock_policy()
{
    static_assert( std::is_same<pmm::LargeDBConfig::lock_policy, pmm::config::SharedMutexLock>::value,
                   "LargeDBConfig must use SharedMutexLock" );
    return true;
}

/// @brief LargeDBConfig использует HeapStorage<LargeAddressTraits>.
static bool test_i146_large_config_storage_backend()
{
    static_assert( std::is_same<pmm::LargeDBConfig::storage_backend, pmm::HeapStorage<pmm::LargeAddressTraits>>::value,
                   "LargeDBConfig must use HeapStorage<LargeAddressTraits>" );
    return true;
}

// =============================================================================
// Section D: LargeDBHeap (64-bit) — runtime tests
// =============================================================================

/// @brief LargeDBHeap: базовый жизненный цикл.
static bool test_i146_large_heap_lifecycle()
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 14620>;

    PMM_TEST( !LDB::is_initialized() );

    bool created = LDB::create( 4096 );
    PMM_TEST( created );
    PMM_TEST( LDB::is_initialized() );
    PMM_TEST( LDB::total_size() == 4096 );

    void* ptr = LDB::allocate( 128 );
    PMM_TEST( ptr != nullptr );
    std::memset( ptr, 0xCC, 128 );
    LDB::deallocate( ptr );

    LDB::destroy();
    PMM_TEST( !LDB::is_initialized() );
    return true;
}

/// @brief LargeDBHeap: typed allocation via pptr<T>.
static bool test_i146_large_heap_typed_alloc()
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 14621>;

    PMM_TEST( LDB::create( 4096 ) );

    LDB::pptr<double> p = LDB::allocate_typed<double>();
    PMM_TEST( !p.is_null() );
    PMM_TEST( p.offset() > 0 );

    *p.resolve() = 2.71828;
    PMM_TEST( *p.resolve() == 2.71828 );

    LDB::deallocate_typed( p );
    LDB::destroy();
    return true;
}

/// @brief LargeDBHeap: множественные аллокации.
static bool test_i146_large_heap_multiple_allocs()
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 14622>;

    PMM_TEST( LDB::create( 8192 ) );

    void* ptrs[8];
    for ( int i = 0; i < 8; ++i )
    {
        ptrs[i] = LDB::allocate( 128 );
        PMM_TEST( ptrs[i] != nullptr );
        std::memset( ptrs[i], static_cast<int>( i ), 128 );
    }

    for ( int i = 0; i < 8; ++i )
    {
        std::uint8_t* bytes = static_cast<std::uint8_t*>( ptrs[i] );
        PMM_TEST( bytes[0] == static_cast<std::uint8_t>( i ) );
    }

    for ( int i = 0; i < 8; ++i )
        LDB::deallocate( ptrs[i] );

    LDB::destroy();
    return true;
}

/// @brief LargeDBHeap: авторасширение хранилища.
static bool test_i146_large_heap_auto_grow()
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 14623>;

    PMM_TEST( LDB::create( 4096 ) );
    std::size_t sz_initial = LDB::total_size();

    // Выделяем блоки, пока не произойдёт расширение
    void* big = LDB::allocate( 8192 );
    PMM_TEST( big != nullptr );
    PMM_TEST( LDB::total_size() >= sz_initial );

    LDB::deallocate( big );
    LDB::destroy();
    return true;
}

// =============================================================================
// Section E: Сравнение размеров pptr для всех вариантов индексов
// =============================================================================

/// @brief Размеры pptr пропорциональны размеру index_type.
static bool test_i146_pptr_size_comparison()
{
    using Small32 = pmm::presets::EmbeddedStaticHeap<4096>;      // 32-bit → 4 байта
    using Small16 = pmm::presets::SmallEmbeddedStaticHeap<1024>; // 16-bit → 2 байта
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

    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue146_index_sizes (Issue #146: 16-bit and 64-bit index support) ===\n\n";
    bool all_passed = true;

    std::cout << "--- I146-A: SmallEmbeddedStaticConfig (16-bit) static checks ---\n";
    PMM_RUN( "I146-A1: SmallEmbeddedStaticConfig uses SmallAddressTraits (uint16_t)",
             test_i146_small_config_address_traits );
    PMM_RUN( "I146-A2: SmallEmbeddedStaticConfig uses StaticStorage<SmallAddressTraits>",
             test_i146_small_config_storage_backend );
    PMM_RUN( "I146-A3: SmallEmbeddedStaticHeap pptr<int> is 2 bytes (16-bit)", test_i146_small_pptr_size );
    PMM_RUN( "I146-A4: SmallEmbeddedStaticConfig granule_size == 16, power of 2", test_i146_small_config_granule_size );

    std::cout << "\n--- I146-B: SmallEmbeddedStaticHeap (16-bit) runtime tests ---\n";
    PMM_RUN( "I146-B1: SmallEmbeddedStaticHeap<1024> lifecycle", test_i146_small_heap_lifecycle );
    PMM_RUN( "I146-B2: SmallEmbeddedStaticHeap<1024> typed allocation", test_i146_small_heap_typed_alloc );
    PMM_RUN( "I146-B3: SmallEmbeddedStaticHeap<1024> no expand (StaticStorage)", test_i146_small_heap_no_expand );
    PMM_RUN( "I146-B4: SmallEmbeddedStaticHeap<1024> multiple allocations", test_i146_small_heap_multiple_allocs );

    std::cout << "\n--- I146-C: LargeDBConfig (64-bit) static checks ---\n";
    PMM_RUN( "I146-C1: LargeDBConfig uses LargeAddressTraits (uint64_t)", test_i146_large_config_address_traits );
    PMM_RUN( "I146-C2: LargeDBHeap pptr<int> is 8 bytes (64-bit)", test_i146_large_pptr_size );
    PMM_RUN( "I146-C3: LargeDBConfig granule_size == 64, power of 2", test_i146_large_config_granule_size );
    PMM_RUN( "I146-C4: LargeDBConfig uses SharedMutexLock", test_i146_large_config_lock_policy );
    PMM_RUN( "I146-C5: LargeDBConfig uses HeapStorage<LargeAddressTraits>", test_i146_large_config_storage_backend );

    std::cout << "\n--- I146-D: LargeDBHeap (64-bit) runtime tests ---\n";
    PMM_RUN( "I146-D1: LargeDBHeap lifecycle", test_i146_large_heap_lifecycle );
    PMM_RUN( "I146-D2: LargeDBHeap typed allocation", test_i146_large_heap_typed_alloc );
    PMM_RUN( "I146-D3: LargeDBHeap multiple allocations", test_i146_large_heap_multiple_allocs );
    PMM_RUN( "I146-D4: LargeDBHeap auto-grow", test_i146_large_heap_auto_grow );

    std::cout << "\n--- I146-E: pptr size comparison across index sizes ---\n";
    PMM_RUN( "I146-E1: pptr sizes: 16-bit=2B, 32-bit=4B, 64-bit=8B", test_i146_pptr_size_comparison );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
