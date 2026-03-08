/**
 * @file avl_tree_view.cpp
 * @brief Implementation of AvlTreeView: AVL free-block statistics panel.
 *
 * Issue #65: Renders free-block statistics from the PMM manager.
 *
 * Note: block-level iteration (for_each_free_block_avl) is not available in
 * the new AbstractPersistMemoryManager API. This panel shows aggregate
 * statistics only.
 */

#include "avl_tree_view.h"

#include "imgui.h"

namespace demo
{

// ─── Snapshot builder ─────────────────────────────────────────────────────────

void AvlTreeView::update_snapshot()
{
    free_block_count_ = DemoMgr::free_block_count();
    total_size_       = DemoMgr::total_size();
    used_size_        = DemoMgr::used_size();
    free_size_        = DemoMgr::free_size();
}

// ─── Main render ──────────────────────────────────────────────────────────────

void AvlTreeView::render()
{
    ImGui::Begin( "AVL Free Tree" );

    ImGui::TextDisabled( "(block-level iteration not available in new API)" );
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text( "Free blocks:  %zu", free_block_count_ );
    ImGui::Text( "Total size:   %zu bytes", total_size_ );
    ImGui::Text( "Used size:    %zu bytes", used_size_ );
    ImGui::Text( "Free size:    %zu bytes", free_size_ );

    ImGui::End();
}

} // namespace demo
