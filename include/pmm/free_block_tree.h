/**
 * @file pmm/free_block_tree.h
 * @brief FreeBlockTree — AVL tree policy for free blocks (Issue #87 Phase 4, #95).
 *
 * Defines C++17 SFINAE concept for free block tree policy and provides
 * the standard implementation `AvlFreeTree<AddressTraits>`.
 *
 * The `FreeBlockTreePolicy<Policy>` concept requires three static methods:
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
 * @version 1.2 (Issue #112 — BlockHeader removed)
 */

#pragma once

#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/types.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace pmm
{

// ─── Helper utilities for concept checking ────────────────────────────────────

namespace detail
{

/// @brief SFINAE check for Policy::insert(uint8_t*, ManagerHeader*, uint32_t).
template <typename Policy, typename = void> struct has_insert : std::false_type
{
};
template <typename Policy>
struct has_insert<Policy, std::void_t<decltype( Policy::insert( std::declval<std::uint8_t*>(),
                                                                std::declval<pmm::detail::ManagerHeader*>(),
                                                                std::declval<std::uint32_t>() ) )>> : std::true_type
{
};

/// @brief SFINAE check for Policy::remove(uint8_t*, ManagerHeader*, uint32_t).
template <typename Policy, typename = void> struct has_remove : std::false_type
{
};
template <typename Policy>
struct has_remove<Policy, std::void_t<decltype( Policy::remove( std::declval<std::uint8_t*>(),
                                                                std::declval<pmm::detail::ManagerHeader*>(),
                                                                std::declval<std::uint32_t>() ) )>> : std::true_type
{
};

/// @brief SFINAE check for Policy::find_best_fit(uint8_t*, ManagerHeader*, uint32_t).
template <typename Policy, typename = void> struct has_find_best_fit : std::false_type
{
};
template <typename Policy>
struct has_find_best_fit<Policy, std::void_t<decltype( Policy::find_best_fit(
                                     std::declval<std::uint8_t*>(), std::declval<pmm::detail::ManagerHeader*>(),
                                     std::declval<std::uint32_t>() ) )>> : std::true_type
{
};

} // namespace detail

/**
 * @brief Check if Policy is a valid free block tree policy.
 *
 * Policy must provide three static methods:
 *   - `insert(uint8_t* base, ManagerHeader* hdr, uint32_t blk_idx)   -> void`
 *   - `remove(uint8_t* base, ManagerHeader* hdr, uint32_t blk_idx)   -> void`
 *   - `find_best_fit(uint8_t* base, ManagerHeader* hdr, uint32_t n)  -> uint32_t`
 *
 * Used as type-concept (C++17 variable template).
 *
 * @tparam Policy  Type to check for compliance.
 */
template <typename Policy>
inline constexpr bool is_free_block_tree_policy_v =
    detail::has_insert<Policy>::value && detail::has_remove<Policy>::value && detail::has_find_best_fit<Policy>::value;

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
 * Fields: prev_offset(0), next_offset(4), left_offset(8), right_offset(12),
 *         parent_offset(16), avl_height(20), _pad(22), weight(24), root_offset(28).
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

    /// @brief Get raw pointer to block memory by granule index.
    static void* blk_at( std::uint8_t* base, std::uint32_t idx )
    {
        assert( idx != detail::kNoBlock );
        return base + detail::idx_to_byte_off( idx );
    }

    /// @brief Get const raw pointer to block memory by granule index.
    static const void* blk_at( const std::uint8_t* base, std::uint32_t idx )
    {
        assert( idx != detail::kNoBlock );
        return base + detail::idx_to_byte_off( idx );
    }

    /// @brief Insert block into AVL tree of free blocks.
    static void insert( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t blk_idx )
    {
        void* blk = blk_at( base, blk_idx );
        BlockState::set_left_offset_of( blk, detail::kNoBlock );
        BlockState::set_right_offset_of( blk, detail::kNoBlock );
        BlockState::set_parent_offset_of( blk, detail::kNoBlock );
        BlockState::set_avl_height_of( blk, 1 );
        if ( hdr->free_tree_root == detail::kNoBlock )
        {
            hdr->free_tree_root = blk_idx;
            return;
        }
        // Issue #59: cache total_gran once; compute blk size in granules before the traversal loop
        std::uint32_t total_gran = detail::byte_off_to_idx( hdr->total_size );
        index_type    blk_next   = BlockState::get_next_offset( blk );
        std::uint32_t blk_gran   = ( blk_next != detail::kNoBlock ) ? ( blk_next - blk_idx ) : ( total_gran - blk_idx );
        std::uint32_t cur = hdr->free_tree_root, parent = detail::kNoBlock;
        bool          go_left = false;
        while ( cur != detail::kNoBlock )
        {
            parent                = cur;
            const void*   n       = blk_at( base, cur );
            index_type    n_next  = BlockState::get_next_offset( n );
            std::uint32_t n_gran  = ( n_next != detail::kNoBlock ) ? ( n_next - cur ) : ( total_gran - cur );
            bool          smaller = ( blk_gran < n_gran ) || ( blk_gran == n_gran && blk_idx < cur );
            go_left               = smaller;
            cur                   = smaller ? BlockState::get_left_offset( n ) : BlockState::get_right_offset( n );
        }
        BlockState::set_parent_offset_of( blk, parent );
        if ( go_left )
            BlockState::set_left_offset_of( blk_at( base, parent ), blk_idx );
        else
            BlockState::set_right_offset_of( blk_at( base, parent ), blk_idx );
        rebalance_up( base, hdr, parent );
    }

    /// @brief Remove block from AVL tree of free blocks.
    static void remove( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t blk_idx )
    {
        void*         blk    = blk_at( base, blk_idx );
        std::uint32_t parent = BlockState::get_parent_offset( blk );
        std::uint32_t left   = BlockState::get_left_offset( blk );
        std::uint32_t right  = BlockState::get_right_offset( blk );
        std::uint32_t rebal  = detail::kNoBlock;

        if ( left == detail::kNoBlock && right == detail::kNoBlock )
        {
            set_child( base, hdr, parent, blk_idx, detail::kNoBlock );
            rebal = parent;
        }
        else if ( left == detail::kNoBlock || right == detail::kNoBlock )
        {
            std::uint32_t child = ( left != detail::kNoBlock ) ? left : right;
            BlockState::set_parent_offset_of( blk_at( base, child ), parent );
            set_child( base, hdr, parent, blk_idx, child );
            rebal = parent;
        }
        else
        {
            std::uint32_t succ_idx    = min_node( base, right );
            void*         succ        = blk_at( base, succ_idx );
            std::uint32_t succ_parent = BlockState::get_parent_offset( succ );
            std::uint32_t succ_right  = BlockState::get_right_offset( succ );

            if ( succ_parent != blk_idx )
            {
                set_child( base, hdr, succ_parent, succ_idx, succ_right );
                if ( succ_right != detail::kNoBlock )
                    BlockState::set_parent_offset_of( blk_at( base, succ_right ), succ_parent );
                BlockState::set_right_offset_of( succ, right );
                BlockState::set_parent_offset_of( blk_at( base, right ), succ_idx );
                rebal = succ_parent;
            }
            else
            {
                rebal = succ_idx;
            }
            BlockState::set_left_offset_of( succ, left );
            BlockState::set_parent_offset_of( blk_at( base, left ), succ_idx );
            BlockState::set_parent_offset_of( succ, parent );
            set_child( base, hdr, parent, blk_idx, succ_idx );
            update_height( base, succ_idx );
        }
        BlockState::set_left_offset_of( blk, detail::kNoBlock );
        BlockState::set_right_offset_of( blk, detail::kNoBlock );
        BlockState::set_parent_offset_of( blk, detail::kNoBlock );
        BlockState::set_avl_height_of( blk, 0 );
        rebalance_up( base, hdr, rebal );
    }

    /// @brief Find smallest block >= needed granules (best-fit, O(log n)).
    static std::uint32_t find_best_fit( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t needed_granules )
    {
        // Issue #59: cache total_gran once to avoid repeated hdr->total_size reads in the hot path
        std::uint32_t total_gran = detail::byte_off_to_idx( hdr->total_size );
        std::uint32_t cur = hdr->free_tree_root, result = detail::kNoBlock;
        while ( cur != detail::kNoBlock )
        {
            const void*   node      = blk_at( base, cur );
            index_type    node_next = BlockState::get_next_offset( node );
            std::uint32_t cur_gran  = ( node_next != detail::kNoBlock ) ? ( node_next - cur ) : ( total_gran - cur );
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
    static std::int32_t height( std::uint8_t* base, std::uint32_t idx )
    {
        return ( idx == detail::kNoBlock )
                   ? 0
                   : static_cast<std::int32_t>( BlockState::get_avl_height( blk_at( base, idx ) ) );
    }

    static void update_height( std::uint8_t* base, std::uint32_t node_idx )
    {
        void*        node = blk_at( base, node_idx );
        std::int32_t h    = 1 + ( std::max )( height( base, BlockState::get_left_offset( node ) ),
                                           height( base, BlockState::get_right_offset( node ) ) );
        assert( h <= std::numeric_limits<std::int16_t>::max() ); // tree height must fit in int16_t
        BlockState::set_avl_height_of( node, static_cast<std::int16_t>( h ) );
    }

    static std::int32_t balance_factor( std::uint8_t* base, std::uint32_t node_idx )
    {
        const void* node = blk_at( base, node_idx );
        return height( base, BlockState::get_left_offset( node ) ) -
               height( base, BlockState::get_right_offset( node ) );
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
        void* p = blk_at( base, parent );
        if ( BlockState::get_left_offset( p ) == old_child )
            BlockState::set_left_offset_of( p, new_child );
        else
            BlockState::set_right_offset_of( p, new_child );
    }

    static std::uint32_t rotate_right( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t y_idx )
    {
        void*         y     = blk_at( base, y_idx );
        std::uint32_t x_idx = BlockState::get_left_offset( y );
        void*         x     = blk_at( base, x_idx );
        std::uint32_t t2    = BlockState::get_right_offset( x );

        BlockState::set_right_offset_of( x, y_idx );
        BlockState::set_left_offset_of( y, t2 );
        BlockState::set_parent_offset_of( x, BlockState::get_parent_offset( y ) );
        BlockState::set_parent_offset_of( y, x_idx );
        if ( t2 != detail::kNoBlock )
            BlockState::set_parent_offset_of( blk_at( base, t2 ), y_idx );
        set_child( base, hdr, BlockState::get_parent_offset( x ), y_idx, x_idx );
        update_height( base, y_idx );
        update_height( base, x_idx );
        return x_idx;
    }

    static std::uint32_t rotate_left( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t x_idx )
    {
        void*         x     = blk_at( base, x_idx );
        std::uint32_t y_idx = BlockState::get_right_offset( x );
        void*         y     = blk_at( base, y_idx );
        std::uint32_t t2    = BlockState::get_left_offset( y );

        BlockState::set_left_offset_of( y, x_idx );
        BlockState::set_right_offset_of( x, t2 );
        BlockState::set_parent_offset_of( y, BlockState::get_parent_offset( x ) );
        BlockState::set_parent_offset_of( x, y_idx );
        if ( t2 != detail::kNoBlock )
            BlockState::set_parent_offset_of( blk_at( base, t2 ), x_idx );
        set_child( base, hdr, BlockState::get_parent_offset( y ), x_idx, y_idx );
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
                void*         node     = blk_at( base, cur );
                std::uint32_t left_idx = BlockState::get_left_offset( node );
                if ( balance_factor( base, left_idx ) < 0 )
                    rotate_left( base, hdr, left_idx );
                cur = rotate_right( base, hdr, cur );
            }
            else if ( bf < -1 )
            {
                void*         node      = blk_at( base, cur );
                std::uint32_t right_idx = BlockState::get_right_offset( node );
                if ( balance_factor( base, right_idx ) > 0 )
                    rotate_right( base, hdr, right_idx );
                cur = rotate_left( base, hdr, cur );
            }
            cur = BlockState::get_parent_offset( blk_at( base, cur ) );
        }
    }

    static std::uint32_t min_node( std::uint8_t* base, std::uint32_t node_idx )
    {
        while ( node_idx != detail::kNoBlock )
        {
            std::uint32_t left = BlockState::get_left_offset( blk_at( base, node_idx ) );
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
