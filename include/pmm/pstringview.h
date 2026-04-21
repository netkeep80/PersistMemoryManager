#pragma once

#include "pmm/avl_tree_mixin.h"
#include "pmm/forest_registry.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pmm
{

template <typename ManagerT> struct pstringview;

template <typename ManagerT> struct pstringview
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using psview_pptr  = typename ManagerT::template pptr<pstringview>;

    struct forest_domain_descriptor
    {
        using manager_type = ManagerT;
        using index_type   = typename ManagerT::index_type;
        using node_type    = pstringview;
        using node_pptr    = psview_pptr;

        static constexpr const char* name() noexcept { return detail::kSystemDomainSymbols; }

        static index_type root_index() noexcept
        {
            auto* domain = ManagerT::symbol_domain_record_unlocked();
            return ManagerT::forest_domain_root_index_unlocked( domain );
        }

        static index_type* root_index_ptr() noexcept
        {
            auto* domain = ManagerT::symbol_domain_record_unlocked();
            return ManagerT::forest_domain_root_index_ptr_unlocked( domain );
        }

        static node_type* resolve_node( node_pptr p ) noexcept { return ManagerT::template resolve<node_type>( p ); }

        static int compare_key( const char* key, node_pptr cur ) noexcept
        {
            if ( key == nullptr )
                key = "";
            node_type* obj = resolve_node( cur );
            return ( obj != nullptr ) ? std::strcmp( key, obj->c_str() ) : 0;
        }

        static bool less_node( node_pptr lhs, node_pptr rhs ) noexcept
        {
            node_type* lhs_obj = resolve_node( lhs );
            node_type* rhs_obj = resolve_node( rhs );
            return lhs_obj != nullptr && rhs_obj != nullptr && std::strcmp( lhs_obj->c_str(), rhs_obj->c_str() ) < 0;
        }

        static bool validate_node( node_pptr p ) noexcept { return resolve_node( p ) != nullptr; }
    };

    using forest_domain_policy = detail::ForestDomainOps<forest_domain_descriptor>;

    static forest_domain_policy forest_domain_ops() noexcept { return forest_domain_policy{}; }

    std::uint32_t length;
    char          str[1];

    explicit pstringview( const char* s ) noexcept : length( 0 ), str{ '\0' } { _interned = _intern( s ); }

    operator psview_pptr() const noexcept { return _interned; }

    const char* c_str() const noexcept { return str; }

    std::size_t size() const noexcept { return static_cast<std::size_t>( length ); }

    bool empty() const noexcept { return length == 0; }

    bool operator==( const char* s ) const noexcept
    {
        if ( s == nullptr )
            return length == 0;
        return std::strcmp( c_str(), s ) == 0;
    }

    bool operator==( const pstringview& other ) const noexcept
    {

        if ( this == &other )
            return true;

        if ( length != other.length )
            return false;
        return std::strcmp( str, other.str ) == 0;
    }

    bool operator!=( const char* s ) const noexcept { return !( *this == s ); }

    bool operator!=( const pstringview& other ) const noexcept { return !( *this == other ); }

    bool operator<( const pstringview& other ) const noexcept { return std::strcmp( c_str(), other.c_str() ) < 0; }

    static psview_pptr intern( const char* s ) noexcept { return _intern( s ); }

    static void reset() noexcept
    {
        if ( !ManagerT::is_initialized() )
            return;
        typename ManagerT::thread_policy::unique_lock_type lock( ManagerT::_mutex );
        forest_domain_ops().reset_root();
    }

    static index_type root_index() noexcept
    {
        if ( !ManagerT::is_initialized() )
            return static_cast<index_type>( 0 );
        typename ManagerT::thread_policy::shared_lock_type lock( ManagerT::_mutex );
        return forest_domain_ops().root_index();
    }

    ~pstringview() = default;

  private:
    psview_pptr _interned;

    static psview_pptr _intern( const char* s ) noexcept
    {
        if ( !ManagerT::is_initialized() )
            return psview_pptr();
        typename ManagerT::thread_policy::unique_lock_type lock( ManagerT::_mutex );
        return ManagerT::intern_symbol_unlocked( s );
    }
};

} // namespace pmm
