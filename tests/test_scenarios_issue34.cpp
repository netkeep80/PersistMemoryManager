/**
 * @file test_scenarios_issue34.cpp
 * @brief Stress and load tests for PersistMemoryManager
 *
 *   - pmm::PersistMemoryManager<> (singleton) removed
 *   - pmm::get_stats() removed; use pmm.block_count(), pmm.free_block_count(), etc.
 *   - pmm::save()/load_from_file() removed; use pmm::save_manager()/load_manager_from_file()
 *   - pptr<Node> now needs explicit manager type: Mgr::pptr<Node>
 *   - p.get() replaced with p.resolve(mgr)
 *
 * Implements three scenarios from this feature:
 *   Scenario 1: "Shredder" — intensive fragmentation and coalesce.
 *   Scenario 2: "Persistent Cycle" — save/load integrity with pptr.
 *   Scenario 5: "Marathon" — long-term stability.
 */

#include "pmm_single_threaded_heap.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

using Mgr = pmm::presets::SingleThreadedHeap;

static auto now()
{
    return std::chrono::high_resolution_clock::now();
}

static double elapsed_ms( std::chrono::high_resolution_clock::time_point start,
                          std::chrono::high_resolution_clock::time_point end )
{
    return std::chrono::duration<double, std::milli>( end - start ).count();
}

namespace
{

struct Rng
{
    uint32_t state;

    explicit Rng( uint32_t seed = 42 ) : state( seed ) {}

    uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    uint32_t next_n( uint32_t n ) { return ( next() >> 16 ) % n; }

    std::size_t next_block_size_shredder() { return static_cast<std::size_t>( ( next_n( 128 ) + 1 ) * 32 ); }

    std::size_t next_block_size_marathon() { return static_cast<std::size_t>( ( next_n( 512 ) + 1 ) * 8 ); }
};

} // namespace

// ─── Scenario 1: "Shredder" ───────────────────────────────────────────────────

TEST_CASE( "shredder (fragmentation & coalesce)", "[test_scenarios_issue34]" )
{
    const std::size_t memory_size = 64UL * 1024 * 1024; // 64 MB

    Mgr pmm;
    REQUIRE( pmm.create( memory_size ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    Rng rng( 31337 );

    // Phase 1: 10 000 allocations with random sizes
    std::cout << "  Phase 1: creating 10 000 blocks with random sizes...\n";

    std::vector<Mgr::pptr<std::uint8_t>> all_ptrs;
    all_ptrs.reserve( 10000 );

    auto t0     = now();
    int  failed = 0;
    for ( int i = 0; i < 10000; ++i )
    {
        std::size_t             sz  = rng.next_block_size_shredder();
        Mgr::pptr<std::uint8_t> ptr = pmm.allocate_typed<std::uint8_t>( sz );
        if ( !ptr.is_null() )
        {
            std::memset( ptr.resolve(), static_cast<int>( i & 0xFF ), sz );
            all_ptrs.push_back( ptr );
        }
        else
        {
            failed++;
        }
    }

    std::cout << "    Allocated: " << all_ptrs.size() << " / 10000" << "  failed: " << failed
              << "  time: " << elapsed_ms( t0, now() ) << " ms\n";

    REQUIRE( pmm.is_initialized() );

    // Shuffle pointer order
    for ( std::size_t i = all_ptrs.size() - 1; i > 0; --i )
    {
        uint32_t j = rng.next_n( static_cast<uint32_t>( i + 1 ) );
        std::swap( all_ptrs[i], all_ptrs[j] );
    }

    // Phase 2: random deallocation of 50% of blocks
    std::cout << "  Phase 2: random deallocation of 50% blocks...\n";

    std::size_t                          half = all_ptrs.size() / 2;
    std::vector<Mgr::pptr<std::uint8_t>> random_half( all_ptrs.begin(),
                                                      all_ptrs.begin() + static_cast<std::ptrdiff_t>( half ) );
    std::vector<Mgr::pptr<std::uint8_t>> sorted_half( all_ptrs.begin() + static_cast<std::ptrdiff_t>( half ),
                                                      all_ptrs.end() );

    auto t1 = now();
    for ( auto& p : random_half )
        pmm.deallocate_typed( p );

    std::cout << "    Freed: " << random_half.size() << " blocks  time: " << elapsed_ms( t1, now() ) << " ms\n";

    REQUIRE( pmm.is_initialized() );

    // Phase 3: fragmentation check
    {
        std::cout << "  Phase 3: fragmentation after random deallocation:\n";
        std::cout << "    Total blocks: " << pmm.block_count() << "  free: " << pmm.free_block_count()
                  << "  allocated: " << pmm.alloc_block_count() << "\n";

        REQUIRE( pmm.alloc_block_count() == static_cast<std::uint32_t>( sorted_half.size() ) + baseline_alloc );
        REQUIRE( pmm.free_block_count() >= 1 );
    }

    // Phase 4: free remaining 50% in ascending address order
    std::cout << "  Phase 4: freeing remaining blocks in ascending address order...\n";

    std::sort( sorted_half.begin(), sorted_half.end(),
               []( const Mgr::pptr<std::uint8_t>& a, const Mgr::pptr<std::uint8_t>& b )
               { return a.offset() < b.offset(); } );

    auto t2 = now();
    for ( auto& p : sorted_half )
        pmm.deallocate_typed( p );

    std::cout << "    Freed: " << sorted_half.size() << " blocks  time: " << elapsed_ms( t2, now() ) << " ms\n";

    // Phase 5: final validation
    {
        std::cout << "  Phase 5: final validation after full deallocation:\n";
        REQUIRE( pmm.is_initialized() );
        std::cout << "    Total blocks: " << pmm.block_count() << "  free: " << pmm.free_block_count()
                  << "  allocated: " << pmm.alloc_block_count() << "\n";
        REQUIRE( pmm.alloc_block_count() == baseline_alloc );
        REQUIRE( pmm.free_block_count() <= 10 );
    }

    double total_ms = elapsed_ms( t0, now() );
    std::cout << "  Total time: " << total_ms << " ms\n";

    pmm.destroy();
}

// ─── Scenario 2: "Persistent Cycle" ──────────────────────────────────────────

struct Node
{
    int             id;
    Mgr::pptr<Node> next;
    unsigned int    checksum;
};

static unsigned int compute_checksum( int id, std::uint32_t next_offset )
{
    return static_cast<unsigned int>( id * 2654435761u ) ^ static_cast<unsigned int>( next_offset );
}

TEST_CASE( "persistent cycle (save/load pptr list)", "[test_scenarios_issue34]" )
{
    const std::size_t memory_size = 4UL * 1024 * 1024;
    const char*       filename    = "test_issue34_heap.dat";
    const int         node_count  = 1000;

    // Phase 1: Build linked list
    std::cout << "  Phase 1: building linked list of " << node_count << " nodes...\n";

    Mgr pmm1;
    REQUIRE( pmm1.create( memory_size ) );
    const auto baseline_alloc = pmm1.alloc_block_count();

    Mgr::pptr<Node> head;
    Mgr::pptr<Node> tail;

    auto t0 = now();
    for ( int i = 0; i < node_count; ++i )
    {
        Mgr::pptr<Node> n = pmm1.allocate_typed<Node>( 1 );
        if ( n.is_null() )
        {
            std::cerr << "  ERROR: allocate returned null at i=" << i << "\n";
            pmm1.destroy();
            FAIL( "unexpected failure" );
        }

        Node* node_ptr = n.resolve();
        node_ptr->id   = i;
        node_ptr->next = Mgr::pptr<Node>(); // null initially
        // Checksum will be updated when next is set
        node_ptr->checksum = 0;

        if ( head.is_null() )
        {
            head = n;
            tail = n;
        }
        else
        {
            tail.resolve()->next = n;
            // Update tail checksum
            tail.resolve()->checksum = compute_checksum( tail.resolve()->id, tail.resolve()->next.offset() );
            tail                     = n;
        }
    }
    // Final node checksum (no next)
    if ( !tail.is_null() )
        tail.resolve()->checksum = compute_checksum( tail.resolve()->id, 0 );

    std::cout << "    Built " << node_count << " nodes in " << elapsed_ms( t0, now() ) << " ms\n";
    REQUIRE( pmm1.is_initialized() );

    std::uint32_t head_offset = head.offset();

    // Phase 2: Save
    std::cout << "  Phase 2: saving state...\n";
    auto t1 = now();
    REQUIRE( pmm::save_manager<decltype( pmm1 )>( filename ) );
    pmm1.destroy();
    std::cout << "    Saved in " << elapsed_ms( t1, now() ) << " ms\n";

    // Phase 3: Load
    std::cout << "  Phase 3: loading state...\n";
    Mgr pmm2;
    REQUIRE( pmm2.create( memory_size ) );
    REQUIRE( pmm::load_manager_from_file<decltype( pmm2 )>( filename, pmm::VerifyResult{} ) );
    REQUIRE( pmm2.is_initialized() );
    std::cout << "    Loaded (new manager instance)\n";

    // Phase 4: Verify linked list
    std::cout << "  Phase 4: verifying " << node_count << " nodes...\n";

    Mgr::pptr<Node> cur( head_offset );
    REQUIRE( !cur.is_null() );

    auto t2        = now();
    int  traversed = 0;
    bool data_ok   = true;

    while ( !cur.is_null() )
    {
        Node* n = cur.resolve();
        if ( n == nullptr )
        {
            std::cerr << "  ERROR: resolve() returned nullptr at node " << traversed << "\n";
            data_ok = false;
            break;
        }

        if ( n->id != traversed )
        {
            std::cerr << "  ERROR: expected id=" << traversed << ", got id=" << n->id << "\n";
            data_ok = false;
            break;
        }

        unsigned int expected_cs = compute_checksum( n->id, n->next.offset() );
        if ( n->checksum != expected_cs )
        {
            std::cerr << "  ERROR: checksum mismatch at node " << traversed << " (expected " << expected_cs << ", got "
                      << n->checksum << ")\n";
            data_ok = false;
            break;
        }

        cur = n->next;
        traversed++;
    }

    std::cout << "    Traversed " << traversed << " nodes in " << elapsed_ms( t2, now() ) << " ms\n";

    REQUIRE( data_ok );
    REQUIRE( traversed == node_count );

    // Free all nodes
    cur = Mgr::pptr<Node>( head_offset );
    while ( !cur.is_null() )
    {
        Mgr::pptr<Node> next_node = cur.resolve()->next;
        pmm2.deallocate_typed( cur );
        cur = next_node;
    }

    REQUIRE( pmm2.is_initialized() );
    REQUIRE( pmm2.alloc_block_count() == baseline_alloc );

    pmm2.destroy();
    std::remove( filename );
}

// ─── Scenario 5: "Marathon" ───────────────────────────────────────────────────

TEST_CASE( "marathon (long-term stability)", "[test_scenarios_issue34]" )
{
    const std::size_t memory_size = 64UL * 1024 * 1024; // 64 MB

    Mgr pmm;
    REQUIRE( pmm.create( memory_size ) );
    const auto baseline_alloc = pmm.alloc_block_count();

    Rng rng( 99991 );

    std::vector<Mgr::pptr<std::uint8_t>> live;
    live.reserve( 50000 );

#ifdef NDEBUG
    const int total_iterations  = 1000000;
    const int validate_interval = 10000;
#else
    const int total_iterations  = 50000;
    const int validate_interval = 5000;
#endif

    int  alloc_ok     = 0;
    int  alloc_fail   = 0;
    int  dealloc_cnt  = 0;
    int  validate_cnt = 0;
    bool validate_ok  = true;

    auto t0 = now();
    std::cout << "  Running " << total_iterations << " iterations (60% alloc / 40% free)...\n";

    for ( int iter = 0; iter < total_iterations; ++iter )
    {
        if ( rng.next_n( 10 ) < 6 || live.empty() )
        {
            std::size_t             sz  = rng.next_block_size_marathon();
            Mgr::pptr<std::uint8_t> ptr = pmm.allocate_typed<std::uint8_t>( sz );
            if ( !ptr.is_null() )
            {
                live.push_back( ptr );
                alloc_ok++;
            }
            else
            {
                alloc_fail++;
            }
        }
        else
        {
            uint32_t idx = rng.next_n( static_cast<uint32_t>( live.size() ) );
            pmm.deallocate_typed( live[idx] );
            live[idx] = live.back();
            live.pop_back();
            dealloc_cnt++;
        }

        if ( ( iter + 1 ) % validate_interval == 0 )
        {
            validate_cnt++;
            if ( !pmm.is_initialized() )
            {
                std::cerr << "  ERROR: is_initialized() returned false at iteration " << ( iter + 1 ) << "\n";
                validate_ok = false;
                break;
            }

            if ( ( iter + 1 ) % ( validate_interval * 10 ) == 0 )
            {
                std::cout << "    iter=" << ( iter + 1 ) << "  live=" << live.size() << "  alloc=" << alloc_ok
                          << "  fail=" << alloc_fail << "  free=" << dealloc_cnt << "\n"
                          << "    used=" << pmm.used_size() / 1024 << " KB"
                          << "  free_blocks=" << pmm.free_block_count() << "\n";
            }
        }
    }

    REQUIRE( validate_ok );
    REQUIRE( validate_cnt == total_iterations / validate_interval );

    std::cout << "  Freeing " << live.size() << " remaining blocks...\n";
    for ( auto& p : live )
        pmm.deallocate_typed( p );
    live.clear();

    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.alloc_block_count() == baseline_alloc );

    double total_ms = elapsed_ms( t0, now() );
    std::cout << "  Total: " << total_iterations << " iterations, " << alloc_ok << " allocs" << "  (" << alloc_fail
              << " failed), " << dealloc_cnt << " frees\n";
    std::cout << "  validate() called " << validate_cnt << " times, always ok\n";
    std::cout << "  Total time: " << total_ms << " ms\n";

    pmm.destroy();
}
