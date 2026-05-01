#pragma once
#include "pmm/pptr.h"
#include "pmm/types.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
namespace pmm
{
/*
## pmm-pstring
req: feat-003, fr-007, fr-008, fr-029, ur-003, dr-007, feat-008, fr-031, ur-008
*/
template <typename ManagerT> struct pstring
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    uint32_t   _length;
    uint32_t   _capacity;
    index_type _data_idx;
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
    size_t size() const noexcept { return static_cast<size_t>( _length ); }
    bool   empty() const noexcept { return _length == 0; }
    char   operator[]( size_t i ) const noexcept
    {
        char* data = resolve_data();
        return ( data != nullptr ) ? data[i] : '\0';
    }
    bool assign( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";
        auto len = static_cast<uint32_t>( std::strlen( s ) );
        if ( !ensure_capacity( len ) )
            return false;
        char* data = resolve_data();
        if ( data == nullptr )
            return false;
        std::memcpy( data, s, static_cast<size_t>( len ) + 1 );
        _length = len;
        return true;
    }
    bool append( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";
        auto add_len = static_cast<uint32_t>( std::strlen( s ) );
        if ( add_len == 0 )
            return true;
        uint32_t new_len = _length + add_len;
        if ( new_len < _length )
            return false;
        if ( !ensure_capacity( new_len ) )
            return false;
        char* data = resolve_data();
        if ( data == nullptr )
            return false;
        std::memcpy( data + _length, s, static_cast<size_t>( add_len ) + 1 );
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
            ManagerT::template deallocate_typed<char>( pmm::pptr<char, ManagerT>( _data_idx ) );
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
    char* resolve_data() const noexcept { return pmm::pptr<char, ManagerT>( _data_idx ).resolve_unchecked(); }
    bool  ensure_capacity( uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return true;
        uint32_t new_cap = _capacity * 2;
        if ( new_cap < required )
            new_cap = required;
        if ( new_cap < 16 )
            new_cap = 16;
        const bool                had_data  = _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits>;
        const std::size_t         old_count = had_data ? static_cast<std::size_t>( _length ) + 1 : 0;
        const std::size_t         new_count = static_cast<std::size_t>( new_cap ) + 1;
        pmm::pptr<char, ManagerT> old_p( _data_idx );
        pmm::pptr<char, ManagerT> new_p = ManagerT::template reallocate_typed<char>( old_p, old_count, new_count );
        if ( new_p.is_null() )
            return false;
        _data_idx  = new_p.offset();
        _capacity  = new_cap;
        char* data = resolve_data();
        if ( data == nullptr )
            return false;
        data[_length] = '\0';
        return true;
    }
};
template <typename ManagerT> struct node_type_for<pstring<ManagerT>>
{
    static constexpr NodeType value = NodeType::PString;
};
}
