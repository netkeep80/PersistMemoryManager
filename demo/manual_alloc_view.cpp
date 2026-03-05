/**
 * @file manual_alloc_view.cpp
 * @brief Implementation of ManualAllocView: step-by-step manual alloc/free panel.
 *
 * Issue #65: provides "Alloc" and "Free" buttons so the user can manually test the
 * memory manager one operation at a time while watching the AVL tree and memory map.
 */

#include "manual_alloc_view.h"

#include "imgui.h"
#include "pmm/legacy_manager.h"

#include <cstdio>

namespace demo
{

// ─── Free all live blocks ─────────────────────────────────────────────────────

void ManualAllocView::clear()
{
    auto* mgr = pmm::PersistMemoryManager<>::instance();
    if ( mgr )
    {
        for ( auto& blk : blocks_ )
        {
            if ( !blk.ptr.is_null() )
                pmm::PersistMemoryManager<>::deallocate_typed( blk.ptr );
        }
    }
    blocks_.clear();
    selected_idx_ = -1;
}

// ─── Render ───────────────────────────────────────────────────────────────────

void ManualAllocView::render()
{
    ImGui::Begin( "Manual Alloc" );

    auto* mgr = pmm::PersistMemoryManager<>::instance();

    // ── Allocation controls ───────────────────────────────────────────────────
    ImGui::TextUnformatted( "Allocation size (bytes):" );
    ImGui::SameLine();
    ImGui::SetNextItemWidth( 120.0f );
    ImGui::InputInt( "##alloc_size", &alloc_size_ );
    if ( alloc_size_ < 1 )
        alloc_size_ = 1;

    ImGui::SameLine();

    bool can_alloc = ( mgr != nullptr );
    if ( !can_alloc )
        ImGui::BeginDisabled();

    if ( ImGui::Button( "Alloc" ) && can_alloc )
    {
        auto ptr = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( static_cast<std::size_t>( alloc_size_ ) );
        if ( !ptr.is_null() )
        {
            ManualBlock blk;
            blk.ptr    = ptr;
            blk.size   = static_cast<std::size_t>( alloc_size_ );
            blk.offset = static_cast<std::ptrdiff_t>( ptr.offset() ) *
                         static_cast<std::ptrdiff_t>( pmm::kGranuleSize ); // granule index → bytes
            char lbl[64];
            std::snprintf( lbl, sizeof( lbl ), "Alloc #%llu", static_cast<unsigned long long>( ++alloc_serial_ ) );
            blk.label = lbl;
            blocks_.push_back( blk );
        }
        else
        {
            // Show OOM as a tooltip on next hover — for now just ignore silently.
            // The memory-map panel will reflect the OOM state.
        }
    }

    if ( !can_alloc )
        ImGui::EndDisabled();

    // ── Free controls ─────────────────────────────────────────────────────────
    ImGui::SameLine();

    bool can_free = ( mgr != nullptr && selected_idx_ >= 0 && selected_idx_ < static_cast<int>( blocks_.size() ) );
    if ( !can_free )
        ImGui::BeginDisabled();

    if ( ImGui::Button( "Free" ) && can_free )
    {
        auto& blk = blocks_[static_cast<std::size_t>( selected_idx_ )];
        if ( !blk.ptr.is_null() )
            pmm::PersistMemoryManager<>::deallocate_typed( blk.ptr );
        blocks_.erase( blocks_.begin() + selected_idx_ );
        selected_idx_ = -1;
    }

    if ( !can_free )
        ImGui::EndDisabled();

    ImGui::SameLine();

    bool can_free_all = ( mgr != nullptr && !blocks_.empty() );
    if ( !can_free_all )
        ImGui::BeginDisabled();

    if ( ImGui::Button( "Free All" ) && can_free_all )
        clear();

    if ( !can_free_all )
        ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Live blocks list ─────────────────────────────────────────────────────
    if ( blocks_.empty() )
    {
        ImGui::TextDisabled( "(no manually allocated blocks)" );
    }
    else
    {
        ImGui::Text( "Live blocks: %zu", blocks_.size() );
        ImGui::Spacing();

        if ( ImGui::BeginTable( "manual_blocks_tbl", 3,
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                ImVec2( 0.0f, 200.0f ) ) )
        {
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableSetupColumn( "Label", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Offset", ImGuiTableColumnFlags_WidthFixed, 100.0f );
            ImGui::TableSetupColumn( "Size", ImGuiTableColumnFlags_WidthFixed, 80.0f );
            ImGui::TableHeadersRow();

            for ( int i = 0; i < static_cast<int>( blocks_.size() ); ++i )
            {
                const auto& blk = blocks_[static_cast<std::size_t>( i )];

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );

                bool selected = ( i == selected_idx_ );
                char sel_id[32];
                std::snprintf( sel_id, sizeof( sel_id ), "##row%d", i );
                if ( ImGui::Selectable( sel_id, selected, ImGuiSelectableFlags_SpanAllColumns ) )
                {
                    selected_idx_ = selected ? -1 : i;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted( blk.label.c_str() );

                ImGui::TableSetColumnIndex( 1 );
                ImGui::Text( "+%td", blk.offset );

                ImGui::TableSetColumnIndex( 2 );
                ImGui::Text( "%zu B", blk.size );
            }

            ImGui::EndTable();
        }
    }

    ImGui::End();
}

} // namespace demo
