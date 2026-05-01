#pragma once
#include "pmm/avl_tree_mixin.h"
#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/types.h"
#include <concepts>
#include <cstdint>
#include <type_traits>
namespace pmm
{
template <typename Policy, typename AT>
concept FreeBlockTreePolicyForTraitsConcept =
    requires( uint8_t* base, detail::ManagerHeader<AT>* hdr, typename AT::index_type idx ) {
        { Policy::insert( base, hdr, idx ) };
        { Policy::remove( base, hdr, idx ) };
        { Policy::find_best_fit( base, hdr, idx ) } -> std::convertible_to<typename AT::index_type>;
    };
/*
## pmm-avlfreetree
req: feat-002, fr-004, fr-013, dr-005, dr-013, qa-perf-001, dr-009, dr-017, qa-maint-002
*/
template <typename AT = DefaultAddressTraits> struct AvlFreeTree
{
    using address_traits                           = AT;
    using index_type                               = typename AT::index_type;
    using BlockT                                   = Block<AT>;
    using BlockState                               = BlockStateBase<AT>;
    using BPPtr                                    = detail::BlockPPtr<AT>;
    static constexpr const char* kForestDomainName = "system/free_tree";
    AvlFreeTree()                                  = delete;
    AvlFreeTree( const AvlFreeTree& )              = delete;
    AvlFreeTree& operator=( const AvlFreeTree& )   = delete;
    static void  insert( uint8_t* base, detail::ManagerHeader<AT>* hdr, index_type blk_idx )
    {
        void* blk = detail::block_at<AT>( base, blk_idx );
        BlockState::set_left_offset_of( blk, AT::no_block );
        BlockState::set_right_offset_of( blk, AT::no_block );
        BlockState::set_parent_offset_of( blk, AT::no_block );
        BlockState::set_avl_height_of( blk, 1 );
        if ( hdr->free_tree_root == AT::no_block )
        {
            hdr->free_tree_root = blk_idx;
            return;
        }
        index_type blk_gran = BlockState::get_weight( blk );
        index_type cur = hdr->free_tree_root, parent = AT::no_block;
        bool       go_left = false;
        while ( cur != AT::no_block )
        {
            parent              = cur;
            const void* n       = detail::block_at<AT>( base, cur );
            index_type  n_gran  = BlockState::get_weight( n );
            bool        smaller = ( blk_gran < n_gran ) || ( blk_gran == n_gran && blk_idx < cur );
            go_left             = smaller;
            cur                 = smaller ? BlockState::get_left_offset( n ) : BlockState::get_right_offset( n );
        }
        BlockState::set_parent_offset_of( blk, parent );
        if ( go_left )
            BlockState::set_left_offset_of( detail::block_at<AT>( base, parent ), blk_idx );
        else
            BlockState::set_right_offset_of( detail::block_at<AT>( base, parent ), blk_idx );
        detail::avl_rebalance_up( BPPtr( base, parent ), hdr->free_tree_root );
    }
    static void remove( uint8_t* base, detail::ManagerHeader<AT>* hdr, index_type blk_idx )
    {
        void*      blk    = detail::block_at<AT>( base, blk_idx );
        index_type parent = BlockState::get_parent_offset( blk );
        index_type left   = BlockState::get_left_offset( blk );
        index_type right  = BlockState::get_right_offset( blk );
        index_type rebal  = AT::no_block;
        if ( left == AT::no_block && right == AT::no_block )
        {
            set_child( base, hdr, parent, blk_idx, AT::no_block );
            rebal = parent;
        }
        else if ( left == AT::no_block || right == AT::no_block )
        {
            index_type child = ( left != AT::no_block ) ? left : right;
            BlockState::set_parent_offset_of( detail::block_at<AT>( base, child ), parent );
            set_child( base, hdr, parent, blk_idx, child );
            rebal = parent;
        }
        else
        {
            BPPtr      succ        = detail::avl_min_node( BPPtr( base, right ) );
            index_type succ_idx    = succ.offset();
            void*      succ_raw    = detail::block_at<AT>( base, succ_idx );
            index_type succ_parent = BlockState::get_parent_offset( succ_raw );
            index_type succ_right  = BlockState::get_right_offset( succ_raw );
            if ( succ_parent != blk_idx )
            {
                set_child( base, hdr, succ_parent, succ_idx, succ_right );
                if ( succ_right != AT::no_block )
                    BlockState::set_parent_offset_of( detail::block_at<AT>( base, succ_right ), succ_parent );
                BlockState::set_right_offset_of( succ_raw, right );
                BlockState::set_parent_offset_of( detail::block_at<AT>( base, right ), succ_idx );
                rebal = succ_parent;
            }
            else
            {
                rebal = succ_idx;
            }
            BlockState::set_left_offset_of( succ_raw, left );
            BlockState::set_parent_offset_of( detail::block_at<AT>( base, left ), succ_idx );
            BlockState::set_parent_offset_of( succ_raw, parent );
            set_child( base, hdr, parent, blk_idx, succ_idx );
            detail::avl_update_height( BPPtr( base, succ_idx ) );
        }
        BlockState::set_left_offset_of( blk, AT::no_block );
        BlockState::set_right_offset_of( blk, AT::no_block );
        BlockState::set_parent_offset_of( blk, AT::no_block );
        BlockState::set_avl_height_of( blk, 0 );
        detail::avl_rebalance_up( BPPtr( base, rebal ), hdr->free_tree_root );
    }
/*
### pmm-avlfreetree-find_best_fit
*/
    static index_type find_best_fit( uint8_t* base, detail::ManagerHeader<AT>* hdr, index_type needed_granules )
    {
        (void)hdr;
        index_type cur = hdr->free_tree_root, result = AT::no_block;
        while ( cur != AT::no_block )
        {
            const void* node     = detail::block_at<AT>( base, cur );
            index_type  cur_gran = BlockState::get_weight( node );
            if ( cur_gran >= needed_granules )
            {
                result = cur;
                cur    = BlockState::get_left_offset( node );
            }
            else
            {
                cur = BlockState::get_right_offset( node );
            }
        }
        return result;
    }

  private:
    static void set_child( uint8_t* base, detail::ManagerHeader<AT>* hdr, index_type parent, index_type old_child,
                           index_type new_child )
    {
        if ( parent == AT::no_block )
        {
            hdr->free_tree_root = new_child;
            return;
        }
        void* p = detail::block_at<AT>( base, parent );
        if ( BlockState::get_left_offset( p ) == old_child )
            BlockState::set_left_offset_of( p, new_child );
        else
            BlockState::set_right_offset_of( p, new_child );
    }
};
static_assert( FreeBlockTreePolicyForTraitsConcept<AvlFreeTree<DefaultAddressTraits>, DefaultAddressTraits>, "" );
}
