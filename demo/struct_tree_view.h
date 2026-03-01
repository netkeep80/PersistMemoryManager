/**
 * @file struct_tree_view.h
 * @brief StructTreeView: renders the internal PMM data structures as an ImGui tree.
 *
 * Shows ManagerHeader fields and a list of all BlockHeaders.
 * Clicking on a block sets the highlighted_block output, which is fed back
 * to MemMapView to highlight the selected block's bytes.
 */

#pragma once

#include "persist_memory_manager.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace demo
{

/**
 * @brief Snapshot of a single PMM block (collected under shared_lock).
 */
struct BlockSnapshot
{
    std::size_t index;
    std::size_t offset;
    std::size_t total_size;
    std::size_t user_size;
    std::size_t alignment;
    bool        used;
};

/**
 * @brief Full PMM structural snapshot (collected under shared_lock).
 */
struct TreeSnapshot
{
    // ManagerHeader fields
    std::uint64_t  magic              = 0;
    std::size_t    total_size         = 0;
    std::size_t    used_size          = 0;
    std::size_t    block_count        = 0;
    std::size_t    free_count         = 0;
    std::size_t    alloc_count        = 0;
    std::ptrdiff_t first_block_offset = -1;
    std::ptrdiff_t first_free_offset  = -1;

    std::vector<BlockSnapshot> blocks;
};

/**
 * @brief ImGui panel showing a tree view of the PMM internal structures.
 */
class StructTreeView
{
  public:
    /**
     * @brief Rebuild the structural snapshot from live PMM state.
     *
     * Must be called while holding a shared_lock on the PMM mutex.
     * @param mgr Live PMM instance (not null).
     */
    void update_snapshot( pmm::PersistMemoryManager* mgr );

    /**
     * @brief Render the Struct Tree ImGui panel.
     *
     * @param[out] highlighted_block Set to the selected block index when the
     *             user clicks on a block row; unchanged if no click occurs.
     */
    void render( std::size_t& highlighted_block );

  private:
    TreeSnapshot snapshot_;

    static constexpr std::size_t kMaxVisibleBlocks = 500;
};

} // namespace demo
