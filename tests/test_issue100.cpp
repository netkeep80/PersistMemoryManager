/**
 * @file test_issue100.cpp
 * @brief Tests for Issue #100: Infrastructure Preparation (updated for Issue #110).
 *
 * Issue #110: Replaced AbstractPersistMemoryManager and StaticPersistMemoryManager
 * with the unified PersistMemoryManager<ConfigT, InstanceId> (fully static model).
 *
 * Tests:
 *   P100-A: pptr<T, ManagerT> — two-parameter persistent pointer
 *     - sizeof(pptr<T, ManagerT>) == 4 (ManagerT not stored)
 *     - pptr<T, ManagerT>::resolve() uses static manager method
 *     - element_type and manager_type typedefs
 *
 *   P100-B: PersistMemoryManager — manager_type and nested pptr<T>
 *     - manager_type typedef refers to own manager type
 *     - Nested alias Manager::pptr<T> == pmm::pptr<T, manager_type>
 *     - allocate_typed returns Manager::pptr<T>
 *     - resolve() and deallocate_typed() work with Manager::pptr<T>
 *     - Full lifecycle with Manager::pptr<T>
 *
 *   P100-C: manager_concept.h — is_persist_memory_manager<T>
 *     - Validates PersistMemoryManager presets through concept
 *     - Rejects int and non-manager types
 *
 *   P100-D: Multiple distinct managers via InstanceId
 *     - Different InstanceId = different types (TypeA::pptr<int> != TypeB::pptr<int>)
 *     - Full lifecycle of PersistMemoryManager with different InstanceIds
 *
 *   P100-E: manager_configs.h — ready-made configurations
 *     - CacheManagerConfig (NoLock)
 *     - PersistentDataConfig (SharedMutexLock)
 *     - EmbeddedManagerConfig (NoLock)
 *     - IndustrialDBConfig (SharedMutexLock)
 *
 * @see include/pmm/pptr.h
 * @see include/pmm/persist_memory_manager.h
 * @see include/pmm/manager_concept.h
 * @see include/pmm/manager_configs.h
 * @version 0.3 (Issue #110 — unified static API)
 */

#include "pmm_single_threaded_heap.h"
#include "pmm_multi_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

// ─── Test macros ─────────────────────────────────────────────────────────────

// =============================================================================
// P100-A: pptr<T, ManagerT> — two-parameter persistent pointer
// =============================================================================

/// @brief pptr<T, ManagerT> stores only 4 bytes — ManagerT is not stored.
TEST_CASE( "P100-A1: sizeof(pptr<T, ManagerT>) == 4 (ManagerT not stored)", "[test_issue100]" )
{
    using MgrType = pmm::presets::SingleThreadedHeap;

    // With manager — size does NOT change (ManagerT is not stored)
    static_assert( sizeof( pmm::pptr<int, MgrType> ) == 4, "sizeof(pptr<int,MgrType>) must be 4" );
    static_assert( sizeof( pmm::pptr<double, MgrType> ) == 4, "sizeof(pptr<double,MgrType>) must be 4" );
    static_assert( sizeof( MgrType::pptr<int> ) == 4, "sizeof(Manager::pptr<int>) must be 4" );

    REQUIRE( sizeof( pmm::pptr<int, MgrType> ) == 4 );
    REQUIRE( sizeof( MgrType::pptr<int> ) == 4 );
}

/// @brief pptr<T, ManagerT> has typedefs element_type and manager_type.
TEST_CASE( "P100-A2: pptr<T,ManagerT> typedefs element_type and manager_type", "[test_issue100]" )
{
    using MgrType = pmm::presets::SingleThreadedHeap;

    // element_type
    static_assert( std::is_same_v<pmm::pptr<int, MgrType>::element_type, int> );
    static_assert( std::is_same_v<pmm::pptr<double, MgrType>::element_type, double> );

    // manager_type
    static_assert( std::is_same_v<pmm::pptr<int, MgrType>::manager_type, MgrType> );
    static_assert( std::is_same_v<MgrType::pptr<int>::manager_type, MgrType> );
}

/// @brief pptr<T, ManagerT>::resolve() uses static manager's resolve method.
TEST_CASE( "P100-A3: pptr<T,ManagerT>::resolve() uses static manager method", "[test_issue100]" )
{
    using MgrType = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 400>;
    REQUIRE( MgrType::create( 16 * 1024 ) );

    using MyPptr = MgrType::pptr<int>;

    MyPptr p = MgrType::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    // Dereference via pptr::resolve() — no argument, uses static method
    int* ptr1 = p.resolve();
    REQUIRE( ptr1 != nullptr );

    // Equivalent to MgrType::resolve<T>(p)
    int* ptr2 = MgrType::resolve<int>( p );
    REQUIRE( ptr2 != nullptr );

    // Both point to the same location
    REQUIRE( ptr1 == ptr2 );

    // Read/write via both methods
    *ptr1 = 42;
    REQUIRE( *ptr2 == 42 );

    *ptr2 = 99;
    REQUIRE( *p.resolve() == 99 );

    MgrType::deallocate_typed( p );
    MgrType::destroy();
}

/// @brief Null pptr<T, ManagerT> is correctly initialized and detected.
TEST_CASE( "P100-A4: null pptr<T,ManagerT> correctly initialized", "[test_issue100]" )
{
    using MgrType = pmm::presets::SingleThreadedHeap;

    MgrType::pptr<int> null_ptr;
    REQUIRE( null_ptr.is_null() );
    REQUIRE( !static_cast<bool>( null_ptr ) );
    REQUIRE( null_ptr.offset() == 0 );

    MgrType::pptr<double> null_double;
    REQUIRE( null_double.is_null() );
    REQUIRE( null_double.offset() == 0 );
}

// =============================================================================
// P100-B: PersistMemoryManager — manager_type and nested pptr<T>
// =============================================================================

/// @brief Manager::manager_type refers to the manager's own type.
TEST_CASE( "P100-B1: Manager::manager_type == Manager", "[test_issue100]" )
{
    using MgrType = pmm::presets::SingleThreadedHeap;

    static_assert( std::is_same_v<MgrType::manager_type, MgrType>,
                   "Manager::manager_type must be the manager's own type" );
}

/// @brief Manager::pptr<T> == pmm::pptr<T, manager_type>.
TEST_CASE( "P100-B2: Manager::pptr<T> == pmm::pptr<T, manager_type>", "[test_issue100]" )
{
    using MgrType = pmm::presets::SingleThreadedHeap;

    // Manager::pptr<T> must be pmm::pptr<T, MgrType>
    static_assert( std::is_same_v<MgrType::pptr<int>, pmm::pptr<int, MgrType>>,
                   "Manager::pptr<int> must be pmm::pptr<int, manager_type>" );

    static_assert( std::is_same_v<MgrType::pptr<double>, pmm::pptr<double, MgrType>>,
                   "Manager::pptr<double> must be pmm::pptr<double, manager_type>" );

    // Different manager types have different pptr<T>
    using MgrType2 = pmm::presets::MultiThreadedHeap;
    static_assert( !std::is_same_v<MgrType::pptr<int>, MgrType2::pptr<int>>,
                   "pptr from different manager types must be different types" );
}

/// @brief allocate_typed returns Manager::pptr<T>.
TEST_CASE( "P100-B3: allocate_typed returns Manager::pptr<T>", "[test_issue100]" )
{
    using MgrType = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 401>;
    REQUIRE( MgrType::create( 32 * 1024 ) );

    // Store in Manager::pptr<T>
    MgrType::pptr<int> p1 = MgrType::allocate_typed<int>();
    REQUIRE( !p1.is_null() );

    MgrType::pptr<int> p2 = MgrType::allocate_typed<int>();
    REQUIRE( !p2.is_null() );

    // Resolve and write
    *p1.resolve() = 111;
    *p2.resolve() = 222;

    REQUIRE( *p1.resolve() == 111 );
    REQUIRE( *p2.resolve() == 222 );

    // Different addresses
    REQUIRE( p1.offset() != p2.offset() );

    MgrType::deallocate_typed( p1 );
    MgrType::deallocate_typed( p2 );
    MgrType::destroy();
}

/// @brief Full lifecycle: allocate/write/read/deallocate with Manager::pptr<T>.
TEST_CASE( "P100-B4: full lifecycle with Manager::pptr<T>", "[test_issue100]" )
{
    using MgrType = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 402>;
    REQUIRE( MgrType::create( 64 * 1024 ) );

    using MgrPptr = MgrType::pptr<int>;

    // Allocate array of 5 elements
    MgrPptr arr = MgrType::allocate_typed<int>( 5 );
    REQUIRE( !arr.is_null() );

    // Write via resolve_at
    for ( int i = 0; i < 5; ++i )
    {
        int* elem = MgrType::resolve_at( arr, static_cast<std::size_t>( i ) );
        REQUIRE( elem != nullptr );
        *elem = i * 10;
    }

    // Read via resolve
    int* base = arr.resolve();
    REQUIRE( base != nullptr );
    for ( int i = 0; i < 5; ++i )
        REQUIRE( base[i] == i * 10 );

    // Dereference via pptr::resolve() (same as static resolve)
    int* base2 = arr.resolve();
    REQUIRE( base2 == base );

    // Deallocate
    MgrType::deallocate_typed( arr );
    MgrType::destroy();
}

// =============================================================================
// P100-C: manager_concept.h — is_persist_memory_manager<T>
// =============================================================================

/// @brief PersistMemoryManager satisfies the concept.
TEST_CASE( "P100-C1: PersistMemoryManager presets satisfy concept", "[test_issue100]" )
{
    static_assert( pmm::is_persist_memory_manager_v<pmm::presets::SingleThreadedHeap>,
                   "SingleThreadedHeap must satisfy is_persist_memory_manager" );

    static_assert( pmm::is_persist_memory_manager_v<pmm::presets::MultiThreadedHeap>,
                   "MultiThreadedHeap must satisfy is_persist_memory_manager" );

    REQUIRE( pmm::is_persist_memory_manager_v<pmm::presets::SingleThreadedHeap> );
    REQUIRE( pmm::is_persist_memory_manager_v<pmm::presets::MultiThreadedHeap> );
}

/// @brief Non-manager types do not satisfy the concept.
TEST_CASE( "P100-C2: non-manager types rejected by concept", "[test_issue100]" )
{
    static_assert( !pmm::is_persist_memory_manager_v<int>, "int must not satisfy is_persist_memory_manager" );

    static_assert( !pmm::is_persist_memory_manager_v<double>, "double must not satisfy is_persist_memory_manager" );

    struct NotAManager
    {
        void foo() {}
    };
    static_assert( !pmm::is_persist_memory_manager_v<NotAManager>,
                   "NotAManager must not satisfy is_persist_memory_manager" );

    REQUIRE( !pmm::is_persist_memory_manager_v<int> );
    REQUIRE( !pmm::is_persist_memory_manager_v<double> );
}

// =============================================================================
// P100-D: Multiple distinct managers via InstanceId
// =============================================================================

/// @brief Different InstanceId creates different manager types and different pptr types.
TEST_CASE( "P100-D1: different InstanceIds = different manager types and pptr", "[test_issue100]" )
{
    using MgrA = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 410>;
    using MgrB = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 411>;

    // Different manager types
    static_assert( !std::is_same_v<MgrA, MgrB>, "Different InstanceIds must produce different manager types" );

    // Different pptr types
    static_assert( !std::is_same_v<MgrA::pptr<int>, MgrB::pptr<int>>,
                   "pptr from different manager instances must be different types" );

    // Same pptr sizes
    static_assert( sizeof( MgrA::pptr<int> ) == 4 );
    static_assert( sizeof( MgrB::pptr<int> ) == 4 );

    REQUIRE( sizeof( MgrA::pptr<int> ) == 4 );
    REQUIRE( sizeof( MgrB::pptr<int> ) == 4 );
}

/// @brief Full lifecycle of PersistMemoryManager with unique InstanceId.
TEST_CASE( "P100-D2: full lifecycle of PersistMemoryManager", "[test_issue100]" )
{
    using MyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 412>;

    REQUIRE( !MyMgr::is_initialized() );

    REQUIRE( MyMgr::create( 32 * 1024 ) );
    REQUIRE( MyMgr::is_initialized() );
    REQUIRE( MyMgr::total_size() >= 32 * 1024 );
    REQUIRE( MyMgr::free_size() > 0 );

    // Allocate via typed API
    MyMgr::pptr<int> p = MyMgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    // Dereference via pptr method (static model)
    *p.resolve() = 123;
    REQUIRE( *p.resolve() == 123 );

    // Dereference via static manager method
    REQUIRE( *MyMgr::resolve<int>( p ) == 123 );

    // Deallocate
    MyMgr::deallocate_typed( p );
    MyMgr::destroy();
    REQUIRE( !MyMgr::is_initialized() );
}

/// @brief PersistMemoryManager satisfies is_persist_memory_manager_v.
TEST_CASE( "P100-D3: PersistMemoryManager satisfies concept", "[test_issue100]" )
{
    using MyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 413>;

    static_assert( pmm::is_persist_memory_manager_v<MyMgr>,
                   "PersistMemoryManager must satisfy is_persist_memory_manager" );

    REQUIRE( pmm::is_persist_memory_manager_v<MyMgr> );
}

/// @brief Two PersistMemoryManager instances with different InstanceIds work independently.
TEST_CASE( "P100-D4: multiple instances with different InstanceIds work independently", "[test_issue100]" )
{
    using MgrA = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 414>;
    using MgrB = pmm::PersistMemoryManager<pmm::PersistentDataConfig, 415>;

    REQUIRE( MgrA::create( 16 * 1024 ) );
    REQUIRE( MgrB::create( 32 * 1024 ) );

    MgrA::pptr<int> ap = MgrA::allocate_typed<int>();
    MgrB::pptr<int> bp = MgrB::allocate_typed<int>();

    REQUIRE( !ap.is_null() );
    REQUIRE( !bp.is_null() );

    *ap.resolve() = 111;
    *bp.resolve() = 222;

    REQUIRE( *ap.resolve() == 111 );
    REQUIRE( *bp.resolve() == 222 );

    // ap and bp are different types (MgrA::pptr<int> != MgrB::pptr<int>)
    static_assert( !std::is_same_v<MgrA::pptr<int>, MgrB::pptr<int>> );

    MgrA::deallocate_typed( ap );
    MgrB::deallocate_typed( bp );

    MgrA::destroy();
    MgrB::destroy();
}

// =============================================================================
// P100-E: manager_configs.h — ready-made configurations
// =============================================================================

/// @brief CacheManagerConfig uses NoLock.
TEST_CASE( "P100-E1: CacheManagerConfig (NoLock)", "[test_issue100]" )
{
    static_assert( std::is_same_v<pmm::CacheManagerConfig::lock_policy, pmm::config::NoLock>,
                   "CacheManagerConfig must use NoLock" );
    static_assert( pmm::CacheManagerConfig::granule_size == 16 );
    static_assert( pmm::CacheManagerConfig::max_memory_gb == 64 );

    using CacheMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 420>;

    REQUIRE( CacheMgr::create( 8 * 1024 ) );

    CacheMgr::pptr<double> p = CacheMgr::allocate_typed<double>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 3.14;
    REQUIRE( *p.resolve() == 3.14 );

    CacheMgr::deallocate_typed( p );
    CacheMgr::destroy();
}

/// @brief PersistentDataConfig uses SharedMutexLock.
TEST_CASE( "P100-E2: PersistentDataConfig (SharedMutexLock)", "[test_issue100]" )
{
    static_assert( std::is_same_v<pmm::PersistentDataConfig::lock_policy, pmm::config::SharedMutexLock>,
                   "PersistentDataConfig must use SharedMutexLock" );

    using PDataMgr = pmm::PersistMemoryManager<pmm::PersistentDataConfig, 421>;

    REQUIRE( PDataMgr::create( 16 * 1024 ) );

    PDataMgr::pptr<std::uint64_t> p = PDataMgr::allocate_typed<std::uint64_t>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 0xDEADBEEFCAFEBABEull;
    REQUIRE( *p.resolve() == 0xDEADBEEFCAFEBABEull );

    PDataMgr::deallocate_typed( p );
    PDataMgr::destroy();
}

/// @brief EmbeddedManagerConfig uses NoLock with conservative growth.
TEST_CASE( "P100-E3: EmbeddedManagerConfig (NoLock + conservative grow)", "[test_issue100]" )
{
    static_assert( std::is_same_v<pmm::EmbeddedManagerConfig::lock_policy, pmm::config::NoLock>,
                   "EmbeddedManagerConfig must use NoLock" );
    static_assert( pmm::EmbeddedManagerConfig::grow_numerator == 3 );
    static_assert( pmm::EmbeddedManagerConfig::grow_denominator == 2 );

    using EmbMgr = pmm::PersistMemoryManager<pmm::EmbeddedManagerConfig, 422>;

    REQUIRE( EmbMgr::create( 4 * 1024 ) );
    REQUIRE( EmbMgr::is_initialized() );

    EmbMgr::pptr<char> p = EmbMgr::allocate_typed<char>( 16 );
    REQUIRE( !p.is_null() );
    std::memcpy( p.resolve(), "hello world!", 12 );
    REQUIRE( std::memcmp( p.resolve(), "hello world!", 12 ) == 0 );

    EmbMgr::deallocate_typed( p );
    EmbMgr::destroy();
}

/// @brief IndustrialDBConfig uses SharedMutexLock with aggressive growth.
TEST_CASE( "P100-E4: IndustrialDBConfig (SharedMutexLock + aggressive grow)", "[test_issue100]" )
{
    static_assert( std::is_same_v<pmm::IndustrialDBConfig::lock_policy, pmm::config::SharedMutexLock>,
                   "IndustrialDBConfig must use SharedMutexLock" );
    static_assert( pmm::IndustrialDBConfig::grow_numerator == 2 );
    static_assert( pmm::IndustrialDBConfig::grow_denominator == 1 );

    using DBMgr = pmm::PersistMemoryManager<pmm::IndustrialDBConfig, 423>;

    REQUIRE( DBMgr::create( 64 * 1024 ) );
    REQUIRE( DBMgr::is_initialized() );

    // Allocate multiple elements
    DBMgr::pptr<int> p1 = DBMgr::allocate_typed<int>();
    DBMgr::pptr<int> p2 = DBMgr::allocate_typed<int>();
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );
    REQUIRE( p1 != p2 );

    *p1.resolve() = 1;
    *p2.resolve() = 2;
    REQUIRE( *p1.resolve() == 1 );
    REQUIRE( *p2.resolve() == 2 );

    DBMgr::deallocate_typed( p1 );
    DBMgr::deallocate_typed( p2 );
    DBMgr::destroy();
}

// =============================================================================
// main
// =============================================================================
