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
    using owner_pptr   = pmm::pptr<pstring, ManagerT>;
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
    static pstring* resolve_owner( owner_pptr owner, pstring* external_owner ) noexcept
    {
        return owner.is_null() ? external_owner : owner.resolve_unchecked();
    }
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
    static bool arena_source_offset( const char* s, size_t len, size_t& offset ) noexcept
    {
        if ( s == nullptr )
            return false;
        const auto* arena = ManagerT::backend().base_ptr();
        const auto  bytes = ManagerT::backend().total_size();
        if ( arena == nullptr || bytes == 0 )
            return false;
        const auto begin = reinterpret_cast<uintptr_t>( arena );
        const auto src   = reinterpret_cast<uintptr_t>( s );
        if ( src < begin || src - begin >= bytes )
            return false;
        offset = static_cast<size_t>( src - begin );
        return len <= bytes - offset;
    }
    bool write( const char* s, size_t len, bool append_mode ) noexcept
    {
        if ( len == 0 )
        {
            if ( !append_mode )
                clear();
            return true;
        }
        pstring* const external_owner = this;
        const owner_pptr owner = pmm::pptr_from_raw<pstring, ManagerT>( this );
        const size_t base = append_mode ? size() : 0;
        if ( s == nullptr || len > static_cast<size_t>( std::numeric_limits<uint32_t>::max() ) - base )
            return false;
        const size_t self_offset = source_offset( s );
        if ( self_offset != npos && ( self_offset > size() || len > size() + 1 - self_offset ) )
            return false;
        size_t arena_offset = npos;
        if ( self_offset == npos )
        {
            const auto* arena = ManagerT::backend().base_ptr();
            const auto  bytes = ManagerT::backend().total_size();
            if ( arena != nullptr && bytes != 0 )
            {
                const auto begin = reinterpret_cast<uintptr_t>( arena );
                const auto src   = reinterpret_cast<uintptr_t>( s );
                if ( src >= begin && src - begin < bytes && !arena_source_offset( s, len, arena_offset ) )
                    return false;
            }
        }
        const uint32_t new_len = static_cast<uint32_t>( base + len );
        if ( !ensure_capacity( new_len, owner, external_owner ) )
            return false;
        pstring* state = resolve_owner( owner, external_owner );
        if ( state == nullptr )
            return false;
        char* p = state->data();
        if ( p == nullptr )
            return false;
        if ( self_offset != npos )
            s = p + self_offset;
        else if ( arena_offset != npos )
            s = reinterpret_cast<const char*>( ManagerT::backend().base_ptr() ) + arena_offset;
        std::memmove( p + base, s, len );
        p[new_len] = '\0';
        state->_length = new_len;
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
    bool ensure_capacity( uint32_t required, owner_pptr owner, pstring* external_owner ) noexcept
    {
        if ( required <= _capacity )
            return true;
        const uint32_t old_length = _length;
        const uint32_t old_capacity = _capacity;
        const index_type old_data_idx = _data_idx;
        const uint64_t max_cap = std::numeric_limits<size_t>::max() < std::numeric_limits<uint32_t>::max()
                                     ? static_cast<uint64_t>( std::numeric_limits<size_t>::max() ) - 1
                                     : static_cast<uint64_t>( std::numeric_limits<uint32_t>::max() );
        if ( required > max_cap )
            return false;
        uint64_t cap = old_capacity ? static_cast<uint64_t>( old_capacity ) * 2 : 16;
        if ( cap < required )
            cap = required;
        if ( cap > max_cap )
            cap = max_cap;
        const uint32_t new_cap = static_cast<uint32_t>( cap );
        const std::size_t old_count = old_data_idx != detail::kNullIdx_v<typename ManagerT::address_traits>
                                          ? static_cast<size_t>( old_length ) + 1
                                          : 0;
        pmm::pptr<char, ManagerT> p = ManagerT::template reallocate_typed<char>(
            pmm::pptr<char, ManagerT>( old_data_idx ), old_count, static_cast<std::size_t>( new_cap ) + 1 );
        if ( p.is_null() )
            return false;
        pstring* state = resolve_owner( owner, external_owner );
        if ( state == nullptr )
            return false;
        state->_data_idx = p.offset();
        state->_capacity = new_cap;
        char* raw = p.resolve_unchecked();
        if ( raw == nullptr )
            return false;
        raw[old_length] = '\0';
        return true;
    }
    static constexpr size_t npos = std::numeric_limits<size_t>::max();
};
template <typename ManagerT> struct node_type_for<pstring<ManagerT>>
{
    static constexpr NodeType value = NodeType::PString;
};
}
