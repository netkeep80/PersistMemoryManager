/**
 * @file pmm/pmap.h
 * @brief pmap<_K,_V,ManagerT> — персистентный AVL-словарь, rooted in a forest domain.
 *
 * Узлы — блоки в ПАП, пары (_K,_V); AVL-поля Block<AT> используются как у pstringview.
 * Корень дерева хранится в forest-domain binding `container/pmap/<type>/<binding>`;
 * в объекте pmap лежит только binding id. erase/clear освобождают узлы.
 *
 * @see pstringview.h, avl_tree_mixin.h, tree_node.h
 */

#pragma once

#include "pmm/avl_tree_mixin.h"
#include "pmm/forest_registry.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

template <typename _K, typename _V, typename ManagerT> struct pmap;

/**
 * @brief Stable persistent type identity for `pmap` domain bindings.
 *
 * Default identity mixes `sizeof`, `alignof`, and a small set of
 * `<type_traits>` category bits — ABI-stable values the persisted payload
 * already depends on. Applications specialize `tag` with a fixed ASCII
 * string to pin identity across toolchains or disambiguate structurally
 * identical PODs.
 */
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

// ─── compact pmap domain-name builder ─────────────────────────────────────────
//
// Name format: "container/pmap/<TTTTTTTT>/<k><HH..>"
//   <T...> is an 8-hex-digit type fingerprint for (_K,_V),
//   <k>    is the binding-kind marker: 'g' (generated) or 'n' (named),
//   <HH..> is the 8- or 16-hex-digit value (sequence for 'g', key hash for 'n').
//
// Identity is ABI-stable: sizeof + alignof + a fixed subset of <type_traits>
// bits, plus pmap_type_identity<T>::tag when the application pins one.

constexpr std::uint32_t pmap_fnv1a( std::uint32_t h, std::uint64_t v, unsigned bytes ) noexcept
{
    for ( unsigned i = 0; i < bytes; ++i, v >>= 8 )
    {
        h ^= static_cast<std::uint8_t>( v & 0xffull );
        h *= 16777619u;
    }
    return h;
}

template <typename T> constexpr std::uint32_t pmap_type_fp() noexcept
{
    const std::uint64_t traits =
        ( std::uint64_t{ std::is_integral_v<T> } << 0 ) | ( std::uint64_t{ std::is_floating_point_v<T> } << 1 ) |
        ( std::uint64_t{ std::is_signed_v<T> } << 2 ) | ( std::uint64_t{ std::is_unsigned_v<T> } << 3 ) |
        ( std::uint64_t{ std::is_pointer_v<T> } << 4 ) | ( std::uint64_t{ std::is_class_v<T> } << 5 ) |
        ( std::uint64_t{ std::is_enum_v<T> } << 6 ) | ( std::uint64_t{ std::is_trivially_copyable_v<T> } << 7 ) |
        ( std::uint64_t{ std::is_standard_layout_v<T> } << 8 );
    std::uint32_t h = 2166136261u;
    h               = pmap_fnv1a( h, sizeof( T ), 8 );
    h               = pmap_fnv1a( h, alignof( T ), 8 );
    h               = pmap_fnv1a( h, traits, 8 );
    for ( const char* t = pmm::pmap_type_identity<T>::tag; t != nullptr && *t != '\0'; ++t )
        h = pmap_fnv1a( h, static_cast<std::uint8_t>( *t ), 1 );
    return h;
}

inline std::uint64_t pmap_key_hash( const char* key ) noexcept
{
    std::uint64_t h = 14695981039346656037ull;
    for ( ; key != nullptr && *key != '\0'; ++key )
    {
        h ^= static_cast<std::uint8_t>( *key );
        h *= 1099511628211ull;
    }
    return h;
}

// Write "container/pmap/<type_fp>/<kind><value>" into a fixed-size buffer.
inline bool pmap_write_name( char ( &out )[kForestDomainNameCapacity], std::uint32_t type_fp, char kind,
                             std::uint64_t value, unsigned value_hex_digits ) noexcept
{
    constexpr const char* kPrefix = "container/pmap/";
    const unsigned        needed  = 15 + 8 + 1 + 1 + value_hex_digits + 1; // prefix + fp + '/' + kind + value + NUL
    if ( needed > kForestDomainNameCapacity )
        return false;
    std::size_t p = 0;
    for ( const char* s = kPrefix; *s != '\0'; ++s )
        out[p++] = *s;
    auto put_hex = [&]( std::uint64_t v, unsigned digits )
    {
        for ( unsigned i = digits; i-- > 0; )
        {
            const std::uint8_t nib = static_cast<std::uint8_t>( ( v >> ( i * 4 ) ) & 0x0full );
            out[p++]               = static_cast<char>( nib < 10 ? ( '0' + nib ) : ( 'a' + ( nib - 10 ) ) );
        }
    };
    put_hex( type_fp, 8 );
    out[p++] = '/';
    out[p++] = kind;
    put_hex( value, value_hex_digits );
    out[p] = '\0';
    return true;
}

} // namespace detail

/**
 * @brief Персистентный ассоциативный контейнер на основе AVL-дерева.
 *
 * `pmap` — это тонкий фасад над forest-domain binding-ом, имя которого
 * зависит от (_K, _V) и либо от пользовательского domain key, либо от
 * сгенерированной последовательности. В объекте хранится только binding id;
 * корень AVL-дерева живёт в registry менеджера.
 */
template <typename _K, typename _V, typename ManagerT> struct pmap
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using node_type    = pmap_node<_K, _V>;
    using node_pptr    = typename ManagerT::template pptr<node_type>;

    static constexpr std::uint32_t domain_type_hash = detail::pmap_fnv1a(
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

        static int compare_key( const _K& key, node_pptr cur ) noexcept
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

    using forest_domain_view_policy = detail::ForestDomainViewOps<forest_domain_descriptor>;
    using forest_domain_policy      = detail::ForestDomainOps<forest_domain_descriptor>;

    static constexpr index_type no_block = ManagerT::address_traits::no_block;

  private:
    index_type _binding_id;

    // Bind (or rebind) to a domain. When domain_key == nullptr, allocate a fresh
    // generated binding; otherwise hash the key into a stable named binding.
    bool bind( const char* domain_key ) noexcept
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
            static std::uint64_t seq = 1;
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
    /// @brief Пустой словарь с уникальным generated binding (создаётся лениво).
    pmap() noexcept : _binding_id( 0 ) {}

    /// @brief Словарь, привязанный к стабильному пользовательскому ключу domain-а.
    explicit pmap( const char* domain_key ) noexcept : _binding_id( 0 ) { bind( domain_key ); }

    /// @brief Имя forest-domain binding-а этого фасада; пустая строка до первого bind.
    const char* domain_name() const noexcept { return descriptor().name(); }

    /// @brief Текущий root binding forest-domain-а; 0 = пустое дерево.
    index_type root_index() const noexcept { return descriptor().root_index(); }

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

    std::size_t size() const noexcept
    {
        const index_type root = root_index();
        return root == static_cast<index_type>( 0 ) ? 0 : detail::avl_subtree_count( node_pptr( root ) );
    }

    /// @brief Вставить или обновить пару ключ-значение. Возвращает pptr на узел; null при OOM.
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

    /// @brief Удалить узел по ключу; true — если найден и удалён.
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

    /// @brief Удалить все элементы, освободив их блоки в ПАП.
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

    /// @brief Обнулить root binding (данные в ПАП не освобождаются). Для тестов.
    void reset() noexcept { forest_domain_policy( descriptor() ).reset_root(); }

    using iterator = detail::AvlInorderIterator<node_pptr>;

    iterator begin() const noexcept
    {
        const index_type root = root_index();
        if ( root == static_cast<index_type>( 0 ) )
            return iterator();
        return iterator( detail::avl_min_node( node_pptr( root ) ).offset() );
    }

    iterator end() const noexcept { return iterator( static_cast<index_type>( 0 ) ); }
};

} // namespace pmm
