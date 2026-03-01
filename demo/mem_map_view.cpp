/**
 * @file mem_map_view.cpp
 * @brief Implementation of MemMapView: PMM memory pixel map widget.
 */

#include "mem_map_view.h"

#include "imgui.h"

#include "persist_memory_manager.h"

namespace demo
{

// ─── Colour palette (ABGR for ImGui) ─────────────────────────────────────────

static constexpr ImU32 kColManagerHeader   = IM_COL32( 0x88, 0x44, 0xFF, 0xFF ); // purple-blue
static constexpr ImU32 kColBlockHeaderUsed = IM_COL32( 0x22, 0x22, 0x88, 0xFF ); // dark blue/red
static constexpr ImU32 kColUserDataUsed    = IM_COL32( 0x44, 0x44, 0xFF, 0xFF ); // red
static constexpr ImU32 kColBlockHeaderFree = IM_COL32( 0x44, 0x44, 0x44, 0xFF ); // dark grey
static constexpr ImU32 kColUserDataFree    = IM_COL32( 0xFF, 0xFF, 0xFF, 0xFF ); // white
static constexpr ImU32 kColOutOfBlocks     = IM_COL32( 0x00, 0x00, 0x00, 0xFF ); // black

static ImU32 type_to_color( ByteInfo::Type t )
{
    switch ( t )
    {
    case ByteInfo::Type::ManagerHeader:
        return kColManagerHeader;
    case ByteInfo::Type::BlockHeaderUsed:
        return kColBlockHeaderUsed;
    case ByteInfo::Type::UserDataUsed:
        return kColUserDataUsed;
    case ByteInfo::Type::BlockHeaderFree:
        return kColBlockHeaderFree;
    case ByteInfo::Type::UserDataFree:
        return kColUserDataFree;
    default:
        return kColOutOfBlocks;
    }
}

// ─── Snapshot builder ─────────────────────────────────────────────────────────

void MemMapView::update_snapshot( pmm::PersistMemoryManager* mgr )
{
    if ( !mgr )
        return;

    total_bytes_ = mgr->total_size();

    // Clamp to 512 KB in detailed mode for performance
    static constexpr std::size_t kDetailLimit  = 512 * 1024;
    const std::size_t            display_bytes = ( total_bytes_ > kDetailLimit ) ? kDetailLimit : total_bytes_;

    snapshot_.resize( display_bytes );
    for ( std::size_t i = 0; i < display_bytes; ++i )
    {
        snapshot_[i].type        = ByteInfo::Type::OutOfBlocks;
        snapshot_[i].block_index = 0;
        snapshot_[i].offset      = i;
    }

    // Mark ManagerHeader region using the public API
    const std::size_t mgr_hdr_sz = pmm::PersistMemoryManager::manager_header_size();
    const std::size_t mark_hdr   = ( mgr_hdr_sz < display_bytes ) ? mgr_hdr_sz : display_bytes;
    for ( std::size_t i = 0; i < mark_hdr; ++i )
        snapshot_[i].type = ByteInfo::Type::ManagerHeader;

    // Walk block linked list via the public for_each_block() iterator
    pmm::for_each_block( mgr,
                         [&]( const pmm::BlockView& blk )
                         {
                             const ByteInfo::Type hdr_type =
                                 blk.used ? ByteInfo::Type::BlockHeaderUsed : ByteInfo::Type::BlockHeaderFree;
                             const ByteInfo::Type data_type =
                                 blk.used ? ByteInfo::Type::UserDataUsed : ByteInfo::Type::UserDataFree;

                             const std::size_t blk_start = static_cast<std::size_t>( blk.offset );
                             const std::size_t hdr_end   = blk_start + blk.header_size;
                             const std::size_t blk_end   = blk_start + blk.total_size;

                             // Mark BlockHeader bytes
                             for ( std::size_t b = blk_start; b < hdr_end && b < display_bytes; ++b )
                             {
                                 snapshot_[b].type        = hdr_type;
                                 snapshot_[b].block_index = blk.index;
                             }

                             // Mark user data bytes
                             for ( std::size_t b = hdr_end; b < blk_end && b < display_bytes; ++b )
                             {
                                 snapshot_[b].type        = data_type;
                                 snapshot_[b].block_index = blk.index;
                             }
                         } );
}

// ─── Renderer ─────────────────────────────────────────────────────────────────

void MemMapView::render()
{
    ImGui::Begin( "Memory Map" );

    // Controls
    if ( auto_scale_ )
    {
        float panel_w = ImGui::GetContentRegionAvail().x;
        float pixel_w = std::max( 1.0f, pixel_scale_ );
        raster_width_ = std::max( 1, static_cast<int>( panel_w / pixel_w ) );
    }

    ImGui::Checkbox( "Auto width", &auto_scale_ );
    if ( !auto_scale_ )
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 120.0f );
        ImGui::SliderInt( "Width", &raster_width_, 8, 1024 );
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth( 100.0f );
    ImGui::SliderFloat( "Scale", &pixel_scale_, 1.0f, 4.0f );

    if ( total_bytes_ > 512 * 1024 )
    {
        ImGui::SameLine();
        ImGui::TextDisabled( "(first 512 KB shown)" );
    }

    ImGui::Separator();

    // Pixel map via DrawList
    ImDrawList*  draw   = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float  ps     = std::max( 1.0f, pixel_scale_ );
    const int    cols   = std::max( 1, raster_width_ );

    const std::size_t n = snapshot_.size();

    // Reserve interaction region
    const int    rows = ( n > 0 ) ? ( static_cast<int>( n ) + cols - 1 ) / cols : 1;
    const ImVec2 canvas_size( static_cast<float>( cols ) * ps, static_cast<float>( rows ) * ps );
    ImGui::InvisibleButton( "memmap_canvas", canvas_size );

    bool   hovered   = ImGui::IsItemHovered();
    ImVec2 mouse_pos = ImGui::GetMousePos();

    std::size_t hovered_idx = static_cast<std::size_t>( -1 );
    if ( hovered )
    {
        int col = static_cast<int>( ( mouse_pos.x - origin.x ) / ps );
        int row = static_cast<int>( ( mouse_pos.y - origin.y ) / ps );
        if ( col >= 0 && row >= 0 )
        {
            std::size_t idx = static_cast<std::size_t>( row * cols + col );
            if ( idx < n )
                hovered_idx = idx;
        }
    }

    for ( std::size_t i = 0; i < n; ++i )
    {
        const int col = static_cast<int>( i ) % cols;
        const int row = static_cast<int>( i ) / cols;

        const float x0 = origin.x + static_cast<float>( col ) * ps;
        const float y0 = origin.y + static_cast<float>( row ) * ps;
        const float x1 = x0 + ps;
        const float y1 = y0 + ps;

        ImU32 col_fill = type_to_color( snapshot_[i].type );

        // Highlight selected block
        if ( snapshot_[i].block_index == highlighted_block && snapshot_[i].type != ByteInfo::Type::ManagerHeader &&
             snapshot_[i].type != ByteInfo::Type::OutOfBlocks )
        {
            col_fill = IM_COL32( 0xFF, 0xFF, 0x00, 0xFF ); // yellow highlight
        }

        draw->AddRectFilled( ImVec2( x0, y0 ), ImVec2( x1, y1 ), col_fill );
    }

    // Tooltip
    if ( hovered && hovered_idx < n )
    {
        const auto& bi        = snapshot_[hovered_idx];
        const char* type_name = "?";
        switch ( bi.type )
        {
        case ByteInfo::Type::ManagerHeader:
            type_name = "ManagerHeader";
            break;
        case ByteInfo::Type::BlockHeaderUsed:
            type_name = "BlockHeader(used)";
            break;
        case ByteInfo::Type::UserDataUsed:
            type_name = "UserData(used)";
            break;
        case ByteInfo::Type::BlockHeaderFree:
            type_name = "BlockHeader(free)";
            break;
        case ByteInfo::Type::UserDataFree:
            type_name = "UserData(free)";
            break;
        default:
            type_name = "OutOfBlocks";
            break;
        }
        ImGui::BeginTooltip();
        ImGui::Text( "Offset: %zu", bi.offset );
        ImGui::Text( "Type:   %s", type_name );
        if ( bi.type != ByteInfo::Type::ManagerHeader && bi.type != ByteInfo::Type::OutOfBlocks )
            ImGui::Text( "Block:  #%zu", bi.block_index );
        ImGui::EndTooltip();
    }

    ImGui::End();
}

} // namespace demo
