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
        if ( char* p = data() )
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
        return s ? compare( s, std::strlen( s ) ) == 0 : _length == 0;
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
        if ( s == nullptr || p == nullptr || reinterpret_cast<uintptr_t>( s ) < reinterpret_cast<uintptr_t>( p ) )
            return npos;
        const size_t off = static_cast<size_t>( reinterpret_cast<uintptr_t>( s ) - reinterpret_cast<uintptr_t>( p ) );
        return off <= static_cast<size_t>( _capacity ) ? off : npos;
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
        const size_t self_off = source_offset( s );
        if ( self_off != npos && ( self_off > size() || len > size() + 1 - self_off ) )
            return false;
        const size_t arena_off = self_off == npos ? detail::arena_offset<ManagerT>( s ) : npos;
        if ( arena_off != npos && len > ManagerT::backend().total_size() - arena_off )
            return false;
        const uint32_t new_len = static_cast<uint32_t>( base + len );
        pstring* state = ensure_capacity( new_len );
        if ( state == nullptr )
            return false;
        char* p = state->data();
        if ( p == nullptr )
            return false;
        if ( self_off != npos )
            s = p + self_off;
        else if ( arena_off != npos )
            s = reinterpret_cast<const char*>( ManagerT::backend().base_ptr() ) + arena_off;
        std::memmove( p + base, s, len );
        p[new_len] = '\0';
        state->_length = new_len;
        return true;
    }
    int compare( const char* rhs, size_t rhs_len ) const noexcept
    {
        const size_t lhs_len = size(), common = lhs_len < rhs_len ? lhs_len : rhs_len;
        const char* lhs = data();
        if ( common && ( lhs == nullptr || rhs == nullptr ) )
            return lhs ? 1 : rhs ? -1 : 0;
        const int cmp = common ? std::memcmp( lhs, rhs, common ) : 0;
        return cmp ? cmp : lhs_len < rhs_len ? -1 : lhs_len > rhs_len ? 1 : 0;
    }
    pstring* ensure_capacity( uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return this;
        detail::relocation_owner<pstring, ManagerT> owner( this );
        const uint32_t old_len = _length, old_cap = _capacity;
        const index_type old_idx = _data_idx;
        const uint64_t max_cap = std::numeric_limits<size_t>::max() < std::numeric_limits<uint32_t>::max()
                                     ? static_cast<uint64_t>( std::numeric_limits<size_t>::max() ) - 1
                                     : static_cast<uint64_t>( std::numeric_limits<uint32_t>::max() );
        if ( required > max_cap )
            return nullptr;
        uint64_t cap = old_cap ? static_cast<uint64_t>( old_cap ) * 2 : 16;
        if ( cap < required )
            cap = required;
        if ( cap > max_cap )
            cap = max_cap;
        const uint32_t new_cap = static_cast<uint32_t>( cap );
        const size_t old_count = old_idx == detail::kNullIdx_v<typename ManagerT::address_traits> ? 0 : old_len + 1;
        auto p = ManagerT::template reallocate_typed<char>( pmm::pptr<char, ManagerT>( old_idx ), old_count,
                                                            static_cast<size_t>( new_cap ) + 1 );
        pstring* state = p.is_null() ? nullptr : owner.get();
        if ( state == nullptr )
            return nullptr;
        state->_data_idx = p.offset();
        state->_capacity = new_cap;
        char* raw = p.resolve_unchecked();
        if ( raw )
            raw[old_len] = '\0';
        return raw ? state : nullptr;
    }
    static constexpr size_t npos = std::numeric_limits<size_t>::max();
};
template <typename ManagerT> struct node_type_for<pstring<ManagerT>>
{
    static constexpr NodeType value = NodeType::PString;
};
}
