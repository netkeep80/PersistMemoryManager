#pragma once
#include "pmm/pptr.h"
#include "pmm/types.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
namespace pmm
{
/*
## pmm-parray
req: feat-003, fr-007, fr-008, fr-029, ur-003, dr-007, feat-008, fr-031, ur-008
*/
template <typename T, typename ManagerT> struct parray
{
    static_assert( std::is_trivially_copyable_v<T>, "" );
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using value_type   = T;
    uint32_t   _size;
    uint32_t   _capacity;
    index_type _data_idx;
    parray() noexcept : _size( 0 ), _capacity( 0 ), _data_idx( detail::kNullIdx_v<typename ManagerT::address_traits> )
    {
    }
    ~parray() noexcept = default;
    size_t size() const noexcept { return static_cast<size_t>( _size ); }
    bool empty() const noexcept { return _size == 0; }
    size_t capacity() const noexcept { return static_cast<size_t>( _capacity ); }
    T* at( size_t i ) noexcept { return i < size() && resolve_data() ? resolve_data() + i : nullptr; }
    const T* at( size_t i ) const noexcept { return i < size() && resolve_data() ? resolve_data() + i : nullptr; }
    T operator[]( size_t i ) const noexcept { const T* p = resolve_data(); return p ? p[i] : T{}; }
    T* front() noexcept { return at( 0 ); }
    const T* front() const noexcept { return at( 0 ); }
    T* back() noexcept { return _size ? at( size() - 1 ) : nullptr; }
    const T* back() const noexcept { return _size ? at( size() - 1 ) : nullptr; }
    T* data() noexcept { return resolve_data(); }
    const T* data() const noexcept { return resolve_data(); }
    bool push_back( const T& value ) noexcept
    {
        if ( _size == std::numeric_limits<uint32_t>::max() )
            return false;
        const T copy = value;
        const uint32_t old_size = _size;
        parray* state = ensure_capacity( old_size + 1 );
        T* d = state ? state->resolve_data() : nullptr;
        if ( d == nullptr )
            return false;
        d[old_size] = copy;
        state->_size = old_size + 1;
        return true;
    }
    void pop_back() noexcept { if ( _size ) --_size; }
    bool set( size_t i, const T& value ) noexcept
    {
        T* d = i < size() ? resolve_data() : nullptr;
        if ( d == nullptr )
            return false;
        d[i] = value;
        return true;
    }
    bool reserve( size_t n ) noexcept
    {
        return n <= std::numeric_limits<uint32_t>::max() && ensure_capacity( static_cast<uint32_t>( n ) ) != nullptr;
    }
    bool resize( size_t n ) noexcept
    {
        if ( n > std::numeric_limits<uint32_t>::max() )
            return false;
        const uint32_t next = static_cast<uint32_t>( n ), old = _size;
        if ( next <= old )
        {
            _size = next;
            return true;
        }
        parray* state = ensure_capacity( next );
        T* d = state ? state->resolve_data() : nullptr;
        if ( d == nullptr )
            return false;
        std::memset( d + old, 0, static_cast<size_t>( next - old ) * sizeof( T ) );
        state->_size = next;
        return true;
    }
    bool insert( size_t index, const T& value ) noexcept
    {
        if ( index > size() || _size == std::numeric_limits<uint32_t>::max() )
            return false;
        const T copy = value;
        const uint32_t old = _size;
        parray* state = ensure_capacity( old + 1 );
        T* d = state ? state->resolve_data() : nullptr;
        if ( d == nullptr )
            return false;
        if ( index < old )
            std::memmove( d + index + 1, d + index, ( static_cast<size_t>( old ) - index ) * sizeof( T ) );
        d[index] = copy;
        state->_size = old + 1;
        return true;
    }
    bool erase( size_t index ) noexcept
    {
        T* d = index < size() ? resolve_data() : nullptr;
        if ( d == nullptr )
            return false;
        if ( index + 1 < size() )
            std::memmove( d + index, d + index + 1, ( size() - index - 1 ) * sizeof( T ) );
        --_size;
        return true;
    }
    void clear() noexcept { _size = 0; }
    void free_data() noexcept
    {
        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
            ManagerT::template deallocate_typed<T>( pmm::pptr<T, ManagerT>( _data_idx ) );
        _data_idx = detail::kNullIdx_v<typename ManagerT::address_traits>;
        _size = _capacity = 0;
    }
    bool operator==( const parray& other ) const noexcept
    {
        if ( this == &other )
            return true;
        if ( _size != other._size )
            return false;
        const T* a = resolve_data();
        const T* b = other.resolve_data();
        return _size == 0 || ( a && b && std::memcmp( a, b, size() * sizeof( T ) ) == 0 );
    }
    bool operator!=( const parray& other ) const noexcept { return !( *this == other ); }

  private:
    T* resolve_data() const noexcept { return pmm::pptr<T, ManagerT>( _data_idx ).resolve_unchecked(); }
    parray* ensure_capacity( uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return this;
        detail::relocation_owner<parray, ManagerT> owner( this );
        const uint32_t old_size = _size, old_cap = _capacity;
        const index_type old_idx = _data_idx;
        uint64_t cap = old_cap ? static_cast<uint64_t>( old_cap ) * 2 : 4;
        if ( cap < required )
            cap = required;
        if ( cap > std::numeric_limits<uint32_t>::max() )
            cap = std::numeric_limits<uint32_t>::max();
        if ( cap > std::numeric_limits<size_t>::max() / sizeof( T ) )
            return nullptr;
        auto p = ManagerT::template reallocate_typed<T>( pmm::pptr<T, ManagerT>( old_idx ), old_size,
                                                         static_cast<size_t>( cap ) );
        parray* state = p.is_null() ? nullptr : owner.get();
        if ( state )
        {
            state->_data_idx = p.offset();
            state->_capacity = static_cast<uint32_t>( cap );
        }
        return state;
    }
};
template <typename T, typename ManagerT> struct node_type_for<parray<T, ManagerT>>
{
    static constexpr NodeType value = NodeType::PArray;
};
}
