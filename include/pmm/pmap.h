#pragma once
#include "pmm/avl_tree_mixin.h"
#include "pmm/forest_registry.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace pmm
{
template <typename _K, typename _V, typename ManagerT> struct pmap;
template <typename T> struct pmap_type_identity
{
    static constexpr const char* tag = "";
};
template <typename _K, typename _V> struct pmap_node
{
    _K key;
    _V value;
};
namespace detail
{
constexpr uint32_t pmap_fnv1a( uint32_t h, uint64_t v, unsigned bytes ) noexcept
{
    for ( unsigned i = 0; i < bytes; ++i, v >>= 8 )
    {
        h ^= static_cast<uint8_t>( v & 0xffull );
        h *= 16777619u;
    }
    return h;
}
template <typename T> constexpr uint32_t pmap_type_fp() noexcept
{
    const uint64_t traits =
        ( uint64_t{ std::is_integral_v<T> } << 0 ) | ( uint64_t{ std::is_floating_point_v<T> } << 1 ) |
        ( uint64_t{ std::is_signed_v<T> } << 2 ) | ( uint64_t{ std::is_unsigned_v<T> } << 3 ) |
        ( uint64_t{ std::is_pointer_v<T> } << 4 ) | ( uint64_t{ std::is_class_v<T> } << 5 ) |
        ( uint64_t{ std::is_enum_v<T> } << 6 ) | ( uint64_t{ std::is_trivially_copyable_v<T> } << 7 ) |
        ( uint64_t{ std::is_standard_layout_v<T> } << 8 );
    uint32_t h = 2166136261u;
    h          = pmap_fnv1a( h, sizeof( T ), 8 );
    h          = pmap_fnv1a( h, alignof( T ), 8 );
    h          = pmap_fnv1a( h, traits, 8 );
    for ( const char* t = pmm::pmap_type_identity<T>::tag; t != nullptr && *t != '\0'; ++t )
        h = pmap_fnv1a( h, static_cast<uint8_t>( *t ), 1 );
    return h;
}
inline uint64_t pmap_key_hash( const char* key ) noexcept
{
    uint64_t h = 14695981039346656037ull;
    for ( ; key != nullptr && *key != '\0'; ++key )
    {
        h ^= static_cast<uint8_t>( *key );
        h *= 1099511628211ull;
    }
    return h;
}
inline bool pmap_write_name( char ( &out )[kForestDomainNameCapacity], uint32_t type_fp, char kind, uint64_t value,
                             unsigned value_hex_digits ) noexcept
{
    constexpr const char* kPrefix = "container/pmap/";
    const unsigned        needed  = 15 + 8 + 1 + 1 + value_hex_digits + 1;
    if ( needed > kForestDomainNameCapacity )
        return false;
    size_t p = 0;
    for ( const char* s = kPrefix; *s != '\0'; ++s )
        out[p++] = *s;
    auto put_hex = [&]( uint64_t v, unsigned digits )
    {
        for ( unsigned i = digits; i-- > 0; )
        {
            const uint8_t nib = static_cast<uint8_t>( ( v >> ( i * 4 ) ) & 0x0full );
            out[p++]          = static_cast<char>( nib < 10 ? ( '0' + nib ) : ( 'a' + ( nib - 10 ) ) );
        }
    };
    put_hex( type_fp, 8 );
    out[p++] = '/';
    out[p++] = kind;
    put_hex( value, value_hex_digits );
    out[p] = '\0';
    return true;
}
}
/*
## pmm-pmap
req: feat-003, fr-007, fr-008, fr-029, ur-003, dr-007, dr-010, dr-011, feat-008, fr-018, ur-008
*/
template <typename _K, typename _V, typename ManagerT> struct pmap
{
    using manager_type                         = ManagerT;
    using index_type                           = typename ManagerT::index_type;
    using node_type                            = pmap_node<_K, _V>;
    using node_pptr                            = typename ManagerT::template pptr<node_type>;
    static constexpr uint32_t domain_type_hash = detail::pmap_fnv1a(
        detail::pmap_fnv1a( 2166136261u, detail::pmap_type_fp<_K>(), 4 ), detail::pmap_type_fp<_V>(), 4 );
    struct forest_domain_descriptor
    {
        using index_type = typename ManagerT::index_type;
        using node_type  = pmap_node<_K, _V>;
        using node_pptr  = typename ManagerT::template pptr<node_type>;
        index_type binding_id;
        constexpr explicit forest_domain_descriptor( index_type id = 0 ) noexcept : binding_id( id ) {}
        const char* name() const noexcept
        {
            const auto* d = ManagerT::find_domain_by_binding_unlocked( binding_id );
            return d != nullptr ? d->name : "";
        }
        index_type root_index() const noexcept
        {
            return ManagerT::forest_domain_root_index_unlocked(
                ManagerT::find_domain_by_binding_unlocked( binding_id ) );
        }
        index_type* root_index_ptr() noexcept
        {
            return binding_id == 0 ? nullptr
                                   : ManagerT::forest_domain_root_index_ptr_unlocked(
                                         ManagerT::find_domain_by_binding_unlocked( binding_id ) );
        }
        static node_type* resolve_node( node_pptr p ) noexcept { return ManagerT::template resolve<node_type>( p ); }
        static int        compare_key( const _K& key, node_pptr cur ) noexcept
        {
            node_type* obj = resolve_node( cur );
            return obj == nullptr ? 0 : ( ( key == obj->key ) ? 0 : ( ( key < obj->key ) ? -1 : 1 ) );
        }
        static bool less_node( node_pptr lhs, node_pptr rhs ) noexcept
        {
            node_type* l = resolve_node( lhs );
            node_type* r = resolve_node( rhs );
            return l != nullptr && r != nullptr && l->key < r->key;
        }
        static bool validate_node( node_pptr p ) noexcept { return resolve_node( p ) != nullptr; }
    };
    using forest_domain_view_policy      = detail::ForestDomainViewOps<forest_domain_descriptor>;
    using forest_domain_policy           = detail::ForestDomainOps<forest_domain_descriptor>;
    static constexpr index_type no_block = ManagerT::address_traits::no_block;

  private:
    index_type _binding_id;
    bool       bind( const char* domain_key ) noexcept
    {
        if ( !ManagerT::is_initialized() )
            return false;
        char buf[detail::kForestDomainNameCapacity]{};
        if ( domain_key != nullptr && domain_key[0] != '\0' )
        {
            if ( !detail::pmap_write_name( buf, domain_type_hash, 'n', detail::pmap_key_hash( domain_key ), 16 ) )
                return false;
        }
        else
        {
            static uint64_t seq = 1;
            do
            {
                if ( !detail::pmap_write_name( buf, domain_type_hash, 'g', seq++, 8 ) )
                    return false;
            } while ( ManagerT::has_domain( buf ) );
        }
        if ( !ManagerT::has_domain( buf ) && !ManagerT::register_domain( buf ) )
            return false;
        _binding_id = ManagerT::find_domain_by_name( buf );
        return _binding_id != 0;
    }
    forest_domain_descriptor descriptor() const noexcept { return forest_domain_descriptor( _binding_id ); }

  public:
    pmap() noexcept : _binding_id( 0 ) {}
    explicit pmap( const char* domain_key ) noexcept : _binding_id( 0 ) { bind( domain_key ); }
    const char*          domain_name() const noexcept { return descriptor().name(); }
    index_type           root_index() const noexcept { return descriptor().root_index(); }
    forest_domain_policy forest_domain_ops() noexcept
    {
        if ( _binding_id == 0 || ManagerT::find_domain_by_binding_unlocked( _binding_id ) == nullptr )
            bind( nullptr );
        return forest_domain_policy( descriptor() );
    }
    forest_domain_view_policy forest_domain_view_ops() const noexcept
    {
        return forest_domain_view_policy( descriptor() );
    }
    bool empty() const noexcept { return root_index() == static_cast<index_type>( 0 ); }
/*
### pmm-pmap-size
*/
    size_t size() const noexcept
    {
        const index_type root = root_index();
        return root == static_cast<index_type>( 0 ) ? 0 : detail::avl_subtree_count( node_pptr( root ) );
    }
/*
### pmm-pmap-insert
*/
    node_pptr insert( const _K& key, const _V& val ) noexcept
    {
        auto ops = forest_domain_ops();
        if ( ops.root_index_ptr() == nullptr )
            return node_pptr();
        node_pptr existing = ops.find( key );
        if ( !existing.is_null() )
        {
            if ( node_type* obj = ManagerT::template resolve<node_type>( existing ); obj != nullptr )
                obj->value = val;
            return existing;
        }
        node_pptr  new_node = ManagerT::template allocate_typed<node_type>();
        node_type* obj      = new_node.is_null() ? nullptr : ManagerT::template resolve<node_type>( new_node );
        if ( obj == nullptr )
            return node_pptr();
        obj->key   = key;
        obj->value = val;
        detail::avl_init_node( new_node );
        ops.insert( new_node );
        return new_node;
    }
    node_pptr find( const _K& key ) const noexcept { return forest_domain_view_ops().find( key ); }
    bool      contains( const _K& key ) const noexcept { return !find( key ).is_null(); }
/*
### pmm-pmap-erase
*/
    bool erase( const _K& key ) noexcept
    {
        auto        ops  = forest_domain_policy( descriptor() );
        index_type* root = ops.root_index_ptr();
        node_pptr   t    = root == nullptr ? node_pptr() : ops.find( key );
        if ( t.is_null() )
            return false;
        detail::avl_remove( t, *root );
        ManagerT::template deallocate_typed<node_type>( t );
        return true;
    }
/*
### pmm-pmap-clear
*/
    void clear() noexcept
    {
        auto        ops  = forest_domain_policy( descriptor() );
        index_type* root = ops.root_index_ptr();
        if ( root == nullptr )
            return;
        if ( *root != static_cast<index_type>( 0 ) )
            detail::avl_clear_subtree( node_pptr( *root ),
                                       []( node_pptr p ) { ManagerT::template deallocate_typed<node_type>( p ); } );
        *root = static_cast<index_type>( 0 );
    }
    void reset() noexcept { forest_domain_policy( descriptor() ).reset_root(); }
    using iterator = detail::AvlInorderIterator<node_pptr>;
/*
### pmm-pmap-begin
*/
    iterator begin() const noexcept
    {
        const index_type root = root_index();
        if ( root == static_cast<index_type>( 0 ) )
            return iterator();
        return iterator( detail::avl_min_node( node_pptr( root ) ).offset() );
    }
    iterator end() const noexcept { return iterator( static_cast<index_type>( 0 ) ); }
};
template <typename _K, typename _V, typename ManagerT> struct node_type_for<pmap<_K, _V, ManagerT>>
{
    static constexpr NodeType value = NodeType::PMap;
};
}
