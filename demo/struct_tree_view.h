/**
 * @file struct_tree_view.h
 * @brief StructTreeView: renders the PMM statistics as an ImGui tree.
 *
 * Shows available statistics (block_count, free_count, alloc_count,
 * total_size, used_size, free_size) from the new AbstractPersistMemoryManager API.
 *
 * Note: block-level iteration (for_each_block) and ManagerHeader field access
 * are not available in the new API. Per-block details are not shown.
 */

#pragma once

#include "demo_globals.h"

#include <cstddef>

namespace demo
{

/**
 * @brief Aggregate PMM statistics snapshot.
 */
struct TreeSnapshot
{
    std::size_t total_size   = 0;
    std::size_t used_size    = 0;
    std::size_t free_size    = 0;
    std::size_t block_count  = 0;
    std::size_t free_count   = 0;
    std::size_t alloc_count  = 0;
};

/**
 * @brief ImGui panel showing a tree view of PMM statistics.
 */
class StructTreeView
{
  public:
    /**
     * @brief Rebuild the structural snapshot from live PMM state.
     *
     * @param mgr Live PMM instance (not null).
     */
    void update_snapshot( DemoMgr* mgr );

    /**
     * @brief Render the Struct Tree ImGui panel.
     *
     * @param[out] highlighted_block Unused in simplified view; retained for API compatibility.
     */
    void render( std::size_t& highlighted_block );

  private:
    TreeSnapshot snapshot_;
};

} // namespace demo
