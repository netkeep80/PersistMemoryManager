/**
 * @file pmm/pstringview.h
 * @brief pstringview<ManagerT> — персистентная строка только для чтения с интернированием (Issue #151, #184).
 *
 * Реализует хранение строк в персистентном адресном пространстве (ПАП) с гарантией
 * уникальности: одна и та же строка хранится в ПАП ровно один раз.
 *
 * Ключевые особенности:
 *   - Read-only: символьные данные никогда не изменяются после создания.
 *   - Интернирование: одинаковые строки используют одно и то же хранилище.
 *     Два pstringview с одинаковым содержимым указывают на один и тот же блок.
 *   - Оптимизированное хранение (Issue #184): длина и строковые данные хранятся
 *     в одном блоке ПАП вместо двух. Это существенно экономит память и ускоряет
 *     работу pmap<pptr<pstringview>, _Tvalue>.
 *   - Блокировка блоков: блоки pstringview блокируются через lock_block_permanent() —
 *     они не могут быть освобождены через deallocate().
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
 * @version 0.7 (Issue #184 — оптимизация: строка хранится в одном блоке вместо двух)
 */

#pragma once

#include "pmm/avl_tree_mixin.h"
#include "pmm/types.h"

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
 * @brief Персистентная интернированная read-only строка (Issue #151, #184).
 *
 * Хранит длину и строковые данные непосредственно в блоке (Issue #184).
 * Оптимизация: вместо двух блоков (pstringview + char[]) используется один блок,
 * содержащий длину и строку. Это существенно экономит память и ускоряет
 * работу pmap<pptr<pstringview>, _Tvalue>.
 *
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
 *   - str[] содержит null-terminated строку, расположенную сразу за полем length.
 *   - Два pstringview с одинаковым содержимым — это один объект (один granule-индекс).
 *
 * @tparam ManagerT Тип менеджера памяти (PersistMemoryManager<ConfigT, InstanceId>).
 */
template <typename ManagerT> struct pstringview
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using psview_pptr  = typename ManagerT::template pptr<pstringview>;

    std::uint32_t length; ///< Длина строки (без нулевого терминатора)
    char          str[1]; ///< Строковые данные (flexible array member pattern)

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
    explicit pstringview( const char* s ) noexcept : length( 0 ), str{ '\0' } { _interned = _intern( s ); }

    /**
     * @brief Implicit conversion к pptr<pstringview<ManagerT>>.
     *
     * Позволяет использовать выражение Mgr::pstringview("hello")
     * в позиции, где ожидается Mgr::pptr<Mgr::pstringview>.
     */
    operator psview_pptr() const noexcept { return _interned; }

    // ─── Методы доступа ──────────────────────────────────────────────────────

    /// @brief Получить raw C-строку. Действителен, пока менеджер инициализирован.
    /// Issue #184: строка хранится непосредственно в блоке после поля length.
    const char* c_str() const noexcept { return str; }

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
    /// Интернирование гарантирует: одинаковые строки → один и тот же блок.
    /// Однако при копировании (например, как ключ в pmap_node) сравниваем по содержимому.
    bool operator==( const pstringview& other ) const noexcept
    {
        // Быстрая проверка: если это один и тот же объект, они равны
        if ( this == &other )
            return true;
        // Сравниваем по длине и содержимому строки
        if ( length != other.length )
            return false;
        return std::strcmp( str, other.str ) == 0;
    }

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

        // Issue #184: выделяем один блок для pstringview + строковых данных.
        // Размер = offsetof(pstringview, str) + len + 1 (null-terminator).
        // Используем offsetof для корректного вычисления без учёта str[1].
        std::size_t alloc_size = offsetof( pstringview, str ) + static_cast<std::size_t>( len ) + 1;

        // Выделяем память через allocate() напрямую, т.к. размер переменный.
        void* raw = ManagerT::allocate( alloc_size );
        if ( raw == nullptr )
            return psview_pptr();

        // Создаём pptr вручную из raw указателя (Issue #188: shared ptr_to_granule_idx).
        std::uint8_t* base = ManagerT::backend().base_ptr();
        psview_pptr   new_node( detail::ptr_to_granule_idx<typename ManagerT::address_traits>( base, raw ) );

        pstringview* obj = static_cast<pstringview*>( raw );
        obj->length      = len;
        // Копируем строку включая null-terminator.
        std::memcpy( obj->str, s, static_cast<std::size_t>( len ) + 1 );

        // Инициализируем AVL-поля нового узла (Issue #188: shared avl_init_node).
        detail::avl_init_node( new_node );

        // Блокируем блок pstringview навечно (Issue #151, Issue #126).
        ManagerT::lock_block_permanent( obj );

        // Вставляем в AVL-дерево.
        _avl_insert( new_node );

        return new_node;
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
