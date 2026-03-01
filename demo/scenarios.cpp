/**
 * @file scenarios.cpp
 * @brief Implementations of all 7 load scenarios for the PMM visual demo.
 *
 * Each scenario runs its logic loop in a dedicated thread managed by
 * ScenarioManager.  All scenarios must honour the stop_flag by checking it
 * frequently and returning as soon as it is set.
 *
 * Scenarios implemented here (Phases 4, 5):
 *   1. LinearFill     – fill then free sequentially
 *   2. RandomStress   – random alloc/dealloc mix
 *   3. FragmentationDemo – create fragmentation holes
 *   4. LargeBlocks    – large allocations, tests auto-grow
 *   5. TinyBlocks     – high-frequency micro-alloc/dealloc
 *   6. MixedSizes     – mixed work profiles with occasional reallocate
 *   7. PersistenceCycle – periodic save/destroy/reload cycle
 */

#include "scenarios.h"

#include "persist_memory_manager.h"
#include "persist_memory_io.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <random>
#include <thread>
#include <vector>

namespace demo
{

// ─── Utility ──────────────────────────────────────────────────────────────────

namespace
{

/// Sleep until the next tick defined by a fixed-rate timer.
inline void rate_sleep( std::chrono::steady_clock::time_point& next, std::chrono::duration<double> interval )
{
    next += interval;
    std::this_thread::sleep_until( next );
}

} // namespace

// ─── Scenario 1: Linear Fill ─────────────────────────────────────────────────

/**
 * Allocates same-size blocks until OOM, then frees them all.
 * Default params: min_block_size=256, alloc_freq=500, dealloc_freq=0.
 */
class LinearFill final : public Scenario
{
  public:
    const char* name() const override { return "Linear Fill"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p ) override
    {
        auto*      mgr      = pmm::PersistMemoryManager::instance();
        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::vector<void*> live;
        live.reserve( 512 );

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            // Fill phase
            while ( !stop.load( std::memory_order_relaxed ) )
            {
                void* ptr = mgr->allocate( p.min_block_size );
                if ( !ptr )
                    break;
                live.push_back( ptr );
                ops.fetch_add( 1, std::memory_order_relaxed );
                rate_sleep( next, interval );
            }

            // Free phase
            for ( void* ptr : live )
            {
                if ( stop.load( std::memory_order_relaxed ) )
                    break;
                mgr->deallocate( ptr );
                ops.fetch_add( 1, std::memory_order_relaxed );
            }
            live.clear();
        }
    }
};

// ─── Scenario 2: Random Stress ────────────────────────────────────────────────

/**
 * Randomly allocates and deallocates blocks with configurable frequencies.
 * Default params: min=64, max=4096, alloc_freq=2000, dealloc_freq=1800.
 */
class RandomStress final : public Scenario
{
  public:
    const char* name() const override { return "Random Stress"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p ) override
    {
        auto*                                      mgr = pmm::PersistMemoryManager::instance();
        std::mt19937                               rng( std::random_device{}() );
        std::uniform_int_distribution<std::size_t> size_dist( p.min_block_size, p.max_block_size );
        std::uniform_real_distribution<float>      choice( 0.0f, p.alloc_freq + p.dealloc_freq );

        const auto alloc_interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next           = std::chrono::steady_clock::now();

        std::vector<void*> live;
        live.reserve( static_cast<std::size_t>( p.max_live_blocks ) );

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            bool do_alloc =
                live.empty() || ( static_cast<int>( live.size() ) < p.max_live_blocks && choice( rng ) < p.alloc_freq );

            if ( do_alloc )
            {
                void* ptr = mgr->allocate( size_dist( rng ) );
                if ( ptr )
                {
                    live.push_back( ptr );
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
            }
            else if ( !live.empty() )
            {
                std::uniform_int_distribution<std::size_t> idx( 0, live.size() - 1 );
                std::size_t                                i = idx( rng );
                mgr->deallocate( live[i] );
                live[i] = live.back();
                live.pop_back();
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            rate_sleep( next, alloc_interval );
        }

        // Cleanup remaining live blocks
        for ( void* ptr : live )
            mgr->deallocate( ptr );
    }
};

// ─── Scenario 3: Fragmentation Demo ──────────────────────────────────────────

/**
 * Alternates small and large allocations, freeing only small blocks to create
 * fragmentation holes.
 * Default params: min=16, max=16384, alloc_freq=300, dealloc_freq=250.
 */
class FragmentationDemo final : public Scenario
{
  public:
    const char* name() const override { return "Fragmentation Demo"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p ) override
    {
        auto*        mgr = pmm::PersistMemoryManager::instance();
        std::mt19937 rng( std::random_device{}() );

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::vector<void*> small_live;
        std::vector<void*> large_live;
        small_live.reserve( 256 );
        large_live.reserve( 64 );

        bool alloc_small = true;

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            if ( alloc_small )
            {
                void* ptr = mgr->allocate( 16 + ( rng() % 48 ) ); // 16..63
                if ( ptr )
                    small_live.push_back( ptr );
            }
            else
            {
                void* ptr = mgr->allocate( 4096 + ( rng() % 12288 ) ); // 4096..16383
                if ( ptr )
                    large_live.push_back( ptr );
            }
            ops.fetch_add( 1, std::memory_order_relaxed );
            alloc_small = !alloc_small;

            // Periodically free small blocks to create holes
            if ( small_live.size() > 20 )
            {
                std::size_t to_free = small_live.size() / 2;
                for ( std::size_t i = 0; i < to_free; ++i )
                {
                    mgr->deallocate( small_live[i] );
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
                small_live.erase( small_live.begin(), small_live.begin() + static_cast<std::ptrdiff_t>( to_free ) );
            }

            // Free large blocks if too many accumulate
            if ( large_live.size() > 16 )
            {
                mgr->deallocate( large_live.front() );
                large_live.erase( large_live.begin() );
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            rate_sleep( next, interval );
        }

        for ( void* ptr : small_live )
            mgr->deallocate( ptr );
        for ( void* ptr : large_live )
            mgr->deallocate( ptr );
    }
};

// ─── Scenario 4: Large Blocks ─────────────────────────────────────────────────

/**
 * Allocates large blocks in FIFO order; PMM auto-grows when memory runs out.
 * Default params: min=65536, max=262144, alloc_freq=20, dealloc_freq=18.
 */
class LargeBlocks final : public Scenario
{
  public:
    const char* name() const override { return "Large Blocks"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p ) override
    {
        auto*                                      mgr = pmm::PersistMemoryManager::instance();
        std::mt19937                               rng( std::random_device{}() );
        std::uniform_int_distribution<std::size_t> size_dist( p.min_block_size, p.max_block_size );

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::deque<void*> fifo;

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            void* ptr = mgr->allocate( size_dist( rng ) );
            if ( ptr )
            {
                fifo.push_back( ptr );
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            if ( fifo.size() > 8 )
            {
                mgr->deallocate( fifo.front() );
                fifo.pop_front();
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            rate_sleep( next, interval );
        }

        while ( !fifo.empty() )
        {
            mgr->deallocate( fifo.front() );
            fifo.pop_front();
        }
    }
};

// ─── Scenario 5: Tiny Blocks ─────────────────────────────────────────────────

/**
 * High-frequency micro-alloc/dealloc using a FIFO queue.
 * Default params: min=8, max=32, alloc_freq=10000, dealloc_freq=9500.
 */
class TinyBlocks final : public Scenario
{
  public:
    const char* name() const override { return "Tiny Blocks"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p ) override
    {
        auto*        mgr = pmm::PersistMemoryManager::instance();
        std::mt19937 rng( std::random_device{}() );

        const std::size_t                          min_sz = std::max( p.min_block_size, std::size_t( 8 ) );
        const std::size_t                          max_sz = std::max( p.max_block_size, min_sz );
        std::uniform_int_distribution<std::size_t> size_dist( min_sz, max_sz );

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::deque<void*> fifo;

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            void* ptr = mgr->allocate( size_dist( rng ) );
            if ( ptr )
            {
                fifo.push_back( ptr );
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            if ( fifo.size() > static_cast<std::size_t>( p.max_live_blocks ) )
            {
                mgr->deallocate( fifo.front() );
                fifo.pop_front();
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            rate_sleep( next, interval );
        }

        while ( !fifo.empty() )
        {
            mgr->deallocate( fifo.front() );
            fifo.pop_front();
        }
    }
};

// ─── Scenario 6: Mixed Sizes ─────────────────────────────────────────────────

/**
 * Simulates two work profiles (A: mostly small, B: medium) with occasional
 * reallocate() calls.
 * Default params: min=32, max=32768, alloc_freq=1000, dealloc_freq=950.
 */
class MixedSizes final : public Scenario
{
  public:
    const char* name() const override { return "Mixed Sizes"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p ) override
    {
        auto*                                 mgr = pmm::PersistMemoryManager::instance();
        std::mt19937                          rng( std::random_device{}() );
        std::uniform_real_distribution<float> chance( 0.0f, 1.0f );

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::vector<void*> live;
        live.reserve( static_cast<std::size_t>( p.max_live_blocks ) );

        // Profile A: 80% small [32..256], 20% large [1024..32768]
        // Profile B: 50% medium [256..4096]
        bool profile_a       = true;
        int  profile_counter = 0;

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            // Switch profile every 50 operations
            if ( ++profile_counter >= 50 )
            {
                profile_a       = !profile_a;
                profile_counter = 0;
            }

            std::size_t sz = 0;
            if ( profile_a )
            {
                sz = ( chance( rng ) < 0.8f ) ? ( 32 + rng() % 225 )      // 32..256
                                              : ( 1024 + rng() % 31745 ); // 1024..32768
            }
            else
            {
                sz = 256 + rng() % 3841; // 256..4096
            }

            // 5% chance of reallocate on a live block
            if ( chance( rng ) < 0.05f && !live.empty() )
            {
                std::uniform_int_distribution<std::size_t> idx( 0, live.size() - 1 );
                std::size_t                                i      = idx( rng );
                void*                                      newptr = mgr->reallocate( live[i], sz );
                if ( newptr )
                {
                    live[i] = newptr;
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
            }
            else if ( static_cast<int>( live.size() ) < p.max_live_blocks )
            {
                void* ptr = mgr->allocate( sz );
                if ( ptr )
                {
                    live.push_back( ptr );
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
            }
            else if ( !live.empty() )
            {
                std::uniform_int_distribution<std::size_t> idx( 0, live.size() - 1 );
                std::size_t                                i = idx( rng );
                mgr->deallocate( live[i] );
                live[i] = live.back();
                live.pop_back();
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            rate_sleep( next, interval );
        }

        for ( void* ptr : live )
            mgr->deallocate( ptr );
    }
};

// ─── Scenario 7: Persistence Cycle ───────────────────────────────────────────

/**
 * Periodically saves the PMM image to disk, destroys and reloads it,
 * then validates the restored state.
 * Default params: min=128, max=1024, cycle_period via alloc_freq (1/alloc_freq s).
 *
 * NOTE: This scenario stops all other operations while performing destroy/load,
 * which involves stopping and restarting the singleton. It is designed to run
 * only when other scenarios are idle or will gracefully fail to allocate
 * during the reload window.
 */
class PersistenceCycle final : public Scenario
{
  public:
    const char* name() const override { return "Persistence Cycle"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p ) override
    {
        auto*                                      mgr = pmm::PersistMemoryManager::instance();
        std::mt19937                               rng( std::random_device{}() );
        std::uniform_int_distribution<std::size_t> size_dist( p.min_block_size, p.max_block_size );

        // cycle_period: use alloc_freq as 1/period (default ~5 s → 0.2)
        const double cycle_period = ( p.alloc_freq > 0.0f ) ? ( 1.0 / static_cast<double>( p.alloc_freq ) ) : 5.0;
        const auto   cycle_dur    = std::chrono::duration<double>( cycle_period );

        std::vector<void*> live;
        live.reserve( 16 );

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            // Allocate a few blocks and write data
            for ( int i = 0; i < 4 && !stop.load( std::memory_order_relaxed ); ++i )
            {
                void* ptr = mgr->allocate( size_dist( rng ) );
                if ( ptr )
                {
                    std::memset( ptr, static_cast<int>( i + 1 ), p.min_block_size );
                    live.push_back( ptr );
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
            }

            if ( stop.load( std::memory_order_relaxed ) )
                break;

            // Save image
            pmm::save( mgr, "pmm_demo.bin" );

            // Free live blocks before destroy
            for ( void* ptr : live )
                mgr->deallocate( ptr );
            live.clear();

            // Retrieve buffer info before destroy
            std::size_t total = mgr->total_size();
            void*       buf   = std::malloc( total );
            if ( !buf )
                break;

            // Destroy and reload from file
            pmm::PersistMemoryManager::destroy();
            auto* mgr2 = pmm::load_from_file( "pmm_demo.bin", buf, total );
            if ( mgr2 )
            {
                mgr2->validate();
                ops.fetch_add( 1, std::memory_order_relaxed );
                mgr = mgr2; // use reloaded instance
            }
            else
            {
                // Fallback: recreate fresh
                pmm::PersistMemoryManager::create( buf, total );
                mgr = pmm::PersistMemoryManager::instance();
            }

            // Wait for cycle period
            auto deadline = std::chrono::steady_clock::now() + cycle_dur;
            while ( !stop.load( std::memory_order_relaxed ) && std::chrono::steady_clock::now() < deadline )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            }
        }

        // Cleanup
        for ( void* ptr : live )
        {
            if ( pmm::PersistMemoryManager::instance() )
                pmm::PersistMemoryManager::instance()->deallocate( ptr );
        }
    }
};

// ─── Factory ──────────────────────────────────────────────────────────────────

/// Create all 7 scenario instances.
std::vector<std::unique_ptr<Scenario>> create_all_scenarios()
{
    std::vector<std::unique_ptr<Scenario>> v;
    v.push_back( std::make_unique<LinearFill>() );
    v.push_back( std::make_unique<RandomStress>() );
    v.push_back( std::make_unique<FragmentationDemo>() );
    v.push_back( std::make_unique<LargeBlocks>() );
    v.push_back( std::make_unique<TinyBlocks>() );
    v.push_back( std::make_unique<MixedSizes>() );
    v.push_back( std::make_unique<PersistenceCycle>() );
    return v;
}

} // namespace demo
