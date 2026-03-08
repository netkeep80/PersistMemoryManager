/**
 * @file struct_tree_view.h
 * @brief StructTreeView: renders the PMM block layout as an ImGui tree.
 *
 * Uses DemoMgr::for_each_block() (Issue #116) to iterate over all blocks and
 * show their offset, size, and type (used/free) in an expandable tree view.
 * Clicking on a block highlights it in the Memory Map.
 */

#pragma once

#include "demo_globals.h"

#include "pmm/types.h"

#include <cstddef>
#include <vector>

namespace demo
{

/**
 * @brief Aggregate PMM statistics snapshot.
 */
struct TreeSnapshot
{
    std::size_t total_size  = 0;
    std::size_t used_size   = 0;
    std::size_t free_size   = 0;
    std::size_t block_count = 0;
    std::size_t free_count  = 0;
    std::size_t alloc_count = 0;
};

/**
 * @brief ImGui panel showing a tree view of PMM block layout.
 *
 * Iterates over all blocks via DemoMgr::for_each_block() and renders each
 * block as a tree node with offset, total size, and state (used/free).
 * Clicking a block sets highlighted_block for cross-panel highlighting.
 */
class StructTreeView
{
  public:
    /**
     * @brief Rebuild the structural snapshot from live PMM state.
     *
     * Reads statistics and block list via DemoMgr:: static methods.
     * Call only when g_pmm is true (manager active).
     */
    void update_snapshot();

    /**
     * @brief Render the Struct Tree ImGui panel.
     *
     * @param[out] highlighted_block  Granule index of the block selected by the user,
     *                                or (std::size_t)-1 when nothing is selected.
     */
    void render( std::size_t& highlighted_block );

  private:
    TreeSnapshot                snapshot_;
    std::vector<pmm::BlockView> blocks_; ///< Per-block snapshot from last update_snapshot().
};

} // namespace demo
