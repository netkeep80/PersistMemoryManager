/**
 * @file avl_tree_view.h
 * @brief AvlTreeView: renders the PMM AVL free-block tree as a hierarchical ImGui panel.
 *
 * Issue #65: Add AVL tree display to the visual demo as a tree-structured list of free
 * blocks, showing each node's offset, size, AVL height, and left/right child links.
 *
 * The panel uses for_each_free_block_avl() to snapshot the tree once per frame, then
 * renders a collapsible ImGui tree that mirrors the AVL structure.
 */

#pragma once

#include "persist_memory_manager.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace demo
{

/**
 * @brief Snapshot of a single node in the AVL free-block tree.
 *
 * Collected once per frame under a shared_lock and used for rendering without
 * holding any lock.
 */
struct AvlNodeSnapshot
{
    std::ptrdiff_t offset;        ///< Byte offset in managed region
    std::size_t    total_size;    ///< Total block size in bytes
    std::size_t    free_size;     ///< Usable free area in bytes
    std::ptrdiff_t left_offset;   ///< Byte offset of left child, or -1
    std::ptrdiff_t right_offset;  ///< Byte offset of right child, or -1
    std::ptrdiff_t parent_offset; ///< Byte offset of parent, or -1
    int            avl_height;    ///< Stored AVL subtree height
    int            avl_depth;     ///< Depth from root (root = 0)
};

/**
 * @brief ImGui panel showing the PMM AVL free-block tree as a collapsible tree widget.
 *
 * Each rendered node shows:
 *  - Byte offset in the managed region
 *  - Total block size and free data size
 *  - AVL height and depth
 *  - Left/right child offsets (or "—" when absent)
 *
 * Issue #65: visualises the free-block AVL tree topology in the demo.
 */
class AvlTreeView
{
  public:
    /**
     * @brief Rebuild the AVL tree snapshot from live PMM state.
     *
     * Calls for_each_free_block_avl() which acquires a shared_lock internally.
     * @param mgr Live PMM instance (not null).
     */
    void update_snapshot( pmm::PersistMemoryManager<>* mgr );

    /// Render the AVL Tree ImGui panel (call without holding any lock).
    void render();

    // ── Accessors for headless testing ───────────────────────────────────────
    /// Returns the snapshot collected during the last update_snapshot() call.
    const std::vector<AvlNodeSnapshot>& snapshot() const noexcept { return snapshot_; }

  private:
    std::vector<AvlNodeSnapshot> snapshot_;         ///< Pre-order snapshot of the AVL tree
    std::ptrdiff_t               root_offset_ = -1; ///< Byte offset of the root node, or -1

    /// Recursively render one AVL node and its subtree using the snapshot.
    void render_node( std::ptrdiff_t node_offset, int depth );
};

} // namespace demo
