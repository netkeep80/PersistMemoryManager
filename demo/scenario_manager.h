/**
 * @file scenario_manager.h
 * @brief ScenarioManager: owns and controls all load-test scenario threads.
 *
 * Each scenario runs in its own std::thread. ScenarioManager provides
 * start/stop controls and renders the Scenarios panel in the ImGui UI.
 */

#pragma once

#include "scenarios.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace demo
{

/**
 * @brief Runtime state for a single scenario instance.
 */
struct ScenarioState
{
    std::string           name;
    std::thread           thread;
    std::atomic<bool>     running{ false };
    std::atomic<bool>     stop_flag{ false };
    std::atomic<uint64_t> total_ops{ 0 };
    ScenarioParams        params;
    bool                  show_params = false;

    // Non-copyable, non-movable because of atomics and thread.
    ScenarioState()                                  = default;
    ScenarioState( const ScenarioState& )            = delete;
    ScenarioState& operator=( const ScenarioState& ) = delete;
    ScenarioState( ScenarioState&& )                 = delete;
    ScenarioState& operator=( ScenarioState&& )      = delete;
};

/**
 * @brief Manages all load-test scenarios: lifecycle and UI rendering.
 */
class ScenarioManager
{
  public:
    ScenarioManager();
    ~ScenarioManager();

    /// Start scenario at index i (no-op if already running).
    void start( std::size_t index );

    /// Request stop for scenario at index i and join its thread.
    void stop( std::size_t index );

    /// Start all scenarios.
    void start_all();

    /// Request stop on all scenarios (does not join).
    void stop_all();

    /// Join all scenario threads (blocks until done).
    void join_all();

    /// Render the Scenarios ImGui panel.
    void render();

    /// Total ops/s across all running scenarios (approximate).
    float total_ops_per_sec() const;

    /// Number of scenarios managed.
    std::size_t count() const;

  private:
    void render_scenario_row( std::size_t i );

    std::vector<std::unique_ptr<Scenario>>      scenarios_;
    std::vector<std::unique_ptr<ScenarioState>> states_;
};

} // namespace demo
