/**
 * @file avl_tree_view.cpp
 * @brief Implementation of AvlTreeView: AVL free-block tree panel.
 *
 * Issue #65: Renders free-block information from the PMM manager.
 * Issue #116: Uses DemoMgr::for_each_free_block() to iterate over all free
 * blocks in-order (ascending size) and build an offset-to-index map.
 * Issue #118: Renders the free-block AVL tree as a visual tree using ImGui
 * TreeNode/TreePop, starting from the root node (parent_offset == -1) and
 * recursively descending to left and right children.
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
    offset_to_idx_.clear();
    DemoMgr::for_each_free_block(
        [&]( const pmm::FreeBlockView& v )
        {
            offset_to_idx_[v.offset] = free_blocks_.size();
            free_blocks_.push_back( v );
        } );
}

// ─── Tree node renderer ───────────────────────────────────────────────────────

void AvlTreeView::render_node( std::ptrdiff_t offset, int depth )
{
    auto it = offset_to_idx_.find( offset );
    if ( it == offset_to_idx_.end() )
        return;

    const pmm::FreeBlockView& v = free_blocks_[it->second];

    bool has_children = ( v.left_offset >= 0 || v.right_offset >= 0 );

    // Build node label: offset + free bytes + height/depth.
    char label[128];
    std::snprintf( label, sizeof( label ), "0x%zX  free=%zu B  h=%d  d=%d##node_%zd",
                   static_cast<std::size_t>( v.offset ), v.free_size, v.avl_height, depth,
                   static_cast<std::size_t>( v.offset ) );

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if ( !has_children )
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    bool open = ImGui::TreeNodeEx( label, flags );

    // Tooltip showing full node details.
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
        ImGui::Text( "Depth:   %d", depth );
        ImGui::EndTooltip();
    }

    if ( open && has_children )
    {
        if ( v.left_offset >= 0 )
            render_node( v.left_offset, depth + 1 );
        if ( v.right_offset >= 0 )
            render_node( v.right_offset, depth + 1 );
        ImGui::TreePop();
    }
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

    if ( free_blocks_.empty() )
    {
        ImGui::TextDisabled( "(no free blocks)" );
        ImGui::End();
        return;
    }

    // ── Visual tree (Issue #118) ───────────────────────────────────────────────
    // Find the root: the node with parent_offset == -1.
    // (There should be exactly one root in a valid AVL tree.)
    ImGui::TextUnformatted( "AVL free-block tree:" );
    ImGui::Spacing();

    bool root_found = false;
    for ( const pmm::FreeBlockView& v : free_blocks_ )
    {
        if ( v.parent_offset < 0 )
        {
            render_node( v.offset, 0 );
            root_found = true;
            break;
        }
    }

    if ( !root_found )
    {
        // Fallback: render all nodes at depth 0 (should not happen with a valid tree).
        for ( const pmm::FreeBlockView& v : free_blocks_ )
            render_node( v.offset, 0 );
    }

    ImGui::End();
}

} // namespace demo
