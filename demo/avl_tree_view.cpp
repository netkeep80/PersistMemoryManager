/**
 * @file avl_tree_view.cpp
 * @brief Implementation of AvlTreeView: AVL free-block tree panel.
 *
 * Issue #65: Renders free-block information from the PMM manager.
 * Issue #116: Uses DemoMgr::for_each_free_block() to iterate over all free
 * blocks in-order (ascending size) and display AVL tree details.
 */

#include "avl_tree_view.h"

#include "imgui.h"

#include <cstdio>

namespace demo
{

// ─── Snapshot builder ─────────────────────────────────────────────────────────

void AvlTreeView::update_snapshot()
{
    free_block_count_ = DemoMgr::free_block_count();
    total_size_       = DemoMgr::total_size();
    used_size_        = DemoMgr::used_size();
    free_size_        = DemoMgr::free_size();

    free_blocks_.clear();
    DemoMgr::for_each_free_block( [&]( const pmm::FreeBlockView& v ) { free_blocks_.push_back( v ); } );
}

// ─── Main render ──────────────────────────────────────────────────────────────

void AvlTreeView::render()
{
    ImGui::Begin( "AVL Free Tree" );

    // ── Aggregate statistics ──────────────────────────────────────────────────
    ImGui::Text( "Free blocks:  %zu", free_block_count_ );
    ImGui::Text( "Total size:   %zu bytes", total_size_ );
    ImGui::Text( "Used size:    %zu bytes", used_size_ );
    ImGui::Text( "Free size:    %zu bytes", free_size_ );

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Per-free-block list (in-order: ascending size) ────────────────────────
    char header[64];
    std::snprintf( header, sizeof( header ), "Free blocks in AVL order (%zu)", free_blocks_.size() );

    if ( ImGui::TreeNode( header ) )
    {
        if ( ImGui::BeginTable( "avl_tbl", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                ImVec2( 0.0f, 200.0f ) ) )
        {
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableSetupColumn( "Offset", ImGuiTableColumnFlags_WidthFixed, 90.0f );
            ImGui::TableSetupColumn( "Free (B)", ImGuiTableColumnFlags_WidthFixed, 80.0f );
            ImGui::TableSetupColumn( "Total (B)", ImGuiTableColumnFlags_WidthFixed, 80.0f );
            ImGui::TableSetupColumn( "Height", ImGuiTableColumnFlags_WidthFixed, 55.0f );
            ImGui::TableSetupColumn( "Depth", ImGuiTableColumnFlags_WidthFixed, 50.0f );
            ImGui::TableHeadersRow();

            for ( const pmm::FreeBlockView& v : free_blocks_ )
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::Text( "0x%zX", static_cast<std::size_t>( v.offset ) );
                ImGui::TableSetColumnIndex( 1 );
                ImGui::Text( "%zu", v.free_size );
                ImGui::TableSetColumnIndex( 2 );
                ImGui::Text( "%zu", v.total_size );
                ImGui::TableSetColumnIndex( 3 );
                ImGui::Text( "%d", v.avl_height );
                ImGui::TableSetColumnIndex( 4 );
                ImGui::Text( "%d", v.avl_depth );

                // Tooltip with AVL links.
                if ( ImGui::IsItemHovered() )
                {
                    ImGui::BeginTooltip();
                    ImGui::Text( "Offset:  0x%zX", static_cast<std::size_t>( v.offset ) );
                    ImGui::Text( "Free:    %zu bytes", v.free_size );
                    ImGui::Text( "Total:   %zu bytes", v.total_size );
                    if ( v.left_offset >= 0 )
                        ImGui::Text( "Left:    0x%zX", static_cast<std::size_t>( v.left_offset ) );
                    else
                        ImGui::TextDisabled( "Left:    (none)" );
                    if ( v.right_offset >= 0 )
                        ImGui::Text( "Right:   0x%zX", static_cast<std::size_t>( v.right_offset ) );
                    else
                        ImGui::TextDisabled( "Right:   (none)" );
                    if ( v.parent_offset >= 0 )
                        ImGui::Text( "Parent:  0x%zX", static_cast<std::size_t>( v.parent_offset ) );
                    else
                        ImGui::TextDisabled( "Parent:  (root)" );
                    ImGui::Text( "Height:  %d", v.avl_height );
                    ImGui::Text( "Depth:   %d", v.avl_depth );
                    ImGui::EndTooltip();
                }
            }

            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    ImGui::End();
}

} // namespace demo
