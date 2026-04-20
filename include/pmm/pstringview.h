/**
 * @file pmm/pstringview.h
 * @brief pstringview<ManagerT> — персистентная строка только для чтения с интернированием.
 *
 * Реализует хранение строк в персистентном адресном пространстве (ПАП) с гарантией
 * уникальности: одна и та же строка хранится в ПАП ровно один раз.
 *
 * Ключевые особенности:
 *   - Read-only: символьные данные никогда не изменяются после создания.
 *   - Интернирование: одинаковые строки используют одно и то же хранилище.
 *     Два pstringview с одинаковым содержимым указывают на один и тот же блок.
 *   - Оптимизированное хранение: длина и строковые данные хранятся
 *     в одном блоке ПАП вместо двух. Это существенно экономит память и ускоряет
 *     работу pmap<pptr<pstringview>, _Tvalue>.
 *   - Блокировка блоков: блоки pstringview блокируются через lock_block_permanent()
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
 * @see persist_memory_manager.h — PersistMemoryManager (статическая модель)
 * @see pptr.h — pptr<T, ManagerT> (персистентный указатель)
 * @see avl_tree_mixin.h — общие AVL-операции
 * @see tree_node.h — TreeNode<AT> (встроенные AVL-поля каждого блока)
 * @version 0.7
 */

#pragma once

#include "pmm/avl_tree_mixin.h"
#include "pmm/forest_registry.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pmm
{

// Forward declaration
template <typename ManagerT> struct pstringview;

// ─── pstringview ─────────────────────────────────────────────────────────────

/**
 * @brief Персистентная интернированная read-only строка.
 *
 * Хранит длину и строковые данные непосредственно в блоке.
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

    struct forest_domain_descriptor
    {
        using manager_type = ManagerT;
        using index_type   = typename ManagerT::index_type;
        using node_type    = pstringview;
        using node_pptr    = psview_pptr;

        static constexpr const char* name() noexcept { return detail::kSystemDomainSymbols; }

        static index_type root_index() noexcept
        {
            auto* domain = ManagerT::symbol_domain_record_unlocked();
            return ManagerT::forest_domain_root_index_unlocked( domain );
        }

        static index_type* root_index_ptr() noexcept
        {
            auto* domain = ManagerT::symbol_domain_record_unlocked();
            return ManagerT::forest_domain_root_index_ptr_unlocked( domain );
        }

        static node_type* resolve_node( node_pptr p ) noexcept { return ManagerT::template resolve<node_type>( p ); }

        static int compare_key( const char* key, node_pptr cur ) noexcept
        {
            if ( key == nullptr )
                key = "";
            node_type* obj = resolve_node( cur );
            return ( obj != nullptr ) ? std::strcmp( key, obj->c_str() ) : 0;
        }

        static bool less_node( node_pptr lhs, node_pptr rhs ) noexcept
        {
            node_type* lhs_obj = resolve_node( lhs );
            node_type* rhs_obj = resolve_node( rhs );
            return lhs_obj != nullptr && rhs_obj != nullptr && std::strcmp( lhs_obj->c_str(), rhs_obj->c_str() ) < 0;
        }

        static bool validate_node( node_pptr p ) noexcept { return resolve_node( p ) != nullptr; }
    };

    using forest_domain_policy = detail::ForestDomainOps<forest_domain_descriptor>;

    static forest_domain_policy forest_domain_ops() noexcept { return forest_domain_policy{}; }

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
    /// Строка хранится непосредственно в блоке после поля length.
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
     * Выполняет поиск в AVL-дереве по лексикографическому ключу. Если строка найдена
     * возвращает существующий pptr. Если нет — создаёт новый pstringview-блок, блокирует
     * его навечно и вставляет в AVL-дерево.
     *
     * @param s C-строка для интернирования (nullptr обрабатывается как "").
     * @return pptr<pstringview<ManagerT>> — персистентный указатель на pstringview.
     *         Нулевой pptr при ошибке аллокации.
     */
    static psview_pptr intern( const char* s ) noexcept { return _intern( s ); }

    /**
     * @brief Сбросить persistent root словаря (для тестов).
     *
     * Не освобождает сами pstringview-блоки в ПАП, а только очищает root binding
     * системного domain `system/symbols`.
     */
    static void reset() noexcept
    {
        if ( !ManagerT::is_initialized() )
            return;
        typename ManagerT::thread_policy::unique_lock_type lock( ManagerT::_mutex );
        forest_domain_ops().reset_root();
    }

    /// @brief Текущий persistent root словаря интернирования; 0 = пустое дерево.
    static index_type root_index() noexcept
    {
        if ( !ManagerT::is_initialized() )
            return static_cast<index_type>( 0 );
        typename ManagerT::thread_policy::shared_lock_type lock( ManagerT::_mutex );
        return forest_domain_ops().root_index();
    }

    // Public destructor required for stack-temporary construction via pstringview<Mgr>("hello").
    ~pstringview() = default;

  private:
    psview_pptr _interned; ///< pptr, полученный при конструировании через intern

    // ─── Реализация интернирования ────────────────────────────────────────────

    /// Канонический путь интернирования: один helper в менеджере выделяет блок,
    /// инициализирует payload, lock'ит навечно и вставляет в symbol-domain AVL.
    /// Здесь только однократный захват writer-lock'а и делегирование.
    static psview_pptr _intern( const char* s ) noexcept
    {
        if ( !ManagerT::is_initialized() )
            return psview_pptr();
        typename ManagerT::thread_policy::unique_lock_type lock( ManagerT::_mutex );
        return ManagerT::intern_symbol_unlocked( s );
    }
};

} // namespace pmm
