#pragma once
#include "pmm/pptr.h"
#include "pmm/types.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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
        const char* ptr = data();
        return ( ptr != nullptr ) ? ptr : "";
    }
    char* data() noexcept
    {
        if ( _data_idx == detail::kNullIdx_v<typename ManagerT::address_traits> )
            return nullptr;
        return resolve_data();
    }
    const char* data() const noexcept
    {
        if ( _data_idx == detail::kNullIdx_v<typename ManagerT::address_traits> )
            return nullptr;
        return resolve_data();
    }
    size_t size() const noexcept { return static_cast<size_t>( _length ); }
    bool   empty() const noexcept { return _length == 0; }
    char   operator[]( size_t i ) const noexcept
    {
        const char* ptr = data();
        return ( ptr != nullptr ) ? ptr[i] : '\0';
    }
    bool assign( const char* s ) noexcept
    {
        if ( s == nullptr )
            return assign( nullptr, 0 );
        return assign( s, std::strlen( s ) );
    }
    bool assign( const char* s, size_t len ) noexcept
    {
        if ( len > static_cast<size_t>( std::numeric_limits<uint32_t>::max() ) )
            return false;
        if ( len == 0 )
        {
            clear();
            return true;
        }
        if ( s == nullptr )
            return false;
        size_t alias_offset = 0;
        const bool aliased  = source_alias_offset( s, alias_offset );
        if ( aliased && len > static_cast<size_t>( _capacity ) + 1 - alias_offset )
            return false;
        const auto length = static_cast<uint32_t>( len );
        if ( !ensure_capacity( length ) )
            return false;
        char* ptr = data();
        if ( ptr == nullptr )
            return false;
        const char* source = aliased ? ptr + alias_offset : s;
        std::memmove( ptr, source, len );
        ptr[len] = '\0';
        _length  = length;
        return true;
    }
    bool append( const char* s ) noexcept
    {
        if ( s == nullptr )
            return true;
        return append( s, std::strlen( s ) );
    }
    bool append( const char* s, size_t len ) noexcept
    {
        if ( len == 0 )
            return true;
        if ( s == nullptr || len > static_cast<size_t>( std::numeric_limits<uint32_t>::max() - _length ) )
            return false;
        size_t alias_offset = 0;
        const bool aliased  = source_alias_offset( s, alias_offset );
        if ( aliased && len > static_cast<size_t>( _capacity ) + 1 - alias_offset )
            return false;
        const uint32_t add_len = static_cast<uint32_t>( len );
        const uint32_t new_len = _length + add_len;
        if ( !ensure_capacity( new_len ) )
            return false;
        char* ptr = data();
        if ( ptr == nullptr )
            return false;
        const char* source = aliased ? ptr + alias_offset : s;
        std::memmove( ptr + _length, source, len );
        ptr[new_len] = '\0';
        _length      = new_len;
        return true;
    }
    void clear() noexcept
    {
        _length = 0;
        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            char* ptr = data();
            if ( ptr != nullptr )
                ptr[0] = '\0';
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
        const size_t len = std::strlen( s );
        if ( len != static_cast<size_t>( _length ) )
            return false;
        const char* ptr = data();
        return len == 0 || ( ptr != nullptr && std::memcmp( ptr, s, len ) == 0 );
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
        const char* lhs = data();
        const char* rhs = other.data();
        return lhs != nullptr && rhs != nullptr && std::memcmp( lhs, rhs, static_cast<size_t>( _length ) ) == 0;
    }
    bool operator!=( const pstring& other ) const noexcept { return !( *this == other ); }
    bool operator<( const pstring& other ) const noexcept
    {
        const size_t common = ( std::min )( static_cast<size_t>( _length ), static_cast<size_t>( other._length ) );
        const char*  lhs    = data();
        const char*  rhs    = other.data();
        if ( common > 0 )
        {
            if ( lhs == nullptr || rhs == nullptr )
                return lhs == nullptr && rhs != nullptr;
            const int cmp = std::memcmp( lhs, rhs, common );
            if ( cmp != 0 )
                return cmp < 0;
        }
        return _length < other._length;
    }

  private:
    char* resolve_data() const noexcept { return pmm::pptr<char, ManagerT>( _data_idx ).resolve_unchecked(); }
    bool source_alias_offset( const char* source, size_t& offset ) const noexcept
    {
        offset = 0;
        if ( source == nullptr || _data_idx == detail::kNullIdx_v<typename ManagerT::address_traits> )
            return false;
        const char* ptr = data();
        if ( ptr == nullptr )
            return false;
        const uintptr_t begin      = reinterpret_cast<uintptr_t>( ptr );
        const uintptr_t source_addr = reinterpret_cast<uintptr_t>( source );
        const size_t    storage     = static_cast<size_t>( _capacity ) + 1;
        if ( storage > std::numeric_limits<uintptr_t>::max() - begin )
            return false;
        const uintptr_t end = begin + storage;
        if ( source_addr < begin || source_addr >= end )
            return false;
        offset = static_cast<size_t>( source_addr - begin );
        return true;
    }
    bool ensure_capacity( uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return true;
        if ( static_cast<uint64_t>( required ) + 1 > static_cast<uint64_t>( std::numeric_limits<size_t>::max() ) )
            return false;
        uint64_t candidate = ( std::max )( static_cast<uint64_t>( required ), uint64_t{ 16 } );
        if ( _capacity != 0 )
        {
            const uint64_t doubled = static_cast<uint64_t>( _capacity ) * 2;
            candidate = ( std::max )( candidate,
                                      ( std::min )( doubled,
                                                    static_cast<uint64_t>( std::numeric_limits<uint32_t>::max() ) ) );
        }
        const uint32_t            new_cap   = static_cast<uint32_t>( candidate );
        const bool                had_data  = _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits>;
        const std::size_t         old_count = had_data ? static_cast<std::size_t>( _length ) + 1 : 0;
        const std::size_t         new_count = static_cast<std::size_t>( new_cap ) + 1;
        pmm::pptr<char, ManagerT> old_p( _data_idx );
        pmm::pptr<char, ManagerT> new_p = ManagerT::template reallocate_typed<char>( old_p, old_count, new_count );
        if ( new_p.is_null() )
            return false;
        _data_idx = new_p.offset();
        _capacity = new_cap;
        char* ptr = data();
        if ( ptr == nullptr )
            return false;
        ptr[_length] = '\0';
        return true;
    }
};
template <typename ManagerT> struct node_type_for<pstring<ManagerT>>
{
    static constexpr NodeType value = NodeType::PString;
};
}
