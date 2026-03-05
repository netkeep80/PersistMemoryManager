/**
 * @file pmm/mmap_storage.h
 * @brief MMapStorage — бэкенд хранилища через отображение файла (Issue #87 Phase 5).
 *
 * Отображает файл в память через `mmap` (POSIX) или `MapViewOfFile` (Windows).
 * Обеспечивает персистентность данных между запусками приложения.
 *
 * Расширение (expand()): не поддерживается в базовой реализации
 * (требует пересоздания отображения, что зависит от ОС).
 *
 * Применение: персистентные базы данных, разделяемая память между процессами.
 *
 * @tparam AddressTraitsT Traits адресного пространства (из address_traits.h).
 *
 * @see plan_issue87.md §5 «Фаза 5: StorageBackend — MMapStorage»
 * @see storage_backend.h — концепт StorageBackend
 * @version 0.1 (Issue #87 Phase 5)
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/storage_backend.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined( _WIN32 ) || defined( _WIN64 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
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

/**
 * @brief Бэкенд хранилища через отображение файла в память.
 *
 * Открывает файл, отображает его в адресное пространство процесса.
 * При создании нового файла инициализирует его нулями.
 *
 * @tparam AddressTraitsT Traits адресного пространства.
 */
template <typename AddressTraitsT = DefaultAddressTraits> class MMapStorage
{
  public:
    using address_traits = AddressTraitsT;

    MMapStorage() noexcept = default;

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

    /**
     * @brief Открыть или создать файл и отобразить его в память.
     *
     * @param path       Путь к файлу.
     * @param size_bytes Размер отображения в байтах (при создании = размер файла).
     *                   Должен быть кратен granule_size и > 0.
     * @return true при успехе.
     */
    bool open( const char* path, std::size_t size_bytes ) noexcept
    {
        if ( _mapped )
            return false; // уже открыт
        if ( path == nullptr || size_bytes == 0 )
            return false;
        // Выравниваем по granule_size
        size_bytes = ( ( size_bytes + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size ) *
                     AddressTraitsT::granule_size;
        return open_impl( path, size_bytes );
    }

    /// @brief Закрыть отображение и файл.
    void close() noexcept
    {
        if ( !_mapped )
            return;
        close_impl();
        _base   = nullptr;
        _size   = 0;
        _mapped = false;
    }

    /// @brief true, если файл успешно отображён.
    bool is_open() const noexcept { return _mapped; }

    /// @brief Указатель на начало отображения.
    std::uint8_t*       base_ptr() noexcept { return _base; }
    const std::uint8_t* base_ptr() const noexcept { return _base; }

    /// @brief Размер отображения в байтах.
    std::size_t total_size() const noexcept { return _size; }

    /**
     * @brief Расширение не поддерживается в базовой реализации.
     * @return Всегда false.
     */
    bool expand( std::size_t /*additional_bytes*/ ) noexcept { return false; }

    /**
     * @brief MMapStorage не владеет памятью в смысле malloc/free
     *        (отображение управляется ОС).
     * @return false (отображение освобождается через munmap/UnmapViewOfFile в close()).
     */
    bool owns_memory() const noexcept { return false; }

  private:
#if defined( _WIN32 ) || defined( _WIN64 )
    std::uint8_t* _base        = nullptr;
    std::size_t   _size        = 0;
    bool          _mapped      = false;
    HANDLE        _file_handle = INVALID_HANDLE_VALUE;
    HANDLE        _map_handle  = nullptr;

    bool open_impl( const char* path, std::size_t size_bytes ) noexcept
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

        if ( static_cast<std::size_t>( existing_size.QuadPart ) < size_bytes )
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

        _base   = static_cast<std::uint8_t*>( view );
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

#else  // POSIX

    std::uint8_t* _base   = nullptr;
    std::size_t   _size   = 0;
    bool          _mapped = false;
    int           _fd     = -1;

    bool open_impl( const char* path, std::size_t size_bytes ) noexcept
    {
        _fd = ::open( path, O_RDWR | O_CREAT, 0600 );
        if ( _fd < 0 )
            return false;

        // Ensure file is at least size_bytes
        struct stat st
        {
        };
        if ( ::fstat( _fd, &st ) != 0 )
        {
            ::close( _fd );
            _fd = -1;
            return false;
        }

        if ( static_cast<std::size_t>( st.st_size ) < size_bytes )
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

        _base   = static_cast<std::uint8_t*>( addr );
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
#endif // _WIN32
};

// ─── static_assert: MMapStorage соответствует концепту StorageBackend ──────────

static_assert( is_storage_backend_v<MMapStorage<>>, "MMapStorage must satisfy StorageBackendConcept" );

} // namespace pmm
