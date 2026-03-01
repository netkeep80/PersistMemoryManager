/**
 * @file persist_memory_io.h
 * @brief Утилиты файлового ввода/вывода для PersistMemoryManager
 *
 * Вспомогательный заголовочный файл с функциями сохранения и загрузки
 * образа памяти в/из файла. Вынесен в отдельный файл, так как файловый
 * ввод/вывод не является основной функциональностью менеджера памяти,
 * но необходим для тестов и примеров использования персистентности.
 *
 * Использование:
 * @code
 * #include "persist_memory_manager.h"
 * #include "persist_memory_io.h"
 *
 * // Сохранить образ в файл
 * bool ok = pmm::save( mgr, "heap.dat" );
 *
 * // Загрузить образ из файла
 * void* buf = std::malloc( size );
 * auto* mgr2 = pmm::load_from_file( "heap.dat", buf, size );
 * @endcode
 */

#pragma once

#include "persist_memory_manager.h"

#include <cstdio>

namespace pmm
{

/**
 * @brief Сохранить образ управляемой памяти в файл.
 *
 * Записывает весь буфер (от начала до total_size байт) в файл побайтово.
 * Поскольку все метаданные используют смещения (offsets) от начала буфера,
 * образ корректно загружается по любому базовому адресу через load_from_file().
 *
 * @param mgr      Указатель на менеджер памяти.
 * @param filename Путь к выходному файлу.
 * @return true при успешной записи, false при ошибке ввода/вывода.
 *
 * Предусловие:  mgr != nullptr, filename != nullptr.
 * Постусловие: файл содержит точную копию управляемой области памяти.
 */
inline bool save( const PersistMemoryManager* mgr, const char* filename )
{
    if ( mgr == nullptr || filename == nullptr )
        return false;
    std::FILE* f = std::fopen( filename, "wb" );
    if ( f == nullptr )
        return false;
    std::size_t total   = mgr->total_size();
    const void* data    = static_cast<const void*>( mgr );
    std::size_t written = std::fwrite( data, 1, total, f );
    std::fclose( f );
    return written == total;
}

/**
 * @brief Загрузить образ менеджера из файла в существующий буфер.
 *
 * Читает файл, записанный функцией save(), в буфер @p memory,
 * затем вызывает PersistMemoryManager::load() для проверки заголовка.
 *
 * @param filename Путь к файлу с образом.
 * @param memory   Указатель на буфер для загрузки (размер >= размера файла).
 * @param size     Размер буфера в байтах.
 * @return Указатель на восстановленный менеджер или nullptr при ошибке.
 *
 * Предусловие:  filename != nullptr, memory != nullptr, size >= kMinMemorySize.
 * Постусловие: менеджер в буфере полностью восстановлен из файла.
 */
inline PersistMemoryManager* load_from_file( const char* filename, void* memory, std::size_t size )
{
    if ( filename == nullptr || memory == nullptr || size < kMinMemorySize )
        return nullptr;

    std::FILE* f = std::fopen( filename, "rb" );
    if ( f == nullptr )
        return nullptr;

    if ( std::fseek( f, 0, SEEK_END ) != 0 )
    {
        std::fclose( f );
        return nullptr;
    }
    long file_size_long = std::ftell( f );
    if ( file_size_long <= 0 )
    {
        std::fclose( f );
        return nullptr;
    }
    std::rewind( f );

    std::size_t file_size = static_cast<std::size_t>( file_size_long );
    if ( file_size > size )
    {
        std::fclose( f );
        return nullptr;
    }

    std::size_t read_bytes = std::fread( memory, 1, file_size, f );
    std::fclose( f );

    if ( read_bytes != file_size )
        return nullptr;

    return PersistMemoryManager::load( memory, file_size );
}

} // namespace pmm
