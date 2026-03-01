/**
 * @file metrics_view.h
 * @brief MetricsView: real-time PMM statistics panel with scrolling plots.
 *
 * Displays a table of nine key metrics plus scrolling history graphs for
 * used memory, fragmentation, and operations per second.
 *
 * Phase 12 addition: shows the result of the latest background integrity check
 * (validate()) and a manual "Validate now" button.
 */

#pragma once

#include <chrono>
#include <cstddef>

namespace demo
{

/**
 * @brief Snapshot of PMM statistics collected once per frame under shared_lock.
 */
struct MetricsSnapshot
{
    std::size_t total_size       = 0;
    std::size_t used_size        = 0;
    std::size_t free_size        = 0;
    std::size_t total_blocks     = 0;
    std::size_t allocated_blocks = 0;
    std::size_t free_blocks      = 0;
    std::size_t fragmentation    = 0;
    std::size_t largest_free     = 0;
    std::size_t smallest_free    = 0;
};

/**
 * @brief Result of the most recent validate() call.
 *
 * Phase 12: updated by DemoApp and displayed in MetricsView.
 */
struct ValidationResult
{
    enum class State
    {
        Unknown, ///< validate() has not been called yet
        Ok,      ///< validate() returned true
        Failed   ///< validate() returned false
    };

    State                                 state = State::Unknown;
    std::chrono::steady_clock::time_point timestamp{}; ///< when validate() was last called
};

/**
 * @brief ImGui panel showing live PMM metrics and scrolling history plots.
 */
class MetricsView
{
  public:
    /**
     * @brief Update the metrics snapshot and append to history buffers.
     *
     * @param snap      Current metrics (collected under shared_lock).
     * @param ops_per_sec Smoothed operations per second.
     */
    void update( const MetricsSnapshot& snap, float ops_per_sec );

    /**
     * @brief Update the latest validation result (Phase 12).
     *
     * Called by DemoApp after each periodic or manual validate() check.
     * @param result  Result struct holding state and timestamp.
     */
    void update_validation( const ValidationResult& result );

    /// Render the Metrics ImGui panel.
    void render();

    /// Returns true if the user pressed the "Validate now" button this frame.
    bool validate_requested() const noexcept { return validate_requested_; }

  private:
    static constexpr int kHistorySize = 256;

    float used_history_[kHistorySize] = {};
    float frag_history_[kHistorySize] = {};
    float ops_history_[kHistorySize]  = {};
    int   history_offset_             = 0;

    MetricsSnapshot current_{};
    float           current_ops_per_sec_ = 0.0f;

    // Phase 12: validation state
    ValidationResult last_validation_{};
    bool             validate_requested_ = false; ///< set to true when button pressed
};

} // namespace demo
