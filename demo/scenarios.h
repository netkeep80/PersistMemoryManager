/**
 * @file scenarios.h
 * @brief Load test scenario definitions for the PersistMemoryManager demo.
 *
 * Declares the base Scenario interface, ScenarioParams, and ScenarioCoordinator
 * used by all scenario implementations. Each scenario runs in its own thread and
 * exercises different allocation patterns to demonstrate PMM behaviour.
 *
 * Phase 10 addition: ScenarioCoordinator allows the PersistenceCycle scenario to
 * safely pause all other scenarios while performing destroy()/reload() of the PMM
 * singleton (fixes plan.md Risk #5).
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace demo
{

/**
 * @brief Tunable parameters passed to every scenario at runtime.
 */
struct ScenarioParams
{
    std::size_t min_block_size  = 64;
    std::size_t max_block_size  = 4096;
    float       alloc_freq      = 1000.0f; ///< target allocations per second
    float       dealloc_freq    = 900.0f;  ///< target deallocations per second
    int         max_live_blocks = 100;
};

/**
 * @brief Coordinates safe PMM singleton replacement across concurrent scenarios.
 *
 * When the PersistenceCycle scenario needs to call PersistMemoryManager::destroy()
 * and reload, it must ensure no other scenario thread is using the PMM singleton.
 * ScenarioCoordinator implements a two-phase pause/resume protocol:
 *
 *  1. pause_others() — sets a pause flag; any scenario calling yield_if_paused()
 *     will block until resume_others() is called.
 *  2. resume_others() — clears the pause flag and wakes all waiting threads.
 *
 * Non-PersistenceCycle scenarios call yield_if_paused() at safe points in their
 * loops (after completing an allocation/deallocation, before the next one).
 *
 * Thread-safety: all methods are safe to call from any thread.
 */
class ScenarioCoordinator
{
  public:
    ScenarioCoordinator()  = default;
    ~ScenarioCoordinator() = default;

    // Non-copyable, non-movable.
    ScenarioCoordinator( const ScenarioCoordinator& )            = delete;
    ScenarioCoordinator& operator=( const ScenarioCoordinator& ) = delete;
    ScenarioCoordinator( ScenarioCoordinator&& )                 = delete;
    ScenarioCoordinator& operator=( ScenarioCoordinator&& )      = delete;

    /**
     * @brief Request all other scenarios to pause.
     *
     * Sets the pause flag. Scenarios that call yield_if_paused() will block.
     * The caller (PersistenceCycle) must call resume_others() to unblock them.
     */
    void pause_others();

    /**
     * @brief Resume all paused scenarios.
     *
     * Clears the pause flag and notifies all blocked scenarios to continue.
     */
    void resume_others();

    /**
     * @brief Block the calling thread if a pause has been requested.
     *
     * Called by non-coordinator scenarios at safe points. Returns immediately
     * when no pause is active. If stop_flag is true the function returns
     * without waiting (to allow fast shutdown).
     *
     * @param stop_flag  The scenario's cooperative-cancellation flag.
     */
    void yield_if_paused( const std::atomic<bool>& stop_flag );

    /// Returns true if a pause is currently in effect.
    bool is_paused() const noexcept;

  private:
    std::atomic<bool>       paused_{ false };
    std::mutex              mutex_;
    std::condition_variable cv_;
};

/**
 * @brief Abstract base class for a load scenario.
 *
 * Implementors override name() and run(). The run() method is called on a
 * dedicated thread; it must check stop_flag frequently and return promptly
 * once stop_flag is set.
 */
class Scenario
{
  public:
    virtual ~Scenario() = default;

    /// Human-readable scenario name shown in the UI.
    virtual const char* name() const = 0;

    /**
     * @brief Execute the scenario loop.
     *
     * @param stop_flag    Set to true by ScenarioManager to request shutdown.
     * @param op_counter   Atomically increment for every allocation/deallocation.
     * @param params       Tunable parameters (read-only snapshot taken at start).
     * @param coordinator  Shared coordinator for safe PMM singleton replacement.
     */
    virtual void run( std::atomic<bool>& stop_flag, std::atomic<uint64_t>& op_counter, const ScenarioParams& params,
                      ScenarioCoordinator& coordinator ) = 0;
};

} // namespace demo
