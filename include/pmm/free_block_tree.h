/**
 * @file pmm/free_block_tree.h
 * @brief AvlFreeTree — specialized forest-policy for the free-tree domain.
 *
 * Implements the free-tree as a specialized forest-policy within the AVL-forest model.
 * The free-tree is the primary system domain of the forest: it indexes free blocks
 * of the persistent address space (PAP) for best-fit allocation.
 *
 * ## Forest-policy: ordering and key derivation
 *
 * Sort key: (block_size_in_granules, block_index) — strict total ordering.
 * The sort key is **derived** from linear PAP geometry (next_offset - block_index),
 * not read from the `weight` field. In the free-tree domain, `weight == 0` serves
 * as a state discriminator (free vs. allocated), not as a sort key.
 *
 * This is an explicit forest-policy choice: the free-tree derives its ordering
 * from physical block layout, while other forest domains (pstringview, pmap)
 * use `weight` directly as their sort key. Both approaches are consistent
 * with the general forest model, which allows each domain to define its own
 * interpretation of `weight` and ordering semantics.
 *
 * @see docs/free_tree_forest_policy.md — canonical free-tree forest-policy document
 * @see docs/pmm_avl_forest.md — general forest model
 * @see docs/block_and_treenode_semantics.md — field semantics
 * @see block_state.h — BlockState machine
 * @see avl_tree_mixin.h — shared AVL operations via BlockPPtr adapter
 * @version 1.6
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
 * @brief C++20 concept: validates that Policy is a correct free-tree forest-policy
 * for a specific AddressTraitsT.
 *
 * A free-tree forest-policy must provide three static methods:
 *   - `insert(uint8_t* base, ManagerHeader<AT>* hdr, AT::index_type blk_idx)   -> void`
 *   - `remove(uint8_t* base, ManagerHeader<AT>* hdr, AT::index_type blk_idx)   -> void`
 *   - `find_best_fit(uint8_t* base, ManagerHeader<AT>* hdr, AT::index_type n)  -> AT::index_type`
 *
 * The ordering policy (sort key, key derivation, weight interpretation) is
 * determined by the concrete policy type. See AvlFreeTree for the standard policy.
 *
 * @tparam Policy         Type to check against the concept.
 * @tparam AddressTraitsT Address space traits (defines index types).
 */
template <typename Policy, typename AddressTraitsT>
concept FreeBlockTreePolicyForTraitsConcept = requires( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr,
                                                        typename AddressTraitsT::index_type idx ) {
    { Policy::insert( base, hdr, idx ) };
    { Policy::remove( base, hdr, idx ) };
    { Policy::find_best_fit( base, hdr, idx ) } -> std::convertible_to<typename AddressTraitsT::index_type>;
};

/**
 * @brief C++20 concept: validates that Policy is a correct free-tree forest-policy.
 *
 * Backward-compatibility variant checked against DefaultAddressTraits (uint32_t, 16B granule).
 * For checking against a specific AddressTraitsT, use FreeBlockTreePolicyForTraitsConcept<Policy, AT>.
 *
 * @tparam Policy  Type to check against the concept.
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
 * @brief Specialized forest-policy: AVL tree for the free-tree domain.
 *
 * All-static class implementing the free-tree forest-policy.
 * Does not depend on PersistMemoryManager singleton — takes base_ptr and header as context.
 *
 * Forest-policy details:
 *   - Sort key: (block_size_in_granules, block_index) — strict total ordering.
 *   - Block size is derived from linear PAP geometry (next_offset - block_index).
 *   - `weight` is NOT used as sort key (it is 0 for free blocks — state discriminator).
 *   - Best-fit search runs in O(log n).
 *   - Uses shared AVL substrate via BlockPPtr adapter.
 *
 * @see docs/free_tree_forest_policy.md — canonical ordering policy documentation
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

    /// Forest-policy tag: identifies this as the free-tree domain policy.
    static constexpr const char* kForestDomainName = "system/free_tree";

    AvlFreeTree()                                = delete;
    AvlFreeTree( const AvlFreeTree& )            = delete;
    AvlFreeTree& operator=( const AvlFreeTree& ) = delete;

    /// @brief Insert block into the free-tree (forest domain: system/free_tree).
    ///
    /// Derives sort key (block_size_in_granules, block_index) from linear PAP geometry.
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
        // Derive sort key from linear PAP geometry: block_size = next_offset - block_index.
        // Use AddressTraitsT::granule_size for correct byte→granule conversion.
        index_type total_gran = detail::byte_off_to_idx_t<AddressTraitsT>( hdr->total_size );
        index_type blk_next   = BlockState::get_next_offset( blk );
        index_type blk_gran =
            ( blk_next != AddressTraitsT::no_block ) ? ( blk_next - blk_idx ) : ( total_gran - blk_idx );
        index_type cur = hdr->free_tree_root, parent = AddressTraitsT::no_block;
        bool       go_left = false;
        while ( cur != AddressTraitsT::no_block )
        {
            parent             = cur;
            const void* n      = detail::block_at<AddressTraitsT>( base, cur );
            index_type  n_next = BlockState::get_next_offset( n );
            index_type  n_gran = ( n_next != AddressTraitsT::no_block ) ? ( n_next - cur ) : ( total_gran - cur );
            // Forest-policy ordering: (block_size, block_index) — smaller size goes left.
            bool smaller = ( blk_gran < n_gran ) || ( blk_gran == n_gran && blk_idx < cur );
            go_left      = smaller;
            cur          = smaller ? BlockState::get_left_offset( n ) : BlockState::get_right_offset( n );
        }
        BlockState::set_parent_offset_of( blk, parent );
        if ( go_left )
            BlockState::set_left_offset_of( detail::block_at<AddressTraitsT>( base, parent ), blk_idx );
        else
            BlockState::set_right_offset_of( detail::block_at<AddressTraitsT>( base, parent ), blk_idx );
        // Delegate rebalancing to shared AVL function via BlockPPtr adapter.
        detail::avl_rebalance_up( BPPtr( base, parent ), hdr->free_tree_root );
    }

    /// @brief Remove block from the free-tree (forest domain: system/free_tree).
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
            // Delegate min_node to shared AVL function via BlockPPtr.
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
            // Delegate height update to shared AVL function via BlockPPtr.
            detail::avl_update_height( BPPtr( base, succ_idx ) );
        }
        BlockState::set_left_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_right_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_parent_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_avl_height_of( blk, 0 );
        // Delegate rebalancing to shared AVL function via BlockPPtr adapter.
        detail::avl_rebalance_up( BPPtr( base, rebal ), hdr->free_tree_root );
    }

    /// @brief Best-fit search: find smallest free block >= needed granules (O(log n)).
    ///
    /// Derives sort key from linear PAP geometry at each node during traversal.
    static index_type find_best_fit( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr,
                                     index_type needed_granules )
    {
        // Cache total_gran once to avoid repeated hdr->total_size reads in the hot path
        // Use AddressTraitsT::granule_size for correct byte→granule conversion.
        index_type total_gran = detail::byte_off_to_idx_t<AddressTraitsT>( hdr->total_size );
        index_type cur = hdr->free_tree_root, result = AddressTraitsT::no_block;
        while ( cur != AddressTraitsT::no_block )
        {
            const void* node      = detail::block_at<AddressTraitsT>( base, cur );
            index_type  node_next = BlockState::get_next_offset( node );
            // Compare against AddressTraitsT::no_block (not detail::kNoBlock)
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
