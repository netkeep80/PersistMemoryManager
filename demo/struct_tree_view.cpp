/**
 * @file struct_tree_view.cpp
 * @brief Implementation of StructTreeView: PMM statistics inspector.
 *
 * Uses the new AbstractPersistMemoryManager API (block_count, free_block_count,
 * alloc_block_count, total_size, used_size, free_size).
 *
 * Block-level iteration (for_each_block) and ManagerHeader field access are not
 * available in the new API.
 */

#include "struct_tree_view.h"

#include "imgui.h"

#include <cstdio>

namespace demo
{

// ─── Snapshot builder ─────────────────────────────────────────────────────────

void StructTreeView::update_snapshot()
{
    snapshot_.total_size  = DemoMgr::total_size();
    snapshot_.used_size   = DemoMgr::used_size();
    snapshot_.free_size   = DemoMgr::free_size();
    snapshot_.block_count = DemoMgr::block_count();
    snapshot_.free_count  = DemoMgr::free_block_count();
    snapshot_.alloc_count = DemoMgr::alloc_block_count();
}

// ─── Renderer ─────────────────────────────────────────────────────────────────

void StructTreeView::render( std::size_t& highlighted_block )
{
    (void)highlighted_block; // block selection not available without per-block iteration

    ImGui::Begin( "Struct Tree" );

    if ( ImGui::TreeNode( "PersistMemoryManager" ) )
    {
        if ( ImGui::TreeNode( "Statistics" ) )
        {
            ImGui::Text( "total_size:   %zu", snapshot_.total_size );
            ImGui::Text( "used_size:    %zu", snapshot_.used_size );
            ImGui::Text( "free_size:    %zu", snapshot_.free_size );
            ImGui::Text( "block_count:  %zu", snapshot_.block_count );
            ImGui::Text( "free_count:   %zu", snapshot_.free_count );
            ImGui::Text( "alloc_count:  %zu", snapshot_.alloc_count );
            ImGui::TreePop();
        }

        ImGui::TextDisabled( "(block-level iteration not available in new API)" );

        ImGui::TreePop();
    }

    ImGui::End();
}

} // namespace demo
