#pragma once
#include "pmm/arena_internals.h"
#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/types.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
namespace pmm::detail
{
template <typename ManagerAccess> struct ManagerLayoutOps
{
    using address_traits  = typename ManagerAccess::address_traits;
    using free_block_tree = typename ManagerAccess::free_block_tree;
    using index_type      = typename address_traits::index_type;
    using logging_policy  = typename ManagerAccess::logging_policy;
    using storage_backend = typename ManagerAccess::storage_backend;
    using BlockState      = BlockStateBase<address_traits>;
    static bool init_layout( storage_backend& backend, uint8_t* base, size_t size ) noexcept
    {
        static constexpr index_type kHdrBlkIdx        = 0;
        static constexpr index_type kFreeBlkIdx       = ManagerAccess::kFreeBlkIdxLayout;
        static constexpr size_t     kGranSz           = address_traits::granule_size;
        static constexpr size_t     kMinBlockDataSize = kGranSz;
        if ( static_cast<size_t>( kFreeBlkIdx ) * kGranSz + sizeof( Block<address_traits> ) + kMinBlockDataSize > size )
            return false;
        void* hdr_blk = base;
        std::memset( hdr_blk, 0, ManagerAccess::kBlockHdrByteSize );
        BlockState::init_fields( hdr_blk, address_traits::no_block, kFreeBlkIdx, 0, ManagerAccess::kMgrHdrGranules,
                                 kHdrBlkIdx, NodeType::ManagerHeader );
        ManagerHeader<address_traits>* hdr = ManagerAccess::get_header( base );
        std::memset( hdr, 0, sizeof( ManagerHeader<address_traits> ) );
        hdr->magic              = ManagerAccess::kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = kHdrBlkIdx;
        hdr->last_block_offset  = address_traits::no_block;
        hdr->free_tree_root     = address_traits::no_block;
        hdr->image_version      = kCurrentImageVersion;
        hdr->granule_size       = static_cast<uint16_t>( kGranSz );
        hdr->root_offset        = address_traits::no_block;
        void*      blk          = base + static_cast<size_t>( kFreeBlkIdx ) * kGranSz;
        index_type total_gran   = static_cast<index_type>( size / kGranSz );
        index_type free_gran    = static_cast<index_type>( total_gran - kFreeBlkIdx );
        std::memset( blk, 0, sizeof( Block<address_traits> ) );
        BlockState::init_fields( blk, kHdrBlkIdx, address_traits::no_block, 1, free_gran, 0, NodeType::Free );
        hdr->last_block_offset = kFreeBlkIdx;
        hdr->free_tree_root    = kFreeBlkIdx;
        hdr->block_count       = 2;
        hdr->free_count        = 1;
        hdr->alloc_count       = 1;
        hdr->used_size         = kFreeBlkIdx + ManagerAccess::kBlockHdrGranules;
        (void)backend;
        ManagerAccess::set_initialized();
        return true;
    }
    static bool do_expand( storage_backend& backend, bool initialized, index_type data_gran_need ) noexcept
    {
        if ( !initialized )
            return false;
        if ( data_gran_need == 0 )
            return false;
        uint8_t*                       base     = backend.base_ptr();
        ManagerHeader<address_traits>* hdr      = ManagerAccess::get_header( base );
        size_t                         old_size = hdr->total_size;
        static constexpr size_t        kGranSz  = address_traits::granule_size;
        auto need_grans                         = checked_add( static_cast<size_t>( ManagerAccess::kBlockHdrGranules ),
                                                               static_cast<size_t>( data_gran_need ) );
        if ( !need_grans )
            return false;
        auto need_grans_total = checked_add( *need_grans, static_cast<size_t>( ManagerAccess::kBlockHdrGranules ) );
        if ( !need_grans_total )
            return false;
        auto min_need = checked_mul( *need_grans_total, kGranSz );
        if ( !min_need )
            return false;
        std::optional<size_t> target_size =
            compute_growth_for_traits<address_traits>( old_size, *min_need, ManagerAccess::kGrowNumerator,
                                                       ManagerAccess::kGrowDenominator, ManagerAccess::kMaxMemoryGB );
        if ( !target_size.has_value() )
            return false;
        if ( !backend.resize_to( *target_size ) )
            return false;
        uint8_t* new_base = backend.base_ptr();
        size_t   new_size = backend.total_size();
        if ( new_base == nullptr || new_size <= old_size )
            return false;
        logging_policy::on_expand( old_size, new_size );
        hdr                     = ManagerAccess::get_header( new_base );
        auto extra_idx_opt      = byte_off_to_idx_checked<address_traits>( old_size );
        auto new_total_gran_opt = byte_off_to_idx_checked<address_traits>( new_size );
        if ( !extra_idx_opt.has_value() || !new_total_gran_opt.has_value() )
            return false;
        index_type extra_idx      = *extra_idx_opt;
        index_type new_total_gran = *new_total_gran_opt;
        size_t     extra_size     = new_size - old_size;
        void*      last_blk_raw =
            ( hdr->last_block_offset != address_traits::no_block )
                     ? static_cast<void*>( new_base + static_cast<size_t>( hdr->last_block_offset ) * kGranSz )
                     : nullptr;
        if ( last_blk_raw != nullptr && pmm::is_free( BlockState::get_node_type( last_blk_raw ) ) )
        {
            Block<address_traits>* last_blk = reinterpret_cast<Block<address_traits>*>( last_blk_raw );
            index_type             loff     = block_idx_t<address_traits>( new_base, last_blk );
            free_block_tree::remove( new_base, hdr, loff );
            BlockState::set_weight_of( last_blk_raw, static_cast<index_type>( new_total_gran - loff ) );
            hdr->total_size = new_size;
            free_block_tree::insert( new_base, hdr, loff );
        }
        else
        {
            if ( extra_size < sizeof( Block<address_traits> ) + kGranSz )
                return false;
            void*      nb_blk       = new_base + static_cast<size_t>( extra_idx ) * kGranSz;
            index_type new_blk_gran = static_cast<index_type>( new_total_gran - extra_idx );
            std::memset( nb_blk, 0, sizeof( Block<address_traits> ) );
            if ( last_blk_raw != nullptr )
            {
                Block<address_traits>* last_blk = reinterpret_cast<Block<address_traits>*>( last_blk_raw );
                index_type             loff     = block_idx_t<address_traits>( new_base, last_blk );
                BlockState::init_fields( nb_blk, loff, address_traits::no_block, 1, new_blk_gran, 0, NodeType::Free );
                BlockState::set_next_offset_of( last_blk_raw, static_cast<index_type>( extra_idx ) );
            }
            else
            {
                BlockState::init_fields( nb_blk, address_traits::no_block, address_traits::no_block, 1, new_blk_gran, 0,
                                         NodeType::Free );
                hdr->first_block_offset = extra_idx;
            }
            hdr->last_block_offset = extra_idx;
            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += ManagerAccess::kBlockHdrGranules;
            hdr->total_size = new_size;
            free_block_tree::insert( new_base, hdr, extra_idx );
        }
        return true;
    }
};
}
