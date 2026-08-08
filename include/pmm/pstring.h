#pragma once
#include "pmm/pptr.h"
#include "pmm/types.h"
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
    char* data() noexcept { return has_data() ? resolve_data() : nullptr; }
    const char* data() const noexcept { return has_data() ? resolve_data() : nullptr; }
    const char* c_str() const noexcept { return data() ? data() : ""; }
    size_t size() const noexcept { return static_cast<size_t>( _length ); }
    bool empty() const noexcept { return _length == 0; }
    char operator[]( size_t i ) const noexcept { return data() ? data()[i] : '\0'; }
    bool assign( const char* s ) noexcept { return assign( s, s ? std::strlen( s ) : 0 ); }
    bool assign( const char* s, size_t len ) noexcept { return write( s, len, false ); }
    bool append( const char* s ) noexcept { return append( s, s ? std::strlen( s ) : 0 ); }
    bool append( const char* s, size_t len ) noexcept { return write( s, len, true ); }
    void clear() noexcept
    {
        _length = 0;
        char* p = data();
        if ( p )
            p[0] = '\0';
    }
    void free_data() noexcept
    {
        if ( has_data() )
            ManagerT::template deallocate_typed<char>( pmm::pptr<char, ManagerT>( _data_idx ) );
        _data_idx = detail::kNullIdx_v<typename ManagerT::address_traits>;
        _length = _capacity = 0;
    }
    bool operator==( const char* s ) const noexcept
    {
        if ( s == nullptr )
            return _length == 0;
        return compare( s, std::strlen( s ) ) == 0;
    }
    bool operator!=( const char* s ) const noexcept { return !( *this == s ); }
    bool operator==( const pstring& other ) const noexcept
    {
        return this == &other || compare( other.data(), other.size() ) == 0;
    }
    bool operator!=( const pstring& other ) const noexcept { return !( *this == other ); }
    bool operator<( const pstring& other ) const noexcept { return compare( other.data(), other.size() ) < 0; }

  private:
    bool has_data() const noexcept { return _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits>; }
    char* resolve_data() const noexcept { return pmm::pptr<char, ManagerT>( _data_idx ).resolve_unchecked(); }
    size_t source_offset( const char* s ) const noexcept
    {
        const char* p = data();
        if ( s == nullptr || p == nullptr )
            return npos;
        const uintptr_t begin = reinterpret_cast<uintptr_t>( p );
        const uintptr_t src   = reinterpret_cast<uintptr_t>( s );
        if ( src < begin )
            return npos;
        const size_t offset = static_cast<size_t>( src - begin );
        return offset <= static_cast<size_t>( _capacity ) ? offset : npos;
    }
    bool write( const char* s, size_t len, bool append_mode ) noexcept
    {
        if ( len == 0 )
        {
            if ( !append_mode )
                clear();
            return true;
        }
        const size_t base = append_mode ? size() : 0;
        if ( s == nullptr || len > static_cast<size_t>( std::numeric_limits<uint32_t>::max() ) - base )
            return false;
        const size_t offset = source_offset( s );
        if ( offset != npos && len > static_cast<size_t>( _capacity ) + 1 - offset )
            return false;
        const uint32_t new_len = static_cast<uint32_t>( base + len );
        if ( !ensure_capacity( new_len ) )
            return false;
        char* p = data();
        if ( p == nullptr )
            return false;
        if ( offset != npos )
            s = p + offset;
        std::memmove( p + base, s, len );
        p[new_len] = '\0';
        _length = new_len;
        return true;
    }
    int compare( const char* rhs, size_t rhs_len ) const noexcept
    {
        const size_t lhs_len = size();
        const size_t common = lhs_len < rhs_len ? lhs_len : rhs_len;
        const char* lhs = data();
        if ( common && ( lhs == nullptr || rhs == nullptr ) )
            return lhs ? 1 : rhs ? -1 : 0;
        const int cmp = common ? std::memcmp( lhs, rhs, common ) : 0;
        if ( cmp )
            return cmp;
        return lhs_len < rhs_len ? -1 : lhs_len > rhs_len ? 1 : 0;
    }
    bool ensure_capacity( uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return true;
        if ( static_cast<uint64_t>( required ) + 1 > std::numeric_limits<size_t>::max() )
            return false;
        uint64_t cap = _capacity ? static_cast<uint64_t>( _capacity ) * 2 : 16;
        if ( cap < required )
            cap = required;
        if ( cap > std::numeric_limits<uint32_t>::max() )
            cap = std::numeric_limits<uint32_t>::max();
        const uint32_t new_cap = static_cast<uint32_t>( cap );
        const std::size_t old_count = has_data() ? size() + 1 : 0;
        pmm::pptr<char, ManagerT> p = ManagerT::template reallocate_typed<char>(
            pmm::pptr<char, ManagerT>( _data_idx ), old_count, static_cast<std::size_t>( new_cap ) + 1 );
        if ( p.is_null() )
            return false;
        _data_idx = p.offset();
        _capacity = new_cap;
        char* raw = data();
        if ( raw == nullptr )
            return false;
        raw[_length] = '\0';
        return true;
    }
    static constexpr size_t npos = std::numeric_limits<size_t>::max();
};
template <typename ManagerT> struct node_type_for<pstring<ManagerT>>
{
    static constexpr NodeType value = NodeType::PString;
};
}
