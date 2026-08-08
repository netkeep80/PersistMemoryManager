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
    using owner_pptr   = pmm::pptr<parray, ManagerT>;
    uint32_t   _size;
    uint32_t   _capacity;
    index_type _data_idx;
    parray() noexcept : _size( 0 ), _capacity( 0 ), _data_idx( detail::kNullIdx_v<typename ManagerT::address_traits> )
    {
    }
    ~parray() noexcept = default;
    size_t size() const noexcept { return static_cast<size_t>( _size ); }
    bool   empty() const noexcept { return _size == 0; }
    size_t capacity() const noexcept { return static_cast<size_t>( _capacity ); }
    T*     at( size_t i ) noexcept
    {
        if ( i >= static_cast<size_t>( _size ) )
            return nullptr;
        T* data = resolve_data();
        return ( data != nullptr ) ? ( data + i ) : nullptr;
    }
    const T* at( size_t i ) const noexcept
    {
        if ( i >= static_cast<size_t>( _size ) )
            return nullptr;
        const T* data = resolve_data();
        return ( data != nullptr ) ? ( data + i ) : nullptr;
    }
    T operator[]( size_t i ) const noexcept
    {
        const T* data = resolve_data();
        return ( data != nullptr ) ? data[i] : T{};
    }
    T*       front() noexcept { return at( 0 ); }
    const T* front() const noexcept { return at( 0 ); }
    T*       back() noexcept { return ( _size > 0 ) ? at( static_cast<size_t>( _size ) - 1 ) : nullptr; }
    const T* back() const noexcept { return ( _size > 0 ) ? at( static_cast<size_t>( _size ) - 1 ) : nullptr; }
    T*       data() noexcept { return resolve_data(); }
    const T* data() const noexcept { return resolve_data(); }
    bool     push_back( const T& value ) noexcept
    {
        if ( _size == std::numeric_limits<uint32_t>::max() )
            return false;
        const T value_copy = value;
        parray* const external_owner = this;
        const owner_pptr owner = pmm::pptr_from_raw<parray, ManagerT>( this );
        const uint32_t old_size = _size;
        if ( !ensure_capacity( old_size + 1, owner, external_owner ) )
            return false;
        parray* state = resolve_owner( owner, external_owner );
        if ( state == nullptr )
            return false;
        T* d = state->resolve_data();
        if ( d == nullptr )
            return false;
        d[old_size] = value_copy;
        state->_size = old_size + 1;
        return true;
    }
    void pop_back() noexcept
    {
        if ( _size > 0 )
            --_size;
    }
    bool set( size_t i, const T& value ) noexcept
    {
        if ( i >= static_cast<size_t>( _size ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;
        d[i] = value;
        return true;
    }
    bool reserve( size_t n ) noexcept
    {
        if ( n > static_cast<size_t>( std::numeric_limits<uint32_t>::max() ) )
            return false;
        parray* const external_owner = this;
        const owner_pptr owner = pmm::pptr_from_raw<parray, ManagerT>( this );
        return ensure_capacity( static_cast<uint32_t>( n ), owner, external_owner );
    }
    bool resize( size_t n ) noexcept
    {
        if ( n > static_cast<size_t>( std::numeric_limits<uint32_t>::max() ) )
            return false;
        const auto new_size = static_cast<uint32_t>( n );
        const uint32_t old_size = _size;
        if ( new_size <= old_size )
        {
            _size = new_size;
            return true;
        }
        parray* const external_owner = this;
        const owner_pptr owner = pmm::pptr_from_raw<parray, ManagerT>( this );
        if ( !ensure_capacity( new_size, owner, external_owner ) )
            return false;
        parray* state = resolve_owner( owner, external_owner );
        if ( state == nullptr )
            return false;
        T* d = state->resolve_data();
        if ( d == nullptr )
            return false;
        std::memset( d + old_size, 0, static_cast<size_t>( new_size - old_size ) * sizeof( T ) );
        state->_size = new_size;
        return true;
    }
    bool insert( size_t index, const T& value ) noexcept
    {
        if ( index > static_cast<size_t>( _size ) || _size == std::numeric_limits<uint32_t>::max() )
            return false;
        const T value_copy = value;
        parray* const external_owner = this;
        const owner_pptr owner = pmm::pptr_from_raw<parray, ManagerT>( this );
        const uint32_t old_size = _size;
        if ( !ensure_capacity( old_size + 1, owner, external_owner ) )
            return false;
        parray* state = resolve_owner( owner, external_owner );
        if ( state == nullptr )
            return false;
        T* d = state->resolve_data();
        if ( d == nullptr )
            return false;
        if ( index < static_cast<size_t>( old_size ) )
            std::memmove( d + index + 1, d + index, ( static_cast<size_t>( old_size ) - index ) * sizeof( T ) );
        d[index] = value_copy;
        state->_size = old_size + 1;
        return true;
    }
    bool erase( size_t index ) noexcept
    {
        if ( index >= static_cast<size_t>( _size ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;
        if ( index + 1 < static_cast<size_t>( _size ) )
            std::memmove( d + index, d + index + 1, ( static_cast<size_t>( _size ) - index - 1 ) * sizeof( T ) );
        --_size;
        return true;
    }
    void clear() noexcept { _size = 0; }
    void free_data() noexcept
    {
        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            ManagerT::template deallocate_typed<T>( pmm::pptr<T, ManagerT>( _data_idx ) );
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
        return std::memcmp( a, b, static_cast<size_t>( _size ) * sizeof( T ) ) == 0;
    }
    bool operator!=( const parray& other ) const noexcept { return !( *this == other ); }

  private:
    static parray* resolve_owner( owner_pptr owner, parray* external_owner ) noexcept
    {
        return owner.is_null() ? external_owner : owner.resolve_unchecked();
    }
    T* resolve_data() const noexcept { return pmm::pptr<T, ManagerT>( _data_idx ).resolve_unchecked(); }
    bool ensure_capacity( uint32_t required, owner_pptr owner, parray* external_owner ) noexcept
    {
        if ( required <= _capacity )
            return true;
        const uint32_t old_size = _size;
        const uint32_t old_capacity = _capacity;
        const index_type old_data_idx = _data_idx;
        uint64_t new_cap = old_capacity ? static_cast<uint64_t>( old_capacity ) * 2 : 4;
        if ( new_cap < required )
            new_cap = required;
        if ( new_cap > std::numeric_limits<uint32_t>::max() )
            new_cap = std::numeric_limits<uint32_t>::max();
        if ( sizeof( T ) > 0 && new_cap > ( std::numeric_limits<size_t>::max )() / sizeof( T ) )
            return false;
        pmm::pptr<T, ManagerT> new_p = ManagerT::template reallocate_typed<T>(
            pmm::pptr<T, ManagerT>( old_data_idx ), static_cast<size_t>( old_size ), static_cast<size_t>( new_cap ) );
        if ( new_p.is_null() )
            return false;
        parray* state = resolve_owner( owner, external_owner );
        if ( state == nullptr )
            return false;
        state->_data_idx = new_p.offset();
        state->_capacity = static_cast<uint32_t>( new_cap );
        return true;
    }
};
template <typename T, typename ManagerT> struct node_type_for<parray<T, ManagerT>>
{
    static constexpr NodeType value = NodeType::PArray;
};
}
