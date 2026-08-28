#pragma once
#include "pmm/avl_tree_mixin.h"
#include "pmm/forest_registry.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
namespace pmm
{
template <typename ManagerT> struct pstringview;
/*
## pmm-pstringview
req: feat-003, fr-007, fr-008, fr-029, ur-003, dr-007, con-012, feat-008, fr-017, ur-008
*/
template <typename ManagerT> struct pstringview
{
  private:
    static std::string_view c_view( const char* s ) noexcept { return s ? std::string_view( s ) : std::string_view(); }

  public:
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
        static index_type            root_index() noexcept
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
        static int compare_key( std::string_view key, node_pptr cur ) noexcept
        {
            node_type* obj = resolve_node( cur );
            return obj != nullptr ? key.compare( obj->view() ) : 0;
        }
        static int compare_key( const char* key, node_pptr cur ) noexcept { return compare_key( c_view( key ), cur ); }
        static bool less_node( node_pptr lhs, node_pptr rhs ) noexcept
        {
            node_type* lhs_obj = resolve_node( lhs );
            node_type* rhs_obj = resolve_node( rhs );
            return lhs_obj != nullptr && rhs_obj != nullptr && lhs_obj->view().compare( rhs_obj->view() ) < 0;
        }
        static bool validate_node( node_pptr p ) noexcept { return resolve_node( p ) != nullptr; }
    };
    using forest_domain_policy = detail::ForestDomainOps<forest_domain_descriptor>;
    static forest_domain_policy forest_domain_ops() noexcept { return forest_domain_policy{}; }
    uint32_t                    length;
    char                        str[1];
    pstringview() = delete;
    const char*      c_str() const noexcept { return str; }
    size_t           size() const noexcept { return static_cast<size_t>( length ); }
    bool             empty() const noexcept { return length == 0; }
    std::string_view view() const noexcept { return std::string_view( str, size() ); }
    bool             operator==( std::string_view s ) const noexcept { return view() == s; }
    bool             operator==( const char* s ) const noexcept { return view() == c_view( s ); }
    bool             operator==( const pstringview& other ) const noexcept { return this == &other || view() == other.view(); }
    bool operator!=( std::string_view s ) const noexcept { return !( *this == s ); }
    bool operator!=( const char* s ) const noexcept { return !( *this == s ); }
    bool operator!=( const pstringview& other ) const noexcept { return !( *this == other ); }
    bool operator<( const pstringview& other ) const noexcept { return view().compare( other.view() ) < 0; }
/*
### pmm-pstringview-intern
*/
    static psview_pptr intern( std::string_view s ) noexcept { return _intern( s ); }
    static psview_pptr intern( const char* s ) noexcept { return _intern( c_view( s ) ); }
    static void        reset() noexcept
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

  private:
    static psview_pptr _intern( std::string_view s ) noexcept
    {
        if ( !ManagerT::is_initialized() )
            return psview_pptr();
        typename ManagerT::thread_policy::unique_lock_type lock( ManagerT::_mutex );
        return ManagerT::intern_symbol_unlocked( s );
    }
};
template <typename ManagerT> struct node_type_for<pstringview<ManagerT>>
{
    static constexpr NodeType value = NodeType::PStringView;
};
}
