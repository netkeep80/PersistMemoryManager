/**
 * @file struct_tree_view.cpp
 * @brief Implementation of StructTreeView: PMM block-layout inspector.
 *
 * Uses DemoMgr::for_each_block() (Issue #116) to iterate over all blocks.
 * Each block is shown as a tree node with offset, size, and state.
 * Clicking a block selects it for highlighting in the Memory Map.
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

    blocks_.clear();
    DemoMgr::for_each_block(
        [&]( const pmm::BlockView& v )
        {
            blocks_.push_back( v );
        } );
}

// ─── Renderer ─────────────────────────────────────────────────────────────────

void StructTreeView::render( std::size_t& highlighted_block )
{
    ImGui::Begin( "Struct Tree" );

    if ( ImGui::TreeNodeEx( "PersistMemoryManager", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        // ── Aggregate statistics ──────────────────────────────────────────────
        if ( ImGui::TreeNodeEx( "Statistics", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Text( "total_size:   %zu", snapshot_.total_size );
            ImGui::Text( "used_size:    %zu", snapshot_.used_size );
            ImGui::Text( "free_size:    %zu", snapshot_.free_size );
            ImGui::Text( "block_count:  %zu", snapshot_.block_count );
            ImGui::Text( "free_count:   %zu", snapshot_.free_count );
            ImGui::Text( "alloc_count:  %zu", snapshot_.alloc_count );
            ImGui::TreePop();
        }

        // ── Per-block list ────────────────────────────────────────────────────
        char header[64];
        std::snprintf( header, sizeof( header ), "Blocks (%zu)", blocks_.size() );

        if ( ImGui::TreeNode( header ) )
        {
            for ( const pmm::BlockView& v : blocks_ )
            {
                bool is_highlighted = ( highlighted_block == v.index );

                // Build a label: "[used|free] offset=N  size=M"
                char label[128];
                std::snprintf( label, sizeof( label ), "[%s] offset=0x%zX  total=%zu  user=%zu##blk%zu",
                               v.used ? "used" : "free",
                               static_cast<std::size_t>( v.offset ),
                               v.total_size,
                               v.user_size,
                               v.index );

                // Colour used blocks blue, free blocks grey.
                if ( v.used )
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.4f, 0.6f, 1.0f, 1.0f ) );
                else
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.7f, 0.7f, 0.7f, 1.0f ) );

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if ( is_highlighted )
                    flags |= ImGuiTreeNodeFlags_Selected;

                ImGui::TreeNodeEx( label, flags );

                ImGui::PopStyleColor();

                if ( ImGui::IsItemClicked() )
                {
                    // Toggle highlight: click again to deselect.
                    highlighted_block = is_highlighted ? static_cast<std::size_t>( -1 ) : v.index;
                }

                // Tooltip with full details.
                if ( ImGui::IsItemHovered() )
                {
                    ImGui::BeginTooltip();
                    ImGui::Text( "State:       %s", v.used ? "allocated" : "free" );
                    ImGui::Text( "Granule idx: %zu", v.index );
                    ImGui::Text( "Byte offset: 0x%zX (%zu)", static_cast<std::size_t>( v.offset ),
                                 static_cast<std::size_t>( v.offset ) );
                    ImGui::Text( "Total size:  %zu bytes", v.total_size );
                    ImGui::Text( "Header size: %zu bytes", v.header_size );
                    ImGui::Text( "User size:   %zu bytes", v.user_size );
                    ImGui::EndTooltip();
                }
            }
            ImGui::TreePop();
        }

        ImGui::TreePop();
    }

    ImGui::End();
}

} // namespace demo
