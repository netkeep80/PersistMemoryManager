#pragma once
#include "pmm/address_traits.h"
#include "pmm/arena_internals.h"
#include "pmm/storage_backend.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#if defined( _WIN32 ) || defined( _WIN64 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace pmm
{
/*
## pmm-mmapstorage
req: feat-001, feat-008, fr-001, fr-014, ur-005, ur-007, qa-rec-001, qa-port-001, feat-006, if-005, sys-003, ur-011
*/
template <typename AT = DefaultAddressTraits> class MMapStorage
{
  public:
    using address_traits                         = AT;
    MMapStorage() noexcept                       = default;
    MMapStorage( const MMapStorage& )            = delete;
    MMapStorage& operator=( const MMapStorage& ) = delete;
    MMapStorage( MMapStorage&& other ) noexcept
        : _base( other._base ), _size( other._size ), _mapped( other._mapped )
#if defined( _WIN32 ) || defined( _WIN64 )
          ,
          _file_handle( other._file_handle ), _map_handle( other._map_handle )
#else
          ,
          _fd( other._fd )
#endif
    {
        other._base   = nullptr;
        other._size   = 0;
        other._mapped = false;
#if defined( _WIN32 ) || defined( _WIN64 )
        other._file_handle = INVALID_HANDLE_VALUE;
        other._map_handle  = nullptr;
#else
        other._fd = -1;
#endif
    }
    ~MMapStorage() { close(); }
    bool open( const char* path, size_t size_bytes ) noexcept
    {
        if ( _mapped )
            return false;
        if ( path == nullptr || size_bytes == 0 )
            return false;
        auto rounded = pmm::detail::round_up_checked( size_bytes, AT::granule_size );
        if ( !rounded.has_value() )
            return false;
        return open_impl( path, *rounded );
    }
    void close() noexcept
    {
        if ( !_mapped )
            return;
        close_impl();
        _base   = nullptr;
        _size   = 0;
        _mapped = false;
    }
    bool           is_open() const noexcept { return _mapped; }
    uint8_t*       base_ptr() noexcept { return _base; }
    const uint8_t* base_ptr() const noexcept { return _base; }
    size_t         total_size() const noexcept { return _size; }
/*
### pmm-mmapstorage-expand
*/
    bool resize_to( size_t new_total_size ) noexcept
    {
        if ( !_mapped )
            return false;
        if ( new_total_size == 0 )
            return false;
        if ( new_total_size % AT::granule_size != 0 )
            return false;
        if ( new_total_size <= _size )
            return false;
        return expand_impl( new_total_size );
    }
    bool owns_memory() const noexcept { return false; }

  private:
#if defined( _WIN32 ) || defined( _WIN64 )
    uint8_t* _base        = nullptr;
    size_t   _size        = 0;
    bool     _mapped      = false;
    HANDLE   _file_handle = INVALID_HANDLE_VALUE;
    HANDLE   _map_handle  = nullptr;
    bool     open_impl( const char* path, size_t size_bytes ) noexcept
    {
        _file_handle = CreateFileA( path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
        if ( _file_handle == INVALID_HANDLE_VALUE )
            return false;
        LARGE_INTEGER existing_size{};
        if ( !GetFileSizeEx( _file_handle, &existing_size ) )
        {
            CloseHandle( _file_handle );
            _file_handle = INVALID_HANDLE_VALUE;
            return false;
        }
        if ( static_cast<size_t>( existing_size.QuadPart ) < size_bytes )
        {
            LARGE_INTEGER new_size_li{};
            new_size_li.QuadPart = static_cast<LONGLONG>( size_bytes );
            if ( !SetFilePointerEx( _file_handle, new_size_li, nullptr, FILE_BEGIN ) || !SetEndOfFile( _file_handle ) )
            {
                CloseHandle( _file_handle );
                _file_handle = INVALID_HANDLE_VALUE;
                return false;
            }
        }
        DWORD size_hi = static_cast<DWORD>( size_bytes >> 32 );
        DWORD size_lo = static_cast<DWORD>( size_bytes & 0xFFFFFFFF );
        _map_handle   = CreateFileMappingA( _file_handle, nullptr, PAGE_READWRITE, size_hi, size_lo, nullptr );
        if ( _map_handle == nullptr )
        {
            CloseHandle( _file_handle );
            _file_handle = INVALID_HANDLE_VALUE;
            return false;
        }
        void* view = MapViewOfFile( _map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size_bytes );
        if ( view == nullptr )
        {
            CloseHandle( _map_handle );
            CloseHandle( _file_handle );
            _map_handle  = nullptr;
            _file_handle = INVALID_HANDLE_VALUE;
            return false;
        }
        _base   = static_cast<uint8_t*>( view );
        _size   = size_bytes;
        _mapped = true;
        return true;
    }
    void close_impl() noexcept
    {
        if ( _base != nullptr )
        {
            FlushViewOfFile( _base, _size );
            UnmapViewOfFile( _base );
        }
        if ( _map_handle != nullptr )
        {
            CloseHandle( _map_handle );
            _map_handle = nullptr;
        }
        if ( _file_handle != INVALID_HANDLE_VALUE )
        {
            CloseHandle( _file_handle );
            _file_handle = INVALID_HANDLE_VALUE;
        }
    }
    bool expand_impl( size_t new_size ) noexcept
    {
        if ( _base != nullptr )
        {
            FlushViewOfFile( _base, _size );
            UnmapViewOfFile( _base );
            _base = nullptr;
        }
        if ( _map_handle != nullptr )
        {
            CloseHandle( _map_handle );
            _map_handle = nullptr;
        }
        LARGE_INTEGER new_size_li{};
        new_size_li.QuadPart = static_cast<LONGLONG>( new_size );
        if ( !SetFilePointerEx( _file_handle, new_size_li, nullptr, FILE_BEGIN ) || !SetEndOfFile( _file_handle ) )
        {
            DWORD hi    = static_cast<DWORD>( _size >> 32 );
            DWORD lo    = static_cast<DWORD>( _size & 0xFFFFFFFF );
            _map_handle = CreateFileMappingA( _file_handle, nullptr, PAGE_READWRITE, hi, lo, nullptr );
            if ( _map_handle != nullptr )
            {
                void* view = MapViewOfFile( _map_handle, FILE_MAP_ALL_ACCESS, 0, 0, _size );
                if ( view != nullptr )
                    _base = static_cast<uint8_t*>( view );
            }
            return false;
        }
        DWORD size_hi = static_cast<DWORD>( new_size >> 32 );
        DWORD size_lo = static_cast<DWORD>( new_size & 0xFFFFFFFF );
        _map_handle   = CreateFileMappingA( _file_handle, nullptr, PAGE_READWRITE, size_hi, size_lo, nullptr );
        if ( _map_handle == nullptr )
            return false;
        void* view = MapViewOfFile( _map_handle, FILE_MAP_ALL_ACCESS, 0, 0, new_size );
        if ( view == nullptr )
        {
            CloseHandle( _map_handle );
            _map_handle = nullptr;
            return false;
        }
        _base = static_cast<uint8_t*>( view );
        _size = new_size;
        return true;
    }
#else
    uint8_t* _base   = nullptr;
    size_t   _size   = 0;
    bool     _mapped = false;
    int      _fd     = -1;
    bool     open_impl( const char* path, size_t size_bytes ) noexcept
    {
        _fd = ::open( path, O_RDWR | O_CREAT, 0600 );
        if ( _fd < 0 )
            return false;
        struct stat st
        {
        };
        if ( ::fstat( _fd, &st ) != 0 )
        {
            ::close( _fd );
            _fd = -1;
            return false;
        }
        if ( static_cast<size_t>( st.st_size ) < size_bytes )
        {
            if ( ::ftruncate( _fd, static_cast<off_t>( size_bytes ) ) != 0 )
            {
                ::close( _fd );
                _fd = -1;
                return false;
            }
        }
        void* addr = ::mmap( nullptr, size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0 );
        if ( addr == MAP_FAILED )
        {
            ::close( _fd );
            _fd = -1;
            return false;
        }
        _base   = static_cast<uint8_t*>( addr );
        _size   = size_bytes;
        _mapped = true;
        return true;
    }
    void close_impl() noexcept
    {
        if ( _base != nullptr )
            ::munmap( _base, _size );
        if ( _fd >= 0 )
        {
            ::close( _fd );
            _fd = -1;
        }
    }
    bool expand_impl( size_t new_size ) noexcept
    {
        if ( _base != nullptr )
        {
            ::munmap( _base, _size );
            _base = nullptr;
        }
        if ( ::ftruncate( _fd, static_cast<off_t>( new_size ) ) != 0 )
        {
            void* addr = ::mmap( nullptr, _size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0 );
            if ( addr != MAP_FAILED )
                _base = static_cast<uint8_t*>( addr );
            return false;
        }
        void* addr = ::mmap( nullptr, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0 );
        if ( addr == MAP_FAILED )
            return false;
        _base = static_cast<uint8_t*>( addr );
        _size = new_size;
        return true;
    }
#endif
};
static_assert( is_storage_backend_v<MMapStorage<>>, "" );
}
