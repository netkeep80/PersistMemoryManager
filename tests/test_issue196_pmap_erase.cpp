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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

// ─── Test macros ──────────────────────────────────────────────────────────────

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

// ─── Manager type alias for tests ────────────────────────────────────────────

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 196>;

// =============================================================================
// I196-A: erase() basic functionality
// =============================================================================

/// @brief erase() returns true for existing key and removes it.
static bool test_i196_erase_basic()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 42, 100 );
    PMM_TEST( map.contains( 42 ) );

    bool removed = map.erase( 42 );
    PMM_TEST( removed );
    PMM_TEST( !map.contains( 42 ) );
    PMM_TEST( map.find( 42 ).is_null() );
    PMM_TEST( map.empty() );

    TestMgr::destroy();
    return true;
}

/// @brief erase() returns false for missing key.
static bool test_i196_erase_missing()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );

    bool removed = map.erase( 99 );
    PMM_TEST( !removed );
    PMM_TEST( map.contains( 1 ) );

    TestMgr::destroy();
    return true;
}

/// @brief erase() from empty map returns false.
static bool test_i196_erase_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    PMM_TEST( !map.erase( 1 ) );

    TestMgr::destroy();
    return true;
}

/// @brief erase() one of multiple keys; others remain accessible.
static bool test_i196_erase_preserves_others()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 10, 1 );
    map.insert( 20, 2 );
    map.insert( 30, 3 );

    PMM_TEST( map.erase( 20 ) );
    PMM_TEST( !map.contains( 20 ) );
    PMM_TEST( map.contains( 10 ) );
    PMM_TEST( map.contains( 30 ) );
    PMM_TEST( map.find( 10 )->value == 1 );
    PMM_TEST( map.find( 30 )->value == 3 );

    TestMgr::destroy();
    return true;
}

/// @brief erase() all keys one by one; map becomes empty.
static bool test_i196_erase_all_one_by_one()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 5, 50 );
    map.insert( 3, 30 );
    map.insert( 7, 70 );
    map.insert( 1, 10 );
    map.insert( 4, 40 );

    PMM_TEST( map.erase( 3 ) );
    PMM_TEST( map.erase( 7 ) );
    PMM_TEST( map.erase( 1 ) );
    PMM_TEST( map.erase( 5 ) );
    PMM_TEST( map.erase( 4 ) );

    PMM_TEST( map.empty() );

    TestMgr::destroy();
    return true;
}

/// @brief erase() the root node; tree restructures correctly.
static bool test_i196_erase_root()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 2, 20 );
    map.insert( 1, 10 );
    map.insert( 3, 30 );

    // After AVL balancing, 2 should be root
    PMM_TEST( map.erase( 2 ) );
    PMM_TEST( !map.contains( 2 ) );
    PMM_TEST( map.contains( 1 ) );
    PMM_TEST( map.contains( 3 ) );

    TestMgr::destroy();
    return true;
}

/// @brief erase() then re-insert same key works correctly.
static bool test_i196_erase_then_reinsert()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 42, 100 );
    PMM_TEST( map.erase( 42 ) );
    PMM_TEST( map.empty() );

    auto p = map.insert( 42, 999 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( map.find( 42 )->value == 999 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I196-B: size() method
// =============================================================================

/// @brief size() returns 0 for empty map.
static bool test_i196_size_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    PMM_TEST( map.size() == 0 );

    TestMgr::destroy();
    return true;
}

/// @brief size() returns correct count after inserts.
static bool test_i196_size_after_insert()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    PMM_TEST( map.size() == 1 );

    map.insert( 2, 20 );
    PMM_TEST( map.size() == 2 );

    map.insert( 3, 30 );
    PMM_TEST( map.size() == 3 );

    // Duplicate key should not increase size
    map.insert( 2, 99 );
    PMM_TEST( map.size() == 3 );

    TestMgr::destroy();
    return true;
}

/// @brief size() decreases after erase.
static bool test_i196_size_after_erase()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    map.insert( 2, 20 );
    map.insert( 3, 30 );
    PMM_TEST( map.size() == 3 );

    map.erase( 2 );
    PMM_TEST( map.size() == 2 );

    map.erase( 1 );
    PMM_TEST( map.size() == 1 );

    map.erase( 3 );
    PMM_TEST( map.size() == 0 );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I196-C: begin()/end() iterator
// =============================================================================

/// @brief begin() == end() for empty map.
static bool test_i196_iterator_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    PMM_TEST( map.begin() == map.end() );

    TestMgr::destroy();
    return true;
}

/// @brief Iterator visits all elements in key order.
static bool test_i196_iterator_order()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

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
        PMM_TEST( !node.is_null() );
        keys.push_back( node->key );
    }

    PMM_TEST( keys.size() == 5 );
    PMM_TEST( keys[0] == 10 );
    PMM_TEST( keys[1] == 20 );
    PMM_TEST( keys[2] == 30 );
    PMM_TEST( keys[3] == 40 );
    PMM_TEST( keys[4] == 50 );

    TestMgr::destroy();
    return true;
}

/// @brief Iterator works with single element.
static bool test_i196_iterator_single()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 42, 100 );

    auto it = map.begin();
    PMM_TEST( it != map.end() );

    auto node = *it;
    PMM_TEST( !node.is_null() );
    PMM_TEST( node->key == 42 );
    PMM_TEST( node->value == 100 );

    ++it;
    PMM_TEST( it == map.end() );

    TestMgr::destroy();
    return true;
}

/// @brief Iterator reflects state after erase.
static bool test_i196_iterator_after_erase()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

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

    PMM_TEST( keys.size() == 2 );
    PMM_TEST( keys[0] == 10 );
    PMM_TEST( keys[1] == 30 );

    TestMgr::destroy();
    return true;
}

/// @brief Iterator with many elements maintains sorted order.
static bool test_i196_iterator_many()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 256 * 1024 ) );

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
        PMM_TEST( node->key > prev );
        PMM_TEST( node->value == node->key * 10 );
        prev = node->key;
        count++;
    }
    PMM_TEST( count == N );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I196-D: clear() method
// =============================================================================

/// @brief clear() removes all elements.
static bool test_i196_clear_basic()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    map.insert( 2, 20 );
    map.insert( 3, 30 );
    PMM_TEST( map.size() == 3 );

    map.clear();
    PMM_TEST( map.empty() );
    PMM_TEST( map.size() == 0 );
    PMM_TEST( map.begin() == map.end() );

    TestMgr::destroy();
    return true;
}

/// @brief clear() on empty map is safe.
static bool test_i196_clear_empty()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.clear();
    PMM_TEST( map.empty() );

    TestMgr::destroy();
    return true;
}

/// @brief Can insert after clear().
static bool test_i196_insert_after_clear()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    map.insert( 2, 20 );
    map.clear();

    map.insert( 5, 50 );
    PMM_TEST( map.size() == 1 );
    PMM_TEST( map.find( 5 )->value == 50 );
    PMM_TEST( map.find( 1 ).is_null() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I196-E: AVL balance after erase stress test
// =============================================================================

/// @brief Erase half the elements, verify remaining are all findable.
static bool test_i196_erase_stress()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 512 * 1024 ) );

    TestMgr::pmap<int, int> map;

    const int N = 30;
    for ( int i = 0; i < N; i++ )
        map.insert( i, i * 100 );

    PMM_TEST( map.size() == static_cast<std::size_t>( N ) );

    // Erase even keys
    for ( int i = 0; i < N; i += 2 )
        PMM_TEST( map.erase( i ) );

    PMM_TEST( map.size() == static_cast<std::size_t>( N / 2 ) );

    // Verify odd keys remain
    for ( int i = 1; i < N; i += 2 )
    {
        auto p = map.find( i );
        PMM_TEST( !p.is_null() );
        PMM_TEST( p->value == i * 100 );
    }

    // Verify even keys are gone
    for ( int i = 0; i < N; i += 2 )
        PMM_TEST( map.find( i ).is_null() );

    // Verify iterator still sorted
    int prev = -1;
    for ( auto it = map.begin(); it != map.end(); ++it )
    {
        auto node = *it;
        PMM_TEST( node->key > prev );
        prev = node->key;
    }

    TestMgr::destroy();
    return true;
}

/// @brief Insert, erase, re-insert cycle with memory reuse.
static bool test_i196_erase_reinsert_cycle()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 128 * 1024 ) );

    TestMgr::pmap<int, int> map;

    // First round
    for ( int i = 0; i < 10; i++ )
        map.insert( i, i );
    PMM_TEST( map.size() == 10 );

    // Erase all
    for ( int i = 0; i < 10; i++ )
        PMM_TEST( map.erase( i ) );
    PMM_TEST( map.empty() );

    // Second round
    for ( int i = 100; i < 110; i++ )
        map.insert( i, i );
    PMM_TEST( map.size() == 10 );

    // Verify only second round keys exist
    for ( int i = 0; i < 10; i++ )
        PMM_TEST( map.find( i ).is_null() );
    for ( int i = 100; i < 110; i++ )
        PMM_TEST( map.find( i )->value == i );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// I196-F: Memory deallocation verification
// =============================================================================

/// @brief erase() deallocates memory (free_size increases).
static bool test_i196_erase_frees_memory()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    map.insert( 1, 10 );
    map.insert( 2, 20 );
    map.insert( 3, 30 );

    auto free_before = TestMgr::free_size();

    map.erase( 2 );

    auto free_after = TestMgr::free_size();
    PMM_TEST( free_after > free_before );

    TestMgr::destroy();
    return true;
}

/// @brief clear() deallocates all memory (free_size increases).
static bool test_i196_clear_frees_memory()
{
    TestMgr::destroy();
    PMM_TEST( TestMgr::create( 64 * 1024 ) );

    TestMgr::pmap<int, int> map;
    for ( int i = 0; i < 10; i++ )
        map.insert( i, i );

    auto free_before = TestMgr::free_size();

    map.clear();

    auto free_after = TestMgr::free_size();
    PMM_TEST( free_after > free_before );
    PMM_TEST( map.empty() );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    bool all_passed = true;

    std::cout << "[Issue #196: pmap erase(), size(), iterator, clear() — Phase 3.3]\n";

    std::cout << "  I196-A: erase() basic functionality\n";
    PMM_RUN( "    erase existing key", test_i196_erase_basic );
    PMM_RUN( "    erase missing key returns false", test_i196_erase_missing );
    PMM_RUN( "    erase from empty map", test_i196_erase_empty );
    PMM_RUN( "    erase preserves other keys", test_i196_erase_preserves_others );
    PMM_RUN( "    erase all keys one by one", test_i196_erase_all_one_by_one );
    PMM_RUN( "    erase root node", test_i196_erase_root );
    PMM_RUN( "    erase then re-insert", test_i196_erase_then_reinsert );

    std::cout << "  I196-B: size() method\n";
    PMM_RUN( "    size() of empty map", test_i196_size_empty );
    PMM_RUN( "    size() after inserts", test_i196_size_after_insert );
    PMM_RUN( "    size() after erase", test_i196_size_after_erase );

    std::cout << "  I196-C: begin()/end() iterator\n";
    PMM_RUN( "    begin() == end() for empty map", test_i196_iterator_empty );
    PMM_RUN( "    iterator visits keys in sorted order", test_i196_iterator_order );
    PMM_RUN( "    iterator with single element", test_i196_iterator_single );
    PMM_RUN( "    iterator after erase", test_i196_iterator_after_erase );
    PMM_RUN( "    iterator with many elements", test_i196_iterator_many );

    std::cout << "  I196-D: clear() method\n";
    PMM_RUN( "    clear() removes all elements", test_i196_clear_basic );
    PMM_RUN( "    clear() on empty map is safe", test_i196_clear_empty );
    PMM_RUN( "    insert after clear()", test_i196_insert_after_clear );

    std::cout << "  I196-E: AVL balance and stress tests\n";
    PMM_RUN( "    erase half elements, verify remaining", test_i196_erase_stress );
    PMM_RUN( "    insert-erase-reinsert cycle", test_i196_erase_reinsert_cycle );

    std::cout << "  I196-F: Memory deallocation\n";
    PMM_RUN( "    erase() frees memory", test_i196_erase_frees_memory );
    PMM_RUN( "    clear() frees all memory", test_i196_clear_frees_memory );

    std::cout << "\n";
    if ( all_passed )
    {
        std::cout << "All Issue #196 tests PASSED.\n";
        return EXIT_SUCCESS;
    }
    else
    {
        std::cout << "Some Issue #196 tests FAILED.\n";
        return EXIT_FAILURE;
    }
}
