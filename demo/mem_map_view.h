/**
 * @file mem_map_view.h
 * @brief MemMapView: visualises the PMM managed memory as a colour-coded pixel map.
 *
 * Each byte of the managed region is rendered as a single coloured pixel in
 * "detail" mode (up to 512 KB).  For PMM regions larger than 512 KB a second
 * "overview" mode is available where every pixel represents N bytes (tile
 * aggregation).  The dominant byte type in each tile determines the tile colour,
 * allowing the full managed region to be displayed at once without performance
 * degradation (Phase 11).
 *
 * A block can be highlighted by setting highlighted_block before calling render().
 */

#pragma once

#include "persist_memory_manager.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace demo
{

/**
 * @brief Semantic classification of a single byte in the PMM region.
 */
struct ByteInfo
{
    enum class Type
    {
        ManagerHeader,   ///< Part of the ManagerHeader struct
        BlockHeaderUsed, ///< Part of a BlockHeader for a used block
        UserDataUsed,    ///< User data area of a used block
        BlockHeaderFree, ///< Part of a BlockHeader for a free block
        UserDataFree,    ///< User data area of a free block
        OutOfBlocks      ///< Past all blocks / unaccounted region
    };

    Type        type        = Type::OutOfBlocks;
    std::size_t block_index = 0; ///< Which block this byte belongs to (0-based)
    std::size_t offset      = 0; ///< Byte offset from start of PMM region
};

/**
 * @brief Aggregated tile used by the overview rendering mode.
 *
 * A tile covers `bytes_per_tile` consecutive bytes.  The dominant ByteInfo::Type
 * (most frequent within the tile) determines the colour shown.
 */
struct TileInfo
{
    ByteInfo::Type dominant_type  = ByteInfo::Type::OutOfBlocks; ///< Most frequent type
    std::size_t    offset         = 0;                           ///< Byte offset of tile start
    std::size_t    bytes_per_tile = 1;                           ///< How many bytes per tile
    /// Type counts used to derive dominant_type (indexed by ByteInfo::Type cast to int).
    std::uint32_t type_counts[6] = {};
};

/**
 * @brief ImGui panel that renders a pixel-level memory map of the PMM region.
 *
 * Supports two rendering modes (Phase 11):
 *  - Detail mode  : 1 pixel = 1 byte, limited to the first 512 KB.
 *  - Overview mode: 1 pixel = N bytes, shows the full managed region.
 *
 * When PMM total_size > kDetailLimit the overview mode is available via the
 * "Show full memory (overview)" checkbox.
 */
class MemMapView
{
  public:
    /// Index of the block to highlight (set by StructTreeView); SIZE_MAX = none.
    std::size_t highlighted_block = static_cast<std::size_t>( -1 );

    /**
     * @brief Rebuild the byte snapshot from live PMM state.
     *
     * Must be called while holding a shared_lock on the PMM mutex.
     * @param mgr Live PMM instance (not null).
     */
    void update_snapshot( pmm::PersistMemoryManager* mgr );

    /// Render the Memory Map ImGui panel (call without holding any lock).
    void render();

    // ── Phase 11: tile overview accessors (used by tests) ────────────────────
    /// Returns the tile snapshot built during the last update_snapshot() call.
    const std::vector<TileInfo>& tile_snapshot() const noexcept { return tile_snapshot_; }
    /// Returns the current bytes-per-tile value.
    std::size_t bytes_per_tile() const noexcept { return bytes_per_tile_; }
    /// Total managed bytes as of the last update_snapshot() call.
    std::size_t total_bytes() const noexcept { return total_bytes_; }

  private:
    // ── Detail-mode data ─────────────────────────────────────────────────────
    std::vector<ByteInfo> snapshot_;
    std::size_t           total_bytes_ = 0;

    // ── Overview-mode data (Phase 11) ────────────────────────────────────────
    std::vector<TileInfo> tile_snapshot_;
    std::size_t           bytes_per_tile_ = 1; ///< computed in update_snapshot

    // ── Controls ─────────────────────────────────────────────────────────────
    int   raster_width_  = 256;
    float pixel_scale_   = 1.0f;
    bool  auto_scale_    = false;
    bool  overview_mode_ = false; ///< Phase 11: show full memory as tiles

    // ── Private helpers ───────────────────────────────────────────────────────
    void render_detail();
    void render_overview();
};

} // namespace demo
