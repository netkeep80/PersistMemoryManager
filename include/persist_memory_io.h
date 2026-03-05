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
 *
 * Использование:
 * @code
 * #include "persist_memory_manager.h"
 * #include "persist_memory_io.h"
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

#include "persist_memory_manager.h"

#include <cstdio>

namespace pmm
{

/**
 * @brief Сохранить образ управляемой памяти в файл.
 *
 * Записывает весь буфер текущего синглтона в файл побайтово.
 * Поскольку все метаданные используют смещения (offsets) от начала буфера,
 * образ корректно загружается по любому базовому адресу через load_from_file().
 *
 * Issue #61: не принимает PersistMemoryManager* — использует синглтон.
 *
 * @param filename Путь к выходному файлу.
 * @return true при успешной записи, false при ошибке ввода/вывода или нет синглтона.
 *
 * Предусловие:  filename != nullptr, менеджер инициализирован.
 * Постусловие: файл содержит точную копию управляемой области памяти.
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
 * @brief Загрузить образ менеджера из файла в существующий буфер.
 *
 * Читает файл, записанный функцией save(), в буфер @p memory,
 * затем вызывает PersistMemoryManager::load() для проверки заголовка
 * и восстановления синглтона.
 *
 * Issue #61: возвращает bool вместо PersistMemoryManager*.
 *
 * @param filename Путь к файлу с образом.
 * @param memory   Указатель на буфер для загрузки (размер >= размера файла).
 * @param size     Размер буфера в байтах.
 * @return true при успешной загрузке, false при ошибке.
 *
 * Предусловие:  filename != nullptr, memory != nullptr, size >= kMinMemorySize.
 * Постусловие: синглтон восстановлен из файла.
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
