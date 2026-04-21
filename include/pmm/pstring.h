#pragma once

#include "pmm/types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm
{

template <typename ManagerT> struct pstring
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;

    std::uint32_t _length;
    std::uint32_t _capacity;
    index_type    _data_idx;

    pstring() noexcept
        : _length( 0 ), _capacity( 0 ), _data_idx( detail::kNullIdx_v<typename ManagerT::address_traits> )
    {
    }

    ~pstring() noexcept = default;

    const char* c_str() const noexcept
    {
        if ( _data_idx == detail::kNullIdx_v<typename ManagerT::address_traits> )
            return "";
        char* data = resolve_data();
        return ( data != nullptr ) ? data : "";
    }

    std::size_t size() const noexcept { return static_cast<std::size_t>( _length ); }

    bool empty() const noexcept { return _length == 0; }

    char operator[]( std::size_t i ) const noexcept
    {
        char* data = resolve_data();
        return ( data != nullptr ) ? data[i] : '\0';
    }

    bool assign( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";
        auto len = static_cast<std::uint32_t>( std::strlen( s ) );
        if ( !ensure_capacity( len ) )
            return false;
        char* data = resolve_data();
        if ( data == nullptr )
            return false;
        std::memcpy( data, s, static_cast<std::size_t>( len ) + 1 );
        _length = len;
        return true;
    }

    bool append( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";
        auto add_len = static_cast<std::uint32_t>( std::strlen( s ) );
        if ( add_len == 0 )
            return true;
        std::uint32_t new_len = _length + add_len;
        if ( new_len < _length )
            return false;
        if ( !ensure_capacity( new_len ) )
            return false;
        char* data = resolve_data();
        if ( data == nullptr )
            return false;
        std::memcpy( data + _length, s, static_cast<std::size_t>( add_len ) + 1 );
        _length = new_len;
        return true;
    }

    void clear() noexcept
    {
        _length = 0;
        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            char* data = resolve_data();
            if ( data != nullptr )
                data[0] = '\0';
        }
    }

    void free_data() noexcept
    {
        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            ManagerT::deallocate( detail::resolve_granule_ptr<typename ManagerT::address_traits>(
                ManagerT::backend().base_ptr(), _data_idx ) );
            _data_idx = detail::kNullIdx_v<typename ManagerT::address_traits>;
        }
        _length   = 0;
        _capacity = 0;
    }

    bool operator==( const char* s ) const noexcept
    {
        if ( s == nullptr )
            return _length == 0;
        return std::strcmp( c_str(), s ) == 0;
    }

    bool operator!=( const char* s ) const noexcept { return !( *this == s ); }

    bool operator==( const pstring& other ) const noexcept
    {
        if ( this == &other )
            return true;
        if ( _length != other._length )
            return false;
        if ( _length == 0 )
            return true;
        return std::strcmp( c_str(), other.c_str() ) == 0;
    }

    bool operator!=( const pstring& other ) const noexcept { return !( *this == other ); }

    bool operator<( const pstring& other ) const noexcept { return std::strcmp( c_str(), other.c_str() ) < 0; }

  private:
    char* resolve_data() const noexcept
    {
        return reinterpret_cast<char*>( detail::resolve_granule_ptr<typename ManagerT::address_traits>(
            ManagerT::backend().base_ptr(), _data_idx ) );
    }

    bool ensure_capacity( std::uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return true;

        std::uint32_t new_cap = _capacity * 2;
        if ( new_cap < required )
            new_cap = required;
        if ( new_cap < 16 )
            new_cap = 16;

        std::size_t alloc_size = static_cast<std::size_t>( new_cap ) + 1;
        void*       new_raw    = ManagerT::allocate( alloc_size );
        if ( new_raw == nullptr )
            return false;

        std::uint8_t* base        = ManagerT::backend().base_ptr();
        index_type    new_dat_idx = detail::ptr_to_granule_idx<typename ManagerT::address_traits>( base, new_raw );

        if ( _length > 0 && _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            char* old_data = resolve_data();
            if ( old_data != nullptr )
                std::memcpy( new_raw, old_data, static_cast<std::size_t>( _length ) + 1 );
        }
        else
        {

            static_cast<char*>( new_raw )[0] = '\0';
        }

        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
            ManagerT::deallocate( detail::resolve_granule_ptr<typename ManagerT::address_traits>( base, _data_idx ) );

        _data_idx = new_dat_idx;
        _capacity = new_cap;
        return true;
    }
};

} // namespace pmm
