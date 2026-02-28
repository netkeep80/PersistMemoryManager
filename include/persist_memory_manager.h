/**
 * @file persist_memory_manager.h
 * @brief Менеджер персистентной кучи памяти для C++17
 *
 * Single-header библиотека управления персистентной памятью.
 * Предоставляет низкоуровневый менеджер памяти, хранящий все метаданные
 * в управляемой области памяти для возможности персистентности между запусками.
 *
 * Фаза 1: Базовая структура, allocate и deallocate.
 *
 * Использование:
 * @code
 * #include "persist_memory_manager.h"
 *
 * int main() {
 *     void* memory = std::malloc( 1024 * 1024 );
 *     auto* mgr    = pmm::PersistMemoryManager::create( memory, 1024 * 1024 );
 *     void* block  = mgr->allocate( 256 );
 *     mgr->deallocate( block );
 *     mgr->destroy();
 *     std::free( memory );
 *     return 0;
 * }
 * @endcode
 *
 * @version 0.1.0 (Фаза 1)
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace pmm
{

// ─── Константы ────────────────────────────────────────────────────────────────

/// Выравнивание по умолчанию (байт)
static constexpr std::size_t kDefaultAlignment = 16;

/// Минимальное поддерживаемое выравнивание (байт)
static constexpr std::size_t kMinAlignment = 8;

/// Максимальное поддерживаемое выравнивание (байт)
static constexpr std::size_t kMaxAlignment = 4096;

/// Минимальный размер области памяти (байт)
static constexpr std::size_t kMinMemorySize = 4096;

/// Минимальный размер блока (байт)
static constexpr std::size_t kMinBlockSize = 32;

/// Магическое число для валидации заголовка менеджера
static constexpr std::uint64_t kMagic = 0x504D4D5F56303130ULL; // "PMM_V010"

// ─── Коды ошибок ──────────────────────────────────────────────────────────────

/**
 * @brief Коды ошибок библиотеки
 */
enum class ErrorCode
{
    OK = 0,            ///< Успешное завершение
    OUT_OF_MEMORY,     ///< Недостаточно памяти
    INVALID_POINTER,   ///< Неверный указатель
    INVALID_ALIGNMENT, ///< Неверное выравнивание
    CORRUPTED_METADATA ///< Повреждённые метаданные
};

/**
 * @brief Результат операции с кодом ошибки и сообщением
 */
struct Result
{
    ErrorCode   code;    ///< Код ошибки
    const char* message; ///< Текстовое описание ошибки
};

// ─── Статистика ───────────────────────────────────────────────────────────────

/**
 * @brief Статистика состояния менеджера памяти
 */
struct MemoryStats
{
    std::size_t total_blocks;        ///< Общее количество блоков
    std::size_t free_blocks;         ///< Количество свободных блоков
    std::size_t allocated_blocks;    ///< Количество занятых блоков
    std::size_t largest_free;        ///< Размер наибольшего свободного блока
    std::size_t smallest_free;       ///< Размер наименьшего свободного блока
    std::size_t total_fragmentation; ///< Суммарная фрагментация (байт)
};

/**
 * @brief Информация о выделенном блоке
 */
struct AllocationInfo
{
    void*       ptr;       ///< Указатель на данные пользователя
    std::size_t size;      ///< Размер пользовательских данных (байт)
    std::size_t alignment; ///< Выравнивание
    bool        is_valid;  ///< Признак корректного блока
};

// ─── Внутренние структуры ─────────────────────────────────────────────────────

namespace detail
{

/**
 * @brief Заголовок одного блока памяти.
 *
 * Все поля — смещения (offsets) от начала управляемой области, что
 * обеспечивает корректную работу после перезагрузки образа по другому
 * базовому адресу.
 *
 * Предусловие: ptr[] выровнен на kDefaultAlignment.
 * Постусловие: sizeof(BlockHeader) кратен kDefaultAlignment.
 */
struct BlockHeader
{
    std::uint64_t  magic;       ///< Магическое число для проверки корректности
    std::ptrdiff_t prev_offset; ///< Смещение предыдущего блока (-1 = нет)
    std::ptrdiff_t next_offset; ///< Смещение следующего блока (-1 = нет)
    std::size_t total_size; ///< Полный размер блока, включая заголовок и выравнивание
    std::size_t  user_size; ///< Размер пользовательских данных (байт)
    std::size_t  alignment; ///< Выравнивание пользовательских данных
    bool         used;      ///< true — блок занят, false — свободен
    std::uint8_t _pad[7];   ///< Выравнивание до 8 байт (совместимость ABI)
    // После заголовка следуют пользовательские данные, выровненные на alignment
};

static_assert( sizeof( BlockHeader ) % 8 == 0, "BlockHeader must be 8-byte aligned" );

/// Магическое число заголовка блока
static constexpr std::uint64_t kBlockMagic = 0x424C4F434B484452ULL; // "BLOCKHDR"

/// Смещение «нет следующего/предыдущего блока»
static constexpr std::ptrdiff_t kNoBlock = -1;

/**
 * @brief Заголовок всей управляемой области памяти.
 *
 * Хранится в самом начале переданного буфера.
 */
struct ManagerHeader
{
    std::uint64_t magic;       ///< Магическое число (kMagic)
    std::size_t   total_size;  ///< Полный размер управляемой области (байт)
    std::size_t   used_size;   ///< Занятый объём (метаданные + данные, байт)
    std::size_t   block_count; ///< Общее количество блоков
    std::size_t   free_count;  ///< Количество свободных блоков
    std::size_t   alloc_count; ///< Количество занятых блоков
    /// Смещение до первого блока в связном списке всех блоков
    std::ptrdiff_t first_block_offset;
    std::uint8_t   _pad[8]; ///< Резерв для будущего расширения
};

static_assert( sizeof( ManagerHeader ) % 16 == 0, "ManagerHeader must be 16-byte aligned" );

// ─── Вспомогательные функции ──────────────────────────────────────────────────

/**
 * @brief Выровнять значение @p value вверх до кратного @p align.
 * @pre align — степень двойки.
 */
inline std::size_t align_up( std::size_t value, std::size_t align )
{
    assert( align != 0 && ( align & ( align - 1 ) ) == 0 );
    return ( value + align - 1 ) & ~( align - 1 );
}

/**
 * @brief Проверить, является ли @p align корректным выравниванием (степень двойки).
 */
inline bool is_valid_alignment( std::size_t align )
{
    return align >= kMinAlignment && align <= kMaxAlignment && ( align & ( align - 1 ) ) == 0;
}

/**
 * @brief Получить указатель на заголовок блока по смещению от базы.
 * @param base  Начало управляемой области.
 * @param offset Смещение (>= 0).
 */
inline BlockHeader* block_at( std::uint8_t* base, std::ptrdiff_t offset )
{
    assert( offset >= 0 );
    return reinterpret_cast<BlockHeader*>( base + offset );
}

/**
 * @brief Вычислить смещение блока от базы.
 */
inline std::ptrdiff_t block_offset( const std::uint8_t* base, const BlockHeader* block )
{
    return reinterpret_cast<const std::uint8_t*>( block ) - base;
}

/**
 * @brief Вычислить указатель на пользовательские данные внутри блока.
 *
 * Данные начинаются сразу после BlockHeader, выровненные на block->alignment.
 */
inline void* user_ptr( BlockHeader* block )
{
    std::uint8_t* raw          = reinterpret_cast<std::uint8_t*>( block ) + sizeof( BlockHeader );
    std::size_t   addr         = reinterpret_cast<std::size_t>( raw );
    std::size_t   aligned_addr = align_up( addr, block->alignment );
    return reinterpret_cast<void*>( aligned_addr );
}

/**
 * @brief Получить заголовок блока из указателя на пользовательские данные.
 *
 * Сканирует назад от ptr в поисках магического числа BlockHeader.
 * @param base  Начало управляемой области.
 * @param ptr   Указатель на пользовательские данные.
 * @return Заголовок блока или nullptr, если не найден.
 */
inline BlockHeader* header_from_ptr( std::uint8_t* base, void* ptr )
{
    if ( ptr == nullptr )
    {
        return nullptr;
    }
    // Сканируем назад от ptr до base + sizeof(ManagerHeader)
    std::uint8_t* p        = reinterpret_cast<std::uint8_t*>( ptr );
    std::uint8_t* min_addr = base + sizeof( ManagerHeader );
    while ( p > min_addr )
    {
        // Отступить на sizeof(BlockHeader) и проверить магическое число
        if ( p - sizeof( BlockHeader ) >= min_addr )
        {
            p -= sizeof( BlockHeader );
            BlockHeader* candidate = reinterpret_cast<BlockHeader*>( p );
            if ( candidate->magic == kBlockMagic && candidate->used )
            {
                // Проверяем, что user_ptr этого кандидата совпадает с исходным ptr
                if ( user_ptr( candidate ) == ptr )
                {
                    return candidate;
                }
            }
        }
        else
        {
            break;
        }
        // Шагаем назад по выравниванию
        p = reinterpret_cast<std::uint8_t*>( ptr ) - sizeof( BlockHeader );
        break;
    }
    return nullptr;
}

/**
 * @brief Найти заголовок блока по указателю пользователя через линейный обход.
 *
 * Надёжный способ: обходит весь связный список блоков.
 * @param base         Начало управляемой области.
 * @param mgr_header   Заголовок менеджера.
 * @param ptr          Указатель пользователя.
 * @return Заголовок блока или nullptr.
 */
inline BlockHeader* find_block_by_ptr( std::uint8_t* base, const ManagerHeader* mgr_header, void* ptr )
{
    if ( mgr_header->first_block_offset == kNoBlock )
    {
        return nullptr;
    }
    std::ptrdiff_t offset = mgr_header->first_block_offset;
    while ( offset != kNoBlock )
    {
        BlockHeader* blk = block_at( base, offset );
        if ( blk->used && user_ptr( blk ) == ptr )
        {
            return blk;
        }
        offset = blk->next_offset;
    }
    return nullptr;
}

/**
 * @brief Вычислить минимальный размер блока для запроса user_size с заданным выравниванием.
 *
 * total_size = sizeof(BlockHeader) + padding_for_alignment + user_size
 * Гарантируется не менее kMinBlockSize.
 */
inline std::size_t required_block_size( std::size_t user_size, std::size_t alignment )
{
    // Максимально возможный padding до alignment (в худшем случае alignment-1 байт)
    std::size_t max_padding = alignment - 1;
    std::size_t min_total   = sizeof( BlockHeader ) + max_padding + user_size;
    return std::max( min_total, kMinBlockSize );
}

} // namespace detail

// ─── Основной класс ───────────────────────────────────────────────────────────

class PersistMemoryManager;

/**
 * @brief Получить статистику менеджера памяти.
 * @param mgr Указатель на менеджер.
 * @return Структура MemoryStats.
 */
MemoryStats get_stats( const PersistMemoryManager* mgr );

/**
 * @brief Получить информацию о блоке по указателю пользователя.
 * @param mgr Указатель на менеджер.
 * @param ptr Указатель пользователя.
 * @return Структура AllocationInfo.
 */
AllocationInfo get_info( const PersistMemoryManager* mgr, void* ptr );

/**
 * @brief Менеджер персистентной памяти.
 *
 * Управляет областью памяти, переданной при создании. Все метаданные
 * хранятся внутри этой области, что позволяет сохранять и загружать
 * образ памяти из файла.
 *
 * Предусловие для create(): размер >= kMinMemorySize.
 * Постусловие для create(): возвращаемый указатель != nullptr.
 */
class PersistMemoryManager
{
  public:
    // ─── Инициализация ────────────────────────────────────────────────────────

    /**
     * @brief Создать новый менеджер памяти в переданной области.
     *
     * Инициализирует метаданные в начале буфера и создаёт один большой
     * свободный блок на всё оставшееся пространство.
     *
     * @param memory Указатель на начало управляемой области.
     * @param size   Размер области в байтах (>= kMinMemorySize).
     * @return Указатель на менеджер или nullptr при ошибке.
     *
     * Предусловие:  memory != nullptr, size >= kMinMemorySize.
     * Постусловие: все байты области инициализированы, первый блок свободен.
     */
    static PersistMemoryManager* create( void* memory, std::size_t size )
    {
        if ( memory == nullptr || size < kMinMemorySize )
        {
            return nullptr;
        }

        std::uint8_t* base = static_cast<std::uint8_t*>( memory );

        // Инициализируем заголовок менеджера
        detail::ManagerHeader* hdr = reinterpret_cast<detail::ManagerHeader*>( base );
        std::memset( hdr, 0, sizeof( detail::ManagerHeader ) );
        hdr->magic              = kMagic;
        hdr->total_size         = size;
        hdr->used_size          = sizeof( detail::ManagerHeader );
        hdr->block_count        = 0;
        hdr->free_count         = 0;
        hdr->alloc_count        = 0;
        hdr->first_block_offset = detail::kNoBlock;

        // Вычисляем позицию первого (и пока единственного) свободного блока
        std::size_t    hdr_end = detail::align_up( sizeof( detail::ManagerHeader ), kDefaultAlignment );
        std::ptrdiff_t blk_off = static_cast<std::ptrdiff_t>( hdr_end );

        if ( static_cast<std::size_t>( blk_off ) + sizeof( detail::BlockHeader ) + kMinBlockSize > size )
        {
            // Слишком мало места даже для одного блока
            return nullptr;
        }

        // Создаём единственный свободный блок на всё доступное пространство
        detail::BlockHeader* blk = detail::block_at( base, blk_off );
        blk->magic               = detail::kBlockMagic;
        blk->prev_offset         = detail::kNoBlock;
        blk->next_offset         = detail::kNoBlock;
        blk->total_size          = size - static_cast<std::size_t>( blk_off );
        blk->user_size           = 0;
        blk->alignment           = kDefaultAlignment;
        blk->used                = false;
        std::memset( blk->_pad, 0, sizeof( blk->_pad ) );

        hdr->first_block_offset = blk_off;
        hdr->block_count        = 1;
        hdr->free_count         = 1;
        hdr->used_size          = hdr_end + sizeof( detail::BlockHeader );

        return reinterpret_cast<PersistMemoryManager*>( base );
    }

    /**
     * @brief Загрузить менеджер из существующего образа памяти.
     *
     * Проверяет магическое число и базовую целостность заголовка.
     * Не пересчитывает абсолютные указатели (используются только смещения).
     *
     * @param memory Указатель на начало области с сохранённым образом.
     * @param size   Размер области в байтах.
     * @return Указатель на менеджер или nullptr при ошибке.
     */
    static PersistMemoryManager* load( void* memory, std::size_t size )
    {
        if ( memory == nullptr || size < kMinMemorySize )
        {
            return nullptr;
        }
        std::uint8_t*          base = static_cast<std::uint8_t*>( memory );
        detail::ManagerHeader* hdr  = reinterpret_cast<detail::ManagerHeader*>( base );
        if ( hdr->magic != kMagic || hdr->total_size != size )
        {
            return nullptr;
        }
        return reinterpret_cast<PersistMemoryManager*>( base );
    }

    /**
     * @brief Уничтожить менеджер (обнулить магическое число в заголовке).
     *
     * После вызова использование менеджера через тот же указатель
     * приведёт к неопределённому поведению.
     */
    void destroy()
    {
        detail::ManagerHeader* hdr = header();
        hdr->magic                 = 0;
    }

    // ─── Выделение / освобождение ─────────────────────────────────────────────

    /**
     * @brief Выделить блок памяти.
     *
     * Алгоритм: линейный поиск подходящего свободного блока (first-fit).
     * При нахождении блока большего размера — разбивает его на два.
     *
     * @param user_size  Требуемый размер пользовательских данных (байт).
     * @param alignment  Требуемое выравнивание (степень двойки, [8..4096]).
     * @return Выровненный указатель на данные или nullptr при ошибке.
     *
     * Постусловие: возвращённый указатель выровнен на @p alignment.
     */
    void* allocate( std::size_t user_size, std::size_t alignment = kDefaultAlignment )
    {
        if ( user_size == 0 )
        {
            return nullptr;
        }
        if ( !detail::is_valid_alignment( alignment ) )
        {
            return nullptr;
        }

        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();

        std::size_t needed = detail::required_block_size( user_size, alignment );

        // Линейный поиск первого подходящего свободного блока
        std::ptrdiff_t offset = hdr->first_block_offset;
        while ( offset != detail::kNoBlock )
        {
            detail::BlockHeader* blk = detail::block_at( base, offset );
            if ( !blk->used && blk->total_size >= needed )
            {
                return allocate_from_block( blk, user_size, alignment );
            }
            offset = blk->next_offset;
        }

        return nullptr; // Не хватает памяти
    }

    /**
     * @brief Освободить ранее выделенный блок памяти.
     *
     * @param ptr Указатель, возвращённый из allocate(). nullptr игнорируется.
     *
     * Предусловие:  ptr == nullptr или ptr был получен из allocate() этого менеджера.
     * Постусловие: блок помечен как свободный, статистика обновлена.
     */
    void deallocate( void* ptr )
    {
        if ( ptr == nullptr )
        {
            return;
        }

        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();

        detail::BlockHeader* blk = detail::find_block_by_ptr( base, hdr, ptr );
        if ( blk == nullptr || !blk->used )
        {
            // Неверный указатель — игнорируем (не бросаем исключение)
            return;
        }

        std::size_t freed = blk->user_size;
        blk->used         = false;
        blk->user_size    = 0;

        hdr->alloc_count--;
        hdr->free_count++;
        if ( hdr->used_size >= freed )
        {
            hdr->used_size -= freed;
        }
    }

    /**
     * @brief Перевыделить блок памяти (изменить размер).
     *
     * Если новый размер меньше или равен текущему — возвращает тот же указатель.
     * Иначе — выделяет новый блок, копирует данные, освобождает старый.
     *
     * @param ptr      Указатель на существующий блок (или nullptr).
     * @param new_size Новый размер пользовательских данных (байт).
     * @return Новый указатель или nullptr при ошибке.
     */
    void* reallocate( void* ptr, std::size_t new_size )
    {
        if ( ptr == nullptr )
        {
            return allocate( new_size );
        }
        if ( new_size == 0 )
        {
            deallocate( ptr );
            return nullptr;
        }

        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();
        detail::BlockHeader*   blk  = detail::find_block_by_ptr( base, hdr, ptr );
        if ( blk == nullptr || !blk->used )
        {
            return nullptr;
        }

        if ( new_size <= blk->user_size )
        {
            return ptr; // Текущего места достаточно
        }

        void* new_ptr = allocate( new_size, blk->alignment );
        if ( new_ptr == nullptr )
        {
            return nullptr;
        }

        std::memcpy( new_ptr, ptr, blk->user_size );
        deallocate( ptr );
        return new_ptr;
    }

    // ─── Метрики ──────────────────────────────────────────────────────────────

    /**
     * @brief Полный размер управляемой области (байт).
     */
    std::size_t total_size() const { return header()->total_size; }

    /**
     * @brief Объём занятой памяти (метаданные + данные пользователя, байт).
     */
    std::size_t used_size() const { return header()->used_size; }

    /**
     * @brief Объём свободной памяти (байт).
     */
    std::size_t free_size() const
    {
        const detail::ManagerHeader* hdr = header();
        return ( hdr->total_size > hdr->used_size ) ? ( hdr->total_size - hdr->used_size ) : 0;
    }

    /**
     * @brief Оценка фрагментации: количество свободных блоков (не сумма байт).
     *
     * 0 — нет фрагментации, >0 — есть фрагментированные свободные регионы.
     */
    std::size_t fragmentation() const
    {
        const detail::ManagerHeader* hdr = header();
        return ( hdr->free_count > 1 ) ? ( hdr->free_count - 1 ) : 0;
    }

    // ─── Диагностика ──────────────────────────────────────────────────────────

    /**
     * @brief Проверить целостность всех структур менеджера.
     *
     * Проверяет магические числа, счётчики блоков и связность списка.
     *
     * @return true, если структуры корректны.
     */
    bool validate() const
    {
        const std::uint8_t*          base = const_base_ptr();
        const detail::ManagerHeader* hdr  = header();

        if ( hdr->magic != kMagic )
        {
            return false;
        }

        std::size_t    block_count = 0;
        std::size_t    free_count  = 0;
        std::size_t    alloc_count = 0;
        std::ptrdiff_t offset      = hdr->first_block_offset;

        while ( offset != detail::kNoBlock )
        {
            if ( offset < 0 || static_cast<std::size_t>( offset ) >= hdr->total_size )
            {
                return false; // Смещение вне диапазона
            }
            const detail::BlockHeader* blk = reinterpret_cast<const detail::BlockHeader*>( base + offset );
            if ( blk->magic != detail::kBlockMagic )
            {
                return false; // Повреждён заголовок блока
            }
            block_count++;
            if ( blk->used )
            {
                alloc_count++;
            }
            else
            {
                free_count++;
            }
            // Проверка цепочки prev/next
            if ( blk->next_offset != detail::kNoBlock )
            {
                const detail::BlockHeader* next_blk =
                    reinterpret_cast<const detail::BlockHeader*>( base + blk->next_offset );
                if ( next_blk->prev_offset != offset )
                {
                    return false; // Нарушена двусвязная цепочка
                }
            }
            offset = blk->next_offset;
        }

        return ( block_count == hdr->block_count && free_count == hdr->free_count && alloc_count == hdr->alloc_count );
    }

    /**
     * @brief Вывести статистику менеджера в stdout.
     */
    void dump_stats() const
    {
        const detail::ManagerHeader* hdr = header();
        std::cout << "=== PersistMemoryManager stats ===\n"
                  << "  total_size  : " << hdr->total_size << " bytes\n"
                  << "  used_size   : " << hdr->used_size << " bytes\n"
                  << "  free_size   : " << free_size() << " bytes\n"
                  << "  blocks      : " << hdr->block_count << " (free=" << hdr->free_count
                  << ", alloc=" << hdr->alloc_count << ")\n"
                  << "  fragmentation: " << fragmentation() << " extra free segments\n"
                  << "==================================\n";
    }

    /**
     * @brief Дружественный доступ для get_stats() и get_info().
     */
    friend MemoryStats    get_stats( const PersistMemoryManager* mgr );
    friend AllocationInfo get_info( const PersistMemoryManager* mgr, void* ptr );

  private:
    // ─── Вспомогательные методы ───────────────────────────────────────────────

    /**
     * @brief Получить указатель на начало управляемой области.
     */
    std::uint8_t* base_ptr() { return reinterpret_cast<std::uint8_t*>( this ); }

    const std::uint8_t* const_base_ptr() const { return reinterpret_cast<const std::uint8_t*>( this ); }

    /**
     * @brief Получить заголовок менеджера.
     */
    detail::ManagerHeader* header() { return reinterpret_cast<detail::ManagerHeader*>( this ); }

    const detail::ManagerHeader* header() const { return reinterpret_cast<const detail::ManagerHeader*>( this ); }

    /**
     * @brief Выделить память из конкретного свободного блока.
     *
     * Если блок значительно больше нужного — разбивает его на два.
     *
     * @param blk        Свободный блок.
     * @param user_size  Размер пользовательских данных.
     * @param alignment  Требуемое выравнивание.
     * @return Указатель на пользовательские данные.
     */
    void* allocate_from_block( detail::BlockHeader* blk, std::size_t user_size, std::size_t alignment )
    {
        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();

        // Минимальный размер остатка для создания нового свободного блока
        std::size_t min_remainder = sizeof( detail::BlockHeader ) + kMinBlockSize;

        std::size_t needed    = detail::required_block_size( user_size, alignment );
        bool        can_split = ( blk->total_size >= needed + min_remainder );

        if ( can_split )
        {
            // Создаём новый свободный блок из остатка
            std::ptrdiff_t blk_off     = detail::block_offset( base, blk );
            std::ptrdiff_t new_blk_off = blk_off + static_cast<std::ptrdiff_t>( needed );

            detail::BlockHeader* new_blk = detail::block_at( base, new_blk_off );
            new_blk->magic               = detail::kBlockMagic;
            new_blk->total_size          = blk->total_size - needed;
            new_blk->user_size           = 0;
            new_blk->alignment           = kDefaultAlignment;
            new_blk->used                = false;
            new_blk->prev_offset         = blk_off;
            new_blk->next_offset         = blk->next_offset;
            std::memset( new_blk->_pad, 0, sizeof( new_blk->_pad ) );

            // Обновляем next указатель у следующего блока
            if ( blk->next_offset != detail::kNoBlock )
            {
                detail::BlockHeader* next_blk = detail::block_at( base, blk->next_offset );
                next_blk->prev_offset         = new_blk_off;
            }

            blk->next_offset = new_blk_off;
            blk->total_size  = needed;

            hdr->block_count++;
            hdr->free_count++;
        }

        // Помечаем блок как занятый
        blk->used      = true;
        blk->user_size = user_size;
        blk->alignment = alignment;

        hdr->alloc_count++;
        hdr->free_count--;
        hdr->used_size += user_size;

        return detail::user_ptr( blk );
    }
};

// ─── Реализация свободных функций ─────────────────────────────────────────────

/**
 * @brief Получить подробную статистику менеджера памяти.
 */
inline MemoryStats get_stats( const PersistMemoryManager* mgr )
{
    MemoryStats stats{};
    if ( mgr == nullptr )
    {
        return stats;
    }

    const std::uint8_t*          base = mgr->const_base_ptr();
    const detail::ManagerHeader* hdr  = mgr->header();

    stats.total_blocks     = hdr->block_count;
    stats.free_blocks      = hdr->free_count;
    stats.allocated_blocks = hdr->alloc_count;

    bool first_free           = true;
    stats.largest_free        = 0;
    stats.smallest_free       = 0;
    stats.total_fragmentation = 0;

    std::ptrdiff_t offset = hdr->first_block_offset;
    while ( offset != detail::kNoBlock )
    {
        const detail::BlockHeader* blk = reinterpret_cast<const detail::BlockHeader*>( base + offset );
        if ( !blk->used )
        {
            if ( first_free )
            {
                stats.largest_free  = blk->total_size;
                stats.smallest_free = blk->total_size;
                first_free          = false;
            }
            else
            {
                stats.largest_free  = std::max( stats.largest_free, blk->total_size );
                stats.smallest_free = std::min( stats.smallest_free, blk->total_size );
                stats.total_fragmentation += blk->total_size;
            }
        }
        offset = blk->next_offset;
    }

    return stats;
}

/**
 * @brief Получить информацию о блоке по указателю пользователя.
 */
inline AllocationInfo get_info( const PersistMemoryManager* mgr, void* ptr )
{
    AllocationInfo info{};
    info.ptr      = ptr;
    info.is_valid = false;

    if ( mgr == nullptr || ptr == nullptr )
    {
        return info;
    }

    std::uint8_t*          base    = const_cast<PersistMemoryManager*>( mgr )->base_ptr();
    detail::ManagerHeader* hdr_ptr = const_cast<PersistMemoryManager*>( mgr )->header();
    detail::BlockHeader*   blk     = detail::find_block_by_ptr( base, hdr_ptr, ptr );

    if ( blk != nullptr && blk->used )
    {
        info.size      = blk->user_size;
        info.alignment = blk->alignment;
        info.is_valid  = true;
    }

    return info;
}

} // namespace pmm
