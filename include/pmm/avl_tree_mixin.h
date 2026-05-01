#pragma once
#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/types.h"
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
namespace pmm
{
namespace detail
{
template <typename PPtr> static constexpr auto pptr_no_block() noexcept
{
    return PPtr::manager_type::address_traits::no_block;
}
template <typename PPtr> static PPtr pptr_make( PPtr, typename PPtr::index_type idx ) noexcept
{
    return PPtr( idx );
}
template <typename PPtr> static PPtr pptr_get_left( PPtr p ) noexcept
{
    auto idx = p.tree_node_unchecked().left_offset;
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : pptr_make( p, idx );
}
template <typename PPtr> static PPtr pptr_get_right( PPtr p ) noexcept
{
    auto idx = p.tree_node_unchecked().right_offset;
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : pptr_make( p, idx );
}
template <typename PPtr> static PPtr pptr_get_parent( PPtr p ) noexcept
{
    auto idx = p.tree_node_unchecked().parent_offset;
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : pptr_make( p, idx );
}
template <typename PPtr> static void pptr_set_left( PPtr p, PPtr child ) noexcept
{
    auto idx                  = child.is_null() ? pptr_no_block<PPtr>() : child.offset();
    p.tree_node_unchecked().left_offset = idx;
}
template <typename PPtr> static void pptr_set_right( PPtr p, PPtr child ) noexcept
{
    auto idx                   = child.is_null() ? pptr_no_block<PPtr>() : child.offset();
    p.tree_node_unchecked().right_offset = idx;
}
template <typename PPtr> static void pptr_set_parent( PPtr p, PPtr parent ) noexcept
{
    auto idx                    = parent.is_null() ? pptr_no_block<PPtr>() : parent.offset();
    p.tree_node_unchecked().parent_offset = idx;
}
template <typename PPtr> static std::int16_t avl_height( PPtr p ) noexcept
{
    if ( p.is_null() )
        return 0;
    return p.tree_node_unchecked().avl_height;
}
template <typename PPtr> static void avl_update_height( PPtr p ) noexcept
{
    if ( p.is_null() )
        return;
    std::int16_t lh = avl_height( pptr_get_left( p ) );
    std::int16_t rh = avl_height( pptr_get_right( p ) );
    std::int16_t h  = static_cast<std::int16_t>( 1 + ( lh > rh ? lh : rh ) );
    assert( h >= 0 );
    assert( h <= static_cast<std::int16_t>( ( std::numeric_limits<std::uint8_t>::max )() ) );
    p.tree_node_unchecked().avl_height = static_cast<std::uint8_t>( h );
}
template <typename PPtr> static std::int16_t avl_balance_factor( PPtr p ) noexcept
{
    if ( p.is_null() )
        return 0;
    std::int16_t lh = avl_height( pptr_get_left( p ) );
    std::int16_t rh = avl_height( pptr_get_right( p ) );
    return static_cast<std::int16_t>( lh - rh );
}
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
/*
### pmm-detail-avlupdateheightonly
req: dr-009, qa-maint-002, sys-004
*/
struct AvlUpdateHeightOnly
{
    template <typename PPtr> void operator()( PPtr p ) const noexcept { avl_update_height( p ); }
};
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
template <typename PPtr, typename IndexType, typename NodeUpdateFn = AvlUpdateHeightOnly>
static void avl_remove( PPtr target, IndexType& root_idx, NodeUpdateFn update_node = {} ) noexcept
{
    PPtr left_p  = pptr_get_left( target );
    PPtr right_p = pptr_get_right( target );
    PPtr par_p   = pptr_get_parent( target );
    if ( left_p.is_null() && right_p.is_null() )
    {
        avl_set_child( par_p, target, PPtr(), root_idx );
        if ( !par_p.is_null() )
            avl_rebalance_up( par_p, root_idx, update_node );
    }
    else if ( left_p.is_null() )
    {
        pptr_set_parent( right_p, par_p );
        avl_set_child( par_p, target, right_p, root_idx );
        if ( !par_p.is_null() )
            avl_rebalance_up( par_p, root_idx, update_node );
        else
            update_node( right_p );
    }
    else if ( right_p.is_null() )
    {
        pptr_set_parent( left_p, par_p );
        avl_set_child( par_p, target, left_p, root_idx );
        if ( !par_p.is_null() )
            avl_rebalance_up( par_p, root_idx, update_node );
        else
            update_node( left_p );
    }
    else
    {
        PPtr successor    = avl_min_node( right_p );
        auto succ_par_idx = successor.tree_node_unchecked().parent_offset;
        PPtr succ_rgt     = pptr_get_right( successor );
        if ( succ_par_idx == target.offset() )
        {
            pptr_set_left( successor, left_p );
            pptr_set_parent( left_p, successor );
            pptr_set_parent( successor, par_p );
            avl_set_child( par_p, target, successor, root_idx );
            avl_rebalance_up( successor, root_idx, update_node );
        }
        else
        {
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
    return PPtr();
}
template <typename PPtr> static PPtr avl_inorder_successor( PPtr cur ) noexcept
{
    if ( cur.is_null() )
        return PPtr();
    PPtr right = pptr_get_right( cur );
    if ( !right.is_null() )
        return avl_min_node( right );
    while ( true )
    {
        PPtr parent = pptr_get_parent( cur );
        if ( parent.is_null() )
            return PPtr();
        PPtr parent_left = pptr_get_left( parent );
        if ( !parent_left.is_null() && parent_left.offset() == cur.offset() )
            return parent;
        cur = parent;
    }
}
template <typename PPtr> static void avl_init_node( PPtr p ) noexcept
{
    auto& tn         = p.tree_node_unchecked();
    tn.left_offset   = pptr_no_block<PPtr>();
    tn.right_offset  = pptr_no_block<PPtr>();
    tn.parent_offset = pptr_no_block<PPtr>();
    tn.avl_height    = static_cast<std::int16_t>( 1 );
}
template <typename PPtr> static size_t avl_subtree_count( PPtr p ) noexcept
{
    if ( p.is_null() )
        return 0;
    return 1 + avl_subtree_count( pptr_get_left( p ) ) + avl_subtree_count( pptr_get_right( p ) );
}
template <typename PPtr, typename DeallocFn> static void avl_clear_subtree( PPtr p, DeallocFn&& dealloc ) noexcept
{
    if ( p.is_null() )
        return;
    PPtr left_p  = pptr_get_left( p );
    PPtr right_p = pptr_get_right( p );
    avl_clear_subtree( left_p, dealloc );
    avl_clear_subtree( right_p, dealloc );
    dealloc( p );
}
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
        new_node.tree_node_unchecked().avl_height = static_cast<std::int16_t>( 1 );
        root_idx                        = new_node.offset();
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
template <typename Domain>
concept ForestDomainViewDescriptor = requires( const Domain domain, typename Domain::node_pptr p ) {
    typename Domain::index_type;
    typename Domain::node_type;
    typename Domain::node_pptr;
    { domain.name() } -> std::convertible_to<const char*>;
    { domain.root_index() } -> std::convertible_to<typename Domain::index_type>;
    { domain.resolve_node( p ) } -> std::convertible_to<typename Domain::node_type*>;
};
template <typename Domain>
concept ForestDomainDescriptor =
    ForestDomainViewDescriptor<Domain> && requires( Domain domain, typename Domain::node_pptr p ) {
        { domain.root_index_ptr() } -> std::same_as<typename Domain::index_type*>;
        { domain.less_node( p, p ) } -> std::convertible_to<bool>;
    };
template <typename Domain, typename Key>
concept ForestDomainDescriptorForKey = ForestDomainViewDescriptor<Domain> &&
                                       requires( const Domain domain, typename Domain::node_pptr p, const Key& key ) {
                                           { domain.compare_key( key, p ) } -> std::convertible_to<int>;
                                       };
template <typename Domain>
static bool forest_domain_validate_node( const Domain& domain, typename Domain::node_pptr p ) noexcept
{
    if constexpr ( requires {
                       { domain.validate_node( p ) } -> std::convertible_to<bool>;
                   } )
        return domain.validate_node( p );
    else
        return true;
}
template <ForestDomainViewDescriptor Domain> struct ForestDomainViewOps
{
    using index_type = typename Domain::index_type;
    using node_type  = typename Domain::node_type;
    using node_pptr  = typename Domain::node_pptr;
    Domain domain;
    constexpr explicit ForestDomainViewOps( Domain d = Domain{} ) noexcept : domain( d ) {}
    const char* name() const noexcept { return domain.name(); }
    index_type  root_index() const noexcept { return domain.root_index(); }
    template <typename Key>
        requires ForestDomainDescriptorForKey<Domain, Key>
    node_pptr find( const Key& key ) const noexcept
    {
        return avl_find<node_pptr>(
            domain.root_index(), [&]( node_pptr cur ) -> int { return domain.compare_key( key, cur ); },
            [this]( node_pptr p ) -> node_type* { return domain.resolve_node( p ); } );
    }
};
template <ForestDomainDescriptor Domain> struct ForestDomainOps : ForestDomainViewOps<Domain>
{
    using view_base  = ForestDomainViewOps<Domain>;
    using index_type = typename view_base::index_type;
    using node_type  = typename view_base::node_type;
    using node_pptr  = typename view_base::node_pptr;
    using view_base::find;
    using view_base::name;
    using view_base::root_index;
    constexpr explicit ForestDomainOps( Domain d = Domain{} ) noexcept : view_base( d ) {}
    index_type* root_index_ptr() noexcept { return this->domain.root_index_ptr(); }
    bool        reset_root() noexcept
    {
        index_type* root = root_index_ptr();
        if ( root == nullptr )
            return false;
        *root = static_cast<index_type>( 0 );
        return true;
    }
    void insert( node_pptr new_node ) noexcept
    {
        index_type* root = this->domain.root_index_ptr();
        if ( root == nullptr || new_node.is_null() )
            return;
        if ( this->domain.resolve_node( new_node ) == nullptr ||
             !forest_domain_validate_node( this->domain, new_node ) )
            return;
        avl_insert(
            new_node, *root,
            [this, new_node]( node_pptr cur ) -> bool { return this->domain.less_node( new_node, cur ); },
            [this]( node_pptr p ) -> node_type* { return this->domain.resolve_node( p ); } );
    }
};
template <typename AT> struct BlockPPtrManagerTag
{
    using address_traits = AT;
};
/*
### pmm-detail-blockpptr
*/
template <typename AT> struct BlockPPtr
{
    using manager_type = BlockPPtrManagerTag<AT>;
    using index_type   = typename AT::index_type;
    uint8_t*   _base;
    index_type _idx;
    BlockPPtr() noexcept : _base( nullptr ), _idx( AT::no_block ) {}
    BlockPPtr( uint8_t* base, index_type idx ) noexcept : _base( base ), _idx( idx ) {}
    bool             is_null() const noexcept { return _idx == AT::no_block; }
    index_type       offset() const noexcept { return _idx; }
    bool             operator==( const BlockPPtr& other ) const noexcept { return _idx == other._idx; }
    bool             operator!=( const BlockPPtr& other ) const noexcept { return _idx != other._idx; }
    BlockHeader<AT>& tree_node_unchecked() const noexcept
    {
        return *block_header_at<AT>( block_at<AT>( _base, _idx ) );
    }
};
template <typename AT> static BlockPPtr<AT> pptr_make( BlockPPtr<AT> source, typename AT::index_type idx ) noexcept
{
    return BlockPPtr<AT>( source._base, idx );
}
/*
### pmm-detail-avlinorderiterator
*/
template <typename NodePPtr> struct AvlInorderIterator
{
    using index_type                     = typename NodePPtr::index_type;
    using value_type                     = typename NodePPtr::element_type;
    using pointer                        = NodePPtr;
    static constexpr index_type no_block = NodePPtr::manager_type::address_traits::no_block;
    index_type                  _current_idx;
    AvlInorderIterator() noexcept : _current_idx( static_cast<index_type>( 0 ) ) {}
    explicit AvlInorderIterator( index_type idx ) noexcept : _current_idx( idx ) {}
    bool     operator==( const AvlInorderIterator& other ) const noexcept { return _current_idx == other._current_idx; }
    bool     operator!=( const AvlInorderIterator& other ) const noexcept { return _current_idx != other._current_idx; }
    NodePPtr operator*() const noexcept
    {
        if ( _current_idx == static_cast<index_type>( 0 ) || _current_idx == no_block )
            return NodePPtr();
        return NodePPtr( _current_idx );
    }
    AvlInorderIterator& operator++() noexcept
    {
        if ( _current_idx == static_cast<index_type>( 0 ) || _current_idx == no_block )
            return *this;
        NodePPtr next = avl_inorder_successor( NodePPtr( _current_idx ) );
        _current_idx  = next.is_null() ? static_cast<index_type>( 0 ) : next.offset();
        return *this;
    }
};
}
}
