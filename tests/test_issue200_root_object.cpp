/**
 * @file test_issue200_root_object.cpp
 * @brief Tests for root object API in ManagerHeader.
 *
 * Verifies the key requirements from this feature:
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
 * @version 0.1
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/io.h"
#include "pmm/pmap.h"
#include "pmm/pstring.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>

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

TEST_CASE( "I200-A  no root by default", "[test_issue200_root_object]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    auto root = TestMgr::get_root<int>();
    REQUIRE( root.is_null() );

    TestMgr::destroy();
}

// =============================================================================
// I200-B: set_root / get_root basic roundtrip
// =============================================================================

TEST_CASE( "I200-B  set/get root int", "[test_issue200_root_object]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    auto p = TestMgr::create_typed<int>( 42 );
    REQUIRE( !p.is_null() );

    TestMgr::set_root( p );

    auto root = TestMgr::get_root<int>();
    REQUIRE( !root.is_null() );
    REQUIRE( root.offset() == p.offset() );
    REQUIRE( *root == 42 );

    TestMgr::destroy();
}

// =============================================================================
// I200-C: set_root with null pptr clears root
// =============================================================================

TEST_CASE( "I200-C  clear root", "[test_issue200_root_object]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    auto p = TestMgr::create_typed<int>( 99 );
    REQUIRE( !p.is_null() );

    TestMgr::set_root( p );
    REQUIRE( !TestMgr::get_root<int>().is_null() );

    // Clear by passing null pptr
    TestMgr::set_root( TestMgr::pptr<int>() );
    REQUIRE( TestMgr::get_root<int>().is_null() );

    TestMgr::destroy();
}

// =============================================================================
// I200-D: Root survives save/load cycle
// =============================================================================

TEST_CASE( "I200-D  persistence save/load", "[test_issue200_root_object]" )
{
    const char* filename = "test_root_persist.dat";
    std::remove( filename );
    TestMgr2::destroy();
    REQUIRE( TestMgr2::create( 64 * 1024 ) );

    auto p = TestMgr2::create_typed<int>( 777 );
    REQUIRE( !p.is_null() );
    TestMgr2::set_root( p );

    // Save
    REQUIRE( pmm::save_manager<TestMgr2>( filename ) );
    auto saved_offset = p.offset();
    TestMgr2::destroy();

    // Reload
    REQUIRE( TestMgr2::create( 64 * 1024 ) );
    REQUIRE( pmm::load_manager_from_file<TestMgr2>( filename, pmm::VerifyResult{} ) );

    auto root = TestMgr2::get_root<int>();
    REQUIRE( !root.is_null() );
    REQUIRE( root.offset() == saved_offset );
    REQUIRE( *root == 777 );

    TestMgr2::destroy();
    std::remove( filename );
}

// =============================================================================
// I200-E: Root with struct type
// =============================================================================

struct TestPoint
{
    int x;
    int y;
};

TEST_CASE( "I200-E  root with struct", "[test_issue200_root_object]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    auto p = TestMgr::create_typed<TestPoint>( TestPoint{ 10, 20 } );
    REQUIRE( !p.is_null() );

    TestMgr::set_root( p );

    auto root = TestMgr::get_root<TestPoint>();
    REQUIRE( !root.is_null() );
    REQUIRE( root->x == 10 );
    REQUIRE( root->y == 20 );

    TestMgr::destroy();
}

// =============================================================================
// I200-F: set_root / get_root safe when not initialized
// =============================================================================

TEST_CASE( "I200-F  safe when not initialized", "[test_issue200_root_object]" )
{
    TestMgr::destroy();

    // set_root should not crash
    TestMgr::pptr<int> p;
    TestMgr::set_root( p );

    // get_root should return null
    auto root = TestMgr::get_root<int>();
    REQUIRE( root.is_null() );
}

// =============================================================================
// I200-G: Root is reset after fresh create()
// =============================================================================

TEST_CASE( "I200-G  reset after create()", "[test_issue200_root_object]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    auto p = TestMgr::create_typed<int>( 55 );
    TestMgr::set_root( p );
    REQUIRE( !TestMgr::get_root<int>().is_null() );

    // Recreate the manager — root should be reset
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    REQUIRE( TestMgr::get_root<int>().is_null() );

    TestMgr::destroy();
}

// =============================================================================
// I200-H: Root overwrite — setting root twice replaces it
// =============================================================================

TEST_CASE( "I200-H  overwrite root", "[test_issue200_root_object]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    auto p1 = TestMgr::create_typed<int>( 1 );
    auto p2 = TestMgr::create_typed<int>( 2 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );

    TestMgr::set_root( p1 );
    REQUIRE( TestMgr::get_root<int>().offset() == p1.offset() );

    TestMgr::set_root( p2 );
    REQUIRE( TestMgr::get_root<int>().offset() == p2.offset() );
    REQUIRE( *TestMgr::get_root<int>() == 2 );

    TestMgr::destroy();
}

// =============================================================================
// I200-I: Root with pmap registry pattern
// =============================================================================

TEST_CASE( "I200-I  pmap registry pattern", "[test_issue200_root_object]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    using MyMap  = TestMgr::pmap<int, int>;
    auto map_ptr = TestMgr::create_typed<MyMap>();
    REQUIRE( !map_ptr.is_null() );

    map_ptr->insert( 1, 100 );
    map_ptr->insert( 2, 200 );

    TestMgr::set_root( map_ptr );

    // Retrieve via root
    auto root = TestMgr::get_root<MyMap>();
    REQUIRE( !root.is_null() );
    auto found = root->find( 1 );
    REQUIRE( !found.is_null() );
    REQUIRE( found->value == 100 );

    found = root->find( 2 );
    REQUIRE( !found.is_null() );
    REQUIRE( found->value == 200 );

    root->clear();
    TestMgr::destroy_typed( map_ptr );
    TestMgr::destroy();
}

// =============================================================================
// I200-J: SmallAddressTraits (uint16_t index) root object
// =============================================================================

TEST_CASE( "I200-J  SmallAddressTraits", "[test_issue200_root_object]" )
{
    SmallMgr::destroy();
    bool created = SmallMgr::create( 4096 );
    CAPTURE( static_cast<int>( SmallMgr::last_error() ) );
    REQUIRE( created );

    auto p = SmallMgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );
    *p = 123;

    SmallMgr::set_root( p );

    auto root = SmallMgr::get_root<int>();
    REQUIRE( !root.is_null() );
    REQUIRE( root.offset() == p.offset() );
    REQUIRE( *root == 123 );

    SmallMgr::destroy();
}

// =============================================================================
// I200-K: LargeAddressTraits (uint64_t index) root object
// =============================================================================

TEST_CASE( "I200-K  LargeAddressTraits", "[test_issue200_root_object]" )
{
    LargeMgr::destroy();
    bool created = LargeMgr::create( 64 * 1024 );
    CAPTURE( static_cast<int>( LargeMgr::last_error() ) );
    REQUIRE( created );

    auto p = LargeMgr::create_typed<int>( 456 );
    REQUIRE( !p.is_null() );

    LargeMgr::set_root( p );

    auto root = LargeMgr::get_root<int>();
    REQUIRE( !root.is_null() );
    REQUIRE( *root == 456 );

    LargeMgr::destroy();
}

// =============================================================================
// I200-L: ManagerHeader size static_assert still holds
// =============================================================================

TEST_CASE( "I200-L  header size check", "[test_issue200_root_object]" )
{
    // Verify that ManagerHeader<DefaultAddressTraits> is still 64 bytes
    REQUIRE( sizeof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits> ) == 64 );

    // Verify root_offset field exists at expected position
    REQUIRE( offsetof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>, root_offset ) > 0 );
}

// =============================================================================
// I200-M: Root persists with no root set (backward compatibility)
// =============================================================================

TEST_CASE( "I200-M  persistence no root", "[test_issue200_root_object]" )
{
    const char* filename = "test_root_no_persist.dat";
    std::remove( filename );
    TestMgr2::destroy();
    REQUIRE( TestMgr2::create( 64 * 1024 ) );

    // Don't set any root — save and reload
    REQUIRE( pmm::save_manager<TestMgr2>( filename ) );
    TestMgr2::destroy();

    REQUIRE( TestMgr2::create( 64 * 1024 ) );
    REQUIRE( pmm::load_manager_from_file<TestMgr2>( filename, pmm::VerifyResult{} ) );

    // Root should still be null
    REQUIRE( TestMgr2::get_root<int>().is_null() );

    TestMgr2::destroy();
    std::remove( filename );
}

// =============================================================================
// main
// =============================================================================
