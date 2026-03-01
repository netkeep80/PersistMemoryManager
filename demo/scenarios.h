/**
 * @file scenarios.h
 * @brief Load test scenario definitions for the PersistMemoryManager demo.
 *
 * Declares the base Scenario interface and ScenarioParams used by all
 * scenario implementations. Each scenario runs in its own thread and
 * exercises different allocation patterns to demonstrate PMM behaviour.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

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
     * @param stop_flag   Set to true by ScenarioManager to request shutdown.
     * @param op_counter  Atomically increment for every allocation/deallocation.
     * @param params      Tunable parameters (read-only snapshot taken at start).
     */
    virtual void run( std::atomic<bool>& stop_flag, std::atomic<uint64_t>& op_counter,
                      const ScenarioParams& params ) = 0;
};

} // namespace demo
