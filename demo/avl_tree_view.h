/**
 * @file avl_tree_view.h
 * @brief AvlTreeView: renders the PMM free-block information as an ImGui panel.
 *
 * Issue #65: Add AVL tree display to the visual demo.
 *
 * Note: block-level iteration (for_each_free_block_avl) is not available in
 * the new AbstractPersistMemoryManager API. This panel shows the free block
 * count and total/used/free sizes from the manager's statistics API.
 */

#pragma once

#include "demo_globals.h"

#include <cstddef>

namespace demo
{

/**
 * @brief ImGui panel showing PMM free-block statistics.
 *
 * Block-level AVL tree iteration is not available in the new API.
 * The panel shows free_block_count, total_size, used_size and free_size.
 *
 * Issue #65: visualises free-block information in the demo.
 */
class AvlTreeView
{
  public:
    /**
     * @brief Rebuild the snapshot from live PMM state.
     *
     * Reads statistics via DemoMgr:: static methods.
     * Call only when g_pmm is true (manager active).
     */
    void update_snapshot();

    /// Render the AVL Tree ImGui panel (call without holding any lock).
    void render();

  private:
    std::size_t free_block_count_ = 0;
    std::size_t total_size_       = 0;
    std::size_t used_size_        = 0;
    std::size_t free_size_        = 0;
};

} // namespace demo
