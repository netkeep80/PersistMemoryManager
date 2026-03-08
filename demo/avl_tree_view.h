/**
 * @file avl_tree_view.h
 * @brief AvlTreeView: renders the PMM AVL free-block tree as an ImGui panel.
 *
 * Issue #65: Add AVL tree display to the visual demo.
 * Issue #116: Uses DemoMgr::for_each_free_block() to iterate over free blocks
 * in-order (by size) and display their offset, size, AVL height, and depth.
 * Issue #118: Renders the AVL tree as a visual tree (not a flat table).
 */

#pragma once

#include "demo_globals.h"

#include "pmm/types.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace demo
{

/**
 * @brief ImGui panel showing the PMM AVL free-block tree.
 *
 * Iterates over free blocks via DemoMgr::for_each_free_block() (in-order by
 * size) and renders the tree structure using ImGui TreeNode/TreePop, starting
 * from the root (the node with parent_offset == -1) and recursively rendering
 * left and right children.
 *
 * Issue #118: Visual tree rendering replaces the flat table view.
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

    /// Map from byte offset to index in free_blocks_ (for O(1) child lookup).
    std::unordered_map<std::ptrdiff_t, std::size_t> offset_to_idx_;

    /// Recursively render one AVL tree node and its children.
    void render_node( std::ptrdiff_t offset, int depth );
};

} // namespace demo
