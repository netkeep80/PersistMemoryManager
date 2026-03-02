/**
 * @file avl_tree_view.cpp
 * @brief Implementation of AvlTreeView: AVL free-block tree visualisation panel.
 *
 * Issue #65: Renders the AVL tree of free blocks as a hierarchical ImGui tree,
 * allowing users to inspect the tree topology, block sizes, and AVL heights.
 */

#include "avl_tree_view.h"

#include "imgui.h"
#include "persist_memory_manager.h"

#include <cstdio>

namespace demo
{

// ─── Snapshot builder ─────────────────────────────────────────────────────────

void AvlTreeView::update_snapshot( pmm::PersistMemoryManager* mgr )
{
    snapshot_.clear();
    root_offset_ = -1;

    if ( !mgr )
        return;

    // Capture the root offset before iterating (get_manager_info acquires shared_lock too,
    // but for_each_free_block_avl will acquire its own shared_lock — that is fine since
    // std::shared_mutex allows multiple concurrent shared_lock holders).
    const pmm::ManagerInfo info = pmm::get_manager_info();
    root_offset_                = info.first_free_offset; // -1 when tree is empty

    pmm::for_each_free_block_avl(
        [&]( const pmm::FreeBlockView& v )
        {
            AvlNodeSnapshot ns;
            ns.offset        = v.offset;
            ns.total_size    = v.total_size;
            ns.free_size     = v.free_size;
            ns.left_offset   = v.left_offset;
            ns.right_offset  = v.right_offset;
            ns.parent_offset = v.parent_offset;
            ns.avl_height    = v.avl_height;
            ns.avl_depth     = v.avl_depth;
            snapshot_.push_back( ns );
        } );
}

// ─── Recursive node renderer ──────────────────────────────────────────────────

void AvlTreeView::render_node( std::ptrdiff_t node_offset, int depth )
{
    if ( node_offset < 0 )
        return;

    // Find the node in the snapshot by offset (snapshot is in linear block order).
    const AvlNodeSnapshot* ns = nullptr;
    for ( const auto& n : snapshot_ )
    {
        if ( n.offset == node_offset )
        {
            ns = &n;
            break;
        }
    }
    if ( !ns )
        return;

    // Build a label that summarises this node.
    char label[256];
    std::snprintf( label, sizeof( label ), "offset=+%td  size=%zu B  free=%zu B  h=%d", ns->offset, ns->total_size,
                   ns->free_size, ns->avl_height );

    bool has_children = ( ns->left_offset >= 0 || ns->right_offset >= 0 );

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if ( !has_children )
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    bool open = ImGui::TreeNodeEx( (void*)static_cast<intptr_t>( ns->offset ), flags, "%s", label );

    // Tooltip with full AVL details
    if ( ImGui::IsItemHovered() )
    {
        ImGui::BeginTooltip();
        ImGui::Text( "Byte offset:  +%td", ns->offset );
        ImGui::Text( "Total size:   %zu bytes", ns->total_size );
        ImGui::Text( "Free data:    %zu bytes", ns->free_size );
        ImGui::Text( "AVL height:   %d", ns->avl_height );
        ImGui::Text( "AVL depth:    %d", depth );
        if ( ns->left_offset >= 0 )
            ImGui::Text( "Left child:   +%td", ns->left_offset );
        else
            ImGui::TextDisabled( "Left child:   —" );
        if ( ns->right_offset >= 0 )
            ImGui::Text( "Right child:  +%td", ns->right_offset );
        else
            ImGui::TextDisabled( "Right child:  —" );
        if ( ns->parent_offset >= 0 )
            ImGui::Text( "Parent:       +%td", ns->parent_offset );
        else
            ImGui::TextDisabled( "Parent:       — (root)" );
        ImGui::EndTooltip();
    }

    if ( open && has_children )
    {
        if ( ns->left_offset >= 0 )
            render_node( ns->left_offset, depth + 1 );
        if ( ns->right_offset >= 0 )
            render_node( ns->right_offset, depth + 1 );
        ImGui::TreePop();
    }
}

// ─── Main render ──────────────────────────────────────────────────────────────

void AvlTreeView::render()
{
    ImGui::Begin( "AVL Free Tree" );

    if ( snapshot_.empty() )
    {
        ImGui::TextDisabled( "(no free blocks)" );
        ImGui::End();
        return;
    }

    // Summary header
    ImGui::Text( "Free blocks: %zu   Root offset: +%td", snapshot_.size(), root_offset_ );
    ImGui::Separator();

    // Render the tree recursively starting from the root
    if ( root_offset_ >= 0 )
        render_node( root_offset_, 0 );

    ImGui::End();
}

} // namespace demo
