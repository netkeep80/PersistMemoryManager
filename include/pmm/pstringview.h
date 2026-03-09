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
 * @see tree_node.h — TreeNode<AT> (встроенные AVL-поля каждого блока, Issue #87, #138)
 * @version 0.4 (Issue #151 — краткий API: Mgr::pstringview("hello"))
 */

#pragma once

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

    /// @brief Получить высоту узла (0 если null).
    static std::int16_t _height( psview_pptr p ) noexcept
    {
        if ( p.is_null() )
            return 0;
        return p.get_tree_height();
    }

    /// @brief Обновить высоту узла по высотам его потомков.
    static void _update_height( psview_pptr p ) noexcept
    {
        if ( p.is_null() )
            return;
        std::int16_t lh = _height( psview_pptr( p.get_tree_left().offset() ) );
        std::int16_t rh = _height( psview_pptr( p.get_tree_right().offset() ) );
        std::int16_t h  = static_cast<std::int16_t>( 1 + ( lh > rh ? lh : rh ) );
        p.set_tree_height( h );
    }

    /// @brief Фактор баланса: height(left) - height(right).
    static std::int16_t _balance_factor( psview_pptr p ) noexcept
    {
        if ( p.is_null() )
            return 0;
        std::int16_t lh = _height( psview_pptr( p.get_tree_left().offset() ) );
        std::int16_t rh = _height( psview_pptr( p.get_tree_right().offset() ) );
        return static_cast<std::int16_t>( lh - rh );
    }

    /// @brief Обновить ссылку child у parent (или корень дерева если parent == null).
    static void _set_child( psview_pptr parent, psview_pptr old_child, psview_pptr new_child ) noexcept
    {
        if ( parent.is_null() )
        {
            _root_idx = new_child.offset();
            return;
        }
        psview_pptr left_of_parent( parent.get_tree_left().offset() );
        if ( left_of_parent == old_child )
            parent.set_tree_left( new_child );
        else
            parent.set_tree_right( new_child );
    }

    /**
     * @brief Правый поворот вокруг y; возвращает новый корень поддерева (x).
     *
     *     y            x
     *    / \          / \
     *   x   C  -->  A    y
     *  / \               / \
     * A   B             B   C
     */
    static psview_pptr _rotate_right( psview_pptr y ) noexcept
    {
        psview_pptr x     = psview_pptr( y.get_tree_left().offset() );
        psview_pptr b     = psview_pptr( x.get_tree_right().offset() );
        psview_pptr y_par = psview_pptr( y.get_tree_parent().offset() );

        // x.right = y; y.parent = x
        x.set_tree_right( y );
        y.set_tree_parent( x );

        // y.left = B; B.parent = y (если B не null)
        y.set_tree_left( b );
        if ( !b.is_null() )
            b.set_tree_parent( y );

        // x.parent = y_par
        x.set_tree_parent( y_par );

        // Обновить ссылку у родителя
        _set_child( y_par, y, x );

        _update_height( y );
        _update_height( x );
        return x;
    }

    /**
     * @brief Левый поворот вокруг x; возвращает новый корень поддерева (y).
     *
     *   x               y
     *  / \             / \
     * A   y   -->    x    C
     *    / \        / \
     *   B   C      A   B
     */
    static psview_pptr _rotate_left( psview_pptr x ) noexcept
    {
        psview_pptr y     = psview_pptr( x.get_tree_right().offset() );
        psview_pptr b     = psview_pptr( y.get_tree_left().offset() );
        psview_pptr x_par = psview_pptr( x.get_tree_parent().offset() );

        // y.left = x; x.parent = y
        y.set_tree_left( x );
        x.set_tree_parent( y );

        // x.right = B; B.parent = x (если B не null)
        x.set_tree_right( b );
        if ( !b.is_null() )
            b.set_tree_parent( x );

        // y.parent = x_par
        y.set_tree_parent( x_par );

        // Обновить ссылку у родителя
        _set_child( x_par, x, y );

        _update_height( x );
        _update_height( y );
        return y;
    }

    /// @brief Ребалансировка начиная с узла p вверх до корня.
    static void _rebalance_up( psview_pptr p ) noexcept
    {
        while ( !p.is_null() )
        {
            _update_height( p );
            std::int16_t bf = _balance_factor( p );
            if ( bf > 1 )
            {
                // Левое поддерево перевешивает.
                psview_pptr left( p.get_tree_left().offset() );
                if ( _balance_factor( left ) < 0 )
                {
                    // LR-случай: сначала левый поворот левого потомка.
                    _rotate_left( left );
                }
                p = _rotate_right( p );
            }
            else if ( bf < -1 )
            {
                // Правое поддерево перевешивает.
                psview_pptr right( p.get_tree_right().offset() );
                if ( _balance_factor( right ) > 0 )
                {
                    // RL-случай: сначала правый поворот правого потомка.
                    _rotate_right( right );
                }
                p = _rotate_left( p );
            }
            // Переходим к родителю.
            p = psview_pptr( p.get_tree_parent().offset() );
        }
    }

    /// @brief Найти узел AVL-дерева с заданной строкой. Возвращает null если не найден.
    static psview_pptr _avl_find( const char* s ) noexcept
    {
        psview_pptr cur( _root_idx );
        while ( !cur.is_null() )
        {
            pstringview* obj = ManagerT::template resolve<pstringview>( cur );
            if ( obj == nullptr )
                break;
            int cmp = std::strcmp( s, obj->c_str() );
            if ( cmp == 0 )
                return cur;
            else if ( cmp < 0 )
                cur = psview_pptr( cur.get_tree_left().offset() );
            else
                cur = psview_pptr( cur.get_tree_right().offset() );
        }
        return psview_pptr(); // null
    }

    /// @brief Вставить новый узел в AVL-дерево. Предполагается, что строка ещё не в дереве.
    static void _avl_insert( psview_pptr new_node ) noexcept
    {
        if ( new_node.is_null() )
            return;

        pstringview* new_obj = ManagerT::template resolve<pstringview>( new_node );
        if ( new_obj == nullptr )
            return;
        const char* new_str = new_obj->c_str();

        if ( _root_idx == static_cast<index_type>( 0 ) )
        {
            // Дерево пустое — новый узел становится корнем.
            new_node.set_tree_left( psview_pptr() );
            new_node.set_tree_right( psview_pptr() );
            new_node.set_tree_parent( psview_pptr() );
            new_node.set_tree_height( static_cast<std::int16_t>( 1 ) );
            _root_idx = new_node.offset();
            return;
        }

        // Ищем место для вставки.
        psview_pptr cur( _root_idx );
        psview_pptr parent;
        bool        go_left = false;

        while ( !cur.is_null() )
        {
            pstringview* obj = ManagerT::template resolve<pstringview>( cur );
            if ( obj == nullptr )
                break;
            parent  = cur;
            int cmp = std::strcmp( new_str, obj->c_str() );
            go_left = ( cmp < 0 );
            if ( go_left )
                cur = psview_pptr( cur.get_tree_left().offset() );
            else
                cur = psview_pptr( cur.get_tree_right().offset() );
        }

        // Устанавливаем родителя нового узла.
        new_node.set_tree_parent( parent );

        // Прикрепляем новый узел к родителю.
        if ( go_left )
            parent.set_tree_left( new_node );
        else
            parent.set_tree_right( new_node );

        // Ребалансировка вверх от родителя.
        _rebalance_up( parent );
    }
};

// Определение статической переменной _root_idx (C++17 inline).
// Объявлено как static inline в теле структуры — определение не требуется вне класса.

} // namespace pmm
