/**
 * @file pmm/free_block_tree.h
 * @brief FreeBlockTree — AVL tree policy for free blocks (Issue #87 Phase 4, #95, #129).
 *
 * Defines C++20 concept `FreeBlockTreePolicyConcept<Policy>` for free block tree policy
 * and provides the standard implementation `AvlFreeTree<AddressTraits>`.
 *
 * The `FreeBlockTreePolicyConcept<Policy>` concept requires three static methods:
 *   - `insert(base, hdr, blk_idx)`       — add block to tree
 *   - `remove(base, hdr, blk_idx)`       — remove block from tree
 *   - `find_best_fit(base, hdr, needed)` — find smallest suitable block
 *
 * Issue #95: AVL tree implementation moved here from persist_avl_tree.h
 * to consolidate all PMM code under include/pmm/.
 *
 * Issue #106: AvlFreeTree uses Block<AddressTraitsT> layout (BlockState machine).
 * All field accesses go through Block<A> (prev/next/left/right/parent/avl_height/weight/root_offset).
 * Issue #112: BlockHeader struct removed — Block<A> is the sole block type.
 *
 * @see plan_issue87.md "Phase 4: FreeBlockTree as template policy"
 * @see block_state.h — BlockState machine (Issue #93, #106)
 * @see avl_tree_mixin.h — shared AVL operations via BlockPPtr adapter (Issue #188)
 * @version 1.5 (Issue #188 — deduplicate AVL operations via BlockPPtr adapter)
 */

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

/**
 * @brief C++20 концепт: проверяет, является ли Policy корректной политикой дерева свободных блоков
 * для конкретного AddressTraitsT.
 *
 * Policy должна предоставлять три статических метода:
 *   - `insert(uint8_t* base, ManagerHeader<AT>* hdr, AT::index_type blk_idx)   -> void`
 *   - `remove(uint8_t* base, ManagerHeader<AT>* hdr, AT::index_type blk_idx)   -> void`
 *   - `find_best_fit(uint8_t* base, ManagerHeader<AT>* hdr, AT::index_type n)  -> AT::index_type`
 *
 * Issue #175: ManagerHeader is now templated on AddressTraitsT, so the concept must be
 * checked against the specific AT used — not hardcoded to DefaultAddressTraits.
 *
 * @tparam Policy         Тип, проверяемый на соответствие концепту.
 * @tparam AddressTraitsT Traits адресного пространства (определяет типы индексов).
 */
template <typename Policy, typename AddressTraitsT>
concept FreeBlockTreePolicyForTraitsConcept = requires( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr,
                                                        typename AddressTraitsT::index_type idx ) {
    { Policy::insert( base, hdr, idx ) };
    { Policy::remove( base, hdr, idx ) };
    { Policy::find_best_fit( base, hdr, idx ) } -> std::convertible_to<typename AddressTraitsT::index_type>;
};

/**
 * @brief C++20 концепт: проверяет, является ли Policy корректной политикой дерева свободных блоков.
 *
 * Backward-compatibility variant checked against DefaultAddressTraits (uint32_t, 16B granule).
 * For checking against a specific AddressTraitsT, use FreeBlockTreePolicyForTraitsConcept<Policy, AT>.
 *
 * @tparam Policy  Тип, проверяемый на соответствие концепту.
 */
template <typename Policy>
concept FreeBlockTreePolicyConcept = FreeBlockTreePolicyForTraitsConcept<Policy, DefaultAddressTraits>;

/**
 * @brief Вспомогательная переменная: true если Policy удовлетворяет FreeBlockTreePolicyConcept.
 *
 * @tparam Policy  Тип, проверяемый на соответствие концепту.
 */
template <typename Policy> inline constexpr bool is_free_block_tree_policy_v = FreeBlockTreePolicyConcept<Policy>;

/**
 * @brief Standard AVL tree implementation for free block tree policy.
 *
 * All-static class for AVL tree of free blocks (Issue #73 FR-02, AR-03).
 * Does not depend on PersistMemoryManager singleton — takes base_ptr and header as context.
 *
 * Sort key: (total_granules, block_index) — strict ordering.
 * Best-fit search runs in O(log n).
 *
 * Issue #106/#112: Uses Block<AddressTraitsT> layout (BlockHeader struct removed).
 * Issue #126: Fields reordered — weight moved to first TreeNode field.
 * Issue #188: Rotation, rebalancing, and min_node operations delegate to shared
 *   AVL functions via BlockPPtr adapter, eliminating ~120 lines of duplicate code.
 *
 * @tparam AddressTraitsT  Address space traits (compatible with DefaultAddressTraits).
 */
template <typename AddressTraitsT = DefaultAddressTraits> struct AvlFreeTree
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;
    using BlockT         = Block<AddressTraitsT>;
    using BlockState     = BlockStateBase<AddressTraitsT>;
    using BPPtr          = detail::BlockPPtr<AddressTraitsT>;

    AvlFreeTree()                                = delete;
    AvlFreeTree( const AvlFreeTree& )            = delete;
    AvlFreeTree& operator=( const AvlFreeTree& ) = delete;

    /// @brief Insert block into AVL tree of free blocks.
    static void insert( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type blk_idx )
    {
        void* blk = detail::block_at<AddressTraitsT>( base, blk_idx );
        BlockState::set_left_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_right_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_parent_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_avl_height_of( blk, 1 );
        if ( hdr->free_tree_root == AddressTraitsT::no_block )
        {
            hdr->free_tree_root = blk_idx;
            return;
        }
        // Issue #59: cache total_gran once; compute blk size in granules before the traversal loop
        // Issue #146: use AddressTraitsT::granule_size for correct byte→granule conversion.
        index_type total_gran = detail::byte_off_to_idx_t<AddressTraitsT>( hdr->total_size );
        index_type blk_next   = BlockState::get_next_offset( blk );
        index_type blk_gran =
            ( blk_next != AddressTraitsT::no_block ) ? ( blk_next - blk_idx ) : ( total_gran - blk_idx );
        index_type cur = hdr->free_tree_root, parent = AddressTraitsT::no_block;
        bool       go_left = false;
        while ( cur != AddressTraitsT::no_block )
        {
            parent              = cur;
            const void* n       = detail::block_at<AddressTraitsT>( base, cur );
            index_type  n_next  = BlockState::get_next_offset( n );
            index_type  n_gran  = ( n_next != AddressTraitsT::no_block ) ? ( n_next - cur ) : ( total_gran - cur );
            bool        smaller = ( blk_gran < n_gran ) || ( blk_gran == n_gran && blk_idx < cur );
            go_left             = smaller;
            cur                 = smaller ? BlockState::get_left_offset( n ) : BlockState::get_right_offset( n );
        }
        BlockState::set_parent_offset_of( blk, parent );
        if ( go_left )
            BlockState::set_left_offset_of( detail::block_at<AddressTraitsT>( base, parent ), blk_idx );
        else
            BlockState::set_right_offset_of( detail::block_at<AddressTraitsT>( base, parent ), blk_idx );
        // Issue #188: delegate rebalancing to shared AVL function via BlockPPtr adapter.
        detail::avl_rebalance_up( BPPtr( base, parent ), hdr->free_tree_root );
    }

    /// @brief Remove block from AVL tree of free blocks.
    static void remove( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type blk_idx )
    {
        void*      blk    = detail::block_at<AddressTraitsT>( base, blk_idx );
        index_type parent = BlockState::get_parent_offset( blk );
        index_type left   = BlockState::get_left_offset( blk );
        index_type right  = BlockState::get_right_offset( blk );
        index_type rebal  = AddressTraitsT::no_block;

        if ( left == AddressTraitsT::no_block && right == AddressTraitsT::no_block )
        {
            set_child( base, hdr, parent, blk_idx, AddressTraitsT::no_block );
            rebal = parent;
        }
        else if ( left == AddressTraitsT::no_block || right == AddressTraitsT::no_block )
        {
            index_type child = ( left != AddressTraitsT::no_block ) ? left : right;
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, child ), parent );
            set_child( base, hdr, parent, blk_idx, child );
            rebal = parent;
        }
        else
        {
            // Issue #188: delegate min_node to shared AVL function via BlockPPtr.
            BPPtr      succ        = detail::avl_min_node( BPPtr( base, right ) );
            index_type succ_idx    = succ.offset();
            void*      succ_raw    = detail::block_at<AddressTraitsT>( base, succ_idx );
            index_type succ_parent = BlockState::get_parent_offset( succ_raw );
            index_type succ_right  = BlockState::get_right_offset( succ_raw );

            if ( succ_parent != blk_idx )
            {
                set_child( base, hdr, succ_parent, succ_idx, succ_right );
                if ( succ_right != AddressTraitsT::no_block )
                    BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, succ_right ),
                                                      succ_parent );
                BlockState::set_right_offset_of( succ_raw, right );
                BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, right ), succ_idx );
                rebal = succ_parent;
            }
            else
            {
                rebal = succ_idx;
            }
            BlockState::set_left_offset_of( succ_raw, left );
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, left ), succ_idx );
            BlockState::set_parent_offset_of( succ_raw, parent );
            set_child( base, hdr, parent, blk_idx, succ_idx );
            // Issue #188: delegate height update to shared AVL function via BlockPPtr.
            detail::avl_update_height( BPPtr( base, succ_idx ) );
        }
        BlockState::set_left_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_right_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_parent_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_avl_height_of( blk, 0 );
        // Issue #188: delegate rebalancing to shared AVL function via BlockPPtr adapter.
        detail::avl_rebalance_up( BPPtr( base, rebal ), hdr->free_tree_root );
    }

    /// @brief Find smallest block >= needed granules (best-fit, O(log n)).
    static index_type find_best_fit( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr,
                                     index_type needed_granules )
    {
        // Issue #59: cache total_gran once to avoid repeated hdr->total_size reads in the hot path
        // Issue #146: use AddressTraitsT::granule_size for correct byte→granule conversion.
        index_type total_gran = detail::byte_off_to_idx_t<AddressTraitsT>( hdr->total_size );
        index_type cur = hdr->free_tree_root, result = AddressTraitsT::no_block;
        while ( cur != AddressTraitsT::no_block )
        {
            const void* node      = detail::block_at<AddressTraitsT>( base, cur );
            index_type  node_next = BlockState::get_next_offset( node );
            // Issue #146: compare against AddressTraitsT::no_block (not detail::kNoBlock)
            // to correctly handle SmallAddressTraits (uint16_t) sentinel 0xFFFF.
            index_type cur_gran =
                ( node_next != AddressTraitsT::no_block ) ? ( node_next - cur ) : ( total_gran - cur );
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
    /// @brief Update parent → child link in tree.
    static void set_child( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type parent,
                           index_type old_child, index_type new_child )
    {
        if ( parent == AddressTraitsT::no_block )
        {
            hdr->free_tree_root = new_child;
            return;
        }
        void* p = detail::block_at<AddressTraitsT>( base, parent );
        if ( BlockState::get_left_offset( p ) == old_child )
            BlockState::set_left_offset_of( p, new_child );
        else
            BlockState::set_right_offset_of( p, new_child );
    }
};

// ─── Static_assert: AvlFreeTree satisfies the concept ────────────────────────

static_assert( is_free_block_tree_policy_v<AvlFreeTree<DefaultAddressTraits>>,
               "AvlFreeTree<DefaultAddressTraits> must satisfy FreeBlockTreePolicy" );

/// @brief Backward compatibility alias for PersistentAvlTree.
/// Deprecated: Use AvlFreeTree<DefaultAddressTraits> instead.
using PersistentAvlTree = AvlFreeTree<DefaultAddressTraits>;

} // namespace pmm
