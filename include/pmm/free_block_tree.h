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
 * @version 1.3 (Issue #129 — переход на C++20 концепты)
 */

#pragma once

#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/types.h"

#include <cassert>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace pmm
{

/**
 * @brief C++20 концепт: проверяет, является ли Policy корректной политикой дерева свободных блоков.
 *
 * Policy должна предоставлять три статических метода:
 *   - `insert(uint8_t* base, ManagerHeader* hdr, uint32_t blk_idx)   -> void`
 *   - `remove(uint8_t* base, ManagerHeader* hdr, uint32_t blk_idx)   -> void`
 *   - `find_best_fit(uint8_t* base, ManagerHeader* hdr, uint32_t n)  -> uint32_t`
 *
 * @tparam Policy  Тип, проверяемый на соответствие концепту.
 */
template <typename Policy>
concept FreeBlockTreePolicyConcept = requires( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t idx ) {
    { Policy::insert( base, hdr, idx ) };
    { Policy::remove( base, hdr, idx ) };
    { Policy::find_best_fit( base, hdr, idx ) } -> std::convertible_to<std::uint32_t>;
};

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
 * Fields: prev_offset(0), next_offset(4), weight(8), left_offset(12), right_offset(16),
 *         parent_offset(20), root_offset(24), avl_height(28), node_type(30).
 *
 * @tparam AddressTraitsT  Address space traits (compatible with DefaultAddressTraits).
 */
template <typename AddressTraitsT = DefaultAddressTraits> struct AvlFreeTree
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;
    using BlockT         = Block<AddressTraitsT>;
    using BlockState     = BlockStateBase<AddressTraitsT>;

    AvlFreeTree()                                = delete;
    AvlFreeTree( const AvlFreeTree& )            = delete;
    AvlFreeTree& operator=( const AvlFreeTree& ) = delete;

    /// @brief Insert block into AVL tree of free blocks.
    static void insert( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t blk_idx )
    {
        void* blk = detail::block_at<AddressTraitsT>( base, blk_idx );
        BlockState::set_left_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_right_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_parent_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_avl_height_of( blk, 1 );
        if ( hdr->free_tree_root == detail::kNoBlock )
        {
            hdr->free_tree_root = blk_idx;
            return;
        }
        // Issue #59: cache total_gran once; compute blk size in granules before the traversal loop
        // Issue #146: use AddressTraitsT::granule_size for correct byte→granule conversion.
        std::uint32_t total_gran = detail::byte_off_to_idx_t<AddressTraitsT>( hdr->total_size );
        index_type    blk_next   = BlockState::get_next_offset( blk );
        std::uint32_t blk_gran   = ( blk_next != AddressTraitsT::no_block )
                                       ? static_cast<std::uint32_t>( blk_next ) - blk_idx
                                       : ( total_gran - blk_idx );
        std::uint32_t cur = hdr->free_tree_root, parent = detail::kNoBlock;
        bool          go_left = false;
        while ( cur != detail::kNoBlock )
        {
            parent                = cur;
            const void*   n       = detail::block_at<AddressTraitsT>( base, cur );
            index_type    n_next  = BlockState::get_next_offset( n );
            std::uint32_t n_gran  = ( n_next != AddressTraitsT::no_block ) ? static_cast<std::uint32_t>( n_next ) - cur
                                                                           : ( total_gran - cur );
            bool          smaller = ( blk_gran < n_gran ) || ( blk_gran == n_gran && blk_idx < cur );
            go_left               = smaller;
            // Issue #146: convert index_type result to uint32_t using sentinel-aware translation.
            cur = detail::to_u32_idx<AddressTraitsT>( smaller ? BlockState::get_left_offset( n )
                                                              : BlockState::get_right_offset( n ) );
        }
        // Issue #146: use sentinel-aware from_u32_idx when storing parent index as index_type.
        BlockState::set_parent_offset_of( blk, detail::from_u32_idx<AddressTraitsT>( parent ) );
        if ( go_left )
            BlockState::set_left_offset_of( detail::block_at<AddressTraitsT>( base, parent ),
                                            static_cast<index_type>( blk_idx ) );
        else
            BlockState::set_right_offset_of( detail::block_at<AddressTraitsT>( base, parent ),
                                             static_cast<index_type>( blk_idx ) );
        rebalance_up( base, hdr, parent );
    }

    /// @brief Remove block from AVL tree of free blocks.
    static void remove( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t blk_idx )
    {
        void* blk = detail::block_at<AddressTraitsT>( base, blk_idx );
        // Issue #146: convert index_type to uint32_t with sentinel translation.
        std::uint32_t parent = detail::to_u32_idx<AddressTraitsT>( BlockState::get_parent_offset( blk ) );
        std::uint32_t left   = detail::to_u32_idx<AddressTraitsT>( BlockState::get_left_offset( blk ) );
        std::uint32_t right  = detail::to_u32_idx<AddressTraitsT>( BlockState::get_right_offset( blk ) );
        std::uint32_t rebal  = detail::kNoBlock;

        if ( left == detail::kNoBlock && right == detail::kNoBlock )
        {
            set_child( base, hdr, parent, blk_idx, detail::kNoBlock );
            rebal = parent;
        }
        else if ( left == detail::kNoBlock || right == detail::kNoBlock )
        {
            std::uint32_t child = ( left != detail::kNoBlock ) ? left : right;
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, child ),
                                              static_cast<index_type>( parent ) );
            set_child( base, hdr, parent, blk_idx, child );
            rebal = parent;
        }
        else
        {
            std::uint32_t succ_idx    = min_node( base, right );
            void*         succ        = detail::block_at<AddressTraitsT>( base, succ_idx );
            std::uint32_t succ_parent = detail::to_u32_idx<AddressTraitsT>( BlockState::get_parent_offset( succ ) );
            std::uint32_t succ_right  = detail::to_u32_idx<AddressTraitsT>( BlockState::get_right_offset( succ ) );

            if ( succ_parent != blk_idx )
            {
                set_child( base, hdr, succ_parent, succ_idx, succ_right );
                if ( succ_right != detail::kNoBlock )
                    BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, succ_right ),
                                                      static_cast<index_type>( succ_parent ) );
                BlockState::set_right_offset_of( succ, static_cast<index_type>( right ) );
                BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, right ),
                                                  static_cast<index_type>( succ_idx ) );
                rebal = succ_parent;
            }
            else
            {
                rebal = succ_idx;
            }
            BlockState::set_left_offset_of( succ, static_cast<index_type>( left ) );
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, left ),
                                              static_cast<index_type>( succ_idx ) );
            BlockState::set_parent_offset_of( succ, static_cast<index_type>( parent ) );
            set_child( base, hdr, parent, blk_idx, succ_idx );
            update_height( base, succ_idx );
        }
        BlockState::set_left_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_right_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_parent_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_avl_height_of( blk, 0 );
        rebalance_up( base, hdr, rebal );
    }

    /// @brief Find smallest block >= needed granules (best-fit, O(log n)).
    static std::uint32_t find_best_fit( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t needed_granules )
    {
        // Issue #59: cache total_gran once to avoid repeated hdr->total_size reads in the hot path
        // Issue #146: use AddressTraitsT::granule_size for correct byte→granule conversion.
        std::uint32_t total_gran = detail::byte_off_to_idx_t<AddressTraitsT>( hdr->total_size );
        std::uint32_t cur = hdr->free_tree_root, result = detail::kNoBlock;
        while ( cur != detail::kNoBlock )
        {
            const void* node      = detail::block_at<AddressTraitsT>( base, cur );
            index_type  node_next = BlockState::get_next_offset( node );
            // Issue #146: compare against AddressTraitsT::no_block (not detail::kNoBlock)
            // to correctly handle SmallAddressTraits (uint16_t) sentinel 0xFFFF.
            std::uint32_t cur_gran = ( node_next != AddressTraitsT::no_block )
                                         ? static_cast<std::uint32_t>( node_next ) - cur
                                         : ( total_gran - cur );
            if ( cur_gran >= needed_granules )
            {
                result = cur;
                // Issue #146: use sentinel-aware translation when converting index_type to uint32_t.
                cur = detail::to_u32_idx<AddressTraitsT>( BlockState::get_left_offset( node ) );
            }
            else
            {
                cur = detail::to_u32_idx<AddressTraitsT>( BlockState::get_right_offset( node ) );
            }
        }
        return result;
    }

  private:
    static std::int32_t height( std::uint8_t* base, std::uint32_t idx )
    {
        return ( idx == detail::kNoBlock ) ? 0
                                           : static_cast<std::int32_t>( BlockState::get_avl_height(
                                                 detail::block_at<AddressTraitsT>( base, idx ) ) );
    }

    static void update_height( std::uint8_t* base, std::uint32_t node_idx )
    {
        void*        node = detail::block_at<AddressTraitsT>( base, node_idx );
        std::int32_t h =
            1 +
            ( std::max )( height( base, detail::to_u32_idx<AddressTraitsT>( BlockState::get_left_offset( node ) ) ),
                          height( base, detail::to_u32_idx<AddressTraitsT>( BlockState::get_right_offset( node ) ) ) );
        assert( h <= std::numeric_limits<std::int16_t>::max() ); // tree height must fit in int16_t
        BlockState::set_avl_height_of( node, static_cast<std::int16_t>( h ) );
    }

    static std::int32_t balance_factor( std::uint8_t* base, std::uint32_t node_idx )
    {
        const void* node = detail::block_at<AddressTraitsT>( base, node_idx );
        return height( base, detail::to_u32_idx<AddressTraitsT>( BlockState::get_left_offset( node ) ) ) -
               height( base, detail::to_u32_idx<AddressTraitsT>( BlockState::get_right_offset( node ) ) );
    }

    /// @brief Update parent → child link in tree.
    static void set_child( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t parent,
                           std::uint32_t old_child, std::uint32_t new_child )
    {
        if ( parent == detail::kNoBlock )
        {
            hdr->free_tree_root = new_child;
            return;
        }
        void* p = detail::block_at<AddressTraitsT>( base, parent );
        if ( detail::to_u32_idx<AddressTraitsT>( BlockState::get_left_offset( p ) ) == old_child )
            BlockState::set_left_offset_of( p, static_cast<index_type>( new_child ) );
        else
            BlockState::set_right_offset_of( p, static_cast<index_type>( new_child ) );
    }

    static std::uint32_t rotate_right( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t y_idx )
    {
        void*         y     = detail::block_at<AddressTraitsT>( base, y_idx );
        std::uint32_t x_idx = detail::to_u32_idx<AddressTraitsT>( BlockState::get_left_offset( y ) );
        void*         x     = detail::block_at<AddressTraitsT>( base, x_idx );
        std::uint32_t t2    = detail::to_u32_idx<AddressTraitsT>( BlockState::get_right_offset( x ) );
        // Capture y's parent before modifying x's parent_offset (Issue #146: use to_u32_idx).
        std::uint32_t y_parent = detail::to_u32_idx<AddressTraitsT>( BlockState::get_parent_offset( y ) );

        BlockState::set_right_offset_of( x, static_cast<index_type>( y_idx ) );
        BlockState::set_left_offset_of( y, static_cast<index_type>( t2 ) );
        // x takes y's old parent; y's new parent is x.
        BlockState::set_parent_offset_of( x, detail::from_u32_idx<AddressTraitsT>( y_parent ) );
        BlockState::set_parent_offset_of( y, static_cast<index_type>( x_idx ) );
        if ( t2 != detail::kNoBlock )
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, t2 ),
                                              static_cast<index_type>( y_idx ) );
        set_child( base, hdr, y_parent, y_idx, x_idx );
        update_height( base, y_idx );
        update_height( base, x_idx );
        return x_idx;
    }

    static std::uint32_t rotate_left( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t x_idx )
    {
        void*         x     = detail::block_at<AddressTraitsT>( base, x_idx );
        std::uint32_t y_idx = detail::to_u32_idx<AddressTraitsT>( BlockState::get_right_offset( x ) );
        void*         y     = detail::block_at<AddressTraitsT>( base, y_idx );
        std::uint32_t t2    = detail::to_u32_idx<AddressTraitsT>( BlockState::get_left_offset( y ) );
        // Capture x's parent before modifying y's parent_offset (Issue #146: use to_u32_idx).
        std::uint32_t x_parent = detail::to_u32_idx<AddressTraitsT>( BlockState::get_parent_offset( x ) );

        BlockState::set_left_offset_of( y, static_cast<index_type>( x_idx ) );
        BlockState::set_right_offset_of( x, static_cast<index_type>( t2 ) );
        // y takes x's old parent; x's new parent is y.
        BlockState::set_parent_offset_of( y, detail::from_u32_idx<AddressTraitsT>( x_parent ) );
        BlockState::set_parent_offset_of( x, static_cast<index_type>( y_idx ) );
        if ( t2 != detail::kNoBlock )
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, t2 ),
                                              static_cast<index_type>( x_idx ) );
        set_child( base, hdr, x_parent, x_idx, y_idx );
        update_height( base, x_idx );
        update_height( base, y_idx );
        return y_idx;
    }

    static void rebalance_up( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t node_idx )
    {
        std::uint32_t cur = node_idx;
        while ( cur != detail::kNoBlock )
        {
            update_height( base, cur );
            std::int32_t bf = balance_factor( base, cur );
            if ( bf > 1 )
            {
                void*         node     = detail::block_at<AddressTraitsT>( base, cur );
                std::uint32_t left_idx = detail::to_u32_idx<AddressTraitsT>( BlockState::get_left_offset( node ) );
                if ( balance_factor( base, left_idx ) < 0 )
                    rotate_left( base, hdr, left_idx );
                cur = rotate_right( base, hdr, cur );
            }
            else if ( bf < -1 )
            {
                void*         node      = detail::block_at<AddressTraitsT>( base, cur );
                std::uint32_t right_idx = detail::to_u32_idx<AddressTraitsT>( BlockState::get_right_offset( node ) );
                if ( balance_factor( base, right_idx ) > 0 )
                    rotate_right( base, hdr, right_idx );
                cur = rotate_left( base, hdr, cur );
            }
            cur = detail::to_u32_idx<AddressTraitsT>(
                BlockState::get_parent_offset( detail::block_at<AddressTraitsT>( base, cur ) ) );
        }
    }

    static std::uint32_t min_node( std::uint8_t* base, std::uint32_t node_idx )
    {
        while ( node_idx != detail::kNoBlock )
        {
            std::uint32_t left = detail::to_u32_idx<AddressTraitsT>(
                BlockState::get_left_offset( detail::block_at<AddressTraitsT>( base, node_idx ) ) );
            if ( left == detail::kNoBlock )
                break;
            node_idx = left;
        }
        return node_idx;
    }
};

// ─── Static_assert: AvlFreeTree satisfies the concept ────────────────────────

static_assert( is_free_block_tree_policy_v<AvlFreeTree<DefaultAddressTraits>>,
               "AvlFreeTree<DefaultAddressTraits> must satisfy FreeBlockTreePolicy" );

/// @brief Backward compatibility alias for PersistentAvlTree.
/// Deprecated: Use AvlFreeTree<DefaultAddressTraits> instead.
using PersistentAvlTree = AvlFreeTree<DefaultAddressTraits>;

} // namespace pmm
