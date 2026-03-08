/**
 * @file pmm/io.h
 * @brief Утилиты файлового ввода/вывода для PersistMemoryManager (Issue #102, #110)
 *
 * Вспомогательный заголовочный файл с функциями сохранения и загрузки
 * образа памяти в/из файла. Вынесен в отдельный файл, так как файловый
 * ввод/вывод не является основной функциональностью менеджера памяти,
 * но необходим для тестов и примеров использования персистентности.
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
 * @version 0.2 (Issue #110 — обновлено для статического интерфейса PersistMemoryManager)
 */

#pragma once

#include "pmm/types.h"

#include <cstdint>
#include <cstdio>

namespace pmm
{

/**
 * @brief Сохранить образ PersistMemoryManager в файл (Issue #110 — статический интерфейс).
 *
 * Записывает весь управляемый буфер в файл побайтово.
 * Поскольку все метаданные используют смещения (offsets) от начала буфера,
 * образ корректно загружается по любому базовому адресу через load_manager_from_file().
 *
 * @tparam MgrT    Тип статического менеджера (PersistMemoryManager<ConfigT, Id>).
 * @param filename Путь к выходному файлу.
 * @return true при успешной записи, false при ошибке ввода/вывода или если не инициализирован.
 *
 * Предусловие:  filename != nullptr, MgrT::is_initialized() == true.
 * Постусловие: файл содержит точную копию управляемой области памяти.
 */
template <typename MgrT>
inline bool save_manager( const char* filename )
{
    if ( filename == nullptr || !MgrT::is_initialized() )
        return false;
    const std::uint8_t* data  = MgrT::backend().base_ptr();
    std::size_t         total = MgrT::backend().total_size();
    if ( data == nullptr || total == 0 )
        return false;
    std::FILE* f = std::fopen( filename, "wb" );
    if ( f == nullptr )
        return false;
    std::size_t written = std::fwrite( data, 1, total, f );
    std::fclose( f );
    return written == total;
}

/**
 * @brief Загрузить образ менеджера из файла в PersistMemoryManager (Issue #110 — статический интерфейс).
 *
 * Читает файл, записанный функцией save_manager(), в буфер менеджера,
 * затем вызывает MgrT::load() для проверки заголовка и восстановления состояния.
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
template <typename MgrT>
inline bool load_manager_from_file( const char* filename )
{
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

    return MgrT::load();
}

} // namespace pmm
