#pragma once
#include "pmm/address_traits.h"
#include "pmm/arena_internals.h"
#include "pmm/storage_backend.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#if defined( _WIN32 )
#include <malloc.h>
#endif
namespace pmm
{
namespace detail
{
/*
### pmm-detail-alignedalloc
*/
inline void* aligned_alloc_for_arena( size_t alignment, size_t size ) noexcept
{
    if ( size == 0 )
        return nullptr;
    auto rounded = round_up_checked( size, alignment );
    if ( !rounded.has_value() )
        return nullptr;
#if defined( _WIN32 )
    return _aligned_malloc( *rounded, alignment );
#else
    void* p = nullptr;
    if ( posix_memalign( &p, alignment < sizeof( void* ) ? sizeof( void* ) : alignment, *rounded ) != 0 )
        return nullptr;
    return p;
#endif
}
inline void aligned_free_for_arena( void* p ) noexcept
{
    if ( p == nullptr )
        return;
#if defined( _WIN32 )
    _aligned_free( p );
#else
    std::free( p );
#endif
}
}
/*
## pmm-heapstorage
req: feat-001, fr-001, fr-026, ur-001, if-008, qa-mem-001, feat-006, fr-013, fr-028, if-005, sys-003
*/
template <typename AT = DefaultAddressTraits> class HeapStorage
{
  public:
    using address_traits   = AT;
    HeapStorage() noexcept = default;
    explicit HeapStorage( size_t initial_size ) noexcept
    {
        if ( initial_size == 0 )
            return;
        auto rounded = pmm::detail::round_up_checked( initial_size, AT::granule_size );
        if ( !rounded.has_value() )
            return;
        _buffer = static_cast<uint8_t*>( detail::aligned_alloc_for_arena( AT::granule_size, *rounded ) );
        if ( _buffer != nullptr )
        {
            _size        = *rounded;
            _owns_memory = true;
        }
        assert( reinterpret_cast<std::uintptr_t>( _buffer ) % AT::granule_size == 0 );
    }
    HeapStorage( const HeapStorage& )            = delete;
    HeapStorage& operator=( const HeapStorage& ) = delete;
    HeapStorage( HeapStorage&& other ) noexcept
        : _buffer( other._buffer ), _size( other._size ), _owns_memory( other._owns_memory )
    {
        other._buffer      = nullptr;
        other._size        = 0;
        other._owns_memory = false;
    }
    HeapStorage& operator=( HeapStorage&& other ) noexcept
    {
        if ( this != &other )
        {
            if ( _owns_memory && _buffer != nullptr )
                detail::aligned_free_for_arena( _buffer );
            _buffer            = other._buffer;
            _size              = other._size;
            _owns_memory       = other._owns_memory;
            other._buffer      = nullptr;
            other._size        = 0;
            other._owns_memory = false;
        }
        return *this;
    }
    ~HeapStorage()
    {
        if ( _owns_memory && _buffer != nullptr )
            detail::aligned_free_for_arena( _buffer );
    }
    void attach( void* memory, size_t size ) noexcept
    {
        if ( _owns_memory && _buffer != nullptr )
            detail::aligned_free_for_arena( _buffer );
        _buffer      = static_cast<uint8_t*>( memory );
        _size        = size;
        _owns_memory = false;
    }
    uint8_t*       base_ptr() noexcept { return _buffer; }
    const uint8_t* base_ptr() const noexcept { return _buffer; }
    size_t         total_size() const noexcept { return _size; }
    bool           resize_to( size_t new_total_size ) noexcept
    {
        if ( new_total_size == 0 )
            return false;
        if ( new_total_size % AT::granule_size != 0 )
            return false;
        if ( new_total_size <= _size )
            return false;
        void* new_buf = detail::aligned_alloc_for_arena( AT::granule_size, new_total_size );
        if ( new_buf == nullptr )
            return false;
        assert( reinterpret_cast<std::uintptr_t>( new_buf ) % AT::granule_size == 0 );
        if ( _buffer != nullptr )
            std::memcpy( new_buf, _buffer, _size );
        if ( _owns_memory && _buffer != nullptr )
            detail::aligned_free_for_arena( _buffer );
        _buffer      = static_cast<uint8_t*>( new_buf );
        _size        = new_total_size;
        _owns_memory = true;
        return true;
    }
    bool owns_memory() const noexcept { return _owns_memory; }

  private:
    uint8_t* _buffer      = nullptr;
    size_t   _size        = 0;
    bool     _owns_memory = false;
};
static_assert( is_storage_backend_v<HeapStorage<>>, "" );
}
