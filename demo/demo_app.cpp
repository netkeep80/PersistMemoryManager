/**
 * @file demo_app.cpp
 * @brief Implementation of DemoApp.
 */

#include "demo_app.h"

#include "imgui.h"

#include <cstdlib>
#include <cstring>

namespace demo
{

// ─── Size table ───────────────────────────────────────────────────────────────

static constexpr std::size_t kPmmSizes[] = {
    1 * 1024 * 1024,   // 1 MB
    8 * 1024 * 1024,   // 8 MB
    32 * 1024 * 1024,  // 32 MB
    256 * 1024 * 1024, // 256 MB
};

static constexpr const char* kPmmSizeLabels[] = { "1 MB", "8 MB", "32 MB", "256 MB" };

// ─── Constructor / Destructor ─────────────────────────────────────────────────

DemoApp::DemoApp()
    : mem_map_view_( std::make_unique<MemMapView>() ), metrics_view_( std::make_unique<MetricsView>() ),
      struct_tree_view_( std::make_unique<StructTreeView>() ), scenario_manager_( std::make_unique<ScenarioManager>() ),
      last_ops_sample_( std::chrono::steady_clock::now() )
{
    pmm_size_ = kPmmSizes[pmm_size_idx_];
    pmm_buffer_.resize( pmm_size_ );
    pmm::PersistMemoryManager::create( pmm_buffer_.data(), pmm_size_ );
}

DemoApp::~DemoApp()
{
    scenario_manager_->stop_all();
    scenario_manager_->join_all();

    if ( pmm::PersistMemoryManager::instance() )
        pmm::PersistMemoryManager::destroy();
}

// ─── Render (called every frame) ─────────────────────────────────────────────

void DemoApp::render()
{
    render_dockspace();
    render_main_menu();

    auto* mgr = pmm::PersistMemoryManager::instance();

    // ── Collect snapshots (PMM methods are individually thread-safe) ──────────
    if ( mgr )
    {
        mem_map_view_->update_snapshot( mgr );
        struct_tree_view_->update_snapshot( mgr );

        // Build MetricsSnapshot
        auto            stats = pmm::get_stats( mgr );
        MetricsSnapshot snap;
        snap.total_size       = mgr->total_size();
        snap.used_size        = mgr->used_size();
        snap.free_size        = ( mgr->total_size() > mgr->used_size() ) ? ( mgr->total_size() - mgr->used_size() ) : 0;
        snap.total_blocks     = stats.total_blocks;
        snap.allocated_blocks = stats.allocated_blocks;
        snap.free_blocks      = stats.free_blocks;
        snap.fragmentation    = stats.total_fragmentation;
        snap.largest_free     = stats.largest_free;
        snap.smallest_free    = stats.smallest_free;

        metrics_view_->update( snap, ops_per_sec_ );
    } // snapshots collected

    // ── Render panels (no lock held) ─────────────────────────────────────────
    mem_map_view_->highlighted_block = highlighted_block_;
    mem_map_view_->render();

    struct_tree_view_->render( highlighted_block_ );
    metrics_view_->render();
    scenario_manager_->render();

    if ( show_help_ )
        render_help_window();
    if ( show_settings_ )
        render_settings_window();
}

// ─── DockSpace ────────────────────────────────────────────────────────────────

void DemoApp::render_dockspace()
{
    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport( 0, ImGui::GetMainViewport() );
    (void)dockspace_id;
}

// ─── Main menu bar ────────────────────────────────────────────────────────────

void DemoApp::render_main_menu()
{
    if ( ImGui::BeginMainMenuBar() )
    {
        ImGui::Text( "PersistMemoryManager Demo   v0.1" );
        ImGui::SameLine( ImGui::GetContentRegionAvail().x - 120.0f );

        if ( ImGui::Button( "?" ) )
            show_help_ = !show_help_;

        ImGui::SameLine();

        if ( ImGui::Button( "Settings" ) )
            show_settings_ = !show_settings_;

        ImGui::EndMainMenuBar();
    }
}

// ─── Help window ─────────────────────────────────────────────────────────────

void DemoApp::render_help_window()
{
    ImGui::Begin( "Help", &show_help_ );

    ImGui::TextUnformatted( "Colour legend for Memory Map:" );
    ImGui::Spacing();

    auto legend_row = [&]( ImVec4 col, const char* label )
    {
        ImGui::ColorButton( "##cb", col, ImGuiColorEditFlags_NoTooltip, ImVec2( 14, 14 ) );
        ImGui::SameLine();
        ImGui::TextUnformatted( label );
    };

    legend_row( ImVec4( 0.53f, 0.27f, 1.0f, 1.0f ), "ManagerHeader (manager metadata)" );
    legend_row( ImVec4( 0.13f, 0.13f, 0.53f, 1.0f ), "BlockHeader (used block)" );
    legend_row( ImVec4( 0.27f, 0.27f, 1.0f, 1.0f ), "User Data (used block)" );
    legend_row( ImVec4( 0.27f, 0.27f, 0.27f, 1.0f ), "BlockHeader (free block)" );
    legend_row( ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ), "User Data (free block)" );
    legend_row( ImVec4( 0.0f, 0.0f, 0.0f, 1.0f ), "Out of blocks (unused)" );

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted( "How to use:" );
    ImGui::BulletText( "Press > next to a scenario to start it." );
    ImGui::BulletText( "Click on a block in Struct Tree to highlight it on the map." );
    ImGui::BulletText( "Use Settings to change PMM size or theme." );

    ImGui::End();
}

// ─── Settings window ─────────────────────────────────────────────────────────

void DemoApp::render_settings_window()
{
    ImGui::Begin( "Settings", &show_settings_ );

    // PMM size selector
    ImGui::TextUnformatted( "Initial PMM size:" );
    for ( int i = 0; i < 4; ++i )
    {
        ImGui::RadioButton( kPmmSizeLabels[i], &pmm_size_idx_, i );
        if ( i < 3 )
            ImGui::SameLine();
    }

    if ( ImGui::Button( "Apply (restart scenarios)" ) )
        apply_pmm_size();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // FPS limit
    ImGui::SetNextItemWidth( 120.0f );
    ImGui::SliderInt( "FPS limit", &fps_limit_, 10, 144 );

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Theme selector
    ImGui::TextUnformatted( "ImGui theme:" );
    int prev_theme = theme_idx_;
    ImGui::RadioButton( "Dark", &theme_idx_, 0 );
    ImGui::SameLine();
    ImGui::RadioButton( "Light", &theme_idx_, 1 );
    ImGui::SameLine();
    ImGui::RadioButton( "Classic", &theme_idx_, 2 );

    if ( theme_idx_ != prev_theme )
    {
        switch ( theme_idx_ )
        {
        case 0:
            ImGui::StyleColorsDark();
            break;
        case 1:
            ImGui::StyleColorsLight();
            break;
        default:
            ImGui::StyleColorsClassic();
            break;
        }
    }

    ImGui::End();
}

// ─── PMM resize ───────────────────────────────────────────────────────────────

void DemoApp::apply_pmm_size()
{
    scenario_manager_->stop_all();
    scenario_manager_->join_all();

    if ( pmm::PersistMemoryManager::instance() )
        pmm::PersistMemoryManager::destroy();

    pmm_size_ = kPmmSizes[pmm_size_idx_];
    pmm_buffer_.assign( pmm_size_, std::uint8_t{ 0 } );
    pmm::PersistMemoryManager::create( pmm_buffer_.data(), pmm_size_ );
}

} // namespace demo
