/**
 * @file avl_tree_view.h
 * @brief AvlTreeView: renders the PMM AVL free-block tree as an ImGui panel.
 *
 * Issue #65: Add AVL tree display to the visual demo.
 * Issue #116: Uses DemoMgr::for_each_free_block() to iterate over free blocks
 * in-order (by size) and display their offset, size, AVL height, and depth.
 */

#pragma once

#include "demo_globals.h"

#include "pmm/types.h"

#include <cstddef>
#include <vector>

namespace demo
{

/**
 * @brief ImGui panel showing the PMM AVL free-block tree.
 *
 * Iterates over free blocks via DemoMgr::for_each_free_block() (in-order by
 * size) and renders each node with offset, free bytes, AVL height, and depth.
 */
class AvlTreeView
{
  public:
    /**
     * @brief Rebuild the snapshot from live PMM state.
     *
     * Reads statistics and free-block list via DemoMgr:: static methods.
     * Call only when g_pmm is true (manager active).
     */
    void update_snapshot();

    /// Render the AVL Free Tree ImGui panel (call without holding any lock).
    void render();

  private:
    std::size_t free_block_count_ = 0;
    std::size_t total_size_       = 0;
    std::size_t used_size_        = 0;
    std::size_t free_size_        = 0;

    std::vector<pmm::FreeBlockView> free_blocks_; ///< In-order snapshot of free blocks.
};

} // namespace demo
