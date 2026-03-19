/**
 * @file pmm/pstring.h
 * @brief pstring<ManagerT> — мутабельная персистентная строка (Issue #45, Phase 3.1).
 *
 * Реализует мутабельную строку в персистентном адресном пространстве (ПАП).
 * В отличие от pstringview (read-only, interned), pstring поддерживает изменение
 * содержимого через assign(), clear() и append().
 *
 * Ключевые особенности:
 *   - Мутабельная: содержимое строки можно изменять после создания.
 *   - Данные в отдельном блоке: заголовок pstring хранит длину, ёмкость и
 *     гранульный индекс блока данных. При изменении — переаллокация блока данных.
 *   - POD-структура: все поля — примитивные типы (trivially copyable),
 *     что позволяет хранить pstring непосредственно в ПАП.
 *   - Нет SSO: все данные хранятся в ПАП через аллокатор менеджера.
 *   - Персистентность: гранульные индексы адресно-независимы при перезагрузке ПАП.
 *
 * Использование:
 * @code
 *   using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *   Mgr::create(64 * 1024);
 *
 *   // Создать мутабельную строку
 *   Mgr::pptr<Mgr::pstring> p = Mgr::create_typed<Mgr::pstring>();
 *   p->assign("hello");
 *   const char* s = p->c_str();   // "hello"
 *   std::size_t n = p->size();    // 5
 *
 *   // Изменить содержимое
 *   p->assign("world!");
 *   // s = p->c_str();  // "world!"
 *
 *   // Дополнить строку
 *   p->append(" test");
 *   // p->c_str() == "world! test"
 *
 *   // Очистить строку
 *   p->clear();
 *   // p->size() == 0, p->c_str() == ""
 *
 *   // Освободить строку (деаллоцирует блок данных + сам блок)
 *   p->free_data();
 *   Mgr::destroy_typed(p);
 *
 *   Mgr::destroy();
 * @endcode
 *
 * @see pstringview.h — pstringview<ManagerT> (read-only интернированная строка)
 * @see persist_memory_manager.h — PersistMemoryManager (статическая модель)
 * @see pptr.h — pptr<T, ManagerT> (персистентный указатель)
 * @version 0.1 (Issue #45 — Phase 3.1: мутабельная персистентная строка)
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm
{

/**
 * @brief Мутабельная персистентная строка (Issue #45, Phase 3.1).
 *
 * Хранит заголовок (длину, ёмкость, индекс блока данных) в ПАП.
 * Строковые данные хранятся в отдельном блоке, выделенном через менеджер.
 *
 * Объект pstring создаётся в ПАП через create_typed<pstring>() и уничтожается
 * через destroy_typed<pstring>() после вызова free_data().
 *
 * Инварианты:
 *   - Если _data_idx != 0, блок данных содержит null-terminated строку длины _length.
 *   - _capacity >= _length (всегда есть место для null-terminator).
 *   - При _data_idx == 0 строка пустая, c_str() возвращает "".
 *
 * @tparam ManagerT Тип менеджера памяти (PersistMemoryManager<ConfigT, InstanceId>).
 */
template <typename ManagerT> struct pstring
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;

    std::uint32_t _length; ///< Длина строки (без нулевого терминатора)
    std::uint32_t _capacity; ///< Ёмкость буфера данных (без нулевого терминатора)
    index_type _data_idx;    ///< Гранульный индекс блока данных (0 = нет данных)

    // ─── Конструктор / Деструктор ────────────────────────────────────────────

    /// @brief Конструктор по умолчанию — пустая строка.
    pstring() noexcept : _length( 0 ), _capacity( 0 ), _data_idx( static_cast<index_type>( 0 ) ) {}

    /// @brief Деструктор — trivial (данные освобождаются через free_data()).
    ~pstring() noexcept = default;

    // ─── Методы доступа ──────────────────────────────────────────────────────

    /**
     * @brief Получить C-строку.
     *
     * Если строка пуста (нет блока данных), возвращает указатель на статическую
     * пустую строку. Иначе возвращает указатель на данные в ПАП.
     *
     * @return const char* — null-terminated строка.
     */
    const char* c_str() const noexcept
    {
        if ( _data_idx == static_cast<index_type>( 0 ) )
            return "";
        char* data = resolve_data();
        return ( data != nullptr ) ? data : "";
    }

    /// @brief Длина строки (без нулевого терминатора).
    std::size_t size() const noexcept { return static_cast<std::size_t>( _length ); }

    /// @brief Проверить, пустая ли строка.
    bool empty() const noexcept { return _length == 0; }

    /// @brief Доступ к символу по индексу (без проверки границ).
    char operator[]( std::size_t i ) const noexcept
    {
        char* data = resolve_data();
        return ( data != nullptr ) ? data[i] : '\0';
    }

    // ─── Мутирующие операции ─────────────────────────────────────────────────

    /**
     * @brief Присвоить новое содержимое строки.
     *
     * Если текущая ёмкость достаточна — копирует данные на место.
     * Если нет — выделяет новый блок данных, копирует, освобождает старый.
     *
     * @param s C-строка для присвоения (nullptr обрабатывается как "").
     * @return true при успехе, false при ошибке аллокации.
     */
    bool assign( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";
        auto len = static_cast<std::uint32_t>( std::strlen( s ) );
        if ( !ensure_capacity( len ) )
            return false;
        char* data = resolve_data();
        if ( data == nullptr )
            return false;
        std::memcpy( data, s, static_cast<std::size_t>( len ) + 1 );
        _length = len;
        return true;
    }

    /**
     * @brief Дополнить строку содержимым s.
     *
     * @param s C-строка для дополнения (nullptr обрабатывается как "").
     * @return true при успехе, false при ошибке аллокации.
     */
    bool append( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";
        auto add_len = static_cast<std::uint32_t>( std::strlen( s ) );
        if ( add_len == 0 )
            return true;
        std::uint32_t new_len = _length + add_len;
        if ( new_len < _length )
            return false; // overflow
        if ( !ensure_capacity( new_len ) )
            return false;
        char* data = resolve_data();
        if ( data == nullptr )
            return false;
        std::memcpy( data + _length, s, static_cast<std::size_t>( add_len ) + 1 );
        _length = new_len;
        return true;
    }

    /**
     * @brief Очистить строку (установить длину в 0), не освобождая блок данных.
     *
     * Буфер данных остаётся выделенным (ёмкость сохраняется) для потенциального
     * повторного использования. Для полного освобождения используйте free_data().
     */
    void clear() noexcept
    {
        _length = 0;
        if ( _data_idx != static_cast<index_type>( 0 ) )
        {
            char* data = resolve_data();
            if ( data != nullptr )
                data[0] = '\0';
        }
    }

    /**
     * @brief Освободить блок данных строки.
     *
     * Деаллоцирует блок данных через менеджер. После вызова строка пуста.
     * Этот метод ДОЛЖЕН быть вызван перед destroy_typed(pptr) для корректного
     * освобождения всех ресурсов.
     */
    void free_data() noexcept
    {
        if ( _data_idx != static_cast<index_type>( 0 ) )
        {
            std::uint8_t* base = ManagerT::backend().base_ptr();
            void*         raw  = base + static_cast<std::size_t>( _data_idx ) * ManagerT::address_traits::granule_size;
            ManagerT::deallocate( raw );
            _data_idx = static_cast<index_type>( 0 );
        }
        _length   = 0;
        _capacity = 0;
    }

    // ─── Операторы сравнения ─────────────────────────────────────────────────

    /// @brief Сравнение с C-строкой.
    bool operator==( const char* s ) const noexcept
    {
        if ( s == nullptr )
            return _length == 0;
        return std::strcmp( c_str(), s ) == 0;
    }

    /// @brief Неравенство с C-строкой.
    bool operator!=( const char* s ) const noexcept { return !( *this == s ); }

    /// @brief Равенство двух pstring.
    bool operator==( const pstring& other ) const noexcept
    {
        if ( this == &other )
            return true;
        if ( _length != other._length )
            return false;
        if ( _length == 0 )
            return true;
        return std::strcmp( c_str(), other.c_str() ) == 0;
    }

    /// @brief Неравенство двух pstring.
    bool operator!=( const pstring& other ) const noexcept { return !( *this == other ); }

    /// @brief Упорядочивание pstring (лексикографическое).
    bool operator<( const pstring& other ) const noexcept { return std::strcmp( c_str(), other.c_str() ) < 0; }

  private:
    // ─── Внутренние помощники ─────────────────────────────────────────────────

    /// @brief Разрешить гранульный индекс данных в сырой указатель.
    char* resolve_data() const noexcept
    {
        if ( _data_idx == static_cast<index_type>( 0 ) )
            return nullptr;
        std::uint8_t* base = ManagerT::backend().base_ptr();
        return reinterpret_cast<char*>( base + static_cast<std::size_t>( _data_idx ) *
                                                   ManagerT::address_traits::granule_size );
    }

    /**
     * @brief Обеспечить ёмкость буфера данных не менее required символов.
     *
     * Если текущая ёмкость достаточна — ничего не делает.
     * Если нет — выделяет новый блок с удвоенной ёмкостью (amortized O(1)),
     * копирует старые данные, освобождает старый блок.
     *
     * @param required Требуемое количество символов (без null-terminator).
     * @return true при успехе, false при ошибке аллокации.
     */
    bool ensure_capacity( std::uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return true;

        // Новая ёмкость: удвоение текущей или required, что больше.
        // Минимум 16 символов для избежания частых реаллокаций.
        std::uint32_t new_cap = _capacity * 2;
        if ( new_cap < required )
            new_cap = required;
        if ( new_cap < 16 )
            new_cap = 16;

        // Выделяем новый блок данных: new_cap + 1 байт для null-terminator.
        std::size_t alloc_size = static_cast<std::size_t>( new_cap ) + 1;
        void*       new_raw    = ManagerT::allocate( alloc_size );
        if ( new_raw == nullptr )
            return false;

        // Создаём новый индекс.
        std::uint8_t* base        = ManagerT::backend().base_ptr();
        std::size_t   byte_off    = static_cast<std::uint8_t*>( new_raw ) - base;
        index_type    new_dat_idx = static_cast<index_type>( byte_off / ManagerT::address_traits::granule_size );

        // Копируем старые данные.
        if ( _length > 0 && _data_idx != static_cast<index_type>( 0 ) )
        {
            char* old_data = resolve_data();
            if ( old_data != nullptr )
                std::memcpy( new_raw, old_data, static_cast<std::size_t>( _length ) + 1 );
        }
        else
        {
            // Инициализируем пустую строку.
            static_cast<char*>( new_raw )[0] = '\0';
        }

        // Освобождаем старый блок.
        if ( _data_idx != static_cast<index_type>( 0 ) )
        {
            void* old_raw = base + static_cast<std::size_t>( _data_idx ) * ManagerT::address_traits::granule_size;
            ManagerT::deallocate( old_raw );
        }

        _data_idx = new_dat_idx;
        _capacity = new_cap;
        return true;
    }
};

// pstring<ManagerT> — POD-структура, хранящая длину, ёмкость и индекс блока данных.
// Trivially copyable для прямой сериализации в ПАП.

} // namespace pmm
