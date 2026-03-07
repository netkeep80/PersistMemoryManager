/**
 * @file mem_map_view.h
 * @brief MemMapView: visualises the PMM managed memory as a colour-coded bar.
 *
 * The new AbstractPersistMemoryManager API does not expose for_each_block() or
 * manager_header_size(). This panel renders a simple proportional bar showing
 * used vs. free memory and provides basic statistics.
 *
 * A block can be highlighted by setting highlighted_block before calling render()
 * (retained for API compatibility; block-level colouring is not available).
 */

#pragma once

#include "demo_globals.h"

#include <cstddef>

namespace demo
{

/**
 * @brief ImGui panel that renders a simple memory usage bar for the PMM region.
 *
 * Shows used/free/total sizes from the new API. Block-level pixel colouring
 * (for_each_block) is not available in the AbstractPersistMemoryManager API.
 */
class MemMapView
{
  public:
    /// Index of the block to highlight (retained for API compatibility; unused in simplified view).
    std::size_t highlighted_block = static_cast<std::size_t>( -1 );

    /**
     * @brief Rebuild the snapshot from live PMM state.
     *
     * @param mgr Live PMM instance (not null).
     */
    void update_snapshot( DemoMgr* mgr );

    /// Render the Memory Map ImGui panel (call without holding any lock).
    void render();

    /// Total managed bytes as of the last update_snapshot() call.
    std::size_t total_bytes() const noexcept { return total_bytes_; }

  private:
    std::size_t total_bytes_ = 0;
    std::size_t used_bytes_  = 0;
    std::size_t free_bytes_  = 0;

    // ── Controls ─────────────────────────────────────────────────────────────
    float bar_height_ = 24.0f;
};

} // namespace demo
