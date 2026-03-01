/**
 * @file metrics_view.cpp
 * @brief Implementation of MetricsView: live PMM metrics panel.
 */

#include "metrics_view.h"

#include "imgui.h"
#include "persist_memory_manager.h"

#include <cstdio>

namespace demo
{

// ─── Update ───────────────────────────────────────────────────────────────────

void MetricsView::update( const MetricsSnapshot& snap, float ops_per_sec )
{
    current_             = snap;
    current_ops_per_sec_ = ops_per_sec;

    float used_ratio =
        ( snap.total_size > 0 ) ? static_cast<float>( snap.used_size ) / static_cast<float>( snap.total_size ) : 0.0f;

    float frag_ratio = ( snap.total_size > 0 )
                           ? static_cast<float>( snap.fragmentation ) / static_cast<float>( snap.total_size )
                           : 0.0f;

    used_history_[history_offset_] = used_ratio;
    frag_history_[history_offset_] = frag_ratio;
    ops_history_[history_offset_]  = ops_per_sec;

    history_offset_ = ( history_offset_ + 1 ) % kHistorySize;
}

// ─── Render ───────────────────────────────────────────────────────────────────

void MetricsView::render()
{
    ImGui::Begin( "Metrics" );

    // Progress bar: used / total
    {
        float ratio = ( current_.total_size > 0 )
                          ? static_cast<float>( current_.used_size ) / static_cast<float>( current_.total_size )
                          : 0.0f;
        char  overlay[64];
        std::snprintf( overlay, sizeof( overlay ), "%.1f%%  %zu / %zu bytes", ratio * 100.0f, current_.used_size,
                       current_.total_size );
        ImGui::ProgressBar( ratio, ImVec2( -1.0f, 0.0f ), overlay );
    }

    ImGui::Spacing();

    // Metrics table
    if ( ImGui::BeginTable( "metrics_tbl", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
    {
        ImGui::TableSetupColumn( "Metric", ImGuiTableColumnFlags_WidthStretch );
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

        row( "Total size (bytes)", current_.total_size );
        row( "Used size (bytes)", current_.used_size );
        row( "Free size (bytes)", current_.free_size );
        row( "Total blocks", current_.total_blocks );
        row( "Allocated blocks", current_.allocated_blocks );
        row( "Free blocks", current_.free_blocks );
        row( "Fragmentation (bytes)", current_.fragmentation );
        row( "Largest free block", current_.largest_free );
        row( "Smallest free block", current_.smallest_free );

        // ops/s as special row
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex( 0 );
        ImGui::TextUnformatted( "Ops / sec" );
        ImGui::TableSetColumnIndex( 1 );
        ImGui::Text( "%.0f", static_cast<double>( current_ops_per_sec_ ) );

        ImGui::EndTable();
    }

    ImGui::Spacing();

    // Scrolling plots
    const float plot_h = 60.0f;
    const float plot_w = -1.0f;

    ImGui::Text( "Used memory ratio (0..1)" );
    ImGui::PlotLines( "##used", used_history_, kHistorySize, history_offset_, nullptr, 0.0f, 1.0f,
                      ImVec2( plot_w, plot_h ) );

    ImGui::Text( "Fragmentation ratio (0..1)" );
    ImGui::PlotLines( "##frag", frag_history_, kHistorySize, history_offset_, nullptr, 0.0f, 1.0f,
                      ImVec2( plot_w, plot_h ) );

    ImGui::Text( "Operations / sec" );
    ImGui::PlotLines( "##ops", ops_history_, kHistorySize, history_offset_, nullptr, 0.0f, -1.0f,
                      ImVec2( plot_w, plot_h ) );

    ImGui::Spacing();
    ImGui::Separator();

    if ( ImGui::Button( "Dump to stdout" ) )
    {
        auto* mgr = pmm::PersistMemoryManager::instance();
        if ( mgr )
            mgr->dump_stats();
    }

    ImGui::End();
}

} // namespace demo
