/**
 * @file scenarios.cpp
 * @brief Implementations of all 8 load scenarios for the PMM visual demo.
 *
 * Each scenario runs its logic loop in a dedicated thread managed by
 * ScenarioManager.  All scenarios must honour the stop_flag by checking it
 * frequently and returning as soon as it is set.
 *
 * Phase 10: All scenarios accept a ScenarioCoordinator reference and call
 * coordinator.yield_if_paused() at safe inter-operation points.  The
 * PersistenceCycle scenario uses coordinator.pause_others() / resume_others()
 * to ensure exclusive access during save/reload (fixes plan.md Risk #5).
 *
 * Migrated to AbstractPersistMemoryManager API (Issue #102):
 *   - All PMM access via demo::g_pmm.load() instead of singleton instance().
 *   - pptr<T> is DemoMgr::pptr<T>.
 *   - p.get() replaced with p.resolve(*mgr).
 *   - reallocate_typed() replaced with manual deallocate+allocate.
 *   - PersistenceCycle: destroy()/reload() replaced with save_manager +
 *     load_manager_from_file without destroying the global manager (the global
 *     manager is owned by DemoApp and cannot be replaced from a scenario thread).
 *
 * Scenarios implemented here:
 *   1. LinearFill       – fill then free sequentially
 *   2. RandomStress     – random alloc/dealloc mix
 *   3. FragmentationDemo – create fragmentation holes
 *   4. LargeBlocks      – large allocations
 *   5. TinyBlocks       – high-frequency micro-alloc/dealloc
 *   6. MixedSizes       – mixed work profiles
 *   7. PersistenceCycle – periodic save/reload cycle (simplified)
 *   8. ReallocateTyped  – grows a block repeatedly (manual realloc)
 */

#include "scenarios.h"

#include "demo_globals.h"
#include "pmm/io.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <thread>
#include <vector>

namespace demo
{

// ─── ScenarioCoordinator implementation ──────────────────────────────────────

void ScenarioCoordinator::register_participant()
{
    std::lock_guard<std::mutex> lk( mutex_ );
    ++participants_;
}

void ScenarioCoordinator::unregister_participant()
{
    std::lock_guard<std::mutex> lk( mutex_ );
    --participants_;
    // If a pause is in progress and this thread hasn't acked yet, ack on its
    // behalf so pause_others() is not left waiting forever.
    if ( paused_.load( std::memory_order_relaxed ) )
    {
        ++ack_count_;
        cv_ack_.notify_all();
    }
}

void ScenarioCoordinator::pause_others( const std::atomic<bool>& stop_flag )
{
    {
        std::lock_guard<std::mutex> lk( mutex_ );
        ack_count_ = 0;
        paused_.store( true, std::memory_order_release );
    }
    // Wait until every registered participant has acknowledged the pause, or
    // until the caller's stop_flag is set.
    std::unique_lock<std::mutex> lk( mutex_ );
    cv_ack_.wait( lk, [this, &stop_flag]
                  { return ack_count_ >= participants_ || stop_flag.load( std::memory_order_relaxed ); } );
}

void ScenarioCoordinator::resume_others()
{
    {
        std::lock_guard<std::mutex> lk( mutex_ );
        ack_count_ = 0;
        paused_.store( false, std::memory_order_release );
    }
    cv_.notify_all();
}

void ScenarioCoordinator::yield_if_paused( const std::atomic<bool>& stop_flag )
{
    if ( !paused_.load( std::memory_order_acquire ) )
        return;

    {
        // Acknowledge the pause: increment ack_count and notify pause_others().
        std::lock_guard<std::mutex> lk( mutex_ );
        ++ack_count_;
        cv_ack_.notify_all();
    }

    // Now block until resume_others() is called (or stop_flag is set).
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
        coord.register_participant();

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::vector<DemoMgr::pptr<uint8_t>> live;
        live.reserve( 512 );

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            auto* mgr_before = g_pmm.load();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // If the PMM manager was replaced while we were paused, our live
            // pointers point into the old (now freed) buffer — discard them.
            if ( g_pmm.load() != mgr_before )
                live.clear();

            // Fill phase
            while ( !stop.load( std::memory_order_relaxed ) )
            {
                auto* inner_before = g_pmm.load();
                coord.yield_if_paused( stop );
                if ( stop.load( std::memory_order_relaxed ) )
                    break;
                if ( g_pmm.load() != inner_before )
                    live.clear();
                auto* mgr = g_pmm.load();
                if ( !mgr )
                    break;
                DemoMgr::pptr<uint8_t> ptr = mgr->allocate_typed<uint8_t>( p.min_block_size );
                if ( ptr.is_null() )
                    break;
                live.push_back( ptr );
                ops.fetch_add( 1, std::memory_order_relaxed );
                rate_sleep( next, interval );
            }

            // Free phase
            auto* mgr = g_pmm.load();
            for ( auto& ptr : live )
            {
                if ( stop.load( std::memory_order_relaxed ) )
                    break;
                if ( mgr )
                    mgr->deallocate_typed( ptr );
                ops.fetch_add( 1, std::memory_order_relaxed );
            }
            live.clear();
        }

        coord.unregister_participant();
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
        coord.register_participant();

        std::mt19937                               rng( std::random_device{}() );
        std::uniform_int_distribution<std::size_t> size_dist( p.min_block_size, p.max_block_size );
        std::uniform_real_distribution<float>      choice( 0.0f, p.alloc_freq + p.dealloc_freq );

        const auto alloc_interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next           = std::chrono::steady_clock::now();

        std::vector<DemoMgr::pptr<uint8_t>> live;
        live.reserve( static_cast<std::size_t>( p.max_live_blocks ) );

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            auto* mgr_before = g_pmm.load();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointers if PMM manager was replaced while paused.
            if ( g_pmm.load() != mgr_before )
                live.clear();

            auto* mgr = g_pmm.load();
            if ( !mgr )
            {
                rate_sleep( next, alloc_interval );
                continue;
            }

            bool do_alloc =
                live.empty() || ( static_cast<int>( live.size() ) < p.max_live_blocks && choice( rng ) < p.alloc_freq );

            if ( do_alloc )
            {
                DemoMgr::pptr<uint8_t> ptr = mgr->allocate_typed<uint8_t>( size_dist( rng ) );
                if ( !ptr.is_null() )
                {
                    live.push_back( ptr );
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
            }
            else if ( !live.empty() )
            {
                std::uniform_int_distribution<std::size_t> idx( 0, live.size() - 1 );
                std::size_t                                i = idx( rng );
                mgr->deallocate_typed( live[i] );
                live[i] = live.back();
                live.pop_back();
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            rate_sleep( next, alloc_interval );
        }

        // Cleanup remaining live blocks
        auto* mgr = g_pmm.load();
        if ( mgr )
        {
            for ( auto& ptr : live )
                mgr->deallocate_typed( ptr );
        }

        coord.unregister_participant();
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
        coord.register_participant();

        std::mt19937 rng( std::random_device{}() );

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::vector<DemoMgr::pptr<uint8_t>> small_live;
        std::vector<DemoMgr::pptr<uint8_t>> large_live;
        small_live.reserve( 256 );
        large_live.reserve( 64 );

        bool alloc_small = true;

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            auto* mgr_before = g_pmm.load();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointers if PMM manager was replaced while paused.
            if ( g_pmm.load() != mgr_before )
            {
                small_live.clear();
                large_live.clear();
            }

            auto* mgr = g_pmm.load();
            if ( !mgr )
            {
                rate_sleep( next, interval );
                continue;
            }

            if ( alloc_small )
            {
                DemoMgr::pptr<uint8_t> ptr = mgr->allocate_typed<uint8_t>( 16 + ( rng() % 48 ) ); // 16..63
                if ( !ptr.is_null() )
                    small_live.push_back( ptr );
            }
            else
            {
                DemoMgr::pptr<uint8_t> ptr =
                    mgr->allocate_typed<uint8_t>( 4096 + ( rng() % 12288 ) ); // 4096..16383
                if ( !ptr.is_null() )
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
                    mgr->deallocate_typed( small_live[i] );
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
                small_live.erase( small_live.begin(), small_live.begin() + static_cast<std::ptrdiff_t>( to_free ) );
            }

            // Free large blocks if too many accumulate
            if ( large_live.size() > 16 )
            {
                mgr->deallocate_typed( large_live.front() );
                large_live.erase( large_live.begin() );
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            rate_sleep( next, interval );
        }

        auto* mgr = g_pmm.load();
        if ( mgr )
        {
            for ( auto& ptr : small_live )
                mgr->deallocate_typed( ptr );
            for ( auto& ptr : large_live )
                mgr->deallocate_typed( ptr );
        }

        coord.unregister_participant();
    }
};

// ─── Scenario 4: Large Blocks ─────────────────────────────────────────────────

/**
 * Allocates large blocks in FIFO order.
 * Default params: min=65536, max=262144, alloc_freq=20, dealloc_freq=18.
 */
class LargeBlocks final : public Scenario
{
  public:
    const char* name() const override { return "Large Blocks"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
    {
        coord.register_participant();

        std::mt19937                               rng( std::random_device{}() );
        std::uniform_int_distribution<std::size_t> size_dist( p.min_block_size, p.max_block_size );

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::deque<DemoMgr::pptr<uint8_t>> fifo;

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            auto* mgr_before = g_pmm.load();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointers if PMM manager was replaced while paused.
            if ( g_pmm.load() != mgr_before )
                fifo.clear();

            auto* mgr = g_pmm.load();
            if ( !mgr )
            {
                rate_sleep( next, interval );
                continue;
            }

            DemoMgr::pptr<uint8_t> ptr = mgr->allocate_typed<uint8_t>( size_dist( rng ) );
            if ( !ptr.is_null() )
            {
                fifo.push_back( ptr );
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            if ( fifo.size() > static_cast<std::size_t>( p.max_live_blocks ) )
            {
                mgr->deallocate_typed( fifo.front() );
                fifo.pop_front();
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            rate_sleep( next, interval );
        }

        auto* mgr = g_pmm.load();
        while ( !fifo.empty() )
        {
            if ( mgr )
                mgr->deallocate_typed( fifo.front() );
            fifo.pop_front();
        }

        coord.unregister_participant();
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
        coord.register_participant();

        std::mt19937 rng( std::random_device{}() );

        const std::size_t                          min_sz = std::max( p.min_block_size, std::size_t( 8 ) );
        const std::size_t                          max_sz = std::max( p.max_block_size, min_sz );
        std::uniform_int_distribution<std::size_t> size_dist( min_sz, max_sz );

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::deque<DemoMgr::pptr<uint8_t>> fifo;

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            auto* mgr_before = g_pmm.load();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointers if PMM manager was replaced while paused.
            if ( g_pmm.load() != mgr_before )
                fifo.clear();

            auto* mgr = g_pmm.load();
            if ( !mgr )
            {
                rate_sleep( next, interval );
                continue;
            }

            DemoMgr::pptr<uint8_t> ptr = mgr->allocate_typed<uint8_t>( size_dist( rng ) );
            if ( !ptr.is_null() )
            {
                fifo.push_back( ptr );
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            if ( fifo.size() > static_cast<std::size_t>( p.max_live_blocks ) )
            {
                mgr->deallocate_typed( fifo.front() );
                fifo.pop_front();
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            rate_sleep( next, interval );
        }

        auto* mgr = g_pmm.load();
        while ( !fifo.empty() )
        {
            if ( mgr )
                mgr->deallocate_typed( fifo.front() );
            fifo.pop_front();
        }

        coord.unregister_participant();
    }
};

// ─── Scenario 6: Mixed Sizes ─────────────────────────────────────────────────

/**
 * Simulates two work profiles (A: mostly small, B: medium).
 * reallocate_typed() is replaced with deallocate + allocate of new size
 * (data not preserved, acceptable for a stress test).
 * Default params: min=32, max=32768, alloc_freq=1000, dealloc_freq=950.
 */
class MixedSizes final : public Scenario
{
  public:
    const char* name() const override { return "Mixed Sizes"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
    {
        coord.register_participant();

        std::mt19937                          rng( std::random_device{}() );
        std::uniform_real_distribution<float> chance( 0.0f, 1.0f );

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        std::vector<DemoMgr::pptr<uint8_t>> live;
        live.reserve( static_cast<std::size_t>( p.max_live_blocks ) );

        // Profile A: 80% small [32..256], 20% large [1024..32768]
        // Profile B: 50% medium [256..4096]
        bool profile_a       = true;
        int  profile_counter = 0;

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            auto* mgr_before = g_pmm.load();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointers if PMM manager was replaced while paused.
            if ( g_pmm.load() != mgr_before )
                live.clear();

            auto* mgr = g_pmm.load();
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

            // 5% chance of resize on a live block: deallocate old + allocate new size
            if ( chance( rng ) < 0.05f && !live.empty() )
            {
                std::uniform_int_distribution<std::size_t> idx( 0, live.size() - 1 );
                std::size_t                                i = idx( rng );
                // Manual "realloc": free old, alloc new (data not preserved)
                mgr->deallocate_typed( live[i] );
                DemoMgr::pptr<uint8_t> newptr = mgr->allocate_typed<uint8_t>( sz );
                if ( !newptr.is_null() )
                {
                    live[i] = newptr;
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
                else
                {
                    // Allocation failed; remove the slot
                    live[i] = live.back();
                    live.pop_back();
                }
            }
            else if ( static_cast<int>( live.size() ) < p.max_live_blocks )
            {
                DemoMgr::pptr<uint8_t> ptr = mgr->allocate_typed<uint8_t>( sz );
                if ( !ptr.is_null() )
                {
                    live.push_back( ptr );
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
            }
            else if ( !live.empty() )
            {
                std::uniform_int_distribution<std::size_t> idx( 0, live.size() - 1 );
                std::size_t                                i = idx( rng );
                mgr->deallocate_typed( live[i] );
                live[i] = live.back();
                live.pop_back();
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            rate_sleep( next, interval );
        }

        auto* mgr = g_pmm.load();
        if ( mgr )
        {
            for ( auto& ptr : live )
                mgr->deallocate_typed( ptr );
        }

        coord.unregister_participant();
    }
};

// ─── Scenario 7: Persistence Cycle ───────────────────────────────────────────

/**
 * Periodically saves the PMM image to disk and optionally reloads it to verify
 * persistence.
 *
 * Note: The global manager is owned by DemoApp and cannot be destroyed/recreated
 * from a scenario thread. This simplified version:
 *   1. Allocates a few blocks and fills them.
 *   2. Saves the image to "pmm_demo.bin" via pmm::save_manager().
 *   3. Frees the allocated blocks.
 *   4. Waits for the cycle period.
 *
 * The pause_others() / resume_others() protocol is retained for compatibility
 * with the coordinator (other scenarios pause during the save).
 *
 * Default params: min=128, max=1024, cycle_period via alloc_freq (1/alloc_freq s).
 */
class PersistenceCycle final : public Scenario
{
  public:
    const char* name() const override { return "Persistence Cycle"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
    {
        std::mt19937                               rng( std::random_device{}() );
        std::uniform_int_distribution<std::size_t> size_dist( p.min_block_size, p.max_block_size );

        // cycle_period: use alloc_freq as 1/period (default ~5 s → 0.2)
        const double cycle_period = ( p.alloc_freq > 0.0f ) ? ( 1.0 / static_cast<double>( p.alloc_freq ) ) : 5.0;
        const auto   cycle_dur    = std::chrono::duration<double>( cycle_period );

        std::vector<DemoMgr::pptr<uint8_t>> live;
        live.reserve( 16 );

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            auto* mgr = g_pmm.load();
            if ( !mgr )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
                continue;
            }

            // Allocate a few blocks and write data
            for ( int i = 0; i < 4 && !stop.load( std::memory_order_relaxed ); ++i )
            {
                DemoMgr::pptr<uint8_t> ptr = mgr->allocate_typed<uint8_t>( size_dist( rng ) );
                if ( !ptr.is_null() )
                {
                    uint8_t* raw = ptr.resolve( *mgr );
                    if ( raw )
                        std::memset( raw, static_cast<int>( i + 1 ), p.min_block_size );
                    live.push_back( ptr );
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
            }

            if ( stop.load( std::memory_order_relaxed ) )
                break;

            // Pause other scenarios while saving
            coord.pause_others( stop );

            if ( !stop.load( std::memory_order_relaxed ) )
            {
                mgr = g_pmm.load();
                if ( mgr )
                    pmm::save_manager( *mgr, "pmm_demo.bin" );
                ops.fetch_add( 1, std::memory_order_relaxed );
            }

            // Free live blocks after save
            mgr = g_pmm.load();
            if ( mgr )
            {
                for ( auto& ptr : live )
                    mgr->deallocate_typed( ptr );
            }
            live.clear();

            // Resume other scenarios
            coord.resume_others();

            // Wait for cycle period
            auto deadline = std::chrono::steady_clock::now() + cycle_dur;
            while ( !stop.load( std::memory_order_relaxed ) && std::chrono::steady_clock::now() < deadline )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            }
        }

        // Cleanup
        auto* mgr = g_pmm.load();
        if ( mgr )
        {
            for ( auto& ptr : live )
                mgr->deallocate_typed( ptr );
        }
    }
};

// ─── Scenario 8: Reallocate Typed ────────────────────────────────────────────

/**
 * Repeatedly allocates a block, fills it with a known byte pattern, then grows
 * it via manual deallocate+allocate (reallocate_typed() is removed in new API).
 * Default params: min=64, max=8192, alloc_freq=200, dealloc_freq=0.
 */
class ReallocateTyped final : public Scenario
{
  public:
    const char* name() const override { return "Reallocate Typed"; }

    void run( std::atomic<bool>& stop, std::atomic<uint64_t>& ops, const ScenarioParams& p,
              ScenarioCoordinator& coord ) override
    {
        coord.register_participant();

        std::mt19937                               rng( std::random_device{}() );
        std::uniform_int_distribution<std::size_t> size_dist( p.min_block_size, p.max_block_size );

        const auto interval = std::chrono::duration<double>( 1.0 / std::max( p.alloc_freq, 1.0f ) );
        auto       next     = std::chrono::steady_clock::now();

        // One live pptr that is repeatedly grown.
        DemoMgr::pptr<uint8_t> live;
        std::size_t             live_count = 0;
        uint8_t                 fill_byte  = 0xA5;

        while ( !stop.load( std::memory_order_relaxed ) )
        {
            auto* mgr_before = g_pmm.load();
            coord.yield_if_paused( stop );
            if ( stop.load( std::memory_order_relaxed ) )
                break;
            // Discard stale pointer if PMM manager was replaced while paused.
            if ( g_pmm.load() != mgr_before )
            {
                live       = DemoMgr::pptr<uint8_t>();
                live_count = 0;
            }

            auto* mgr = g_pmm.load();
            if ( !mgr )
            {
                rate_sleep( next, interval );
                continue;
            }

            if ( live.is_null() )
            {
                // Allocate a fresh block and initialise it.
                std::size_t init_count = p.min_block_size;
                live                   = mgr->allocate_typed<uint8_t>( init_count );
                if ( !live.is_null() )
                {
                    uint8_t* raw = live.resolve( *mgr );
                    if ( raw )
                        std::memset( raw, static_cast<int>( fill_byte ), init_count );
                    live_count = init_count;
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
            }
            else
            {
                // Grow the live block: manual deallocate + allocate of larger size.
                std::size_t            new_count = live_count + size_dist( rng );
                DemoMgr::pptr<uint8_t> new_live  = mgr->allocate_typed<uint8_t>( new_count );
                if ( !new_live.is_null() )
                {
                    // Copy old data to new block
                    uint8_t* old_raw = live.resolve( *mgr );
                    uint8_t* new_raw = new_live.resolve( *mgr );
                    if ( old_raw && new_raw )
                    {
                        std::memcpy( new_raw, old_raw, live_count );
                        if ( new_count > live_count )
                            std::memset( new_raw + live_count, static_cast<int>( fill_byte ),
                                         new_count - live_count );
                    }
                    mgr->deallocate_typed( live );
                    live       = new_live;
                    live_count = new_count;
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }

                // Periodically free and restart with a different fill byte.
                if ( live_count >= p.max_block_size * 4 )
                {
                    mgr->deallocate_typed( live );
                    live       = DemoMgr::pptr<uint8_t>();
                    live_count = 0;
                    ++fill_byte;
                    ops.fetch_add( 1, std::memory_order_relaxed );
                }
            }

            rate_sleep( next, interval );
        }

        auto* mgr = g_pmm.load();
        if ( !live.is_null() && mgr )
            mgr->deallocate_typed( live );

        coord.unregister_participant();
    }
};

// ─── Factory ──────────────────────────────────────────────────────────────────

/// Create all 8 scenario instances.
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
    v.push_back( std::make_unique<ReallocateTyped>() );
    return v;
}

} // namespace demo
