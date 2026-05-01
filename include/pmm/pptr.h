#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace pmm
{
namespace detail
{
template <typename ManagerT> struct manager_index_type
{
    using type = uint32_t;
};
template <typename ManagerT>
    requires requires { typename ManagerT::address_traits::index_type; }
struct manager_index_type<ManagerT>
{
    using type = typename ManagerT::address_traits::index_type;
};
}
template <class T, class ManagerT>
    requires( !std::is_void_v<ManagerT> )
/*
## pmm-pptr
req: feat-003, fr-007, fr-008, fr-030, fr-033, dr-007, qa-port-001, con-007, rule-001, fr-032, qa-mem-001, ur-003
*/
class pptr
{
  public:
    using element_type = T;
    using manager_type = ManagerT;
    using index_type   = typename detail::manager_index_type<ManagerT>::type;

  private:
    index_type _idx;

  public:
    constexpr pptr() noexcept : _idx( 0 ) {}
    constexpr explicit pptr( index_type idx ) noexcept : _idx( idx ) {}
    constexpr pptr( const pptr& ) noexcept            = default;
    constexpr pptr& operator=( const pptr& ) noexcept = default;
    ~pptr() noexcept                                  = default;
    pptr&                operator++()                 = delete;
    pptr                 operator++( int )            = delete;
    pptr&                operator--()                 = delete;
    pptr                 operator--( int )            = delete;
    constexpr bool       is_null() const noexcept { return _idx == 0; }
    constexpr explicit   operator bool() const noexcept { return _idx != 0; }
    constexpr index_type offset() const noexcept { return _idx; }
/*
### pmm-pptr-byte_offset
req: dr-007, qa-port-001
*/
    constexpr size_t byte_offset() const noexcept
    {
        return static_cast<size_t>( _idx ) * ManagerT::address_traits::granule_size;
    }
    constexpr bool operator==( const pptr& other ) const noexcept { return _idx == other._idx; }
    constexpr bool operator!=( const pptr& other ) const noexcept { return _idx != other._idx; }
    bool           operator<( const pptr& other ) const noexcept
    {
        static_assert(
            requires( const T& a, const T& b ) {
                { a < b } -> std::convertible_to<bool>;
            }, "" );
        if ( is_null() && !other.is_null() )
            return true;
        if ( !is_null() && other.is_null() )
            return false;
        if ( is_null() && other.is_null() )
            return false;
        return **this < *other;
    }
    T&   operator*() const noexcept { return *ManagerT::template resolve_checked<T>( *this ); }
    T*   operator->() const noexcept { return ManagerT::template resolve_checked<T>( *this ); }
    T*   resolve() const noexcept { return ManagerT::template resolve_checked<T>( *this ); }
    T*   resolve_unchecked() const noexcept { return ManagerT::template resolve_unchecked<T>( *this ); }
    auto try_tree_node() const noexcept { return ManagerT::try_tree_node( *this ); }
    auto& tree_node_unchecked() const noexcept { return ManagerT::tree_node_unchecked( *this ); }
};
}
