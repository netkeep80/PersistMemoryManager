/**
 * @file pmm/avl_tree_mixin.h
 * @brief Shared AVL tree helper functions for pmap and pstringview (Issue #155, #162).
 *
 * Provides a set of free static template functions implementing the core AVL
 * tree operations (height, balance factor, rotations, rebalancing, insert, find)
 * that are shared between pmap<_K,_V,ManagerT> and pstringview<ManagerT>.
 *
 * Both pmap and pstringview use the same AVL logic via pptr<T,ManagerT> — the
 * only difference is the pptr type and the comparison key. This header factors
 * out the common tree structure code, eliminating ~130 lines of duplication.
 *
 * Template parameter PPtr must support:
 *   - is_null()
 *   - get_tree_left(), get_tree_right(), get_tree_parent()
 *   - set_tree_left(), set_tree_right(), set_tree_parent()
 *   - get_tree_height(), set_tree_height()
 *   - offset()
 *
 * @see pmap.h — pmap<_K,_V,ManagerT> (Issue #153)
 * @see pstringview.h — pstringview<ManagerT> (Issue #151)
 * @version 0.2 (Issue #162 — avl_find() deduplication)
 */

#pragma once

#include <cstdint>

namespace pmm
{
namespace detail
{

/// @brief Get height of node (0 if null).
template <typename PPtr> static std::int16_t avl_height( PPtr p ) noexcept
{
    if ( p.is_null() )
        return 0;
    return p.get_tree_height();
}

/// @brief Update height of node from its children.
template <typename PPtr> static void avl_update_height( PPtr p ) noexcept
{
    if ( p.is_null() )
        return;
    std::int16_t lh = avl_height( PPtr( p.get_tree_left().offset() ) );
    std::int16_t rh = avl_height( PPtr( p.get_tree_right().offset() ) );
    std::int16_t h  = static_cast<std::int16_t>( 1 + ( lh > rh ? lh : rh ) );
    p.set_tree_height( h );
}

/// @brief Balance factor: height(left) - height(right).
template <typename PPtr> static std::int16_t avl_balance_factor( PPtr p ) noexcept
{
    if ( p.is_null() )
        return 0;
    std::int16_t lh = avl_height( PPtr( p.get_tree_left().offset() ) );
    std::int16_t rh = avl_height( PPtr( p.get_tree_right().offset() ) );
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
    PPtr left_of_parent( parent.get_tree_left().offset() );
    if ( left_of_parent == old_child )
        parent.set_tree_left( new_child );
    else
        parent.set_tree_right( new_child );
}

/**
 * @brief Right rotation around y; returns new subtree root (x).
 *
 *     y            x
 *    / \          / \
 *   x   C  -->  A    y
 *  / \               / \
 * A   B             B   C
 */
template <typename PPtr, typename IndexType> static PPtr avl_rotate_right( PPtr y, IndexType& root_idx ) noexcept
{
    PPtr x     = PPtr( y.get_tree_left().offset() );
    PPtr b     = PPtr( x.get_tree_right().offset() );
    PPtr y_par = PPtr( y.get_tree_parent().offset() );

    x.set_tree_right( y );
    y.set_tree_parent( x );

    y.set_tree_left( b );
    if ( !b.is_null() )
        b.set_tree_parent( y );

    x.set_tree_parent( y_par );

    avl_set_child( y_par, y, x, root_idx );

    avl_update_height( y );
    avl_update_height( x );
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
 */
template <typename PPtr, typename IndexType> static PPtr avl_rotate_left( PPtr x, IndexType& root_idx ) noexcept
{
    PPtr y     = PPtr( x.get_tree_right().offset() );
    PPtr b     = PPtr( y.get_tree_left().offset() );
    PPtr x_par = PPtr( x.get_tree_parent().offset() );

    y.set_tree_left( x );
    x.set_tree_parent( y );

    x.set_tree_right( b );
    if ( !b.is_null() )
        b.set_tree_parent( x );

    y.set_tree_parent( x_par );

    avl_set_child( x_par, x, y, root_idx );

    avl_update_height( x );
    avl_update_height( y );
    return y;
}

/// @brief Rebalance from node p upward to root.
template <typename PPtr, typename IndexType> static void avl_rebalance_up( PPtr p, IndexType& root_idx ) noexcept
{
    while ( !p.is_null() )
    {
        avl_update_height( p );
        std::int16_t bf = avl_balance_factor( p );
        if ( bf > 1 )
        {
            PPtr left( p.get_tree_left().offset() );
            if ( avl_balance_factor( left ) < 0 )
                avl_rotate_left( left, root_idx );
            p = avl_rotate_right( p, root_idx );
        }
        else if ( bf < -1 )
        {
            PPtr right( p.get_tree_right().offset() );
            if ( avl_balance_factor( right ) > 0 )
                avl_rotate_right( right, root_idx );
            p = avl_rotate_left( p, root_idx );
        }
        p = PPtr( p.get_tree_parent().offset() );
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
            cur = PPtr( cur.get_tree_left().offset() );
        else
            cur = PPtr( cur.get_tree_right().offset() );
    }
    return PPtr(); // not found
}

/// @brief Insert new_node into the AVL tree with the given root.
/// The caller must resolve the comparison ordering via go_left callback.
/// @param new_node  The node to insert (must not be null, not already in tree).
/// @param root_idx  Reference to the tree root index.
/// @param go_left   Callable(cur_node_ptr) -> bool: returns true if new_node < cur.
/// @param resolve   Callable(pptr) -> NodeObjPtr: resolves pptr to raw object pointer.
template <typename PPtr, typename IndexType, typename GoLeftFn, typename ResolveFn>
static void avl_insert( PPtr new_node, IndexType& root_idx, GoLeftFn&& go_left, ResolveFn&& resolve ) noexcept
{
    if ( new_node.is_null() )
        return;
    if ( resolve( new_node ) == nullptr )
        return;

    if ( root_idx == static_cast<IndexType>( 0 ) )
    {
        new_node.set_tree_left( PPtr() );
        new_node.set_tree_right( PPtr() );
        new_node.set_tree_parent( PPtr() );
        new_node.set_tree_height( static_cast<std::int16_t>( 1 ) );
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
            cur = PPtr( cur.get_tree_left().offset() );
        else
            cur = PPtr( cur.get_tree_right().offset() );
    }

    new_node.set_tree_parent( parent );
    if ( left )
        parent.set_tree_left( new_node );
    else
        parent.set_tree_right( new_node );

    avl_rebalance_up( parent, root_idx );
}

} // namespace detail
} // namespace pmm
