/**
 * @file pmm/avl_tree_mixin.h
 * @brief Shared AVL tree helper functions for pmap, pstringview and pvector (Issue #155, #162, #188).
 *
 * Provides a set of free static template functions implementing the core AVL
 * tree operations (height, balance factor, rotations, rebalancing, insert, find,
 * remove) that are shared between pmap<_K,_V,ManagerT>, pstringview<ManagerT>
 * and pvector<T,ManagerT>.
 *
 * All rotation and rebalance functions accept an optional NodeUpdateFn callback
 * that is invoked after structural changes to update derived node attributes.
 * By default avl_update_height is used (height-only). pvector passes a custom
 * callback that also updates the order-statistic weight field (Issue #188).
 *
 * Template parameter PPtr must support:
 *   - is_null()
 *   - tree_node() returning a TreeNode& with get_left/set_left/get_right/set_right/
 *     get_parent/set_parent/get_height/set_height
 *   - offset()
 *   - PPtr::manager_type::address_traits::no_block sentinel
 *
 * @see pmap.h — pmap<_K,_V,ManagerT> (Issue #153)
 * @see pstringview.h — pstringview<ManagerT> (Issue #151)
 * @see pvector.h — pvector<T,ManagerT> (Issue #186, #188)
 * @version 0.4 (Issue #188 — NodeUpdateFn hook, avl_remove, avl_min_node for pvector deduplication)
 */

#pragma once

#include <cstdint>

namespace pmm
{
namespace detail
{

/// @cond INTERNAL

/// @brief Sentinel value for "no node" in TreeNode fields.
template <typename PPtr> static constexpr auto pptr_no_block() noexcept
{
    return PPtr::manager_type::address_traits::no_block;
}

/// @brief Get left child as PPtr (translates no_block sentinel to null PPtr).
template <typename PPtr> static PPtr pptr_get_left( PPtr p ) noexcept
{
    auto idx = p.tree_node().get_left();
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : PPtr( idx );
}

/// @brief Get right child as PPtr (translates no_block sentinel to null PPtr).
template <typename PPtr> static PPtr pptr_get_right( PPtr p ) noexcept
{
    auto idx = p.tree_node().get_right();
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : PPtr( idx );
}

/// @brief Get parent as PPtr (translates no_block sentinel to null PPtr).
template <typename PPtr> static PPtr pptr_get_parent( PPtr p ) noexcept
{
    auto idx = p.tree_node().get_parent();
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : PPtr( idx );
}

/// @brief Set left child from PPtr (translates null PPtr to no_block sentinel).
template <typename PPtr> static void pptr_set_left( PPtr p, PPtr child ) noexcept
{
    auto idx = child.is_null() ? pptr_no_block<PPtr>() : child.offset();
    p.tree_node().set_left( idx );
}

/// @brief Set right child from PPtr (translates null PPtr to no_block sentinel).
template <typename PPtr> static void pptr_set_right( PPtr p, PPtr child ) noexcept
{
    auto idx = child.is_null() ? pptr_no_block<PPtr>() : child.offset();
    p.tree_node().set_right( idx );
}

/// @brief Set parent from PPtr (translates null PPtr to no_block sentinel).
template <typename PPtr> static void pptr_set_parent( PPtr p, PPtr parent ) noexcept
{
    auto idx = parent.is_null() ? pptr_no_block<PPtr>() : parent.offset();
    p.tree_node().set_parent( idx );
}

/// @brief Get height of node (0 if null).
template <typename PPtr> static std::int16_t avl_height( PPtr p ) noexcept
{
    if ( p.is_null() )
        return 0;
    return p.tree_node().get_height();
}

/// @brief Update height of node from its children.
template <typename PPtr> static void avl_update_height( PPtr p ) noexcept
{
    if ( p.is_null() )
        return;
    std::int16_t lh = avl_height( pptr_get_left( p ) );
    std::int16_t rh = avl_height( pptr_get_right( p ) );
    std::int16_t h  = static_cast<std::int16_t>( 1 + ( lh > rh ? lh : rh ) );
    p.tree_node().set_height( h );
}

/// @brief Balance factor: height(left) - height(right).
template <typename PPtr> static std::int16_t avl_balance_factor( PPtr p ) noexcept
{
    if ( p.is_null() )
        return 0;
    std::int16_t lh = avl_height( pptr_get_left( p ) );
    std::int16_t rh = avl_height( pptr_get_right( p ) );
    return static_cast<std::int16_t>( lh - rh );
}

/// @brief Update parent → child link (or root if parent is null).
/// @param root_idx Reference to the tree's root index, updated when parent is null.
template <typename PPtr, typename IndexType>
static void avl_set_child( PPtr parent, PPtr old_child, PPtr new_child, IndexType& root_idx ) noexcept
{
    if ( parent.is_null() )
    {
        root_idx = new_child.offset();
        return;
    }
    PPtr left_of_parent = pptr_get_left( parent );
    if ( left_of_parent == old_child )
        pptr_set_left( parent, new_child );
    else
        pptr_set_right( parent, new_child );
}

/// @brief Default node-update functor: updates height only (used by pmap, pstringview).
struct AvlUpdateHeightOnly
{
    template <typename PPtr> void operator()( PPtr p ) const noexcept { avl_update_height( p ); }
};

/**
 * @brief Right rotation around y; returns new subtree root (x).
 *
 *     y            x
 *    / \          / \
 *   x   C  -->  A    y
 *  / \               / \
 * A   B             B   C
 *
 * @param update_node  Callable(PPtr) invoked on y then x after structural change.
 *                     Default: AvlUpdateHeightOnly (updates height only).
 */
template <typename PPtr, typename IndexType, typename NodeUpdateFn = AvlUpdateHeightOnly>
static PPtr avl_rotate_right( PPtr y, IndexType& root_idx, NodeUpdateFn update_node = {} ) noexcept
{
    PPtr x     = pptr_get_left( y );
    PPtr b     = pptr_get_right( x );
    PPtr y_par = pptr_get_parent( y );

    pptr_set_right( x, y );
    pptr_set_parent( y, x );

    pptr_set_left( y, b );
    if ( !b.is_null() )
        pptr_set_parent( b, y );

    pptr_set_parent( x, y_par );

    avl_set_child( y_par, y, x, root_idx );

    update_node( y );
    update_node( x );
    return x;
}

/**
 * @brief Left rotation around x; returns new subtree root (y).
 *
 *   x               y
 *  / \             / \
 * A   y   -->    x    C
 *    / \        / \
 *   B   C      A   B
 *
 * @param update_node  Callable(PPtr) invoked on x then y after structural change.
 *                     Default: AvlUpdateHeightOnly (updates height only).
 */
template <typename PPtr, typename IndexType, typename NodeUpdateFn = AvlUpdateHeightOnly>
static PPtr avl_rotate_left( PPtr x, IndexType& root_idx, NodeUpdateFn update_node = {} ) noexcept
{
    PPtr y     = pptr_get_right( x );
    PPtr b     = pptr_get_left( y );
    PPtr x_par = pptr_get_parent( x );

    pptr_set_left( y, x );
    pptr_set_parent( x, y );

    pptr_set_right( x, b );
    if ( !b.is_null() )
        pptr_set_parent( b, x );

    pptr_set_parent( y, x_par );

    avl_set_child( x_par, x, y, root_idx );

    update_node( x );
    update_node( y );
    return y;
}

/// @brief Rebalance from node p upward to root.
/// @param update_node  Callable(PPtr) for node attribute update after rotations.
///                     Default: AvlUpdateHeightOnly (height only).
template <typename PPtr, typename IndexType, typename NodeUpdateFn = AvlUpdateHeightOnly>
static void avl_rebalance_up( PPtr p, IndexType& root_idx, NodeUpdateFn update_node = {} ) noexcept
{
    while ( !p.is_null() )
    {
        update_node( p );
        std::int16_t bf = avl_balance_factor( p );
        if ( bf > 1 )
        {
            PPtr left = pptr_get_left( p );
            if ( avl_balance_factor( left ) < 0 )
                avl_rotate_left( left, root_idx, update_node );
            p = avl_rotate_right( p, root_idx, update_node );
        }
        else if ( bf < -1 )
        {
            PPtr right = pptr_get_right( p );
            if ( avl_balance_factor( right ) > 0 )
                avl_rotate_right( right, root_idx, update_node );
            p = avl_rotate_left( p, root_idx, update_node );
        }
        p = pptr_get_parent( p );
    }
}

/// @brief Find the minimum (leftmost) node in the subtree rooted at p (Issue #188).
template <typename PPtr> static PPtr avl_min_node( PPtr p ) noexcept
{
    while ( !p.is_null() )
    {
        PPtr left = pptr_get_left( p );
        if ( left.is_null() )
            break;
        p = left;
    }
    return p;
}

/// @brief Find the maximum (rightmost) node in the subtree rooted at p (Issue #188).
template <typename PPtr> static PPtr avl_max_node( PPtr p ) noexcept
{
    while ( !p.is_null() )
    {
        PPtr right = pptr_get_right( p );
        if ( right.is_null() )
            break;
        p = right;
    }
    return p;
}

/// @brief Remove target node from AVL tree and rebalance (Issue #188).
///
/// Standard BST removal with in-order successor replacement, followed by
/// AVL rebalancing from the lowest affected node upward.
///
/// @param target      The node to remove (must be in the tree).
/// @param root_idx    Reference to the tree root index.
/// @param update_node Callable(PPtr) for node attribute update after rotations.
///                    Default: AvlUpdateHeightOnly (height only).
template <typename PPtr, typename IndexType, typename NodeUpdateFn = AvlUpdateHeightOnly>
static void avl_remove( PPtr target, IndexType& root_idx, NodeUpdateFn update_node = {} ) noexcept
{
    PPtr left_p  = pptr_get_left( target );
    PPtr right_p = pptr_get_right( target );
    PPtr par_p   = pptr_get_parent( target );

    if ( left_p.is_null() && right_p.is_null() )
    {
        // Leaf node — just detach.
        avl_set_child( par_p, target, PPtr(), root_idx );
        if ( !par_p.is_null() )
            avl_rebalance_up( par_p, root_idx, update_node );
    }
    else if ( left_p.is_null() )
    {
        // Only right child.
        pptr_set_parent( right_p, par_p );
        avl_set_child( par_p, target, right_p, root_idx );
        if ( !par_p.is_null() )
            avl_rebalance_up( par_p, root_idx, update_node );
        else
            update_node( right_p );
    }
    else if ( right_p.is_null() )
    {
        // Only left child.
        pptr_set_parent( left_p, par_p );
        avl_set_child( par_p, target, left_p, root_idx );
        if ( !par_p.is_null() )
            avl_rebalance_up( par_p, root_idx, update_node );
        else
            update_node( left_p );
    }
    else
    {
        // Two children — replace with in-order successor.
        PPtr successor = avl_min_node( right_p );

        auto succ_par_idx = successor.tree_node().get_parent();
        PPtr succ_rgt     = pptr_get_right( successor );

        if ( succ_par_idx == target.offset() )
        {
            // Successor is direct right child of target.
            pptr_set_left( successor, left_p );
            pptr_set_parent( left_p, successor );
            // successor keeps its right subtree
            pptr_set_parent( successor, par_p );
            avl_set_child( par_p, target, successor, root_idx );
            avl_rebalance_up( successor, root_idx, update_node );
        }
        else
        {
            // Successor is deeper — detach it first.
            PPtr succ_par( succ_par_idx );
            if ( !succ_rgt.is_null() )
            {
                pptr_set_parent( succ_rgt, succ_par );
                pptr_set_left( succ_par, succ_rgt );
            }
            else
            {
                pptr_set_left( succ_par, PPtr() );
            }

            // Put successor in target's place.
            pptr_set_left( successor, left_p );
            pptr_set_parent( left_p, successor );
            pptr_set_right( successor, right_p );
            pptr_set_parent( right_p, successor );
            pptr_set_parent( successor, par_p );
            avl_set_child( par_p, target, successor, root_idx );

            avl_rebalance_up( succ_par, root_idx, update_node );
        }
    }
}

/// @brief Search the AVL tree for a node matching the given key.
///
/// Traverses the tree starting from root_idx, using compare_less to determine
/// the direction at each node. When compare_less(cur, key) is true the search
/// goes right; when compare_less(key, cur) is true it goes left; otherwise the
/// node is considered a match and is returned.
///
/// @param root_idx     Index of the tree root (0 = empty tree).
/// @param compare_less Callable(PPtr cur) -> int: returns negative if key < cur,
///                     zero if key == cur, positive if key > cur.
/// @param resolve      Callable(PPtr) -> NodeObjPtr: resolves pptr to raw pointer.
///                     Must return nullptr when the pointer is invalid.
/// @return PPtr to the matching node, or a null PPtr if not found.
template <typename PPtr, typename IndexType, typename CompareThreeWayFn, typename ResolveFn>
static PPtr avl_find( IndexType root_idx, CompareThreeWayFn&& compare_three_way, ResolveFn&& resolve ) noexcept
{
    PPtr cur( root_idx );
    while ( !cur.is_null() )
    {
        if ( resolve( cur ) == nullptr )
            break;
        int cmp = compare_three_way( cur );
        if ( cmp == 0 )
            return cur;
        else if ( cmp < 0 )
            cur = pptr_get_left( cur );
        else
            cur = pptr_get_right( cur );
    }
    return PPtr(); // not found
}

/// @brief Insert new_node into the AVL tree with the given root.
/// The caller must resolve the comparison ordering via go_left callback.
/// @param new_node    The node to insert (must not be null, not already in tree).
/// @param root_idx    Reference to the tree root index.
/// @param go_left     Callable(cur_node_ptr) -> bool: returns true if new_node < cur.
/// @param resolve     Callable(pptr) -> NodeObjPtr: resolves pptr to raw object pointer.
/// @param update_node Callable(PPtr) for node attribute update after rotations (Issue #188).
///                    Default: AvlUpdateHeightOnly (height only).
template <typename PPtr, typename IndexType, typename GoLeftFn, typename ResolveFn,
          typename NodeUpdateFn = AvlUpdateHeightOnly>
static void avl_insert( PPtr new_node, IndexType& root_idx, GoLeftFn&& go_left, ResolveFn&& resolve,
                        NodeUpdateFn update_node = {} ) noexcept
{
    if ( new_node.is_null() )
        return;
    if ( resolve( new_node ) == nullptr )
        return;

    if ( root_idx == static_cast<IndexType>( 0 ) )
    {
        pptr_set_left( new_node, PPtr() );
        pptr_set_right( new_node, PPtr() );
        pptr_set_parent( new_node, PPtr() );
        new_node.tree_node().set_height( static_cast<std::int16_t>( 1 ) );
        root_idx = new_node.offset();
        return;
    }

    PPtr cur( root_idx );
    PPtr parent;
    bool left = false;

    while ( !cur.is_null() )
    {
        if ( resolve( cur ) == nullptr )
            break;
        parent = cur;
        left   = go_left( cur );
        if ( left )
            cur = pptr_get_left( cur );
        else
            cur = pptr_get_right( cur );
    }

    pptr_set_parent( new_node, parent );
    if ( left )
        pptr_set_left( parent, new_node );
    else
        pptr_set_right( parent, new_node );

    avl_rebalance_up( parent, root_idx, update_node );
}

/// @endcond

} // namespace detail
} // namespace pmm
