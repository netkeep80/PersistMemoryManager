/**
 * @file mem_map_view.h
 * @brief MemMapView: visualises the PMM managed memory as a colour-coded pixel map.
 *
 * DemoMgr is a fully static class — update_snapshot() reads block layout via
 * DemoMgr::for_each_block() and statistics via static DemoMgr:: methods.
 *
 * Each pixel in the map represents `bytes_per_pixel` bytes of managed memory.
 * A logarithmic slider (power-of-2 steps) controls the scale.
 *
 * Colour legend:
 *   - Purple      — ManagerHeader metadata region
 *   - Dark blue   — BlockHeader (used block)
 *   - Bright blue — User data (used block)
 *   - Dark grey   — BlockHeader (free block)
 *   - White       — User data (free block)
 *   - Black       — Beyond last block (unused granules)
 */

#pragma once

#include "demo_globals.h"

#include <cstddef>
#include <vector>

namespace demo
{

/// @brief Colour tag for one pixel in the memory map.
enum class PixelKind : std::uint8_t
{
    Unused        = 0, ///< Beyond managed blocks
    ManagerHeader = 1, ///< ManagerHeader metadata
    UsedHeader    = 2, ///< Block<A> header of an allocated block
    UsedData      = 3, ///< User data of an allocated block
    FreeHeader    = 4, ///< Block<A> header of a free block
    FreeData      = 5, ///< Data region of a free block (available space)
};

/**
 * @brief ImGui panel that renders the PMM region as a colour-coded pixel map.
 *
 * Uses DemoMgr::for_each_block() to iterate over all blocks and build a
 * per-pixel colour array. A bytes-per-pixel slider (logarithmic, power-of-2)
 * controls the zoom level.
 */
class MemMapView
{
  public:
    /// Index of the block to highlight (granule index).
    std::size_t highlighted_block = static_cast<std::size_t>( -1 );

    /**
     * @brief Rebuild the block-level snapshot from live PMM state.
     *
     * Calls DemoMgr::for_each_block() to collect per-block layout.
     * Call only when g_pmm is true (manager active).
     */
    void update_snapshot();

    /// Render the Memory Map ImGui panel (call without holding any lock).
    void render();

    /// Total managed bytes as of the last update_snapshot() call.
    std::size_t total_bytes() const noexcept { return total_bytes_; }

  private:
    std::size_t total_bytes_ = 0;
    std::size_t used_bytes_  = 0;
    std::size_t free_bytes_  = 0;

    /// Per-byte kind array (one entry per byte in the managed region).
    /// Rebuilt by update_snapshot(); downsampled to pixels during render().
    std::vector<PixelKind> byte_kinds_;

    // ── Controls ─────────────────────────────────────────────────────────────
    float       bar_height_      = 24.0f; ///< Height of the simple usage bar
    int         bpp_log2_        = 0;     ///< log2(bytes_per_pixel), slider value
    std::size_t bytes_per_pixel_ = 1;     ///< Current bytes per pixel (2^bpp_log2_)
    float       pixel_size_      = 4.0f;  ///< Issue #118: side length of one pixel square in screen pixels

    // ── Helpers ───────────────────────────────────────────────────────────────
    void rebuild_pixel_map();
    void render_pixel_map( float map_width );
};

} // namespace demo
