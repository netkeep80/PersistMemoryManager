/**
 * @file pmm/layout.h
 * @brief Stateless helpers for manager layout initialization and expansion.
 */

#pragma once

#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

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

    static bool init_layout( storage_backend& backend, std::uint8_t* base, std::size_t size ) noexcept
    {
        static constexpr index_type  kHdrBlkIdx  = 0;
        static constexpr index_type  kFreeBlkIdx = ManagerAccess::kFreeBlkIdxLayout;
        static constexpr std::size_t kGranSz     = address_traits::granule_size;

        static constexpr std::size_t kMinBlockDataSize = kGranSz;
        if ( static_cast<std::size_t>( kFreeBlkIdx ) * kGranSz + sizeof( Block<address_traits> ) + kMinBlockDataSize >
             size )
            return false;

        void* hdr_blk = base;
        std::memset( hdr_blk, 0, ManagerAccess::kBlockHdrByteSize );
        BlockState::init_fields( hdr_blk,
                                 /*prev*/ address_traits::no_block,
                                 /*next*/ kFreeBlkIdx,
                                 /*avl_height*/ 0,
                                 /*weight*/ ManagerAccess::kMgrHdrGranules,
                                 /*root_offset*/ kHdrBlkIdx );

        ManagerHeader<address_traits>* hdr = ManagerAccess::get_header( base );
        std::memset( hdr, 0, sizeof( ManagerHeader<address_traits> ) );
        hdr->magic              = ManagerAccess::kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = kHdrBlkIdx;
        hdr->last_block_offset  = address_traits::no_block;
        hdr->free_tree_root     = address_traits::no_block;
        hdr->granule_size       = static_cast<std::uint16_t>( kGranSz );
        hdr->root_offset        = address_traits::no_block;

        void* blk = base + static_cast<std::size_t>( kFreeBlkIdx ) * kGranSz;
        std::memset( blk, 0, sizeof( Block<address_traits> ) );
        BlockState::init_fields( blk,
                                 /*prev*/ kHdrBlkIdx,
                                 /*next*/ address_traits::no_block,
                                 /*avl_height*/ 1,
                                 /*weight*/ 0,
                                 /*root_offset*/ 0 );

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

    static bool do_expand( storage_backend& backend, bool initialized, std::size_t user_size ) noexcept
    {
        if ( !initialized )
            return false;
        std::uint8_t*                  base     = backend.base_ptr();
        ManagerHeader<address_traits>* hdr      = ManagerAccess::get_header( base );
        std::size_t                    old_size = hdr->total_size;

        static constexpr std::size_t kGranSz        = address_traits::granule_size;
        index_type                   data_gran_need = bytes_to_granules_t<address_traits>( user_size );
        if ( data_gran_need == 0 )
            data_gran_need = 1;
        std::size_t min_need = static_cast<std::size_t>( ManagerAccess::kBlockHdrGranules + data_gran_need +
                                                         ManagerAccess::kBlockHdrGranules ) *
                               kGranSz;
        std::size_t growth = old_size / 4;
        if ( growth < min_need )
            growth = min_need;

        if ( !backend.expand( growth ) )
            return false;

        std::uint8_t* new_base = backend.base_ptr();
        std::size_t   new_size = backend.total_size();
        if ( new_base == nullptr || new_size <= old_size )
            return false;

        logging_policy::on_expand( old_size, new_size );
        hdr = ManagerAccess::get_header( new_base );

        index_type  extra_idx  = byte_off_to_idx_t<address_traits>( old_size );
        std::size_t extra_size = new_size - old_size;

        void* last_blk_raw =
            ( hdr->last_block_offset != address_traits::no_block )
                ? static_cast<void*>( new_base + static_cast<std::size_t>( hdr->last_block_offset ) * kGranSz )
                : nullptr;

        if ( last_blk_raw != nullptr && BlockState::get_weight( last_blk_raw ) == 0 )
        {
            Block<address_traits>* last_blk = reinterpret_cast<Block<address_traits>*>( last_blk_raw );
            index_type             loff     = block_idx_t<address_traits>( new_base, last_blk );
            free_block_tree::remove( new_base, hdr, loff );
            hdr->total_size = new_size;
            free_block_tree::insert( new_base, hdr, loff );
        }
        else
        {
            if ( extra_size < sizeof( Block<address_traits> ) + kGranSz )
                return false;
            void* nb_blk = new_base + static_cast<std::size_t>( extra_idx ) * kGranSz;
            std::memset( nb_blk, 0, sizeof( Block<address_traits> ) );
            if ( last_blk_raw != nullptr )
            {
                Block<address_traits>* last_blk = reinterpret_cast<Block<address_traits>*>( last_blk_raw );
                index_type             loff     = block_idx_t<address_traits>( new_base, last_blk );
                BlockState::init_fields( nb_blk,
                                         /*prev*/ loff,
                                         /*next*/ address_traits::no_block,
                                         /*avl_height*/ 1,
                                         /*weight*/ 0,
                                         /*root_offset*/ 0 );
                BlockState::set_next_offset_of( last_blk_raw, static_cast<index_type>( extra_idx ) );
            }
            else
            {
                BlockState::init_fields( nb_blk,
                                         /*prev*/ address_traits::no_block,
                                         /*next*/ address_traits::no_block,
                                         /*avl_height*/ 1,
                                         /*weight*/ 0,
                                         /*root_offset*/ 0 );
                hdr->first_block_offset = extra_idx;
            }
            hdr->last_block_offset = extra_idx;
            hdr->block_count++;
            hdr->free_count++;
            hdr->total_size = new_size;
            free_block_tree::insert( new_base, hdr, extra_idx );
        }
        return true;
    }
};

} // namespace pmm::detail
