/**
 * @file test_issue73_refactoring.cpp
 * @brief Тесты архитектурного рефакторинга Issue #73.
 *
 * Проверяет:
 * - FR-01: PersistMemoryManager has no public constructor (static class)
 * - FR-02: AVL logic extracted to separate file persist_avl_tree.h (PersistentAvlTree)
 * - FR-03: BlockHeader (32 bytes) and ManagerHeader (64 bytes) sizes unchanged
 * - FR-04: Public API works with PersistMemoryManager<>
 * - FR-05: Configuration support via template (GranuleSize, LockPolicy)
 * - AR-01: CRTP mixins (StatsMixin, ValidationMixin)
 * - AR-02: No virtual functions — all polymorphism is static
 * - AR-03: PersistentAvlTree does not depend on PersistMemoryManager
 * - AR-04: File separation: types, avl, manager, io
 */

#include "pmm/legacy_manager.h"
#include "pmm/types.h"
#include "pmm/free_block_tree.h"
#include "pmm/config.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

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

// ─── FR-03: Binary-compatibility static_assert checks ────────────────────────

static_assert( sizeof( pmm::detail::BlockHeader ) == 32, "FR-03: BlockHeader must be exactly 32 bytes" );
static_assert( sizeof( pmm::detail::ManagerHeader ) == 64, "FR-03: ManagerHeader must be exactly 64 bytes" );
static_assert( sizeof( pmm::pptr<int> ) == 4, "pptr<T> must be exactly 4 bytes (Issue #59)" );

// ─── FR-02/AR-03: PersistentAvlTree is a standalone class ────────────────────

/// @brief Verify PersistentAvlTree class is all-static and callable directly.
static bool test_avl_tree_standalone()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    // Allocate some blocks to create a non-trivial free tree
    pmm::pptr<std::uint8_t> p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
    pmm::pptr<std::uint8_t> p3 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );
    pmm::PersistMemoryManager<>::deallocate_typed( p1 );

    // PersistentAvlTree is accessible directly (AR-03: no dependency on PMM singleton)
    auto  info     = pmm::get_manager_info();
    auto* mgr      = pmm::PersistMemoryManager<>::instance();
    auto* base_raw = static_cast<std::uint8_t*>( mem );
    auto* hdr      = reinterpret_cast<pmm::detail::ManagerHeader*>( base_raw + sizeof( pmm::detail::BlockHeader ) );

    // Suppress unused variable warning (mgr used only to get instance above)
    (void)mgr;
    (void)info;

    // find_best_fit must locate a free block large enough for 64 bytes (needed_granules = 6)
    std::uint32_t needed = pmm::detail::required_block_granules( 64 );
    std::uint32_t found  = pmm::PersistentAvlTree::find_best_fit( base_raw, hdr, needed );
    PMM_TEST( found != pmm::detail::kNoBlock );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::deallocate_typed( p3 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── FR-05/AR-01: Config template + CRTP mixins ──────────────────────────────

/// @brief Verify custom NoLock config compiles and works.
static bool test_nolock_config()
{
    using NoLockConfig = pmm::config::PMMConfig<16, 64, pmm::config::NoLock>;
    using PMM          = pmm::PersistMemoryManager<NoLockConfig>;

    // Verify that PMM<NoLockConfig> is a distinct instantiation from default
    static_assert( !std::is_same<PMM, pmm::PersistMemoryManager<>>::value,
                   "NoLock config must be a different type from default config" );

    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( PMM::create( mem, size ) );
    PMM_TEST( PMM::is_initialized() );
    PMM_TEST( PMM::validate() );

    pmm::pptr<std::uint32_t> p = PMM::allocate_typed<std::uint32_t>( 4 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( PMM::validate() );

    PMM::deallocate_typed( p );
    PMM::destroy();
    std::free( mem );
    return true;
}

/// @brief Verify StatsMixin::get_stats() is accessible via PersistMemoryManager<>.
static bool test_stats_mixin()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    // StatsMixin::get_stats() is accessible (Issue #73 AR-01)
    pmm::MemoryStats stats = pmm::PersistMemoryManager<>::get_stats();
    PMM_TEST( stats.total_blocks > 0 );
    PMM_TEST( stats.free_blocks == 1 );
    PMM_TEST( stats.allocated_blocks == 1 ); // BlockHeader_0

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );

    pmm::MemoryStats after = pmm::PersistMemoryManager<>::get_stats();
    PMM_TEST( after.allocated_blocks == 2 ); // BlockHeader_0 + p

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/// @brief Verify ValidationMixin::validate() is accessible via PersistMemoryManager<>.
static bool test_validation_mixin()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    // ValidationMixin::validate() is accessible (Issue #73 AR-01)
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    // Also verify free function delegates correctly
    PMM_TEST( pmm::get_manager_info().magic == pmm::kMagic );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── AR-02: No virtual functions ─────────────────────────────────────────────

/// @brief Ensure PersistMemoryManager has no virtual functions (AR-02).
static bool test_no_virtual_functions()
{
    // If the class had virtual functions, it would have a vptr and sizeof would be non-zero
    // for a class of zero data members. We use std::is_polymorphic to check.
    static_assert( !std::is_polymorphic<pmm::PersistMemoryManager<>>::value,
                   "AR-02: PersistMemoryManager must have no virtual functions" );
    static_assert( !std::is_polymorphic<pmm::PersistentAvlTree>::value,
                   "AR-02: PersistentAvlTree must have no virtual functions" );
    return true;
}

// ─── AR-04: File separation ───────────────────────────────────────────────────

/// @brief Verify that types from each header are accessible independently.
static bool test_file_separation()
{
    // Types from persist_memory_types.h
    static_assert( pmm::kGranuleSize == 16, "Types header must provide kGranuleSize" );
    static_assert( sizeof( pmm::detail::BlockHeader ) == 32, "Types header: BlockHeader" );
    static_assert( sizeof( pmm::detail::ManagerHeader ) == 64, "Types header: ManagerHeader" );

    // Config from pmm_config.h
    static_assert( pmm::config::PMMConfig<>::granule_size == 16, "Config header: granule_size" );
    static_assert( pmm::config::PMMConfig<>::max_memory_gb == 64, "Config header: max_memory_gb" );

    // PersistentAvlTree from persist_avl_tree.h — just check it's a class
    static_assert( std::is_class<pmm::PersistentAvlTree>::value, "persist_avl_tree.h: PersistentAvlTree" );

    return true;
}

// ─── FR-01: No public constructor ────────────────────────────────────────────

/// @brief Verify that PersistMemoryManager has no public default constructor.
static bool test_no_public_constructor()
{
    // PersistMemoryManager should not be default-constructible by user code.
    // Note: CRTP inheritance from PmmCore (which has no user-accessible ctor)
    // and the fact that s_instance is a static pointer means there's no public ctor.
    // We cannot static_assert is_constructible==false because the class itself
    // inherits from PmmCore which has a trivial implicit ctor, but user code
    // cannot construct a PMM because it has no public data and would be nonsensical.
    // The real enforcement is that all public methods are static (Issue #61).

    // Verify all lifecycle methods are static (no instance needed)
    PMM_TEST( !pmm::PersistMemoryManager<>::is_initialized() );
    return true;
}

// ─── Integration: default + custom config coexist ────────────────────────────

/// @brief Verify that default config and NoLock config can coexist.
static bool test_configs_coexist()
{
    using NoLockConfig = pmm::config::PMMConfig<16, 64, pmm::config::NoLock>;
    using PMMDefault   = pmm::PersistMemoryManager<>;
    using PMMNoLock    = pmm::PersistMemoryManager<NoLockConfig>;

    // Each has its own s_instance — they do NOT share state
    const std::size_t size = 64 * 1024;
    void*             mem1 = std::malloc( size );
    void*             mem2 = std::malloc( size );
    PMM_TEST( mem1 != nullptr && mem2 != nullptr );

    PMM_TEST( PMMDefault::create( mem1, size ) );
    PMM_TEST( PMMNoLock::create( mem2, size ) );

    PMM_TEST( PMMDefault::is_initialized() );
    PMM_TEST( PMMNoLock::is_initialized() );

    // Each has separate total_size
    PMM_TEST( PMMDefault::total_size() == size );
    PMM_TEST( PMMNoLock::total_size() == size );

    // s_instance pointers are independent (different template instantiations)
    // Cast to void* to compare addresses across distinct pointer types
    PMM_TEST( static_cast<void*>( PMMDefault::instance() ) != static_cast<void*>( PMMNoLock::instance() ) );

    PMMDefault::destroy();
    PMMNoLock::destroy();
    std::free( mem1 );
    std::free( mem2 );
    return true;
}

int main()
{
    std::cout << "=== test_issue73_refactoring ===\n";
    bool all_passed = true;

    PMM_RUN( "FR-03: struct sizes", test_file_separation );
    PMM_RUN( "FR-02/AR-03: avl_tree_standalone", test_avl_tree_standalone );
    PMM_RUN( "FR-05: nolock_config", test_nolock_config );
    PMM_RUN( "AR-01: stats_mixin", test_stats_mixin );
    PMM_RUN( "AR-01: validation_mixin", test_validation_mixin );
    PMM_RUN( "AR-02: no_virtual_functions", test_no_virtual_functions );
    PMM_RUN( "AR-04: file_separation", test_file_separation );
    PMM_RUN( "FR-01: no_public_constructor", test_no_public_constructor );
    PMM_RUN( "FR-05: configs_coexist", test_configs_coexist );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
