#pragma once

#include "pmm/types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm
{

template <typename T, typename ManagerT> struct parray
{
    static_assert( std::is_trivially_copyable_v<T>, "parray<T>: T must be trivially copyable for PAP persistence" );

    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using value_type   = T;

    std::uint32_t _size;
    std::uint32_t _capacity;
    index_type    _data_idx;

    parray() noexcept : _size( 0 ), _capacity( 0 ), _data_idx( detail::kNullIdx_v<typename ManagerT::address_traits> )
    {
    }

    ~parray() noexcept = default;

    std::size_t size() const noexcept { return static_cast<std::size_t>( _size ); }

    bool empty() const noexcept { return _size == 0; }

    std::size_t capacity() const noexcept { return static_cast<std::size_t>( _capacity ); }

    T* at( std::size_t i ) noexcept
    {
        if ( i >= static_cast<std::size_t>( _size ) )
            return nullptr;
        T* data = resolve_data();
        return ( data != nullptr ) ? ( data + i ) : nullptr;
    }

    const T* at( std::size_t i ) const noexcept
    {
        if ( i >= static_cast<std::size_t>( _size ) )
            return nullptr;
        const T* data = resolve_data();
        return ( data != nullptr ) ? ( data + i ) : nullptr;
    }

    T operator[]( std::size_t i ) const noexcept
    {
        const T* data = resolve_data();
        return ( data != nullptr ) ? data[i] : T{};
    }

    T* front() noexcept { return at( 0 ); }

    const T* front() const noexcept { return at( 0 ); }

    T* back() noexcept { return ( _size > 0 ) ? at( static_cast<std::size_t>( _size ) - 1 ) : nullptr; }

    const T* back() const noexcept { return ( _size > 0 ) ? at( static_cast<std::size_t>( _size ) - 1 ) : nullptr; }

    T* data() noexcept { return resolve_data(); }

    const T* data() const noexcept { return resolve_data(); }

    bool push_back( const T& value ) noexcept
    {
        if ( !ensure_capacity( _size + 1 ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;
        d[_size] = value;
        ++_size;
        return true;
    }

    void pop_back() noexcept
    {
        if ( _size > 0 )
            --_size;
    }

    bool set( std::size_t i, const T& value ) noexcept
    {
        if ( i >= static_cast<std::size_t>( _size ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;
        d[i] = value;
        return true;
    }

    bool reserve( std::size_t n ) noexcept
    {
        if ( n > static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) )
            return false;
        return ensure_capacity( static_cast<std::uint32_t>( n ) );
    }

    bool resize( std::size_t n ) noexcept
    {
        if ( n > static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) )
            return false;
        auto new_size = static_cast<std::uint32_t>( n );
        if ( new_size > _size )
        {
            if ( !ensure_capacity( new_size ) )
                return false;
            T* d = resolve_data();
            if ( d == nullptr )
                return false;

            std::memset( d + _size, 0, static_cast<std::size_t>( new_size - _size ) * sizeof( T ) );
        }
        _size = new_size;
        return true;
    }

    bool insert( std::size_t index, const T& value ) noexcept
    {
        if ( index > static_cast<std::size_t>( _size ) )
            return false;
        if ( !ensure_capacity( _size + 1 ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;

        if ( index < static_cast<std::size_t>( _size ) )
            std::memmove( d + index + 1, d + index, ( static_cast<std::size_t>( _size ) - index ) * sizeof( T ) );
        d[index] = value;
        ++_size;
        return true;
    }

    bool erase( std::size_t index ) noexcept
    {
        if ( index >= static_cast<std::size_t>( _size ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;

        if ( index + 1 < static_cast<std::size_t>( _size ) )
            std::memmove( d + index, d + index + 1, ( static_cast<std::size_t>( _size ) - index - 1 ) * sizeof( T ) );
        --_size;
        return true;
    }

    void clear() noexcept { _size = 0; }

    void free_data() noexcept
    {
        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            ManagerT::deallocate( detail::resolve_granule_ptr<typename ManagerT::address_traits>(
                ManagerT::backend().base_ptr(), _data_idx ) );
            _data_idx = detail::kNullIdx_v<typename ManagerT::address_traits>;
        }
        _size     = 0;
        _capacity = 0;
    }

    bool operator==( const parray& other ) const noexcept
    {
        if ( this == &other )
            return true;
        if ( _size != other._size )
            return false;
        if ( _size == 0 )
            return true;
        const T* a = resolve_data();
        const T* b = other.resolve_data();
        if ( a == nullptr || b == nullptr )
            return ( a == b );
        return std::memcmp( a, b, static_cast<std::size_t>( _size ) * sizeof( T ) ) == 0;
    }

    bool operator!=( const parray& other ) const noexcept { return !( *this == other ); }

  private:
    T* resolve_data() const noexcept
    {
        return reinterpret_cast<T*>( detail::resolve_granule_ptr<typename ManagerT::address_traits>(
            ManagerT::backend().base_ptr(), _data_idx ) );
    }

    bool ensure_capacity( std::uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return true;

        std::uint32_t new_cap = _capacity * 2;
        if ( new_cap < required )
            new_cap = required;
        if ( new_cap < 4 )
            new_cap = 4;

        std::size_t alloc_size = static_cast<std::size_t>( new_cap ) * sizeof( T );
        if ( sizeof( T ) > 0 && alloc_size / sizeof( T ) != static_cast<std::size_t>( new_cap ) )
            return false;

        void* new_raw = ManagerT::allocate( alloc_size );
        if ( new_raw == nullptr )
            return false;

        std::uint8_t* base        = ManagerT::backend().base_ptr();
        index_type    new_dat_idx = detail::ptr_to_granule_idx<typename ManagerT::address_traits>( base, new_raw );

        if ( _size > 0 && _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            T* old_data = resolve_data();
            if ( old_data != nullptr )
                std::memcpy( new_raw, old_data, static_cast<std::size_t>( _size ) * sizeof( T ) );
        }

        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
            ManagerT::deallocate( detail::resolve_granule_ptr<typename ManagerT::address_traits>( base, _data_idx ) );

        _data_idx = new_dat_idx;
        _capacity = new_cap;
        return true;
    }
};

} // namespace pmm
