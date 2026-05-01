#pragma once
#include <cstddef>
#include <limits>
#include <new>
namespace pmm
{
/*
## pmm-pallocator
req: feat-006, fr-027, ur-003, if-001, feat-008, ur-008
*/
template <typename T, typename ManagerT> struct pallocator
{
    using value_type                                   = T;
    using size_type                                    = size_t;
    using difference_type                              = std::ptrdiff_t;
    using propagate_on_container_copy_assignment       = std::true_type;
    using propagate_on_container_move_assignment       = std::true_type;
    using propagate_on_container_swap                  = std::true_type;
    using is_always_equal                              = std::true_type;
    constexpr pallocator() noexcept                    = default;
    constexpr pallocator( const pallocator& ) noexcept = default;
    template <typename U> constexpr pallocator( const pallocator<U, ManagerT>& ) noexcept {}
    [[nodiscard]] T* allocate( size_t n )
    {
        if ( n == 0 )
            throw std::bad_alloc();
        if ( n > max_size() )
            throw std::bad_alloc();
        void* raw = ManagerT::allocate( n * sizeof( T ) );
        if ( raw == nullptr )
            throw std::bad_alloc();
        return static_cast<T*>( raw );
    }
    void   deallocate( T* p, size_t ) noexcept { ManagerT::deallocate( static_cast<void*>( p ) ); }
    size_t max_size() const noexcept { return ( std::numeric_limits<size_t>::max )() / sizeof( T ); }
    template <typename U> bool operator==( const pallocator<U, ManagerT>& ) const noexcept { return true; }
    template <typename U> bool operator!=( const pallocator<U, ManagerT>& ) const noexcept { return false; }
};
}
