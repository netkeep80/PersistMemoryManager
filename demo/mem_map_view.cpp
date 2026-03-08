/**
 * @file mem_map_view.cpp
 * @brief Implementation of MemMapView: PMM memory usage bar widget.
 *
 * DemoMgr is a fully static class. Statistics are read via static DemoMgr::
 * methods directly — no instance pointer is needed.
 */

#include "mem_map_view.h"

#include "imgui.h"

#include <algorithm>

namespace demo
{

// ─── Snapshot builder ─────────────────────────────────────────────────────────

void MemMapView::update_snapshot()
{
    total_bytes_ = DemoMgr::total_size();
    used_bytes_  = DemoMgr::used_size();
    free_bytes_  = DemoMgr::free_size();
}

// ─── Main render ──────────────────────────────────────────────────────────────

void MemMapView::render()
{
    ImGui::Begin( "Memory Map" );

    ImGui::TextDisabled( "(block-level pixel map not available in new API — showing usage bar)" );
    ImGui::Spacing();

    if ( total_bytes_ == 0 )
    {
        ImGui::TextDisabled( "(no PMM active)" );
        ImGui::End();
        return;
    }

    // ── Used/Free bar ─────────────────────────────────────────────────────────
    float used_ratio =
        ( total_bytes_ > 0 ) ? static_cast<float>( used_bytes_ ) / static_cast<float>( total_bytes_ ) : 0.0f;

    char overlay[64];
    std::snprintf( overlay, sizeof( overlay ), "Used: %zu / %zu bytes  (%.1f%%)", used_bytes_, total_bytes_,
                   used_ratio * 100.0f );
    ImGui::ProgressBar( used_ratio, ImVec2( -1.0f, bar_height_ ), overlay );

    ImGui::Spacing();

    // ── Statistics ────────────────────────────────────────────────────────────
    if ( ImGui::BeginTable( "mem_map_tbl", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
    {
        ImGui::TableSetupColumn( "Field", ImGuiTableColumnFlags_WidthStretch );
        ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthFixed, 160.0f );
        ImGui::TableHeadersRow();

        auto row = [&]( const char* label, std::size_t value )
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( label );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::Text( "%zu", value );
        };

        row( "Total size (bytes)", total_bytes_ );
        row( "Used size (bytes)", used_bytes_ );
        row( "Free size (bytes)", free_bytes_ );

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace demo
