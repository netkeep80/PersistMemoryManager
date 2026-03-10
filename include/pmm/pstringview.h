/**
 * @file pmm/pstringview.h
 * @brief pstringview<ManagerT> — персистентная строка только для чтения с интернированием (Issue #151).
 *
 * Реализует хранение строк в персистентном адресном пространстве (ПАП) с гарантией
 * уникальности: одна и та же строка хранится в ПАП ровно один раз.
 *
 * Ключевые особенности:
 *   - Read-only: символьные данные никогда не изменяются после создания.
 *   - Интернирование: одинаковые строки используют одно и то же хранилище.
 *     Два pstringview с одинаковым содержимым указывают на один chars_idx.
 *   - Блокировка блоков: блоки с символьными данными и блоки pstringview блокируются через
 *     lock_block_permanent() — они не могут быть освобождены через deallocate().
 *   - Словарь: AVL-дерево pstringview-узлов растёт в течение жизни менеджера,
 *     экономя память за счёт дедупликации строковых констант.
 *   - Встроенный AVL: каждый pstringview-блок использует встроенные поля TreeNode
 *     (left_offset, right_offset, parent_offset, avl_height) из Block<AT> в качестве
 *     AVL-ссылок. Это "лес AVL-деревьев" ПАП, встроенный в концепцию менеджера.
 *   - Персистентность: granule-индексы адресно-независимы и корректны
 *     при перезагрузке ПАП по другому базовому адресу.
 *
 * Использование:
 * @code
 *   using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *   Mgr::create(64 * 1024);
 *
 *   // Интернировать строку (найти существующую или создать новую)
 *   Mgr::pptr<Mgr::pstringview> p = Mgr::pstringview("hello");
 *   if (p) {
 *       const char* s = p->c_str();   // "hello"
 *       std::size_t n = p->size();    // 5
 *   }
 *
 *   // Повторное интернирование возвращает тот же pptr
 *   Mgr::pptr<Mgr::pstringview> p2 = Mgr::pstringview("hello");
 *   assert(p == p2);  // одинаковый granule index
 *
 *   Mgr::destroy();
 * @endcode
 *
 * @see persist_memory_manager.h — PersistMemoryManager (статическая модель, Issue #110)
 * @see pptr.h — pptr<T, ManagerT> (персистентный указатель)
 * @see avl_tree_mixin.h — общие AVL-операции (Issue #155)
 * @see tree_node.h — TreeNode<AT> (встроенные AVL-поля каждого блока, Issue #87, #138)
 * @version 0.6 (Issue #162 — дедупликация _avl_find через detail::avl_find())
 */

#pragma once

#include "pmm/avl_tree_mixin.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm
{

// Forward declaration
template <typename ManagerT> struct pstringview;

// ─── pstringview ─────────────────────────────────────────────────────────────

/**
 * @brief Персистентная интернированная read-only строка (Issue #151).
 *
 * Хранит granule-индекс символьного массива (chars_idx) и длину строки.
 * Объекты pstringview живут в ПАП и не могут быть созданы на стеке напрямую.
 *
 * Простой API (рекомендуемый способ):
 * @code
 *   // Конструктор-хелпер: создаёт временный объект, возвращает pptr через implicit conversion
 *   Mgr::pptr<Mgr::pstringview> p = Mgr::pstringview("hello");
 *   Mgr::pptr<Mgr::pstringview> p2 = Mgr::pstringview("hello");
 *   assert(p == p2);  // true — дедупликация
 * @endcode
 *
 * AVL-дерево: каждый pstringview использует встроенные поля TreeNode своего блока
 * (left_offset, right_offset, parent_offset, avl_height) как ссылки AVL-дерева
 * словаря интернирования. Это является частью "леса AVL-деревьев" ПАП.
 *
 * Инварианты:
 *   - chars_idx указывает на null-terminated char[], заблокированный навечно.
 *   - Два pstringview с одинаковым содержимым — это один объект (один granule-индекс).
 *
 * @tparam ManagerT Тип менеджера памяти (PersistMemoryManager<ConfigT, InstanceId>).
 */
template <typename ManagerT> struct pstringview
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using psview_pptr  = typename ManagerT::template pptr<pstringview>;
    using char_pptr    = typename ManagerT::template pptr<char>;

    index_type    chars_idx; ///< Granule-индекс массива char в ПАП; 0 = пустая строка
    std::uint32_t length;    ///< Длина строки (без нулевого терминатора)

    // ─── Простой API: конструктор-хелпер + implicit conversion ───────────────

    /**
     * @brief Конструктор-хелпер для интернирования строки.
     *
     * Создаёт временный объект на стеке, содержащий pptr на интернированный pstringview.
     * Используется через implicit conversion к psview_pptr:
     * @code
     *   Mgr::pptr<Mgr::pstringview> p = Mgr::pstringview("hello");
     * @endcode
     *
     * @param s C-строка для интернирования (nullptr обрабатывается как "").
     */
    explicit pstringview( const char* s ) noexcept : chars_idx( 0 ), length( 0 ) { _interned = _intern( s ); }

    /**
     * @brief Implicit conversion к pptr<pstringview<ManagerT>>.
     *
     * Позволяет использовать выражение Mgr::pstringview("hello")
     * в позиции, где ожидается Mgr::pptr<Mgr::pstringview>.
     */
    operator psview_pptr() const noexcept { return _interned; }

    // ─── Методы доступа ──────────────────────────────────────────────────────

    /// @brief Получить raw C-строку. Действителен, пока менеджер инициализирован.
    const char* c_str() const noexcept
    {
        if ( chars_idx == 0 )
            return "";
        char_pptr   p( chars_idx );
        const char* raw = ManagerT::template resolve<char>( p );
        return ( raw != nullptr ) ? raw : "";
    }

    /// @brief Длина строки (без нулевого терминатора).
    std::size_t size() const noexcept { return static_cast<std::size_t>( length ); }

    /// @brief Проверить, пустая ли строка.
    bool empty() const noexcept { return length == 0; }

    /// @brief Сравнение с C-строкой.
    bool operator==( const char* s ) const noexcept
    {
        if ( s == nullptr )
            return length == 0;
        return std::strcmp( c_str(), s ) == 0;
    }

    /// @brief Равенство двух pstringview.
    ///
    /// Интернирование гарантирует: одинаковые строки → одинаковый chars_idx.
    bool operator==( const pstringview& other ) const noexcept { return chars_idx == other.chars_idx; }

    /// @brief Неравенство с C-строкой.
    bool operator!=( const char* s ) const noexcept { return !( *this == s ); }

    /// @brief Неравенство двух pstringview.
    bool operator!=( const pstringview& other ) const noexcept { return !( *this == other ); }

    /// @brief Упорядочивание pstringview (для использования в pmap).
    bool operator<( const pstringview& other ) const noexcept { return std::strcmp( c_str(), other.c_str() ) < 0; }

    // ─── Статическое управление словарём ─────────────────────────────────────

    /**
     * @brief Интернировать строку s: найти существующий pstringview или создать новый.
     *
     * Выполняет поиск в AVL-дереве по лексикографическому ключу. Если строка найдена —
     * возвращает существующий pptr. Если нет — создаёт новый pstringview-блок, блокирует
     * его навечно и вставляет в AVL-дерево.
     *
     * @param s C-строка для интернирования (nullptr обрабатывается как "").
     * @return pptr<pstringview<ManagerT>> — персистентный указатель на pstringview.
     *         Нулевой pptr при ошибке аллокации.
     */
    static psview_pptr intern( const char* s ) noexcept { return _intern( s ); }

    /**
     * @brief Сбросить синглтон словаря (для тестов).
     *
     * Сбрасывает статическую переменную _root_idx, но не освобождает
     * данные в ПАП (блоки заблокированы навечно).
     */
    static void reset() noexcept { _root_idx = static_cast<index_type>( 0 ); }

    /// @brief Granule-индекс корня AVL-дерева интернирования; 0 = пустое дерево.
    static inline index_type _root_idx = static_cast<index_type>( 0 );

    // Public destructor required for stack-temporary construction via pstringview<Mgr>("hello").
    ~pstringview() = default;

  private:
    psview_pptr _interned; ///< pptr, полученный при конструировании через intern

    // Default constructor for creating objects in PAP (without interning).
    pstringview() noexcept : chars_idx( 0 ), length( 0 ) {}

    // ─── Реализация интернирования ────────────────────────────────────────────

    static psview_pptr _intern( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";

        // Ищем в AVL-дереве.
        psview_pptr found = _avl_find( s );
        if ( !found.is_null() )
            return found;

        // Не найдено — создаём новый объект pstringview.
        auto len = static_cast<std::uint32_t>( std::strlen( s ) );

        // Создаём char[] в ПАП и блокируем навечно.
        index_type new_chars = _create_chars( s, len );
        if ( new_chars == static_cast<index_type>( 0 ) && len > 0 )
            return psview_pptr();

        // Создаём объект pstringview в ПАП.
        psview_pptr new_node = ManagerT::template allocate_typed<pstringview>();
        if ( new_node.is_null() )
            return psview_pptr();

        pstringview* obj = ManagerT::template resolve<pstringview>( new_node );
        if ( obj == nullptr )
            return psview_pptr();
        obj->chars_idx = new_chars;
        obj->length    = len;

        // Инициализируем AVL-поля нового узла (пустые ссылки, высота 1).
        auto& tn = new_node.tree_node();
        tn.set_left( static_cast<index_type>( 0 ) );
        tn.set_right( static_cast<index_type>( 0 ) );
        tn.set_parent( static_cast<index_type>( 0 ) );
        tn.set_height( static_cast<std::int16_t>( 1 ) );

        // Блокируем блок pstringview навечно (Issue #151, Issue #126).
        ManagerT::lock_block_permanent( obj );

        // Вставляем в AVL-дерево.
        _avl_insert( new_node );

        return new_node;
    }

    // ─── Вспомогательные методы ────────────────────────────────────────────────

    /// @brief Создать массив char в ПАП и заблокировать навечно.
    static index_type _create_chars( const char* s, std::uint32_t len ) noexcept
    {
        if ( len == 0 )
        {
            // Пустая строка: выделяем один байт для нулевого терминатора.
            char_pptr arr = ManagerT::template allocate_typed<char>( 1 );
            if ( arr.is_null() )
                return static_cast<index_type>( 0 );
            char* dst = ManagerT::template resolve<char>( arr );
            if ( dst != nullptr )
                dst[0] = '\0';
            if ( dst != nullptr )
                ManagerT::lock_block_permanent( dst );
            return arr.offset();
        }

        char_pptr arr = ManagerT::template allocate_typed<char>( static_cast<std::size_t>( len + 1 ) );
        if ( arr.is_null() )
            return static_cast<index_type>( 0 );
        char* dst = ManagerT::template resolve<char>( arr );
        if ( dst != nullptr )
            std::memcpy( dst, s, static_cast<std::size_t>( len + 1 ) );
        if ( dst != nullptr )
            ManagerT::lock_block_permanent( dst );
        return arr.offset();
    }

    // ─── AVL-дерево (использует встроенные TreeNode-поля каждого pstringview-блока) ─

    /// @brief Найти узел AVL-дерева с заданной строкой. Возвращает null если не найден.
    static psview_pptr _avl_find( const char* s ) noexcept
    {
        return detail::avl_find<psview_pptr>(
            _root_idx,
            [&]( psview_pptr cur ) -> int
            {
                pstringview* obj = ManagerT::template resolve<pstringview>( cur );
                return ( obj != nullptr ) ? std::strcmp( s, obj->c_str() ) : 0;
            },
            []( psview_pptr p ) -> pstringview* { return ManagerT::template resolve<pstringview>( p ); } );
    }

    /// @brief Вставить новый узел в AVL-дерево. Предполагается, что строка ещё не в дереве.
    static void _avl_insert( psview_pptr new_node ) noexcept
    {
        pstringview* new_obj = ManagerT::template resolve<pstringview>( new_node );
        const char*  new_str = ( new_obj != nullptr ) ? new_obj->c_str() : "";
        detail::avl_insert(
            new_node, _root_idx,
            [&]( psview_pptr cur ) -> bool
            {
                pstringview* obj = ManagerT::template resolve<pstringview>( cur );
                return ( obj != nullptr ) && ( std::strcmp( new_str, obj->c_str() ) < 0 );
            },
            []( psview_pptr p ) -> pstringview* { return ManagerT::template resolve<pstringview>( p ); } );
    }
};

// Определение статической переменной _root_idx (C++17 inline).
// Объявлено как static inline в теле структуры — определение не требуется вне класса.

} // namespace pmm
