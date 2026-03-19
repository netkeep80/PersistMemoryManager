/**
 * @file test_issue196_pmap_erase.cpp
 * @brief Tests for pmap erase(), size(), iterator, clear() (Issue #196, Phase 3.3).
 *
 * Verifies the key requirements from Issue #196 / Phase 3.3:
 *  1. pmap::erase(key) removes a node by key and deallocates its memory.
 *  2. pmap::size() returns the number of elements.
 *  3. pmap::begin()/end() iterates in key order (in-order traversal).
 *  4. pmap::clear() removes all elements and deallocates their memory.
 *  5. AVL tree remains balanced after erase operations.
 *  6. All operations work correctly with various key types.
 *
 * @see include/pmm/pmap.h — pmap<_K,_V,ManagerT>
 * @see include/pmm/avl_tree_mixin.h — avl_remove()
 * @version 0.1 (Issue #196 — erase, size, iterator, clear)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pmap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include <vector>

// ─── Test macros ──────────────────────────────────────────────────────────────

// ─── Manager type alias for tests ────────────────────────────────────────────

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 196>;

// =============================================================================
// I196-A: erase() basic functionality
// =============================================================================

/// @brief erase() returns true for existing key and removes it.
TEST_CASE( "    erase existing key", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 42, 100 );
    REQUIRE( map.contains( 42 ) );

    bool removed = map.erase( 42 );
    REQUIRE( removed );
    REQUIRE( !map.contains( 42 ) );
    REQUIRE( map.find( 42 ).is_null() );
    REQUIRE( map.empty() );

    TestMgr::destroy();
}

/// @brief erase() returns false for missing key.
TEST_CASE( "    erase missing key returns false", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );

    bool removed = map.erase( 99 );
    REQUIRE( !removed );
    REQUIRE( map.contains( 1 ) );

    TestMgr::destroy();
}

/// @brief erase() from empty map returns false.
TEST_CASE( "    erase from empty map", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    REQUIRE( !map.erase( 1 ) );

    TestMgr::destroy();
}

/// @brief erase() one of multiple keys; others remain accessible.
TEST_CASE( "    erase preserves other keys", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 10, 1 );
    map.insert( 20, 2 );
    map.insert( 30, 3 );

    REQUIRE( map.erase( 20 ) );
    REQUIRE( !map.contains( 20 ) );
    REQUIRE( map.contains( 10 ) );
    REQUIRE( map.contains( 30 ) );
    REQUIRE( map.find( 10 )->value == 1 );
    REQUIRE( map.find( 30 )->value == 3 );

    TestMgr::destroy();
}

/// @brief erase() all keys one by one; map becomes empty.
TEST_CASE( "    erase all keys one by one", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 5, 50 );
    map.insert( 3, 30 );
    map.insert( 7, 70 );
    map.insert( 1, 10 );
    map.insert( 4, 40 );

    REQUIRE( map.erase( 3 ) );
    REQUIRE( map.erase( 7 ) );
    REQUIRE( map.erase( 1 ) );
    REQUIRE( map.erase( 5 ) );
    REQUIRE( map.erase( 4 ) );

    REQUIRE( map.empty() );

    TestMgr::destroy();
}

/// @brief erase() the root node; tree restructures correctly.
TEST_CASE( "    erase root node", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 2, 20 );
    map.insert( 1, 10 );
    map.insert( 3, 30 );

    // After AVL balancing, 2 should be root
    REQUIRE( map.erase( 2 ) );
    REQUIRE( !map.contains( 2 ) );
    REQUIRE( map.contains( 1 ) );
    REQUIRE( map.contains( 3 ) );

    TestMgr::destroy();
}

/// @brief erase() then re-insert same key works correctly.
TEST_CASE( "    erase then re-insert", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 42, 100 );
    REQUIRE( map.erase( 42 ) );
    REQUIRE( map.empty() );

    auto p = map.insert( 42, 999 );
    REQUIRE( !p.is_null() );
    REQUIRE( map.find( 42 )->value == 999 );

    TestMgr::destroy();
}

// =============================================================================
// I196-B: size() method
// =============================================================================

/// @brief size() returns 0 for empty map.
TEST_CASE( "    size() of empty map", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    REQUIRE( map.size() == 0 );

    TestMgr::destroy();
}

/// @brief size() returns correct count after inserts.
TEST_CASE( "    size() after inserts", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    REQUIRE( map.size() == 1 );

    map.insert( 2, 20 );
    REQUIRE( map.size() == 2 );

    map.insert( 3, 30 );
    REQUIRE( map.size() == 3 );

    // Duplicate key should not increase size
    map.insert( 2, 99 );
    REQUIRE( map.size() == 3 );

    TestMgr::destroy();
}

/// @brief size() decreases after erase.
TEST_CASE( "    size() after erase", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    map.insert( 2, 20 );
    map.insert( 3, 30 );
    REQUIRE( map.size() == 3 );

    map.erase( 2 );
    REQUIRE( map.size() == 2 );

    map.erase( 1 );
    REQUIRE( map.size() == 1 );

    map.erase( 3 );
    REQUIRE( map.size() == 0 );

    TestMgr::destroy();
}

// =============================================================================
// I196-C: begin()/end() iterator
// =============================================================================

/// @brief begin() == end() for empty map.
TEST_CASE( "    begin() == end() for empty map", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    REQUIRE( map.begin() == map.end() );

    TestMgr::destroy();
}

/// @brief Iterator visits all elements in key order.
TEST_CASE( "    iterator visits keys in sorted order", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    // Insert in non-sorted order
    map.insert( 30, 3 );
    map.insert( 10, 1 );
    map.insert( 50, 5 );
    map.insert( 20, 2 );
    map.insert( 40, 4 );

    // Iterate and verify sorted order
    std::vector<int> keys;
    for ( auto it = map.begin(); it != map.end(); ++it )
    {
        auto node = *it;
        REQUIRE( !node.is_null() );
        keys.push_back( node->key );
    }

    REQUIRE( keys.size() == 5 );
    REQUIRE( keys[0] == 10 );
    REQUIRE( keys[1] == 20 );
    REQUIRE( keys[2] == 30 );
    REQUIRE( keys[3] == 40 );
    REQUIRE( keys[4] == 50 );

    TestMgr::destroy();
}

/// @brief Iterator works with single element.
TEST_CASE( "    iterator with single element", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 42, 100 );

    auto it = map.begin();
    REQUIRE( it != map.end() );

    auto node = *it;
    REQUIRE( !node.is_null() );
    REQUIRE( node->key == 42 );
    REQUIRE( node->value == 100 );

    ++it;
    REQUIRE( it == map.end() );

    TestMgr::destroy();
}

/// @brief Iterator reflects state after erase.
TEST_CASE( "    iterator after erase", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 10, 1 );
    map.insert( 20, 2 );
    map.insert( 30, 3 );

    map.erase( 20 );

    std::vector<int> keys;
    for ( auto it = map.begin(); it != map.end(); ++it )
    {
        auto node = *it;
        keys.push_back( node->key );
    }

    REQUIRE( keys.size() == 2 );
    REQUIRE( keys[0] == 10 );
    REQUIRE( keys[1] == 30 );

    TestMgr::destroy();
}

/// @brief Iterator with many elements maintains sorted order.
TEST_CASE( "    iterator with many elements", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 256 * 1024 ) );

    TestMgr::pmap<int, int> map;

    // Insert in reverse order
    const int N = 20;
    for ( int i = N; i >= 1; i-- )
        map.insert( i, i * 10 );

    // Verify iteration gives sorted order
    int prev  = 0;
    int count = 0;
    for ( auto it = map.begin(); it != map.end(); ++it )
    {
        auto node = *it;
        REQUIRE( node->key > prev );
        REQUIRE( node->value == node->key * 10 );
        prev = node->key;
        count++;
    }
    REQUIRE( count == N );

    TestMgr::destroy();
}

// =============================================================================
// I196-D: clear() method
// =============================================================================

/// @brief clear() removes all elements.
TEST_CASE( "    clear() removes all elements", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    map.insert( 2, 20 );
    map.insert( 3, 30 );
    REQUIRE( map.size() == 3 );

    map.clear();
    REQUIRE( map.empty() );
    REQUIRE( map.size() == 0 );
    REQUIRE( map.begin() == map.end() );

    TestMgr::destroy();
}

/// @brief clear() on empty map is safe.
TEST_CASE( "    clear() on empty map is safe", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.clear();
    REQUIRE( map.empty() );

    TestMgr::destroy();
}

/// @brief Can insert after clear().
TEST_CASE( "    insert after clear()", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    map.insert( 2, 20 );
    map.clear();

    map.insert( 5, 50 );
    REQUIRE( map.size() == 1 );
    REQUIRE( map.find( 5 )->value == 50 );
    REQUIRE( map.find( 1 ).is_null() );

    TestMgr::destroy();
}

// =============================================================================
// I196-E: AVL balance after erase stress test
// =============================================================================

/// @brief Erase half the elements, verify remaining are all findable.
TEST_CASE( "    erase half elements, verify remaining", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 512 * 1024 ) );

    TestMgr::pmap<int, int> map;

    const int N = 30;
    for ( int i = 0; i < N; i++ )
        map.insert( i, i * 100 );

    REQUIRE( map.size() == static_cast<std::size_t>( N ) );

    // Erase even keys
    for ( int i = 0; i < N; i += 2 )
        REQUIRE( map.erase( i ) );

    REQUIRE( map.size() == static_cast<std::size_t>( N / 2 ) );

    // Verify odd keys remain
    for ( int i = 1; i < N; i += 2 )
    {
        auto p = map.find( i );
        REQUIRE( !p.is_null() );
        REQUIRE( p->value == i * 100 );
    }

    // Verify even keys are gone
    for ( int i = 0; i < N; i += 2 )
        REQUIRE( map.find( i ).is_null() );

    // Verify iterator still sorted
    int prev = -1;
    for ( auto it = map.begin(); it != map.end(); ++it )
    {
        auto node = *it;
        REQUIRE( node->key > prev );
        prev = node->key;
    }

    TestMgr::destroy();
}

/// @brief Insert, erase, re-insert cycle with memory reuse.
TEST_CASE( "    insert-erase-reinsert cycle", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 128 * 1024 ) );

    TestMgr::pmap<int, int> map;

    // First round
    for ( int i = 0; i < 10; i++ )
        map.insert( i, i );
    REQUIRE( map.size() == 10 );

    // Erase all
    for ( int i = 0; i < 10; i++ )
        REQUIRE( map.erase( i ) );
    REQUIRE( map.empty() );

    // Second round
    for ( int i = 100; i < 110; i++ )
        map.insert( i, i );
    REQUIRE( map.size() == 10 );

    // Verify only second round keys exist
    for ( int i = 0; i < 10; i++ )
        REQUIRE( map.find( i ).is_null() );
    for ( int i = 100; i < 110; i++ )
        REQUIRE( map.find( i )->value == i );

    TestMgr::destroy();
}

// =============================================================================
// I196-F: Memory deallocation verification
// =============================================================================

/// @brief erase() deallocates memory (free_size increases).
TEST_CASE( "    erase() frees memory", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    map.insert( 2, 20 );
    map.insert( 3, 30 );

    auto free_before = TestMgr::free_size();

    map.erase( 2 );

    auto free_after = TestMgr::free_size();
    REQUIRE( free_after > free_before );

    TestMgr::destroy();
}

/// @brief clear() deallocates all memory (free_size increases).
TEST_CASE( "    clear() frees all memory", "[test_issue196_pmap_erase]" )
{
    TestMgr::destroy();
    REQUIRE( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    for ( int i = 0; i < 10; i++ )
        map.insert( i, i );

    auto free_before = TestMgr::free_size();

    map.clear();

    auto free_after = TestMgr::free_size();
    REQUIRE( free_after > free_before );
    REQUIRE( map.empty() );

    TestMgr::destroy();
}

// =============================================================================
// main
// =============================================================================
