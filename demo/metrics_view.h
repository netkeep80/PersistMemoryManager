/**
 * @file metrics_view.h
 * @brief MetricsView: real-time PMM statistics panel with scrolling plots.
 *
 * Displays a table of nine key metrics plus scrolling history graphs for
 * used memory, fragmentation, and operations per second.
 */

#pragma once

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

    /// Render the Metrics ImGui panel.
    void render();

  private:
    static constexpr int kHistorySize = 256;

    float used_history_[kHistorySize] = {};
    float frag_history_[kHistorySize] = {};
    float ops_history_[kHistorySize]  = {};
    int   history_offset_             = 0;

    MetricsSnapshot current_{};
    float           current_ops_per_sec_ = 0.0f;
};

} // namespace demo
