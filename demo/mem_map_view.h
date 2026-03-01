/**
 * @file mem_map_view.h
 * @brief MemMapView: visualises the PMM managed memory as a colour-coded pixel map.
 *
 * Each byte of the managed region is rendered as a single coloured pixel.
 * Colour encodes the byte's semantic role (ManagerHeader, BlockHeader, user data, …).
 * A block can be highlighted by setting highlighted_block before calling render().
 */

#pragma once

#include "persist_memory_manager.h"

#include <cstddef>
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
 * @brief ImGui panel that renders a pixel-level memory map of the PMM region.
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

  private:
    std::vector<ByteInfo> snapshot_;
    std::size_t           total_bytes_ = 0;

    int   raster_width_ = 256;
    float pixel_scale_  = 1.0f;
    bool  auto_scale_   = false;
};

} // namespace demo
