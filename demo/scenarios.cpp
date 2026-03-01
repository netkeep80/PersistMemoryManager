/**
 * @file scenarios.cpp
 * @brief Implementations of all 7 load scenarios for the PMM visual demo.
 *
 * Each scenario runs its logic loop in a dedicated thread managed by
 * ScenarioManager.  All scenarios must honour the stop_flag by checking it
 * frequently and returning as soon as it is set.
 *
 * Phase 10: All scenarios accept a ScenarioCoordinator reference and call
 * coordinator.yield_if_paused() at safe inter-operation points.  The
 * PersistenceCycle scenario uses coordinator.pause_others() / resume_others()
 * to ensure exclusive access during PMM destroy()/reload() (fixes plan.md
 * Risk #5).
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
#include <cstdlib>
#include <deque>
#include <random>
#include <thread>
#include <vector>

namespace demo
{

// ─── ScenarioCoordinator implementation ──────────────────────────────────────

void ScenarioCoordinator::pause_others()
{
    {
        std::lock_guard<std::mutex> lk( mutex_ );
        paused_.store( true, std::memory_order_release );
    }
    // No need to notify here; threads already running will hit yield_if_paused
    // on their next iteration and block themselves.
}

void ScenarioCoordinator::resume_others()
{
    {
        std::lock_guard<std::mutex> lk( mutex_ );
        paused_.store( false, std::memory_order_release );
    }
    cv_.notify_all();
}

void ScenarioCoordinator::yield_if_paused( const std::atomic<bool>& stop_flag )
{
    if ( !paused_.load( std::memory_order_acquire ) )
        return;

    std::unique_lock<std::mutex> lk( mutex_ );
    cv_.wait( lk, [this, &stop_flag]
              { return !paused_.load( std::memory_order_relaxed ) || stop_flag.load( std::memory_order_relaxed ); } );
}

bool ScenarioCoordinator::is_paused() const noexcept
{
    return paused_.load( std::memory_order_acquire );
}

// ─── Utility ──────────────────────────────────────────────────────────────────

namespace
{

/// Sleep until the next tick defined by a fixed-rate timer.
inline void rate_sleep( std::chrono::steady_clock::time_point& next, std::chrono::duration<double> interval )
{
    next += std::chrono::duration_cast<std::chrono::steady_clock::duration>( interval );
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

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
    {
        auto*      mgr      = pmm::PersistMemoryManager::instance();
        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::vector<void*> live;
        live.reserve( 512 );

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            auto* mgr_before = pmm::PersistMemoryManager::instance();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // If the PMM singleton was replaced while we were paused, our live
            // pointers point into the old (now freed) buffer — discard them.
            if ( pmm::PersistMemoryManager::instance() != mgr_before )
                live.clear();

            // Fill phase
            while ( !stop.load( std::memory_order_relaxed ) )
            {
                auto* inner_before = pmm::PersistMemoryManager::instance();
                coord.yield_if_paused( stop );
                if ( stop.load( std::memory_order_relaxed ) )
                    break;
                if ( pmm::PersistMemoryManager::instance() != inner_before )
                    live.clear();
                mgr = pmm::PersistMemoryManager::instance();
                if ( !mgr )
                    break;
                void* ptr = mgr->allocate( p.min_block_size );
                if ( !ptr )
                    break;
                live.push_back( ptr );
                ops.fetch_add( 1, std::memory_order_relaxed );
                rate_sleep( next, interval );
            }

            // Free phase
            mgr = pmm::PersistMemoryManager::instance();
            for ( void* ptr : live )
            {
                if ( stop.load( std::memory_order_relaxed ) )
                    break;
                if ( mgr )
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

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
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
            auto* mgr_before = pmm::PersistMemoryManager::instance();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointers if PMM singleton was replaced while paused.
            if ( pmm::PersistMemoryManager::instance() != mgr_before )
                live.clear();

            mgr = pmm::PersistMemoryManager::instance();
            if ( !mgr )
            {
                rate_sleep( next, alloc_interval );
                continue;
            }

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
        mgr = pmm::PersistMemoryManager::instance();
        if ( mgr )
        {
            for ( void* ptr : live )
                mgr->deallocate( ptr );
        }
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

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
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
            auto* mgr_before = pmm::PersistMemoryManager::instance();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointers if PMM singleton was replaced while paused.
            if ( pmm::PersistMemoryManager::instance() != mgr_before )
            {
                small_live.clear();
                large_live.clear();
            }

            mgr = pmm::PersistMemoryManager::instance();
            if ( !mgr )
            {
                rate_sleep( next, interval );
                continue;
            }

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

        mgr = pmm::PersistMemoryManager::instance();
        if ( mgr )
        {
            for ( void* ptr : small_live )
                mgr->deallocate( ptr );
            for ( void* ptr : large_live )
                mgr->deallocate( ptr );
        }
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

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
    {
        auto*                                      mgr = pmm::PersistMemoryManager::instance();
        std::mt19937                               rng( std::random_device{}() );
        std::uniform_int_distribution<std::size_t> size_dist( p.min_block_size, p.max_block_size );

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::deque<void*> fifo;

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            auto* mgr_before = pmm::PersistMemoryManager::instance();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointers if PMM singleton was replaced while paused.
            if ( pmm::PersistMemoryManager::instance() != mgr_before )
                fifo.clear();

            mgr = pmm::PersistMemoryManager::instance();
            if ( !mgr )
            {
                rate_sleep( next, interval );
                continue;
            }

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

        mgr = pmm::PersistMemoryManager::instance();
        if ( mgr )
        {
            while ( !fifo.empty() )
            {
                mgr->deallocate( fifo.front() );
                fifo.pop_front();
            }
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

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
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
            auto* mgr_before = pmm::PersistMemoryManager::instance();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointers if PMM singleton was replaced while paused.
            if ( pmm::PersistMemoryManager::instance() != mgr_before )
                fifo.clear();

            mgr = pmm::PersistMemoryManager::instance();
            if ( !mgr )
            {
                rate_sleep( next, interval );
                continue;
            }

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

        mgr = pmm::PersistMemoryManager::instance();
        if ( mgr )
        {
            while ( !fifo.empty() )
            {
                mgr->deallocate( fifo.front() );
                fifo.pop_front();
            }
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

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
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
            auto* mgr_before = pmm::PersistMemoryManager::instance();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointers if PMM singleton was replaced while paused.
            if ( pmm::PersistMemoryManager::instance() != mgr_before )
                live.clear();

            mgr = pmm::PersistMemoryManager::instance();
            if ( !mgr )
            {
                rate_sleep( next, interval );
                continue;
            }

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

        mgr = pmm::PersistMemoryManager::instance();
        if ( mgr )
        {
            for ( void* ptr : live )
                mgr->deallocate( ptr );
        }
    }
};

// ─── Scenario 7: Persistence Cycle ───────────────────────────────────────────

/**
 * Periodically saves the PMM image to disk, destroys and reloads it,
 * then validates the restored state.
 * Default params: min=128, max=1024, cycle_period via alloc_freq (1/alloc_freq s).
 *
 * Phase 10: Uses ScenarioCoordinator to pause all other scenarios before
 * calling destroy() and to resume them after reload().  This fixes plan.md
 * Risk #5 (critical: destroy() must not be called while other scenario threads
 * are using the PMM singleton).
 */
class PersistenceCycle final : public Scenario
{
  public:
    const char* name() const override { return "Persistence Cycle"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
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
                mgr = pmm::PersistMemoryManager::instance();
                if ( !mgr )
                    break;
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

            mgr = pmm::PersistMemoryManager::instance();
            if ( !mgr )
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

            // --- Phase 10: pause all other scenarios before destroy() ---
            coord.pause_others();

            // Give other scenario threads a moment to reach yield_if_paused()
            // and block.  50 ms is conservative; typical loop iterations are
            // << 1 ms except for TinyBlocks which has a 0.1 ms interval.
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

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
                mgr = pmm::PersistMemoryManager::create( buf, total );
            }

            // --- Phase 10: resume all other scenarios ---
            coord.resume_others();

            // Wait for cycle period
            auto deadline = std::chrono::steady_clock::now() + cycle_dur;
            while ( !stop.load( std::memory_order_relaxed ) && std::chrono::steady_clock::now() < deadline )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            }
        }

        // Cleanup
        mgr = pmm::PersistMemoryManager::instance();
        if ( mgr )
        {
            for ( void* ptr : live )
                mgr->deallocate( ptr );
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
