/**
 * @file persist_memory_manager.h
 * @brief Менеджер персистентной кучи памяти для C++17
 *
 * Single-header библиотека управления персистентной памятью.
 * Предоставляет низкоуровневый менеджер памяти, хранящий все метаданные
 * в управляемой области памяти для возможности персистентности между запусками.
 *
 * Фаза 1: Базовая структура, allocate и deallocate.
 * Фаза 2: Слияние соседних свободных блоков (coalescing).
 * Фаза 3: Персистентность (save/load образа из файла).
 * Фаза 5: Персистный типизированный указатель pptr<T>.
 * Фаза 6: Оптимизация производительности (отдельный список свободных блоков).
 * Фаза 7: Синглтон и автоматическое расширение памяти.
 * Фаза 9: Потокобезопасность (std::recursive_mutex).
 *
 * Использование (синглтон):
 * @code
 * #include "persist_memory_manager.h"
 *
 * int main() {
 *     void* memory = std::malloc( 1024 * 1024 );
 *     auto* mgr    = pmm::PersistMemoryManager::create( memory, 1024 * 1024 );
 *     void* block  = mgr->allocate( 256 );
 *     mgr->save( "heap.dat" );   // сохранить образ в файл
 *     mgr->deallocate( block );
 *     pmm::PersistMemoryManager::destroy();
 *     // память освобождается менеджером автоматически при expand
 *     // либо вручную по sys_memory() + destroy
 *
 *     // --- следующий запуск ---
 *     void* buf2 = std::malloc( 1024 * 1024 );
 *     auto* mgr2 = pmm::load_from_file( "heap.dat", buf2, 1024 * 1024 );
 *     // mgr2 восстановлен с теми же блоками
 *     pmm::PersistMemoryManager::destroy();
 *     std::free( buf2 );
 *     return 0;
 * }
 * @endcode
 *
 * @version 0.7.0 (Фаза 9)
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>

namespace pmm
{

// Предварительное объявление для использования в pptr<T>
class PersistMemoryManager;

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

/// Коэффициент расширения памяти при нехватке (25%)
static constexpr std::size_t kGrowNumerator   = 5;
static constexpr std::size_t kGrowDenominator = 4;

// ─── Коды ошибок ──────────────────────────────────────────────────────────────

/**
 * @brief Коды ошибок библиотеки
 */
enum class ErrorCode
{
    OK = 0,             ///< Успешное завершение
    OUT_OF_MEMORY,      ///< Недостаточно памяти
    INVALID_POINTER,    ///< Неверный указатель
    INVALID_ALIGNMENT,  ///< Неверное выравнивание
    CORRUPTED_METADATA, ///< Повреждённые метаданные
    FILE_IO_ERROR       ///< Ошибка файлового ввода/вывода
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
 * Постусловие: sizeof(BlockHeader) кратен 8.
 */
struct BlockHeader
{
    std::uint64_t magic; ///< Магическое число для проверки корректности
    std::ptrdiff_t prev_offset; ///< Смещение предыдущего блока в общем списке (-1 = нет)
    std::ptrdiff_t next_offset; ///< Смещение следующего блока в общем списке (-1 = нет)
    std::size_t total_size; ///< Полный размер блока, включая заголовок и выравнивание
    std::size_t  user_size; ///< Размер пользовательских данных (байт)
    std::size_t  alignment; ///< Выравнивание пользовательских данных
    bool         used;      ///< true — блок занят, false — свободен
    std::uint8_t _pad[7];   ///< Выравнивание до 8 байт (совместимость ABI)
    /// Смещение предыдущего свободного блока в списке свободных (-1 = нет).
    /// Используется только при used == false.
    std::ptrdiff_t free_prev_offset;
    /// Смещение следующего свободного блока в списке свободных (-1 = нет).
    /// Используется только при used == false.
    std::ptrdiff_t free_next_offset;
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
    /// Смещение до первого свободного блока в отдельном списке свободных блоков.
    /// Позволяет выделению памяти сразу находить свободный блок, не обходя занятые.
    std::ptrdiff_t first_free_offset;
    bool owns_memory; ///< true — буфер принадлежит нам (malloc), destroy() освободит его
    std::uint8_t _hdr_pad[7]; ///< padding до 8 байт
    std::size_t prev_total_size; ///< размер предыдущего буфера (0 если expand() не вызывался)
    void* prev_base; ///< предыдущий буфер; не nullptr после expand(), освобождается отложено
    bool         prev_owns;    ///< true если prev_base принадлежит нам
    std::uint8_t _prev_pad[7]; ///< padding до 8 байт
};

static_assert( sizeof( ManagerHeader ) % 8 == 0, "ManagerHeader must be 8-byte aligned" );

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
 * user_ptr = align_up(block + sizeof(BlockHeader), alignment), поэтому заголовок
 * в диапазоне [ptr - sizeof(BlockHeader) - (kMaxAlignment-1), ptr - sizeof(BlockHeader)].
 * Шаг kMinAlignment: O(1) — не более kMaxAlignment/kMinAlignment = 512 итераций.
 */
inline BlockHeader* header_from_ptr( std::uint8_t* base, void* ptr )
{
    if ( ptr == nullptr )
        return nullptr;
    std::uint8_t* min_addr = base + sizeof( ManagerHeader );
    std::uint8_t* raw_ptr  = reinterpret_cast<std::uint8_t*>( ptr );
    for ( std::size_t padding = 0; padding < kMaxAlignment; padding += kMinAlignment )
    {
        if ( raw_ptr < min_addr + sizeof( BlockHeader ) + padding )
            break;
        std::uint8_t* candidate_addr = raw_ptr - sizeof( BlockHeader ) - padding;
        if ( candidate_addr < min_addr )
            break;
        BlockHeader* candidate = reinterpret_cast<BlockHeader*>( candidate_addr );
        if ( candidate->magic == kBlockMagic && candidate->used && user_ptr( candidate ) == ptr )
            return candidate;
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
 * Результат выровнен до kMinAlignment, гарантируя, что все заголовки блоков
 * располагаются на kMinAlignment-выровненных адресах — требование header_from_ptr.
 */
inline std::size_t required_block_size( std::size_t user_size, std::size_t alignment )
{
    std::size_t min_total = sizeof( BlockHeader ) + ( alignment - 1 ) + user_size;
    return align_up( std::max( min_total, kMinBlockSize ), kMinAlignment );
}

/**
 * @brief Вставить свободный блок в начало списка свободных блоков.
 *
 * @param base  Начало управляемой области.
 * @param hdr   Заголовок менеджера.
 * @param blk   Свободный блок для вставки.
 *
 * Предусловие: blk->used == false.
 */
inline void free_list_insert( std::uint8_t* base, ManagerHeader* hdr, BlockHeader* blk )
{
    std::ptrdiff_t blk_off = block_offset( base, blk );

    blk->free_prev_offset = kNoBlock;
    blk->free_next_offset = hdr->first_free_offset;

    if ( hdr->first_free_offset != kNoBlock )
    {
        BlockHeader* old_head      = block_at( base, hdr->first_free_offset );
        old_head->free_prev_offset = blk_off;
    }

    hdr->first_free_offset = blk_off;
}

/**
 * @brief Удалить блок из списка свободных блоков (при аллокации).
 *
 * @param base  Начало управляемой области.
 * @param hdr   Заголовок менеджера.
 * @param blk   Блок для удаления из списка свободных.
 */
inline void free_list_remove( std::uint8_t* base, ManagerHeader* hdr, BlockHeader* blk )
{
    if ( blk->free_prev_offset != kNoBlock )
    {
        BlockHeader* prev_free      = block_at( base, blk->free_prev_offset );
        prev_free->free_next_offset = blk->free_next_offset;
    }
    else
    {
        // blk был головой списка
        hdr->first_free_offset = blk->free_next_offset;
    }

    if ( blk->free_next_offset != kNoBlock )
    {
        BlockHeader* next_free      = block_at( base, blk->free_next_offset );
        next_free->free_prev_offset = blk->free_prev_offset;
    }

    blk->free_prev_offset = kNoBlock;
    blk->free_next_offset = kNoBlock;
}

} // namespace detail

// ─── Персистный типизированный указатель ──────────────────────────────────────

/**
 * @brief Персистный типизированный указатель.
 *
 * Хранит смещение (offset) от начала управляемой области менеджера памяти
 * вместо абсолютного адреса. Это обеспечивает корректную работу после
 * сохранения и загрузки образа памяти по другому базовому адресу.
 *
 * Требования:
 *   - sizeof(pptr<T>) == sizeof(void*) — размер равен размеру обычного указателя
 *   - Нулевое смещение (0) обозначает нулевой указатель (null)
 *   - pptr<T> может находиться как в обычной памяти, так и в персистной области
 *   - Разыменование использует синглтон PersistMemoryManager::instance()
 *     (не требует явной передачи указателя на менеджер)
 *
 * @tparam T Тип объекта, на который указывает персистный указатель.
 */
template <class T> class pptr
{
    /// Смещение объекта от начала управляемой области (0 = нулевой указатель).
    /// Размер поля == sizeof(void*) — выполняется требование sizeof(pptr<T>) == sizeof(void*).
    std::ptrdiff_t _offset;

  public:
    /// Конструктор по умолчанию — нулевой указатель.
    inline pptr() noexcept : _offset( 0 ) {}

    /// Конструктор из смещения (используется менеджером памяти).
    inline explicit pptr( std::ptrdiff_t offset ) noexcept : _offset( offset ) {}

    /// Конструктор копирования.
    inline pptr( const pptr<T>& ) noexcept = default;

    /// Оператор присваивания.
    inline pptr<T>& operator=( const pptr<T>& ) noexcept = default;

    /// Деструктор — не освобождает ресурсы.
    inline ~pptr() noexcept = default;

    // -----------------------------------------------------------------------
    // Проверка на нулевой указатель
    // -----------------------------------------------------------------------

    /// Возвращает true, если указатель нулевой.
    inline bool is_null() const noexcept { return _offset == 0; }

    /// Явная проверка на не-нулевой указатель (для использования в if).
    inline explicit operator bool() const noexcept { return _offset != 0; }

    // -----------------------------------------------------------------------
    // Получение смещения
    // -----------------------------------------------------------------------

    /// Возвращает хранимое смещение от базы менеджера памяти.
    inline std::ptrdiff_t offset() const noexcept { return _offset; }

    // -----------------------------------------------------------------------
    // Разыменование через синглтон (не требует явного указателя на менеджер)
    // -----------------------------------------------------------------------

    /**
     * @brief Разыменовать — получить указатель на объект T.
     *
     * Использует синглтон PersistMemoryManager::instance() автоматически.
     * @return Указатель на объект или nullptr, если указатель нулевой или нет экземпляра.
     */
    inline T* get() const noexcept;

    /**
     * @brief Оператор разыменования.
     * @return Ссылка на объект T.
     */
    inline T& operator*() const noexcept { return *get(); }

    /**
     * @brief Оператор доступа к членам.
     * @return Указатель на объект T.
     */
    inline T* operator->() const noexcept { return get(); }

    /**
     * @brief Доступ к элементу массива по индексу.
     * @param index Индекс элемента.
     * @return Указатель на элемент с заданным индексом.
     */
    inline T* get_at( std::size_t index ) const noexcept;

    // -----------------------------------------------------------------------
    // Разыменование с явным менеджером (для совместимости и save/load сценариев)
    // -----------------------------------------------------------------------

    /**
     * @brief Разыменовать — получить указатель на объект T.
     * @param mgr Менеджер памяти, в котором был выделен объект.
     * @return Указатель на объект или nullptr, если указатель нулевой.
     */
    inline T* resolve( PersistMemoryManager* mgr ) const noexcept;

    /**
     * @brief Разыменовать — получить константный указатель на объект T.
     * @param mgr Менеджер памяти, в котором был выделен объект.
     * @return Константный указатель на объект или nullptr, если указатель нулевой.
     */
    inline const T* resolve( const PersistMemoryManager* mgr ) const noexcept;

    /**
     * @brief Доступ к элементу массива по индексу.
     * @param mgr   Менеджер памяти.
     * @param index Индекс элемента.
     * @return Указатель на элемент с заданным индексом.
     */
    inline T* resolve_at( PersistMemoryManager* mgr, std::size_t index ) const noexcept;

    // -----------------------------------------------------------------------
    // Операторы сравнения
    // -----------------------------------------------------------------------

    inline bool operator==( const pptr<T>& other ) const noexcept { return _offset == other._offset; }
    inline bool operator!=( const pptr<T>& other ) const noexcept { return _offset != other._offset; }
};

// Проверяем требование: sizeof(pptr<T>) == sizeof(void*)
static_assert( sizeof( pptr<int> ) == sizeof( void* ), "sizeof(pptr<T>) должен быть равен sizeof(void*)" );
static_assert( sizeof( pptr<double> ) == sizeof( void* ), "sizeof(pptr<T>) должен быть равен sizeof(void*)" );

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
 * @brief Загрузить образ менеджера из файла в существующий буфер.
 *
 * Читает файл, записанный методом save(), в буфер @p memory,
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
PersistMemoryManager* load_from_file( const char* filename, void* memory, std::size_t size );

/**
 * @brief Менеджер персистентной памяти (синглтон).
 *
 * Управляет областью памяти, переданной при создании. Все метаданные
 * хранятся внутри этой области, что позволяет сохранять и загружать
 * образ памяти из файла.
 *
 * Одновременно может существовать только один экземпляр менеджера.
 * Доступ к нему осуществляется через статический метод instance().
 *
 * При нехватке памяти менеджер автоматически расширяет управляемую
 * область на 25%, копирует данные и освобождает старый буфер.
 *
 * Предусловие для create(): размер >= kMinMemorySize.
 * Постусловие для create(): возвращаемый указатель != nullptr.
 */
class PersistMemoryManager
{
  public:
    // ─── Синглтон ─────────────────────────────────────────────────────────────

    /// @brief Получить указатель на текущий экземпляр менеджера (синглтон). nullptr если не создан.
    static PersistMemoryManager* instance() noexcept { return s_instance; }

    // ─── Инициализация ────────────────────────────────────────────────────────

    /**
     * @brief Создать новый менеджер памяти в переданной области.
     * @param memory Начало управляемой области (не nullptr, size >= kMinMemorySize).
     * @param size   Размер области в байтах.
     * @return Указатель на менеджер или nullptr при ошибке.
     */
    static PersistMemoryManager* create( void* memory, std::size_t size )
    {
        std::lock_guard<std::recursive_mutex> lock( s_mutex );
        if ( memory == nullptr || size < kMinMemorySize )
        {
            return nullptr;
        }

        std::uint8_t* base = static_cast<std::uint8_t*>( memory );

        detail::ManagerHeader* hdr = reinterpret_cast<detail::ManagerHeader*>( base );
        std::memset( hdr, 0, sizeof( detail::ManagerHeader ) );
        hdr->magic              = kMagic;
        hdr->total_size         = size;
        hdr->used_size          = sizeof( detail::ManagerHeader );
        hdr->first_block_offset = detail::kNoBlock;
        hdr->first_free_offset  = detail::kNoBlock;
        hdr->owns_memory        = true; ///< destroy() освободит буфер

        std::size_t    hdr_end = detail::align_up( sizeof( detail::ManagerHeader ), kDefaultAlignment );
        std::ptrdiff_t blk_off = static_cast<std::ptrdiff_t>( hdr_end );

        if ( static_cast<std::size_t>( blk_off ) + sizeof( detail::BlockHeader ) + kMinBlockSize > size )
        {
            return nullptr;
        }

        detail::BlockHeader* blk = detail::block_at( base, blk_off );
        blk->magic               = detail::kBlockMagic;
        blk->prev_offset         = detail::kNoBlock;
        blk->next_offset         = detail::kNoBlock;
        blk->total_size          = size - static_cast<std::size_t>( blk_off );
        blk->user_size           = 0;
        blk->alignment           = kDefaultAlignment;
        blk->used                = false;
        blk->free_prev_offset    = detail::kNoBlock;
        blk->free_next_offset    = detail::kNoBlock;
        std::memset( blk->_pad, 0, sizeof( blk->_pad ) );

        hdr->first_block_offset = blk_off;
        hdr->first_free_offset  = blk_off;
        hdr->block_count        = 1;
        hdr->free_count         = 1;
        hdr->used_size          = hdr_end + sizeof( detail::BlockHeader );

        PersistMemoryManager* mgr = reinterpret_cast<PersistMemoryManager*>( base );
        s_instance                = mgr;
        return mgr;
    }

    /**
     * @brief Загрузить менеджер из существующего образа памяти.
     * @param memory Начало области с сохранённым образом. @param size Размер в байтах.
     * @return Указатель на менеджер или nullptr при ошибке.
     */
    static PersistMemoryManager* load( void* memory, std::size_t size )
    {
        std::lock_guard<std::recursive_mutex> lock( s_mutex );
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
        hdr->owns_memory     = true; ///< destroy() освободит буфер
        hdr->prev_total_size = 0;
        hdr->prev_base       = nullptr;
        hdr->prev_owns       = false;
        // Перестраиваем список свободных блоков после загрузки образа
        auto* mgr = reinterpret_cast<PersistMemoryManager*>( base );
        mgr->rebuild_free_list();
        s_instance = mgr;
        return mgr;
    }

    /// @brief Уничтожить синглтон: обнулить магическое число, освободить буферы, сбросить s_instance.
    static void destroy()
    {
        std::lock_guard<std::recursive_mutex> lock( s_mutex );
        if ( s_instance != nullptr )
        {
            detail::ManagerHeader* hdr = s_instance->header();
            hdr->magic                 = 0;
            bool  owns                 = hdr->owns_memory;
            void* prev                 = hdr->prev_base;
            bool  prev_owns            = hdr->prev_owns;
            void* buf                  = s_instance->base_ptr();
            s_instance                 = nullptr;
            if ( prev != nullptr && prev_owns )
                std::free( prev );
            if ( owns )
                std::free( buf );
        }
    }

    // ─── Выделение / освобождение ─────────────────────────────────────────────

    /**
     * @brief Выделить блок памяти (first-fit). При нехватке автоматически расширяет область на 25%.
     * @param user_size Размер данных (байт). @param alignment Выравнивание ([8..4096]).
     * @return Выровненный указатель или nullptr при ошибке.
     */
    void* allocate( std::size_t user_size, std::size_t alignment = kDefaultAlignment )
    {
        std::lock_guard<std::recursive_mutex> lock( s_mutex );
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

        // Поиск первого подходящего блока по списку свободных блоков (O(f) вместо O(n))
        std::ptrdiff_t offset = hdr->first_free_offset;
        while ( offset != detail::kNoBlock )
        {
            detail::BlockHeader* blk = detail::block_at( base, offset );
            if ( blk->total_size >= needed )
            {
                return allocate_from_block( blk, user_size, alignment );
            }
            offset = blk->free_next_offset;
        }

        // Нет подходящего блока — пытаемся расширить память
        if ( !expand( user_size, alignment ) )
        {
            return nullptr; // Расширение не удалось
        }

        // После расширения s_instance указывает на новый буфер (this устарел).
        // Делегируем повторный поиск новому синглтону.
        PersistMemoryManager* new_mgr = s_instance;
        if ( new_mgr == nullptr )
        {
            return nullptr;
        }
        std::uint8_t*          new_base   = new_mgr->base_ptr();
        detail::ManagerHeader* new_hdr    = new_mgr->header();
        std::ptrdiff_t         new_offset = new_hdr->first_free_offset;
        while ( new_offset != detail::kNoBlock )
        {
            detail::BlockHeader* blk = detail::block_at( new_base, new_offset );
            if ( blk->total_size >= needed )
            {
                return new_mgr->allocate_from_block( blk, user_size, alignment );
            }
            new_offset = blk->free_next_offset;
        }

        return nullptr;
    }

    /**
     * @brief Освободить ранее выделенный блок (ptr из allocate()), выполнить coalescing.
     * nullptr игнорируется.
     */
    void deallocate( void* ptr )
    {
        std::lock_guard<std::recursive_mutex> lock( s_mutex );
        if ( ptr == nullptr )
        {
            return;
        }

        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();

        // Транслируем указатель из предыдущего буфера (после expand()) в текущий
        if ( hdr->prev_base != nullptr && hdr->prev_total_size > 0 )
        {
            std::uint8_t* prev = static_cast<std::uint8_t*>( hdr->prev_base );
            std::uint8_t* raw  = static_cast<std::uint8_t*>( ptr );
            if ( raw >= prev && raw < prev + hdr->prev_total_size )
                ptr = base + ( raw - prev );
        }

        detail::BlockHeader* blk = detail::header_from_ptr( base, ptr );
        if ( blk == nullptr || !blk->used )
            return;

        std::size_t freed = blk->user_size;
        blk->used         = false;
        blk->user_size    = 0;

        hdr->alloc_count--;
        hdr->free_count++;
        if ( hdr->used_size >= freed )
        {
            hdr->used_size -= freed;
        }

        // Добавляем блок в список свободных
        detail::free_list_insert( base, hdr, blk );

        // Фаза 2: слияние соседних свободных блоков
        coalesce( blk );
    }

    /// @brief Изменить размер блока. Если new_size <= текущему — возвращает тот же ptr. nullptr — выделяет новый.
    void* reallocate( void* ptr, std::size_t new_size )
    {
        std::lock_guard<std::recursive_mutex> lock( s_mutex );
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

        // Транслируем указатель из предыдущего буфера (после expand()) в текущий
        if ( hdr->prev_base != nullptr && hdr->prev_total_size > 0 )
        {
            std::uint8_t* prev = static_cast<std::uint8_t*>( hdr->prev_base );
            std::uint8_t* raw  = static_cast<std::uint8_t*>( ptr );
            if ( raw >= prev && raw < prev + hdr->prev_total_size )
                ptr = base + ( raw - prev );
        }

        detail::BlockHeader* blk = detail::header_from_ptr( base, ptr );
        if ( blk == nullptr || !blk->used )
            return nullptr;
        if ( new_size <= blk->user_size )
            return ptr;

        void* new_ptr = allocate( new_size, blk->alignment );
        if ( new_ptr == nullptr )
        {
            return nullptr;
        }

        std::memcpy( new_ptr, ptr, blk->user_size );
        deallocate( ptr );
        return new_ptr;
    }

    // ─── Персистные типизированные указатели (pptr<T>) ────────────────────────

    /// @brief Выделить sizeof(T) байт с alignof(T), вернуть pptr<T>. Нулевой при ошибке.
    template <class T> pptr<T> allocate_typed()
    {
        std::size_t alignment = alignof( T ) < kMinAlignment ? kMinAlignment : alignof( T );
        void*       raw       = allocate( sizeof( T ), alignment );
        if ( raw == nullptr )
        {
            return pptr<T>();
        }
        std::ptrdiff_t off = static_cast<std::ptrdiff_t>( static_cast<std::uint8_t*>( raw ) - base_ptr() );
        return pptr<T>( off );
    }

    /// @brief Выделить sizeof(T)*count байт с alignof(T), вернуть pptr<T> на первый элемент.
    template <class T> pptr<T> allocate_typed( std::size_t count )
    {
        if ( count == 0 )
        {
            return pptr<T>();
        }
        std::size_t alignment = alignof( T ) < kMinAlignment ? kMinAlignment : alignof( T );
        void*       raw       = allocate( sizeof( T ) * count, alignment );
        if ( raw == nullptr )
        {
            return pptr<T>();
        }
        std::ptrdiff_t off = static_cast<std::ptrdiff_t>( static_cast<std::uint8_t*>( raw ) - base_ptr() );
        return pptr<T>( off );
    }

    /// @brief Освободить блок памяти по персистному указателю pptr<T>. Нулевой игнорируется.
    template <class T> void deallocate_typed( pptr<T> p )
    {
        if ( p.is_null() )
        {
            return;
        }
        void* raw = base_ptr() + p.offset();
        deallocate( raw );
    }

    /// @brief Получить абсолютный указатель по смещению от базы. nullptr если offset == 0.
    void* offset_to_ptr( std::ptrdiff_t offset ) noexcept
    {
        if ( offset == 0 )
        {
            return nullptr;
        }
        return base_ptr() + offset;
    }

    /// @brief Получить абсолютный константный указатель по смещению (offset==0 → nullptr).
    const void* offset_to_ptr( std::ptrdiff_t offset ) const noexcept
    {
        if ( offset == 0 )
        {
            return nullptr;
        }
        return const_base_ptr() + offset;
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

    // ─── Персистентность ──────────────────────────────────────────────────────

    /**
     * @brief Сохранить образ управляемой памяти в файл.
     *
     * Записывает весь буфер (от начала до total_size байт) в файл побайтово.
     * Поскольку все метаданные используют смещения (offsets) от base_ptr,
     * образ корректно загружается по любому базовому адресу через load_from_file().
     *
     * @param filename Путь к выходному файлу.
     * @return true при успешной записи, false при ошибке ввода/вывода.
     *
     * Предусловие:  filename != nullptr, менеджер валиден (validate() == true).
     * Постусловие: файл содержит точную копию управляемой области памяти.
     */
    bool save( const char* filename ) const
    {
        if ( filename == nullptr )
        {
            return false;
        }
        const detail::ManagerHeader* hdr = header();
        std::FILE*                   f   = std::fopen( filename, "wb" );
        if ( f == nullptr )
        {
            return false;
        }
        const void* data    = const_base_ptr();
        std::size_t written = std::fwrite( data, 1, hdr->total_size, f );
        std::fclose( f );
        return written == hdr->total_size;
    }

    /**
     * @brief Дружественный доступ для get_stats(), get_info() и load_from_file().
     */
    friend MemoryStats           get_stats( const PersistMemoryManager* mgr );
    friend AllocationInfo        get_info( const PersistMemoryManager* mgr, void* ptr );
    friend PersistMemoryManager* load_from_file( const char* filename, void* memory, std::size_t size );

  private:
    /// Единственный экземпляр менеджера (синглтон).
    static PersistMemoryManager* s_instance;
    /// Мьютекс для потокобезопасного доступа к синглтону.
    static std::recursive_mutex s_mutex;

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

    /// @brief Расширить управляемую область на 25% (old_size * 5/4). Обновляет s_instance.
    bool expand( std::size_t user_size, std::size_t alignment )
    {
        detail::ManagerHeader* hdr      = header();
        std::size_t            old_size = hdr->total_size;

        std::size_t new_size = old_size * kGrowNumerator / kGrowDenominator;
        std::size_t needed   = detail::required_block_size( user_size, alignment );
        if ( new_size < old_size + needed )
            new_size = old_size + needed + detail::align_up( sizeof( detail::BlockHeader ), kDefaultAlignment );

        void* new_memory = std::malloc( new_size );
        if ( new_memory == nullptr )
            return false;

        bool old_owns = hdr->owns_memory;
        std::memcpy( new_memory, base_ptr(), old_size );

        detail::ManagerHeader* new_hdr  = reinterpret_cast<detail::ManagerHeader*>( new_memory );
        std::uint8_t*          new_base = static_cast<std::uint8_t*>( new_memory );
        new_hdr->owns_memory            = true;

        std::ptrdiff_t extra_offset = static_cast<std::ptrdiff_t>( old_size );
        std::size_t    extra_size   = new_size - old_size;

        detail::BlockHeader* last_blk   = nullptr;
        std::ptrdiff_t       blk_offset = new_hdr->first_block_offset;
        while ( blk_offset != detail::kNoBlock )
        {
            detail::BlockHeader* blk = detail::block_at( new_base, blk_offset );
            if ( blk->next_offset == detail::kNoBlock )
            {
                last_blk = blk;
            }
            blk_offset = blk->next_offset;
        }

        if ( last_blk != nullptr && !last_blk->used )
        {
            detail::free_list_remove( new_base, new_hdr, last_blk );
            last_blk->total_size += extra_size;
            detail::free_list_insert( new_base, new_hdr, last_blk );
        }
        else
        {
            if ( extra_size < sizeof( detail::BlockHeader ) + kMinBlockSize )
            {
                std::free( new_memory );
                return false;
            }

            detail::BlockHeader* new_blk = detail::block_at( new_base, extra_offset );
            new_blk->magic               = detail::kBlockMagic;
            new_blk->total_size          = extra_size;
            new_blk->user_size           = 0;
            new_blk->alignment           = kDefaultAlignment;
            new_blk->used                = false;
            new_blk->free_prev_offset    = detail::kNoBlock;
            new_blk->free_next_offset    = detail::kNoBlock;
            std::memset( new_blk->_pad, 0, sizeof( new_blk->_pad ) );

            if ( last_blk != nullptr )
            {
                std::ptrdiff_t last_off = detail::block_offset( new_base, last_blk );
                new_blk->prev_offset    = last_off;
                new_blk->next_offset    = detail::kNoBlock;
                last_blk->next_offset   = extra_offset;
            }
            else
            {
                new_blk->prev_offset        = detail::kNoBlock;
                new_blk->next_offset        = detail::kNoBlock;
                new_hdr->first_block_offset = extra_offset;
            }

            new_hdr->block_count++;
            new_hdr->free_count++;
            detail::free_list_insert( new_base, new_hdr, new_blk );
        }

        new_hdr->total_size = new_size;

        // Освобождаем дедушку-буфер; старый буфер сохраняем как prev_base для
        // трансляции указателей в deallocate() — будет освобождён при следующем
        // expand() или destroy().
        if ( hdr->prev_base != nullptr && hdr->prev_owns )
            std::free( hdr->prev_base );
        new_hdr->prev_base       = base_ptr();
        new_hdr->prev_total_size = old_size;
        new_hdr->prev_owns       = old_owns;

        s_instance = reinterpret_cast<PersistMemoryManager*>( new_memory );
        return true;
    }

    /// @brief Перестроить список свободных блоков при загрузке образа (load).
    void rebuild_free_list()
    {
        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();

        hdr->first_free_offset = detail::kNoBlock;

        std::ptrdiff_t offset = hdr->first_block_offset;
        while ( offset != detail::kNoBlock )
        {
            detail::BlockHeader* blk = detail::block_at( base, offset );
            if ( !blk->used )
            {
                blk->free_prev_offset = detail::kNoBlock;
                blk->free_next_offset = detail::kNoBlock;
                detail::free_list_insert( base, hdr, blk );
            }
            else
            {
                blk->free_prev_offset = detail::kNoBlock;
                blk->free_next_offset = detail::kNoBlock;
            }
            offset = blk->next_offset;
        }
    }

    /**
     * @brief Слияние свободного блока @p blk с соседними свободными блоками.
     * Предусловие: blk != nullptr, blk->used == false.
     */
    void coalesce( detail::BlockHeader* blk )
    {
        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();

        if ( blk->next_offset != detail::kNoBlock )
        {
            detail::BlockHeader* next_blk = detail::block_at( base, blk->next_offset );
            if ( !next_blk->used )
            {
                detail::free_list_remove( base, hdr, blk );
                detail::free_list_remove( base, hdr, next_blk );
                blk->total_size += next_blk->total_size;
                blk->next_offset = next_blk->next_offset;
                if ( next_blk->next_offset != detail::kNoBlock )
                {
                    detail::BlockHeader* after_next = detail::block_at( base, next_blk->next_offset );
                    after_next->prev_offset         = detail::block_offset( base, blk );
                }

                next_blk->magic = 0;

                hdr->block_count--;
                hdr->free_count--;

                detail::free_list_insert( base, hdr, blk );
            }
        }

        if ( blk->prev_offset != detail::kNoBlock )
        {
            detail::BlockHeader* prev_blk = detail::block_at( base, blk->prev_offset );
            if ( !prev_blk->used )
            {
                detail::free_list_remove( base, hdr, prev_blk );
                detail::free_list_remove( base, hdr, blk );
                prev_blk->total_size += blk->total_size;
                prev_blk->next_offset = blk->next_offset;
                if ( blk->next_offset != detail::kNoBlock )
                {
                    detail::BlockHeader* next_blk = detail::block_at( base, blk->next_offset );
                    next_blk->prev_offset         = detail::block_offset( base, prev_blk );
                }

                blk->magic = 0;

                hdr->block_count--;
                hdr->free_count--;

                detail::free_list_insert( base, hdr, prev_blk );
            }
        }
    }

    /// @brief Выделить память из конкретного свободного блока; при необходимости разбивает блок на два.
    void* allocate_from_block( detail::BlockHeader* blk, std::size_t user_size, std::size_t alignment )
    {
        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();

        detail::free_list_remove( base, hdr, blk );

        std::size_t min_remainder = sizeof( detail::BlockHeader ) + kMinBlockSize;

        std::size_t needed    = detail::required_block_size( user_size, alignment );
        bool        can_split = ( blk->total_size >= needed + min_remainder );

        if ( can_split )
        {
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
            new_blk->free_prev_offset    = detail::kNoBlock;
            new_blk->free_next_offset    = detail::kNoBlock;
            std::memset( new_blk->_pad, 0, sizeof( new_blk->_pad ) );

            if ( blk->next_offset != detail::kNoBlock )
            {
                detail::BlockHeader* next_blk = detail::block_at( base, blk->next_offset );
                next_blk->prev_offset         = new_blk_off;
            }

            blk->next_offset = new_blk_off;
            blk->total_size  = needed;

            hdr->block_count++;
            hdr->free_count++;
            detail::free_list_insert( base, hdr, new_blk );
        }

        blk->used             = true;
        blk->user_size        = user_size;
        blk->alignment        = alignment;
        blk->free_prev_offset = detail::kNoBlock;
        blk->free_next_offset = detail::kNoBlock;

        hdr->alloc_count++;
        hdr->free_count--;
        hdr->used_size += user_size;

        return detail::user_ptr( blk );
    }
};

// ─── Определение статического члена синглтона ──────────────────────────────────

inline PersistMemoryManager* PersistMemoryManager::s_instance = nullptr;
inline std::recursive_mutex  PersistMemoryManager::s_mutex;

// ─── Реализация методов pptr<T> (после полного определения PersistMemoryManager) ──

/**
 * @brief Разыменовать — получить указатель на объект T через синглтон.
 */
template <class T> inline T* pptr<T>::get() const noexcept
{
    PersistMemoryManager* mgr = PersistMemoryManager::instance();
    if ( mgr == nullptr || _offset == 0 )
    {
        return nullptr;
    }
    return static_cast<T*>( mgr->offset_to_ptr( _offset ) );
}

/**
 * @brief Доступ к элементу массива по индексу через синглтон.
 */
template <class T> inline T* pptr<T>::get_at( std::size_t index ) const noexcept
{
    T* base_elem = get();
    if ( base_elem == nullptr )
    {
        return nullptr;
    }
    return base_elem + index;
}

/**
 * @brief Разыменовать — получить указатель на объект T (явный менеджер).
 */
template <class T> inline T* pptr<T>::resolve( PersistMemoryManager* mgr ) const noexcept
{
    if ( mgr == nullptr || _offset == 0 )
    {
        return nullptr;
    }
    return static_cast<T*>( mgr->offset_to_ptr( _offset ) );
}

/**
 * @brief Разыменовать — получить константный указатель на объект T (явный менеджер).
 */
template <class T> inline const T* pptr<T>::resolve( const PersistMemoryManager* mgr ) const noexcept
{
    if ( mgr == nullptr || _offset == 0 )
    {
        return nullptr;
    }
    return static_cast<const T*>( mgr->offset_to_ptr( _offset ) );
}

/**
 * @brief Доступ к элементу массива по индексу (явный менеджер).
 */
template <class T> inline T* pptr<T>::resolve_at( PersistMemoryManager* mgr, std::size_t index ) const noexcept
{
    T* base_elem = resolve( mgr );
    if ( base_elem == nullptr )
    {
        return nullptr;
    }
    return base_elem + index;
}

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

/**
 * @brief Загрузить образ менеджера из файла (save()) в существующий буфер.
 * @param filename Путь к файлу. @param memory Буфер. @param size Размер буфера.
 * @return Указатель на менеджер или nullptr при ошибке.
 */
inline PersistMemoryManager* load_from_file( const char* filename, void* memory, std::size_t size )
{
    if ( filename == nullptr || memory == nullptr || size < kMinMemorySize )
    {
        return nullptr;
    }

    std::FILE* f = std::fopen( filename, "rb" );
    if ( f == nullptr )
    {
        return nullptr;
    }

    // Определяем размер файла
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
        // Буфер слишком мал для образа
        std::fclose( f );
        return nullptr;
    }

    std::size_t read_bytes = std::fread( memory, 1, file_size, f );
    std::fclose( f );

    if ( read_bytes != file_size )
    {
        return nullptr;
    }

    // Проверяем заголовок и возвращаем менеджер
    return PersistMemoryManager::load( memory, file_size );
}

} // namespace pmm
