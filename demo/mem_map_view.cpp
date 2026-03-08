/**
 * @file mem_map_view.cpp
 * @brief Implementation of MemMapView: PMM block-level pixel-map widget.
 *
 * Uses DemoMgr::for_each_block() (Issue #116) to iterate over all blocks and
 * build a per-byte PixelKind array, then downsamples to pixels for display.
 *
 * A logarithmic (power-of-2) slider controls how many bytes each pixel covers.
 */

#include "mem_map_view.h"

#include "imgui.h"

#include "pmm/types.h"

#include <algorithm>
#include <cstdio>

namespace demo
{

// ─── Colour palette ───────────────────────────────────────────────────────────

static ImVec4 pixel_colour( PixelKind k, bool highlighted )
{
    if ( highlighted )
        return ImVec4( 1.0f, 1.0f, 0.0f, 1.0f ); // yellow highlight

    switch ( k )
    {
    case PixelKind::ManagerHeader:
        return ImVec4( 0.53f, 0.27f, 1.0f, 1.0f ); // purple
    case PixelKind::UsedHeader:
        return ImVec4( 0.13f, 0.13f, 0.53f, 1.0f ); // dark blue
    case PixelKind::UsedData:
        return ImVec4( 0.27f, 0.27f, 1.0f, 1.0f ); // bright blue
    case PixelKind::FreeHeader:
        return ImVec4( 0.27f, 0.27f, 0.27f, 1.0f ); // dark grey
    case PixelKind::FreeData:
        return ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ); // white
    default:
        return ImVec4( 0.0f, 0.0f, 0.0f, 1.0f ); // black (unused)
    }
}

// ─── Snapshot builder ─────────────────────────────────────────────────────────

void MemMapView::update_snapshot()
{
    total_bytes_ = DemoMgr::total_size();
    used_bytes_  = DemoMgr::used_size();
    free_bytes_  = DemoMgr::free_size();

    rebuild_pixel_map();
}

void MemMapView::rebuild_pixel_map()
{
    if ( total_bytes_ == 0 )
    {
        byte_kinds_.clear();
        return;
    }

    // Allocate per-byte kind array; default everything to Unused.
    byte_kinds_.assign( total_bytes_, PixelKind::Unused );

    // Block<A> header size in bytes (32 bytes for DefaultAddressTraits).
    const std::size_t kBlockHdrSize = pmm::kGranuleSize * pmm::detail::kBlockHeaderGranules;
    // ManagerHeader size in bytes (64 bytes).
    const std::size_t kMgrHdrSize = sizeof( pmm::detail::ManagerHeader );

    // First block (Block_0) starts at offset 0; ManagerHeader follows immediately.
    // Mark Block_0 + ManagerHeader as ManagerHeader colour (manager metadata region).
    std::size_t mgr_region = kBlockHdrSize + kMgrHdrSize;
    if ( mgr_region <= total_bytes_ )
    {
        for ( std::size_t b = 0; b < mgr_region; ++b )
            byte_kinds_[b] = PixelKind::ManagerHeader;
    }

    // Walk all blocks via the public iteration API.
    DemoMgr::for_each_block(
        [&]( const pmm::BlockView& v )
        {
            std::size_t off = static_cast<std::size_t>( v.offset );
            if ( off >= total_bytes_ )
                return;

            // Skip the very first (header) block — already painted above.
            if ( off < mgr_region )
                return;

            // Mark block header bytes.
            std::size_t hdr_end = std::min( off + v.header_size, total_bytes_ );
            PixelKind   hdr_k   = v.used ? PixelKind::UsedHeader : PixelKind::FreeHeader;
            for ( std::size_t b = off; b < hdr_end; ++b )
                byte_kinds_[b] = hdr_k;

            if ( v.used && v.user_size > 0 )
            {
                // Mark user data bytes.
                std::size_t data_end = std::min( off + v.header_size + v.user_size, total_bytes_ );
                for ( std::size_t b = hdr_end; b < data_end; ++b )
                    byte_kinds_[b] = PixelKind::UsedData;
            }
            else if ( !v.used )
            {
                // Mark free data region.
                std::size_t free_end = std::min( off + v.total_size, total_bytes_ );
                for ( std::size_t b = hdr_end; b < free_end; ++b )
                    byte_kinds_[b] = PixelKind::FreeData;
            }
        } );
}

// ─── Pixel-map renderer ───────────────────────────────────────────────────────

void MemMapView::render_pixel_map( float map_width )
{
    if ( byte_kinds_.empty() || bytes_per_pixel_ == 0 )
        return;

    std::size_t num_pixels = ( total_bytes_ + bytes_per_pixel_ - 1 ) / bytes_per_pixel_;
    if ( num_pixels == 0 )
        return;

    // Pixel width: fill available area, at least 1 px.
    float px_w = std::max( 1.0f, map_width / static_cast<float>( num_pixels ) );
    float px_h = 16.0f;

    // Highlighted byte range (from highlighted_block granule index).
    std::size_t hl_byte_start = ( highlighted_block != static_cast<std::size_t>( -1 ) )
                                    ? highlighted_block * pmm::kGranuleSize
                                    : static_cast<std::size_t>( -1 );
    std::size_t hl_byte_end =
        ( hl_byte_start != static_cast<std::size_t>( -1 ) ) ? ( hl_byte_start + pmm::kGranuleSize ) : 0;

    ImDrawList* dl     = ImGui::GetWindowDrawList();
    ImVec2      cursor = ImGui::GetCursorScreenPos();

    for ( std::size_t p = 0; p < num_pixels; ++p )
    {
        std::size_t byte_start = p * bytes_per_pixel_;
        std::size_t byte_end   = std::min( byte_start + bytes_per_pixel_, total_bytes_ );

        // Pick the most "interesting" kind in this pixel range.
        // Priority order: UsedData > UsedHeader > ManagerHeader > FreeData > FreeHeader > Unused
        PixelKind dominant = PixelKind::Unused;
        for ( std::size_t b = byte_start; b < byte_end; ++b )
        {
            PixelKind k = byte_kinds_[b];
            if ( static_cast<int>( k ) > static_cast<int>( dominant ) )
                dominant = k;
        }

        bool hl =
            ( hl_byte_start != static_cast<std::size_t>( -1 ) && byte_start < hl_byte_end && byte_end > hl_byte_start );

        ImU32  col  = ImGui::ColorConvertFloat4ToU32( pixel_colour( dominant, hl ) );
        ImVec2 pmin = ImVec2( cursor.x + static_cast<float>( p ) * px_w, cursor.y );
        ImVec2 pmax = ImVec2( pmin.x + px_w, cursor.y + px_h );
        dl->AddRectFilled( pmin, pmax, col );
    }

    // Advance cursor past the drawn area.
    ImGui::Dummy( ImVec2( map_width, px_h ) );
}

// ─── Main render ──────────────────────────────────────────────────────────────

void MemMapView::render()
{
    ImGui::Begin( "Memory Map" );

    if ( total_bytes_ == 0 )
    {
        ImGui::TextDisabled( "(no PMM active)" );
        ImGui::End();
        return;
    }

    // ── Bytes-per-pixel scale slider (logarithmic, power-of-2) ───────────────
    {
        int max_log2 = 30;
        ImGui::SetNextItemWidth( 200.0f );
        if ( ImGui::SliderInt( "Bytes / pixel (2^N)", &bpp_log2_, 0, max_log2 ) )
            bytes_per_pixel_ = static_cast<std::size_t>( 1 ) << bpp_log2_;
        ImGui::SameLine();
        ImGui::Text( "= %zu B", bytes_per_pixel_ );
    }

    ImGui::Spacing();

    // ── Pixel map ─────────────────────────────────────────────────────────────
    float map_width = ImGui::GetContentRegionAvail().x;
    render_pixel_map( map_width );

    ImGui::Spacing();

    // ── Simple usage bar ──────────────────────────────────────────────────────
    float used_ratio =
        ( total_bytes_ > 0 ) ? static_cast<float>( used_bytes_ ) / static_cast<float>( total_bytes_ ) : 0.0f;

    char overlay[64];
    std::snprintf( overlay, sizeof( overlay ), "Used: %zu / %zu bytes  (%.1f%%)", used_bytes_, total_bytes_,
                   used_ratio * 100.0f );
    ImGui::ProgressBar( used_ratio, ImVec2( -1.0f, bar_height_ ), overlay );

    ImGui::Spacing();

    // ── Statistics table ──────────────────────────────────────────────────────
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

    // ── Colour legend ─────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted( "Legend:" );

    auto legend_item = [&]( ImVec4 col, const char* label )
    {
        ImGui::ColorButton( "##cb", col, ImGuiColorEditFlags_NoTooltip, ImVec2( 12.0f, 12.0f ) );
        ImGui::SameLine();
        ImGui::TextUnformatted( label );
    };

    legend_item( ImVec4( 0.53f, 0.27f, 1.0f, 1.0f ), "Manager metadata" );
    legend_item( ImVec4( 0.13f, 0.13f, 0.53f, 1.0f ), "Block header (used)" );
    legend_item( ImVec4( 0.27f, 0.27f, 1.0f, 1.0f ), "User data (used)" );
    legend_item( ImVec4( 0.27f, 0.27f, 0.27f, 1.0f ), "Block header (free)" );
    legend_item( ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ), "Free space" );
    legend_item( ImVec4( 0.0f, 0.0f, 0.0f, 1.0f ), "Unused" );

    ImGui::End();
}

} // namespace demo
