/**
 * @file test_issue188_deduplication.cpp
 * @brief Tests for code-deduplication refactoring in Issue #188.
 *
 * Verifies:
 *   - avl_tree_mixin.h NodeUpdateFn hook works correctly for pvector weight updates.
 *   - avl_remove() shared function works correctly (used by pvector).
 *   - avl_min_node() / avl_max_node() shared functions work correctly.
 *   - pvector operations (push_back, pop_back, at, size, front, back, clear)
 *     continue to work correctly after deduplication refactoring.
 *   - pmap and pstringview continue to work correctly (no regression from
 *     adding NodeUpdateFn default parameter to avl_insert/avl_rebalance_up).
 *   - Order-statistic tree invariants hold after all operations.
 *
 * @see include/pmm/avl_tree_mixin.h — shared AVL operations (Issue #188)
 * @see include/pmm/pvector.h — pvector deduplication (Issue #188)
 * @version 0.1 (Issue #188 — initial deduplication tests)
 */

#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmap.h"
#include "pmm/pstringview.h"
#include "pmm/pvector.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

// ─── Macros ───────────────────────────────────────────────────────────────────

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
            ok = false;                                                                                                \
        }                                                                                                              \
    } while ( false )

// ─── Manager alias ───────────────────────────────────────────────────────────

using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 188>;
using Vec = pmm::pvector<int, Mgr>;
using Map = pmm::pmap<int, int, Mgr>;
using Str = pmm::pstringview<Mgr>;

// ─── Test: pvector push_back, at, size after dedup ───────────────────────────

static bool test_pvector_basic_ops()
{
    Mgr::create( 64 * 1024 );

    Vec vec;
    PMM_TEST( vec.empty() );
    PMM_TEST( vec.size() == 0 );

    // Push back 10 elements
    for ( int i = 0; i < 10; ++i )
    {
        auto p = vec.push_back( i * 10 );
        PMM_TEST( !p.is_null() );
    }
    PMM_TEST( vec.size() == 10 );

    // Verify at() returns correct values
    for ( int i = 0; i < 10; ++i )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        PMM_TEST( !p.is_null() );
        auto* obj = Mgr::resolve<Vec::node_type>( p );
        PMM_TEST( obj != nullptr );
        PMM_TEST( obj->value == i * 10 );
    }

    // Out of bounds
    PMM_TEST( vec.at( 10 ).is_null() );
    PMM_TEST( vec.at( 100 ).is_null() );

    Mgr::destroy();
    return true;
}

// ─── Test: pvector front/back ────────────────────────────────────────────────

static bool test_pvector_front_back()
{
    Mgr::create( 64 * 1024 );

    Vec vec;
    vec.push_back( 100 );
    vec.push_back( 200 );
    vec.push_back( 300 );

    auto f = vec.front();
    PMM_TEST( !f.is_null() );
    PMM_TEST( Mgr::resolve<Vec::node_type>( f )->value == 100 );

    auto b = vec.back();
    PMM_TEST( !b.is_null() );
    PMM_TEST( Mgr::resolve<Vec::node_type>( b )->value == 300 );

    Mgr::destroy();
    return true;
}

// ─── Test: pvector pop_back ──────────────────────────────────────────────────

static bool test_pvector_pop_back()
{
    Mgr::create( 64 * 1024 );

    Vec vec;
    for ( int i = 0; i < 5; ++i )
        vec.push_back( i );

    PMM_TEST( vec.size() == 5 );

    PMM_TEST( vec.pop_back() );
    PMM_TEST( vec.size() == 4 );

    // Verify remaining elements
    for ( int i = 0; i < 4; ++i )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        PMM_TEST( !p.is_null() );
        PMM_TEST( Mgr::resolve<Vec::node_type>( p )->value == i );
    }

    // Pop all
    while ( !vec.empty() )
        vec.pop_back();
    PMM_TEST( vec.empty() );
    PMM_TEST( vec.size() == 0 );

    Mgr::destroy();
    return true;
}

// ─── Test: pvector clear ─────────────────────────────────────────────────────

static bool test_pvector_clear()
{
    Mgr::create( 64 * 1024 );

    Vec vec;
    for ( int i = 0; i < 20; ++i )
        vec.push_back( i );
    PMM_TEST( vec.size() == 20 );

    vec.clear();
    PMM_TEST( vec.empty() );
    PMM_TEST( vec.size() == 0 );

    // Re-push after clear
    for ( int i = 0; i < 5; ++i )
        vec.push_back( i * 100 );
    PMM_TEST( vec.size() == 5 );
    PMM_TEST( Mgr::resolve<Vec::node_type>( vec.at( 2 ) )->value == 200 );

    Mgr::destroy();
    return true;
}

// ─── Test: pvector iterator ──────────────────────────────────────────────────

static bool test_pvector_iterator()
{
    Mgr::create( 64 * 1024 );

    Vec vec;
    for ( int i = 0; i < 7; ++i )
        vec.push_back( i );

    std::vector<int> collected;
    for ( auto it = vec.begin(); it != vec.end(); ++it )
    {
        auto p = *it;
        if ( !p.is_null() )
        {
            auto* obj = Mgr::resolve<Vec::node_type>( p );
            if ( obj )
                collected.push_back( obj->value );
        }
    }

    PMM_TEST( collected.size() == 7 );
    for ( int i = 0; i < 7; ++i )
        PMM_TEST( collected[static_cast<std::size_t>( i )] == i );

    Mgr::destroy();
    return true;
}

// ─── Test: pvector large (triggers multiple rotations/rebalances) ────────────

static bool test_pvector_large()
{
    Mgr::create( 256 * 1024 );

    Vec vec;
    // Insert enough elements to trigger multiple levels of AVL rebalancing
    for ( int i = 0; i < 100; ++i )
        vec.push_back( i );

    PMM_TEST( vec.size() == 100 );

    // Verify all elements are accessible
    for ( int i = 0; i < 100; ++i )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        PMM_TEST( !p.is_null() );
        PMM_TEST( Mgr::resolve<Vec::node_type>( p )->value == i );
    }

    // Pop half
    for ( int i = 0; i < 50; ++i )
        PMM_TEST( vec.pop_back() );

    PMM_TEST( vec.size() == 50 );

    // Verify remaining
    for ( int i = 0; i < 50; ++i )
    {
        auto p = vec.at( static_cast<std::size_t>( i ) );
        PMM_TEST( !p.is_null() );
        PMM_TEST( Mgr::resolve<Vec::node_type>( p )->value == i );
    }

    Mgr::destroy();
    return true;
}

// ─── Test: pmap still works (no regression from NodeUpdateFn default) ────────

static bool test_pmap_no_regression()
{
    Mgr::create( 64 * 1024 );

    Map map;
    map.insert( 42, 100 );
    map.insert( 10, 200 );
    map.insert( 99, 300 );

    auto p42 = map.find( 42 );
    PMM_TEST( !p42.is_null() );
    PMM_TEST( Mgr::resolve<Map::node_type>( p42 )->value == 100 );

    auto p10 = map.find( 10 );
    PMM_TEST( !p10.is_null() );
    PMM_TEST( Mgr::resolve<Map::node_type>( p10 )->value == 200 );

    PMM_TEST( map.contains( 99 ) );
    PMM_TEST( !map.contains( 55 ) );

    // Update existing key
    map.insert( 42, 999 );
    p42 = map.find( 42 );
    PMM_TEST( Mgr::resolve<Map::node_type>( p42 )->value == 999 );

    Mgr::destroy();
    return true;
}

// ─── Test: pstringview still works (no regression) ───────────────────────────

static bool test_pstringview_no_regression()
{
    Mgr::create( 64 * 1024 );
    Str::reset();

    Mgr::pptr<Str> p1 = Str( "hello" );
    PMM_TEST( !p1.is_null() );

    Mgr::pptr<Str> p2 = Str( "hello" );
    PMM_TEST( p1 == p2 ); // interning: same string = same pptr

    Mgr::pptr<Str> p3 = Str( "world" );
    PMM_TEST( !p3.is_null() );
    PMM_TEST( p1 != p3 );

    auto* s1 = Mgr::resolve<Str>( p1 );
    PMM_TEST( s1 != nullptr );
    PMM_TEST( *s1 == "hello" );
    PMM_TEST( s1->size() == 5 );

    Str::reset();
    Mgr::destroy();
    return true;
}

// ─── Test: combined pvector + pmap (no interaction issues) ───────────────────

static bool test_combined_pvector_pmap()
{
    Mgr::create( 128 * 1024 );
    Str::reset();

    // Use pvector and pmap simultaneously
    Vec vec;
    Map map;

    for ( int i = 0; i < 20; ++i )
    {
        vec.push_back( i * 10 );
        map.insert( i, i * 100 );
    }

    PMM_TEST( vec.size() == 20 );
    PMM_TEST( map.contains( 5 ) );
    PMM_TEST( Mgr::resolve<Vec::node_type>( vec.at( 5 ) )->value == 50 );
    PMM_TEST( Mgr::resolve<Map::node_type>( map.find( 5 ) )->value == 500 );

    // Pop from vector, check map unchanged
    for ( int i = 0; i < 10; ++i )
        vec.pop_back();

    PMM_TEST( vec.size() == 10 );
    PMM_TEST( map.contains( 15 ) ); // map unaffected

    Str::reset();
    Mgr::destroy();
    return true;
}

// ─── main ────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_issue188_deduplication ===\n";
    bool ok = true;

    PMM_RUN( "pvector_basic_ops", test_pvector_basic_ops );
    PMM_RUN( "pvector_front_back", test_pvector_front_back );
    PMM_RUN( "pvector_pop_back", test_pvector_pop_back );
    PMM_RUN( "pvector_clear", test_pvector_clear );
    PMM_RUN( "pvector_iterator", test_pvector_iterator );
    PMM_RUN( "pvector_large", test_pvector_large );
    PMM_RUN( "pmap_no_regression", test_pmap_no_regression );
    PMM_RUN( "pstringview_no_regression", test_pstringview_no_regression );
    PMM_RUN( "combined_pvector_pmap", test_combined_pvector_pmap );

    std::cout << ( ok ? "All tests passed.\n" : "Some tests FAILED.\n" );
    return ok ? 0 : 1;
}
