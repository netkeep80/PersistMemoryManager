/**
 * @file test_issue200_root_object.cpp
 * @brief Tests for root object API in ManagerHeader (Issue #200, Phase 3.7).
 *
 * Verifies the key requirements from Issue #200 Phase 3.7:
 *  1. get_root<T>() returns null pptr when no root is set.
 *  2. set_root<T>(p) stores the root and get_root<T>() retrieves it.
 *  3. set_root<T>(null pptr) clears the root.
 *  4. Root object survives save/load cycle (persistence).
 *  5. Root object works with different types (int, struct, pmap).
 *  6. set_root/get_root are safe when manager is not initialized.
 *  7. Root object works with SmallAddressTraits (uint16_t index).
 *  8. Root object works with LargeAddressTraits (uint64_t index).
 *  9. Root is reset to no_block after create() (fresh heap).
 * 10. Root can be used to store a pmap registry.
 *
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @see include/pmm/types.h — ManagerHeader
 * @version 0.1 (Issue #200 — Phase 3.7: root object in ManagerHeader)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/io.h"
#include "pmm/pmap.h"
#include "pmm/pstring.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

// --- Test macros -------------------------------------------------------------

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

// --- Manager type aliases for tests -----------------------------------------

using TestMgr  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 200>;
using TestMgr2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2000>;

// Small embedded manager (uint16_t index)
using SmallMgr = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 2001>;

// Large DB manager (uint64_t index)
using LargeMgr = pmm::PersistMemoryManager<pmm::LargeDBConfig, 2002>;

// =============================================================================
// I200-A: get_root returns null when no root is set
// =============================================================================

static bool test_i200_no_root_by_default()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    auto root = TestMgr::get_root<int>();
    PMM_TEST( root.is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I200-B: set_root / get_root basic roundtrip
// =============================================================================

static bool test_i200_set_get_root_int()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    auto p = TestMgr::create_typed<int>( 42 );
    PMM_TEST( !p.is_null() );

    TestMgr::set_root( p );

    auto root = TestMgr::get_root<int>();
    PMM_TEST( !root.is_null() );
    PMM_TEST( root.offset() == p.offset() );
    PMM_TEST( *root == 42 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I200-C: set_root with null pptr clears root
// =============================================================================

static bool test_i200_clear_root()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    auto p = TestMgr::create_typed<int>( 99 );
    PMM_TEST( !p.is_null() );

    TestMgr::set_root( p );
    PMM_TEST( !TestMgr::get_root<int>().is_null() );

    // Clear by passing null pptr
    TestMgr::set_root( TestMgr::pptr<int>() );
    PMM_TEST( TestMgr::get_root<int>().is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I200-D: Root survives save/load cycle
// =============================================================================

static bool test_i200_persistence()
{
    const char* filename = "test_root_persist.dat";
    TestMgr2::destroy();
    PMM_TEST( TestMgr2::create( 64 * 1024 ) );

    auto p = TestMgr2::create_typed<int>( 777 );
    PMM_TEST( !p.is_null() );
    TestMgr2::set_root( p );

    // Save
    PMM_TEST( pmm::save_manager<TestMgr2>( filename ) );
    auto saved_offset = p.offset();
    TestMgr2::destroy();

    // Reload
    PMM_TEST( TestMgr2::create( 64 * 1024 ) );
    PMM_TEST( pmm::load_manager_from_file<TestMgr2>( filename ) );

    auto root = TestMgr2::get_root<int>();
    PMM_TEST( !root.is_null() );
    PMM_TEST( root.offset() == saved_offset );
    PMM_TEST( *root == 777 );

    TestMgr2::destroy();
    std::remove( filename );
    return true;
}

// =============================================================================
// I200-E: Root with struct type
// =============================================================================

struct TestPoint
{
    int x;
    int y;
};

static bool test_i200_root_struct()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    auto p = TestMgr::create_typed<TestPoint>( TestPoint{ 10, 20 } );
    PMM_TEST( !p.is_null() );

    TestMgr::set_root( p );

    auto root = TestMgr::get_root<TestPoint>();
    PMM_TEST( !root.is_null() );
    PMM_TEST( root->x == 10 );
    PMM_TEST( root->y == 20 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I200-F: set_root / get_root safe when not initialized
// =============================================================================

static bool test_i200_not_initialized()
{
    TestMgr::destroy();

    // set_root should not crash
    TestMgr::pptr<int> p;
    TestMgr::set_root( p );

    // get_root should return null
    auto root = TestMgr::get_root<int>();
    PMM_TEST( root.is_null() );

    return true;
}

// =============================================================================
// I200-G: Root is reset after fresh create()
// =============================================================================

static bool test_i200_reset_after_create()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    auto p = TestMgr::create_typed<int>( 55 );
    TestMgr::set_root( p );
    PMM_TEST( !TestMgr::get_root<int>().is_null() );

    // Recreate the manager — root should be reset
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    PMM_TEST( TestMgr::get_root<int>().is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I200-H: Root overwrite — setting root twice replaces it
// =============================================================================

static bool test_i200_overwrite_root()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    auto p1 = TestMgr::create_typed<int>( 1 );
    auto p2 = TestMgr::create_typed<int>( 2 );
    PMM_TEST( !p1.is_null() );
    PMM_TEST( !p2.is_null() );

    TestMgr::set_root( p1 );
    PMM_TEST( TestMgr::get_root<int>().offset() == p1.offset() );

    TestMgr::set_root( p2 );
    PMM_TEST( TestMgr::get_root<int>().offset() == p2.offset() );
    PMM_TEST( *TestMgr::get_root<int>() == 2 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I200-I: Root with pmap registry pattern
// =============================================================================

static bool test_i200_pmap_registry()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

    using MyMap  = TestMgr::pmap<int, int>;
    auto map_ptr = TestMgr::create_typed<MyMap>();
    PMM_TEST( !map_ptr.is_null() );

    map_ptr->insert( 1, 100 );
    map_ptr->insert( 2, 200 );

    TestMgr::set_root( map_ptr );

    // Retrieve via root
    auto root = TestMgr::get_root<MyMap>();
    PMM_TEST( !root.is_null() );
    auto found = root->find( 1 );
    PMM_TEST( !found.is_null() );
    PMM_TEST( found->value == 100 );

    found = root->find( 2 );
    PMM_TEST( !found.is_null() );
    PMM_TEST( found->value == 200 );

    root->clear();
    TestMgr::destroy_typed( map_ptr );
    TestMgr::destroy();
    return true;
}

// =============================================================================
// I200-J: SmallAddressTraits (uint16_t index) root object
// =============================================================================

static bool test_i200_small_address_traits()
{
    SmallMgr::destroy();
    PMM_TEST( SmallMgr::create( 4096 ) );

    auto p = SmallMgr::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    *p = 123;

    SmallMgr::set_root( p );

    auto root = SmallMgr::get_root<int>();
    PMM_TEST( !root.is_null() );
    PMM_TEST( root.offset() == p.offset() );
    PMM_TEST( *root == 123 );

    SmallMgr::destroy();
    return true;
}

// =============================================================================
// I200-K: LargeAddressTraits (uint64_t index) root object
// =============================================================================

static bool test_i200_large_address_traits()
{
    LargeMgr::destroy();
    PMM_TEST( LargeMgr::create( 64 * 1024 ) );

    auto p = LargeMgr::create_typed<int>( 456 );
    PMM_TEST( !p.is_null() );

    LargeMgr::set_root( p );

    auto root = LargeMgr::get_root<int>();
    PMM_TEST( !root.is_null() );
    PMM_TEST( *root == 456 );

    LargeMgr::destroy();
    return true;
}

// =============================================================================
// I200-L: ManagerHeader size static_assert still holds
// =============================================================================

static bool test_i200_header_size()
{
    // Verify that ManagerHeader<DefaultAddressTraits> is still 64 bytes
    PMM_TEST( sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) == 64 );

    // Verify root_offset field exists at expected position
    PMM_TEST( offsetof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>, root_offset ) > 0 );

    return true;
}

// =============================================================================
// I200-M: Root persists with no root set (backward compatibility)
// =============================================================================

static bool test_i200_persistence_no_root()
{
    const char* filename = "test_root_no_persist.dat";
    TestMgr2::destroy();
    PMM_TEST( TestMgr2::create( 64 * 1024 ) );

    // Don't set any root — save and reload
    PMM_TEST( pmm::save_manager<TestMgr2>( filename ) );
    TestMgr2::destroy();

    PMM_TEST( TestMgr2::create( 64 * 1024 ) );
    PMM_TEST( pmm::load_manager_from_file<TestMgr2>( filename ) );

    // Root should still be null
    PMM_TEST( TestMgr2::get_root<int>().is_null() );

    TestMgr2::destroy();
    std::remove( filename );
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "test_issue200_root_object:\n";
    bool all_passed = true;

    PMM_RUN( "I200-A  no root by default", test_i200_no_root_by_default );
    PMM_RUN( "I200-B  set/get root int", test_i200_set_get_root_int );
    PMM_RUN( "I200-C  clear root", test_i200_clear_root );
    PMM_RUN( "I200-D  persistence save/load", test_i200_persistence );
    PMM_RUN( "I200-E  root with struct", test_i200_root_struct );
    PMM_RUN( "I200-F  safe when not initialized", test_i200_not_initialized );
    PMM_RUN( "I200-G  reset after create()", test_i200_reset_after_create );
    PMM_RUN( "I200-H  overwrite root", test_i200_overwrite_root );
    PMM_RUN( "I200-I  pmap registry pattern", test_i200_pmap_registry );
    PMM_RUN( "I200-J  SmallAddressTraits", test_i200_small_address_traits );
    PMM_RUN( "I200-K  LargeAddressTraits", test_i200_large_address_traits );
    PMM_RUN( "I200-L  header size check", test_i200_header_size );
    PMM_RUN( "I200-M  persistence no root", test_i200_persistence_no_root );

    std::cout << ( all_passed ? "\nAll tests PASSED.\n" : "\nSome tests FAILED!\n" );
    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
