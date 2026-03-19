/**
 * @file pmm/io.h
 * @brief Утилиты файлового ввода/вывода для PersistMemoryManager (Issue #102, #110)
 *
 * Вспомогательный заголовочный файл с функциями сохранения и загрузки
 * образа памяти в/из файла. Вынесен в отдельный файл, так как файловый
 * ввод/вывод не является основной функциональностью менеджера памяти,
 * но необходим для тестов и примеров использования персистентности.
 *
 * Issue #43 Phase 2.1: CRC32 checksum — save_manager computes and stores CRC32
 * in the ManagerHeader.crc32 field; load_manager_from_file verifies it.
 *
 * Issue #43 Phase 2.2: Atomic save — save_manager writes to a temporary file
 * (filename + ".tmp") and atomically renames it to the target on success.
 *
 * Использование (Issue #110 — статический интерфейс):
 * @code
 * #include "pmm/manager_configs.h"
 * #include "pmm/persist_memory_manager.h"
 * #include "pmm/io.h"
 *
 * using MyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 * MyMgr::create(64 * 1024);
 *
 * // Сохранить образ в файл
 * bool ok = pmm::save_manager<MyMgr>("heap.dat");
 *
 * // Загрузить образ из файла (сначала нужно выделить буфер нужного размера)
 * MyMgr::create(64 * 1024);
 * bool ok2 = pmm::load_manager_from_file<MyMgr>("heap.dat");
 * @endcode
 *
 * @version 0.3 (Issue #43 Phase 2 — CRC32 + atomic save)
 */

#pragma once

#include "pmm/types.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined( _WIN32 ) || defined( _WIN64 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib> // std::rename (POSIX)
#endif

namespace pmm
{

namespace detail
{

/// @brief Issue #43 Phase 2.2: Atomic file rename (write-then-rename pattern).
/// On POSIX: std::rename is atomic if source and dest are on the same filesystem.
/// On Windows: MoveFileExA with MOVEFILE_REPLACE_EXISTING.
/// @return true on success.
inline bool atomic_rename( const char* tmp_path, const char* final_path ) noexcept
{
#if defined( _WIN32 ) || defined( _WIN64 )
    return MoveFileExA( tmp_path, final_path, MOVEFILE_REPLACE_EXISTING ) != 0;
#else
    return std::rename( tmp_path, final_path ) == 0;
#endif
}

} // namespace detail

/**
 * @brief Сохранить образ PersistMemoryManager в файл (Issue #110 — статический интерфейс).
 *
 * Issue #43 Phase 2.1: Computes CRC32 of the entire managed region (treating
 * the crc32 field as zero) and stores it in ManagerHeader.crc32 before writing.
 *
 * Issue #43 Phase 2.2: Uses atomic write-then-rename — writes to "filename.tmp",
 * then renames to "filename" on success. If the process crashes during fwrite,
 * the original file remains intact.
 *
 * @tparam MgrT    Тип статического менеджера (PersistMemoryManager<ConfigT, Id>).
 * @param filename Путь к выходному файлу.
 * @return true при успешной записи, false при ошибке ввода/вывода или если не инициализирован.
 *
 * Предусловие:  filename != nullptr, MgrT::is_initialized() == true.
 * Постусловие: файл содержит точную копию управляемой области памяти с CRC32.
 */
template <typename MgrT> inline bool save_manager( const char* filename )
{
    using address_traits = typename MgrT::address_traits;

    if ( filename == nullptr || !MgrT::is_initialized() )
        return false;
    std::uint8_t* data  = MgrT::backend().base_ptr();
    std::size_t   total = MgrT::backend().total_size();
    if ( data == nullptr || total == 0 )
        return false;

    // Phase 2.1: Compute and store CRC32 in the manager header.
    // The header is located after Block_0 (sizeof(Block<AT>) bytes from base).
    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<address_traits> );
    auto*                 hdr        = reinterpret_cast<detail::ManagerHeader<address_traits>*>( data + kHdrOffset );
    hdr->crc32                       = 0; // zero the field before computing CRC
    hdr->crc32                       = detail::compute_image_crc32<address_traits>( data, total );

    // Phase 2.2: Atomic save — write to temp file, then rename.
    std::string tmp_path = std::string( filename ) + ".tmp";

    std::FILE* f = std::fopen( tmp_path.c_str(), "wb" );
    if ( f == nullptr )
        return false;
    std::size_t written = std::fwrite( data, 1, total, f );
    if ( std::fflush( f ) != 0 )
    {
        std::fclose( f );
        std::remove( tmp_path.c_str() );
        return false;
    }
    std::fclose( f );

    if ( written != total )
    {
        std::remove( tmp_path.c_str() );
        return false;
    }

    // Atomic rename: tmp → final
    if ( !detail::atomic_rename( tmp_path.c_str(), filename ) )
    {
        std::remove( tmp_path.c_str() );
        return false;
    }
    return true;
}

/**
 * @brief Загрузить образ менеджера из файла в PersistMemoryManager (Issue #110 — статический интерфейс).
 *
 * Читает файл, записанный функцией save_manager(), в буфер менеджера,
 * затем вызывает MgrT::load() для проверки заголовка и восстановления состояния.
 *
 * Issue #43 Phase 2.1: After reading the file, verifies CRC32 before calling load().
 * If the CRC does not match, returns false without modifying manager state.
 *
 * Примечание: бэкенд менеджера должен уже иметь выделенный буфер достаточного размера.
 * Для HeapStorage вызовите MgrT::create(size) перед load_manager_from_file().
 *
 * @tparam MgrT    Тип статического менеджера (PersistMemoryManager<ConfigT, Id>).
 * @param filename Путь к файлу с образом.
 * @return true при успешной загрузке, false при ошибке.
 *
 * Предусловие:  filename != nullptr, MgrT::backend().base_ptr() != nullptr.
 * Постусловие: менеджер восстановлен из файла.
 */
template <typename MgrT> inline bool load_manager_from_file( const char* filename )
{
    using address_traits = typename MgrT::address_traits;

    if ( filename == nullptr )
        return false;

    std::uint8_t* buf  = MgrT::backend().base_ptr();
    std::size_t   size = MgrT::backend().total_size();
    if ( buf == nullptr || size < detail::kMinMemorySize )
        return false;

    std::FILE* f = std::fopen( filename, "rb" );
    if ( f == nullptr )
        return false;

    if ( std::fseek( f, 0, SEEK_END ) != 0 )
    {
        std::fclose( f );
        return false;
    }
    long file_size_long = std::ftell( f );
    if ( file_size_long <= 0 )
    {
        std::fclose( f );
        return false;
    }
    std::rewind( f );

    std::size_t file_size = static_cast<std::size_t>( file_size_long );
    if ( file_size > size )
    {
        std::fclose( f );
        return false;
    }

    std::size_t read_bytes = std::fread( buf, 1, file_size, f );
    std::fclose( f );

    if ( read_bytes != file_size )
        return false;

    // Phase 2.1: Verify CRC32 before calling load().
    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<address_traits> );
    if ( file_size >= kHdrOffset + sizeof( detail::ManagerHeader<address_traits> ) )
    {
        auto*         hdr          = reinterpret_cast<detail::ManagerHeader<address_traits>*>( buf + kHdrOffset );
        std::uint32_t stored_crc   = hdr->crc32;
        std::uint32_t computed_crc = detail::compute_image_crc32<address_traits>( buf, file_size );
        if ( stored_crc != 0 && stored_crc != computed_crc )
        {
            MgrT::set_last_error( PmmError::CrcMismatch );
            MgrT::logging_policy::on_corruption_detected( PmmError::CrcMismatch );
            return false;
        }
        // Note: stored_crc==0 is accepted for backward compatibility with images
        // saved before Phase 2.1 (which had _reserved[8] zeroed).
    }

    return MgrT::load();
}

} // namespace pmm
