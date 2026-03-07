/**
 * @file persist_memory_io.h
 * @brief Утилиты файлового ввода/вывода для PersistMemoryManager
 *
 * Вспомогательный заголовочный файл с функциями сохранения и загрузки
 * образа памяти в/из файла. Вынесен в отдельный файл, так как файловый
 * ввод/вывод не является основной функциональностью менеджера памяти,
 * но необходим для тестов и примеров использования персистентности.
 *
 * Issue #61: все функции используют только статический API PersistMemoryManager.
 * Issue #97: добавлены функции для работы с AbstractPersistMemoryManager (не синглтон).
 *
 * Использование с новым API (Issue #97):
 * @code
 * #include "pmm/abstract_pmm.h"
 * #include "pmm/io.h"
 *
 * pmm::presets::SingleThreadedHeap pmm;
 * pmm.create(64 * 1024);
 *
 * // Сохранить образ в файл
 * bool ok = pmm::save_manager(pmm, "heap.dat");
 *
 * // Загрузить образ из файла (через HeapStorage — сначала нужно выделить буфер)
 * pmm::presets::SingleThreadedHeap pmm2;
 * pmm2.create(64 * 1024);   // выделяем буфер нужного размера
 * bool ok2 = pmm::load_manager_from_file(pmm2, "heap.dat");
 * @endcode
 *
 * Использование со старым API (синглтон, устарело):
 * @code
 * #include "pmm/legacy_manager.h"
 * #include "pmm/io.h"
 *
 * // Сохранить образ в файл
 * bool ok = pmm::save( "heap.dat" );
 *
 * // Загрузить образ из файла в предоставленный буфер
 * void* buf = std::malloc( size );
 * bool ok2 = pmm::load_from_file( "heap.dat", buf, size );
 * @endcode
 */

#pragma once

#include "pmm/abstract_pmm.h"
#include "pmm/legacy_manager.h"

#include <cstdio>

namespace pmm
{

// ─── Новый API для AbstractPersistMemoryManager (Issue #97) ──────────────────

/**
 * @brief Сохранить образ AbstractPersistMemoryManager в файл.
 *
 * Записывает весь управляемый буфер в файл побайтово.
 * Поскольку все метаданные используют смещения (offsets) от начала буфера,
 * образ корректно загружается по любому базовому адресу через load_manager_from_file().
 *
 * Issue #97: работает с любым экземпляром AbstractPersistMemoryManager (не синглтон).
 *
 * @tparam AddressTraitsT  Traits адресного пространства.
 * @tparam StorageBackendT Бэкенд хранилища.
 * @tparam FreeBlockTreeT  Политика дерева свободных блоков.
 * @tparam ThreadPolicyT   Политика многопоточности.
 * @param mgr      Менеджер персистентной памяти.
 * @param filename Путь к выходному файлу.
 * @return true при успешной записи, false при ошибке ввода/вывода или если не инициализирован.
 *
 * Предусловие:  filename != nullptr, mgr.is_initialized() == true.
 * Постусловие: файл содержит точную копию управляемой области памяти.
 */
template <typename AddressTraitsT, typename StorageBackendT, typename FreeBlockTreeT, typename ThreadPolicyT>
inline bool
save_manager( const AbstractPersistMemoryManager<AddressTraitsT, StorageBackendT, FreeBlockTreeT, ThreadPolicyT>& mgr,
              const char* filename )
{
    if ( filename == nullptr || !mgr.is_initialized() )
        return false;
    const std::uint8_t* data  = mgr.backend().base_ptr();
    std::size_t         total = mgr.backend().total_size();
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
 * @brief Загрузить образ менеджера из файла в экземпляр AbstractPersistMemoryManager.
 *
 * Читает файл, записанный функцией save_manager(), в буфер менеджера,
 * затем вызывает mgr.load() для проверки заголовка и восстановления состояния.
 *
 * Issue #97: работает с любым экземпляром AbstractPersistMemoryManager (не синглтон).
 *
 * Примечание: бэкенд менеджера должен уже иметь выделенный буфер достаточного размера.
 * Для HeapStorage вызовите create(size) перед load_manager_from_file().
 * Для MMapStorage вызовите backend().open() перед load_manager_from_file().
 *
 * @tparam AddressTraitsT  Traits адресного пространства.
 * @tparam StorageBackendT Бэкенд хранилища.
 * @tparam FreeBlockTreeT  Политика дерева свободных блоков.
 * @tparam ThreadPolicyT   Политика многопоточности.
 * @param mgr      Менеджер персистентной памяти (должен иметь уже выделенный буфер).
 * @param filename Путь к файлу с образом.
 * @return true при успешной загрузке, false при ошибке.
 *
 * Предусловие:  filename != nullptr, mgr.backend().base_ptr() != nullptr.
 * Постусловие: менеджер восстановлен из файла.
 */
template <typename AddressTraitsT, typename StorageBackendT, typename FreeBlockTreeT, typename ThreadPolicyT>
inline bool load_manager_from_file(
    AbstractPersistMemoryManager<AddressTraitsT, StorageBackendT, FreeBlockTreeT, ThreadPolicyT>& mgr,
    const char*                                                                                   filename )
{
    if ( filename == nullptr )
        return false;

    std::uint8_t* buf  = mgr.backend().base_ptr();
    std::size_t   size = mgr.backend().total_size();
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

    return mgr.load();
}

// ─── Устаревший API для PersistMemoryManager-синглтона (Issue #61) ────────────

/**
 * @brief Сохранить образ управляемой памяти в файл (устаревший API синглтона).
 *
 * @deprecated Используйте save_manager() с AbstractPersistMemoryManager (Issue #97).
 *
 * Записывает весь буфер текущего синглтона в файл побайтово.
 *
 * @param filename Путь к выходному файлу.
 * @return true при успешной записи, false при ошибке ввода/вывода или нет синглтона.
 */
inline bool save( const char* filename )
{
    if ( filename == nullptr )
        return false;
    PersistMemoryManager<>* mgr = PersistMemoryManager<>::instance();
    if ( mgr == nullptr )
        return false;
    std::FILE* f = std::fopen( filename, "wb" );
    if ( f == nullptr )
        return false;
    std::size_t total   = PersistMemoryManager<>::total_size();
    const void* data    = static_cast<const void*>( mgr );
    std::size_t written = std::fwrite( data, 1, total, f );
    std::fclose( f );
    return written == total;
}

/**
 * @brief Загрузить образ менеджера из файла в существующий буфер (устаревший API синглтона).
 *
 * @deprecated Используйте load_manager_from_file() с AbstractPersistMemoryManager (Issue #97).
 *
 * @param filename Путь к файлу с образом.
 * @param memory   Указатель на буфер для загрузки (размер >= размера файла).
 * @param size     Размер буфера в байтах.
 * @return true при успешной загрузке, false при ошибке.
 */
inline bool load_from_file( const char* filename, void* memory, std::size_t size )
{
    if ( filename == nullptr || memory == nullptr || size < detail::kMinMemorySize )
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

    std::size_t read_bytes = std::fread( memory, 1, file_size, f );
    std::fclose( f );

    if ( read_bytes != file_size )
        return false;

    return PersistMemoryManager<>::load( memory, file_size );
}

} // namespace pmm
