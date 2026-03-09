/**
 * @file pmm/pmap.h
 * @brief pmap<_K,_V,ManagerT> — персистентный словарь на основе AVL-дерева (Issue #153).
 *
 * Реализует шаблонный ассоциативный контейнер в персистентном адресном пространстве (ПАП).
 * Каждый узел словаря — это блок в ПАП, хранящий пару ключ-значение (_K, _V).
 * Узлы используют встроенные поля TreeNode (left_offset, right_offset, parent_offset,
 * avl_height) для организации AVL-дерева, как это делает pstringview (Issue #151).
 *
 * Ключевые особенности:
 *   - Персистентный: гранульные индексы адресно-независимы при перезагрузке ПАП.
 *   - AVL-балансировка: O(log n) для вставки и поиска.
 *   - Встроенный AVL: узлы используют встроенные TreeNode-поля Block<AT> без
 *     дополнительных аллокаций структур дерева.
 *   - Не дублирует ключи: повторная вставка по существующему ключу обновляет значение.
 *   - Блокировка: узлы блокируются навечно через lock_block_permanent() (Issue #126).
 *   - Тип ключа _K должен поддерживать operator< и operator==.
 *
 * Пример использования:
 * @code
 *   using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *   Mgr::create(64 * 1024);
 *
 *   // Создать словарь
 *   using MyMap = Mgr::pmap<int, int>;
 *
 *   MyMap map;
 *   map.insert(42, 100);
 *   map.insert(10, 200);
 *
 *   auto p = map.find(42);
 *   if (!p.is_null()) {
 *       int val = p->value;  // 100
 *   }
 *
 *   map.insert(42, 300);  // обновит значение
 *
 *   Mgr::destroy();
 * @endcode
 *
 * Пример с pstringview:
 * @code
 *   using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *   Mgr::create(64 * 1024);
 *
 *   using StrIntMap = Mgr::pmap<Mgr::pstringview, int>;
 *   StrIntMap dict;
 *   auto key = static_cast<Mgr::pptr<Mgr::pstringview>>(Mgr::pstringview("hello"));
 *   dict.insert(*key.resolve(), 42);
 *
 *   Mgr::destroy();
 * @endcode
 *
 * @see pstringview.h — аналогичный тип с AVL-деревом (Issue #151)
 * @see pptr.h — pptr<T, ManagerT> (персистентный указатель)
 * @see tree_node.h — TreeNode<AT> (встроенные AVL-поля каждого блока)
 * @version 0.1 (Issue #153 — реализация pmap<_K,_V>)
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

// Forward declaration
template <typename _K, typename _V, typename ManagerT> struct pmap;

// ─── pmap_node ────────────────────────────────────────────────────────────────

/**
 * @brief Узел pmap — хранит пару ключ-значение в ПАП (Issue #153).
 *
 * Каждый узел является отдельным блоком в ПАП. Встроенные поля TreeNode
 * (left_offset, right_offset, parent_offset, avl_height) используются для
 * организации AVL-дерева словаря.
 *
 * @tparam _K  Тип ключа. Должен поддерживать operator< и operator==.
 * @tparam _V  Тип значения.
 */
template <typename _K, typename _V> struct pmap_node
{
    _K key;   ///< Ключ узла
    _V value; ///< Значение узла
};

// ─── pmap ─────────────────────────────────────────────────────────────────────

/**
 * @brief Персистентный ассоциативный контейнер (словарь) на основе AVL-дерева (Issue #153).
 *
 * Объект pmap сам по себе не хранится в ПАП — он является хелпером на стеке,
 * содержащим гранульный индекс корня AVL-дерева. Узлы словаря (pmap_node) хранятся
 * в ПАП и используют встроенные TreeNode-поля для организации AVL-дерева.
 *
 * Особенности:
 *   - Вставка/поиск за O(log n).
 *   - Повторная вставка по существующему ключу обновляет значение.
 *   - Узлы блокируются через lock_block_permanent() (Issue #126).
 *
 * @tparam _K       Тип ключа. Должен поддерживать operator< и operator==.
 * @tparam _V       Тип значения.
 * @tparam ManagerT Тип менеджера памяти (PersistMemoryManager<ConfigT, InstanceId>).
 */
template <typename _K, typename _V, typename ManagerT> struct pmap
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using node_type    = pmap_node<_K, _V>;
    using node_pptr    = typename ManagerT::template pptr<node_type>;

    /// @brief Гранульный индекс корня AVL-дерева; 0 = пустое дерево.
    index_type _root_idx;

    // ─── Конструктор ──────────────────────────────────────────────────────────

    /// @brief Создать пустой словарь.
    pmap() noexcept : _root_idx( static_cast<index_type>( 0 ) ) {}

    // ─── Методы доступа ───────────────────────────────────────────────────────

    /// @brief Проверить, пуст ли словарь.
    bool empty() const noexcept { return _root_idx == static_cast<index_type>( 0 ); }

    // ─── Операции со словарём ─────────────────────────────────────────────────

    /**
     * @brief Вставить или обновить пару ключ-значение.
     *
     * Если ключ уже существует в словаре — обновляет значение.
     * Если ключ новый — создаёт новый узел в ПАП, блокирует его навечно и
     * вставляет в AVL-дерево.
     *
     * @param key   Ключ для вставки.
     * @param val   Значение для вставки.
     * @return pptr на узел с данным ключом. Нулевой pptr при ошибке аллокации.
     */
    node_pptr insert( const _K& key, const _V& val ) noexcept
    {
        // Ищем существующий узел.
        node_pptr existing = _avl_find( key );
        if ( !existing.is_null() )
        {
            // Ключ найден — обновляем значение.
            node_type* obj = ManagerT::template resolve<node_type>( existing );
            if ( obj != nullptr )
                obj->value = val;
            return existing;
        }

        // Создаём новый узел в ПАП.
        node_pptr new_node = ManagerT::template allocate_typed<node_type>();
        if ( new_node.is_null() )
            return node_pptr();

        node_type* obj = ManagerT::template resolve<node_type>( new_node );
        if ( obj == nullptr )
            return node_pptr();

        obj->key   = key;
        obj->value = val;

        // Инициализируем AVL-поля нового узла.
        auto& tn = new_node.tree_node();
        tn.set_left( static_cast<index_type>( 0 ) );
        tn.set_right( static_cast<index_type>( 0 ) );
        tn.set_parent( static_cast<index_type>( 0 ) );
        tn.set_height( static_cast<std::int16_t>( 1 ) );

        // Блокируем блок навечно (Issue #126).
        ManagerT::lock_block_permanent( obj );

        // Вставляем в AVL-дерево.
        _avl_insert( new_node );

        return new_node;
    }

    /**
     * @brief Найти узел по ключу.
     *
     * @param key Ключ для поиска.
     * @return pptr на найденный узел, или нулевой pptr если не найден.
     */
    node_pptr find( const _K& key ) const noexcept { return _avl_find( key ); }

    /**
     * @brief Проверить, содержит ли словарь заданный ключ.
     *
     * @param key Ключ для проверки.
     * @return true если ключ найден.
     */
    bool contains( const _K& key ) const noexcept { return !_avl_find( key ).is_null(); }

    /**
     * @brief Сбросить словарь (для тестов).
     *
     * Сбрасывает _root_idx, но не освобождает данные в ПАП (блоки заблокированы).
     */
    void reset() noexcept { _root_idx = static_cast<index_type>( 0 ); }

    // ─── AVL-дерево (использует встроенные TreeNode-поля каждого узла) ────────

  private:
    /// @brief Получить высоту узла (0 если null).
    static std::int16_t _height( node_pptr p ) noexcept
    {
        if ( p.is_null() )
            return 0;
        return p.get_tree_height();
    }

    /// @brief Обновить высоту узла по высотам его потомков.
    void _update_height( node_pptr p ) noexcept
    {
        if ( p.is_null() )
            return;
        std::int16_t lh = _height( node_pptr( p.get_tree_left().offset() ) );
        std::int16_t rh = _height( node_pptr( p.get_tree_right().offset() ) );
        std::int16_t h  = static_cast<std::int16_t>( 1 + ( lh > rh ? lh : rh ) );
        p.set_tree_height( h );
    }

    /// @brief Фактор баланса: height(left) - height(right).
    static std::int16_t _balance_factor( node_pptr p ) noexcept
    {
        if ( p.is_null() )
            return 0;
        std::int16_t lh = _height( node_pptr( p.get_tree_left().offset() ) );
        std::int16_t rh = _height( node_pptr( p.get_tree_right().offset() ) );
        return static_cast<std::int16_t>( lh - rh );
    }

    /// @brief Обновить ссылку child у parent (или корень дерева если parent == null).
    void _set_child( node_pptr parent, node_pptr old_child, node_pptr new_child ) noexcept
    {
        if ( parent.is_null() )
        {
            _root_idx = new_child.offset();
            return;
        }
        node_pptr left_of_parent( parent.get_tree_left().offset() );
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
    node_pptr _rotate_right( node_pptr y ) noexcept
    {
        node_pptr x     = node_pptr( y.get_tree_left().offset() );
        node_pptr b     = node_pptr( x.get_tree_right().offset() );
        node_pptr y_par = node_pptr( y.get_tree_parent().offset() );

        x.set_tree_right( y );
        y.set_tree_parent( x );

        y.set_tree_left( b );
        if ( !b.is_null() )
            b.set_tree_parent( y );

        x.set_tree_parent( y_par );

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
    node_pptr _rotate_left( node_pptr x ) noexcept
    {
        node_pptr y     = node_pptr( x.get_tree_right().offset() );
        node_pptr b     = node_pptr( y.get_tree_left().offset() );
        node_pptr x_par = node_pptr( x.get_tree_parent().offset() );

        y.set_tree_left( x );
        x.set_tree_parent( y );

        x.set_tree_right( b );
        if ( !b.is_null() )
            b.set_tree_parent( x );

        y.set_tree_parent( x_par );

        _set_child( x_par, x, y );

        _update_height( x );
        _update_height( y );
        return y;
    }

    /// @brief Ребалансировка начиная с узла p вверх до корня.
    void _rebalance_up( node_pptr p ) noexcept
    {
        while ( !p.is_null() )
        {
            _update_height( p );
            std::int16_t bf = _balance_factor( p );
            if ( bf > 1 )
            {
                node_pptr left( p.get_tree_left().offset() );
                if ( _balance_factor( left ) < 0 )
                {
                    _rotate_left( left );
                }
                p = _rotate_right( p );
            }
            else if ( bf < -1 )
            {
                node_pptr right( p.get_tree_right().offset() );
                if ( _balance_factor( right ) > 0 )
                {
                    _rotate_right( right );
                }
                p = _rotate_left( p );
            }
            p = node_pptr( p.get_tree_parent().offset() );
        }
    }

    /// @brief Найти узел AVL-дерева с заданным ключом. Возвращает null если не найден.
    node_pptr _avl_find( const _K& key ) const noexcept
    {
        node_pptr cur( _root_idx );
        while ( !cur.is_null() )
        {
            node_type* obj = ManagerT::template resolve<node_type>( cur );
            if ( obj == nullptr )
                break;
            if ( key == obj->key )
                return cur;
            else if ( key < obj->key )
                cur = node_pptr( cur.get_tree_left().offset() );
            else
                cur = node_pptr( cur.get_tree_right().offset() );
        }
        return node_pptr(); // null
    }

    /// @brief Вставить новый узел в AVL-дерево. Предполагается, что ключ ещё не в дереве.
    void _avl_insert( node_pptr new_node ) noexcept
    {
        if ( new_node.is_null() )
            return;

        node_type* new_obj = ManagerT::template resolve<node_type>( new_node );
        if ( new_obj == nullptr )
            return;

        if ( _root_idx == static_cast<index_type>( 0 ) )
        {
            // Дерево пустое — новый узел становится корнем.
            new_node.set_tree_left( node_pptr() );
            new_node.set_tree_right( node_pptr() );
            new_node.set_tree_parent( node_pptr() );
            new_node.set_tree_height( static_cast<std::int16_t>( 1 ) );
            _root_idx = new_node.offset();
            return;
        }

        // Ищем место для вставки.
        node_pptr cur( _root_idx );
        node_pptr parent;
        bool      go_left = false;

        while ( !cur.is_null() )
        {
            node_type* obj = ManagerT::template resolve<node_type>( cur );
            if ( obj == nullptr )
                break;
            parent  = cur;
            go_left = ( new_obj->key < obj->key );
            if ( go_left )
                cur = node_pptr( cur.get_tree_left().offset() );
            else
                cur = node_pptr( cur.get_tree_right().offset() );
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

} // namespace pmm
