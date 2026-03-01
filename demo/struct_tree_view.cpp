/**
 * @file struct_tree_view.cpp
 * @brief Implementation of StructTreeView: PMM internal structure inspector.
 */

#include "struct_tree_view.h"

#include "imgui.h"

#include <cstdio>
#include <cstdint>

namespace demo
{

// ─── Snapshot builder ─────────────────────────────────────────────────────────

void StructTreeView::update_snapshot( pmm::PersistMemoryManager* mgr )
{
    if ( !mgr )
        return;

    snapshot_.blocks.clear();

    const auto* base = reinterpret_cast<const std::uint8_t*>( mgr );
    const auto* hdr  = reinterpret_cast<const pmm::detail::ManagerHeader*>( base );

    snapshot_.magic              = hdr->magic;
    snapshot_.total_size         = hdr->total_size;
    snapshot_.used_size          = hdr->used_size;
    snapshot_.block_count        = hdr->block_count;
    snapshot_.free_count         = hdr->free_count;
    snapshot_.alloc_count        = hdr->alloc_count;
    snapshot_.first_block_offset = hdr->first_block_offset;
    snapshot_.first_free_offset  = hdr->first_free_offset;

    std::ptrdiff_t offset = hdr->first_block_offset;
    std::size_t    idx    = 0;

    while ( offset != pmm::detail::kNoBlock )
    {
        if ( offset < 0 || static_cast<std::size_t>( offset ) >= hdr->total_size )
            break;

        const auto* blk = reinterpret_cast<const pmm::detail::BlockHeader*>( base + offset );

        BlockSnapshot bs;
        bs.index      = idx;
        bs.offset     = static_cast<std::size_t>( offset );
        bs.total_size = blk->total_size;
        bs.user_size  = blk->user_size;
        bs.alignment  = blk->alignment;
        bs.used       = blk->used;

        snapshot_.blocks.push_back( bs );
        ++idx;

        offset = blk->next_offset;
    }
}

// ─── Renderer ─────────────────────────────────────────────────────────────────

void StructTreeView::render( std::size_t& highlighted_block )
{
    ImGui::Begin( "Struct Tree" );

    if ( ImGui::TreeNode( "PersistMemoryManager" ) )
    {
        if ( ImGui::TreeNode( "ManagerHeader" ) )
        {
            ImGui::Text( "magic:              0x%016llX", static_cast<unsigned long long>( snapshot_.magic ) );
            ImGui::Text( "total_size:         %zu", snapshot_.total_size );
            ImGui::Text( "used_size:          %zu", snapshot_.used_size );
            ImGui::Text( "block_count:        %zu", snapshot_.block_count );
            ImGui::Text( "free_count:         %zu", snapshot_.free_count );
            ImGui::Text( "alloc_count:        %zu", snapshot_.alloc_count );
            ImGui::Text( "first_block_offset: %td", snapshot_.first_block_offset );
            ImGui::Text( "first_free_offset:  %td", snapshot_.first_free_offset );
            ImGui::TreePop();
        }

        const std::size_t n = snapshot_.blocks.size();
        char              blocks_label[64];
        std::snprintf( blocks_label, sizeof( blocks_label ), "Blocks [%zu]", n );

        if ( ImGui::TreeNode( blocks_label ) )
        {
            const bool show_all = ( n <= kMaxVisibleBlocks * 2 );

            auto render_block = [&]( const BlockSnapshot& bs )
            {
                char label[128];
                std::snprintf( label, sizeof( label ), "Block #%zu  offset=%zu  size=%zu  %s  user=%zu  align=%zu",
                               bs.index, bs.offset, bs.total_size, bs.used ? "USED" : "FREE", bs.user_size,
                               bs.alignment );

                bool selected = ( bs.index == highlighted_block );
                if ( ImGui::Selectable( label, selected ) )
                    highlighted_block = bs.index;
            };

            if ( show_all )
            {
                for ( const auto& bs : snapshot_.blocks )
                    render_block( bs );
            }
            else
            {
                // First kMaxVisibleBlocks
                for ( std::size_t i = 0; i < kMaxVisibleBlocks; ++i )
                    render_block( snapshot_.blocks[i] );

                ImGui::TextDisabled( "... %zu blocks hidden ...", n - kMaxVisibleBlocks * 2 );

                // Last kMaxVisibleBlocks
                for ( std::size_t i = n - kMaxVisibleBlocks; i < n; ++i )
                    render_block( snapshot_.blocks[i] );
            }

            ImGui::TreePop();
        }

        ImGui::TreePop();
    }

    ImGui::End();
}

} // namespace demo
