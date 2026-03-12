/**
 * @file pmm/pvector.h
 * @brief pvector<T,ManagerT> — персистентный вектор для менеджера ПАП (Issue #186).
 *
 * Реализует шаблонный последовательный контейнер в персистентном адресном пространстве (ПАП).
 * Каждый элемент вектора — это блок в ПАП, хранящий значение типа T.
 * Вектор использует встроенные поля TreeNode для построения AVL-дерева, индексированного
 * по позиции элемента в последовательности (order-statistic tree).
 *
 * Ключевые особенности:
 *   - Персистентный: гранульные индексы адресно-независимы при перезагрузке ПАП.
 *   - O(log n) для push_back, pop_back и доступа по индексу at(i).
 *   - O(1) для size(), front() и back().
 *   - Узлы НЕ блокируются навечно — они могут быть освобождены после удаления из вектора.
 *
 * Реализация:
 *   Каждый узел хранит в поле `weight` общий размер своего поддерева (сам узел + потомки).
 *   Поля `left_offset`, `right_offset`, `parent_offset`, `avl_height` — стандартные
 *   поля AVL-дерева. Это позволяет за O(log n) находить k-й по порядку элемент.
 *
 * Пример использования:
 * @code
 *   using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *   Mgr::create(64 * 1024);
 *
 *   // Создать вектор
 *   using MyVec = Mgr::pvector<int>;
 *
 *   MyVec vec;
 *   vec.push_back(10);
 *   vec.push_back(20);
 *   vec.push_back(30);
 *
 *   auto p = vec.at(1);  // O(log n) доступ по индексу
 *   if (!p.is_null()) {
 *       int val = p->value;  // 20
 *   }
 *
 *   std::size_t n = vec.size();  // O(1): 3
 *
 *   Mgr::destroy();
 * @endcode
 *
 * @see pmap.h — аналогичный персистентный контейнер (Issue #153)
 * @see avl_tree_mixin.h — общие AVL-операции (Issue #155)
 * @see pptr.h — pptr<T, ManagerT> (персистентный указатель)
 * @see tree_node.h — TreeNode<AT> (встроенные поля каждого блока)
 * @version 0.2 (Issue #186 — O(log n) at() via order-statistic AVL tree)
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

// Forward declaration
template <typename T, typename ManagerT> struct pvector;

// ─── pvector_node ─────────────────────────────────────────────────────────────

/**
 * @brief Узел pvector — хранит значение в ПАП (Issue #186).
 *
 * Каждый узел является отдельным блоком в ПАП. Встроенные поля TreeNode
 * используются для организации AVL-дерева, индексированного по позиции:
 *   - weight       → размер поддерева (сам узел + все потомки)
 *   - left_offset  → левый потомок (меньшие индексы)
 *   - right_offset → правый потомок (большие индексы)
 *   - parent_offset→ родительский узел
 *   - avl_height   → высота поддерева для балансировки
 *
 * @tparam T  Тип хранимого значения.
 */
template <typename T> struct pvector_node
{
    T value; ///< Значение узла
};

// ─── pvector ──────────────────────────────────────────────────────────────────

/**
 * @brief Персистентный последовательный контейнер (вектор) для ПАП (Issue #186).
 *
 * Объект pvector сам по себе не хранится в ПАП — он является хелпером на стеке,
 * содержащим гранульный индекс корня AVL-дерева узлов элементов.
 * Элементы (pvector_node) хранятся в ПАП и используют встроенные TreeNode-поля
 * для организации AVL-дерева, индексированного по позиции (order-statistic tree).
 *
 * Поле `weight` каждого узла хранит размер его поддерева (сам + все потомки),
 * что позволяет находить k-й элемент за O(log n) обходом дерева.
 *
 * Особенности:
 *   - push_back() за O(log n) — вставка в крайнюю правую позицию.
 *   - at(i) за O(log n) — поиск по позиции через размеры поддеревьев.
 *   - size() за O(1) — хранится в weight корня.
 *   - front()/back() за O(log n) — обход до крайнего левого/правого узла.
 *   - pop_back() за O(log n) — удаление крайнего правого узла.
 *   - Узлы НЕ блокируются навечно: можно освобождать через clear().
 *
 * @tparam T        Тип элемента.
 * @tparam ManagerT Тип менеджера памяти (PersistMemoryManager<ConfigT, InstanceId>).
 */
template <typename T, typename ManagerT> struct pvector
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using node_type    = pvector_node<T>;
    using node_pptr    = typename ManagerT::template pptr<node_type>;

    /// @brief Sentinel value for "no node" in TreeNode fields.
    static constexpr index_type no_block = ManagerT::address_traits::no_block;

    /// @brief Гранульный индекс корня AVL-дерева; 0 = пустой вектор.
    index_type _root_idx;

    // ─── Конструктор ──────────────────────────────────────────────────────────

    /// @brief Создать пустой вектор.
    pvector() noexcept : _root_idx( static_cast<index_type>( 0 ) ) {}

    // ─── Методы доступа ───────────────────────────────────────────────────────

    /// @brief Проверить, пуст ли вектор.
    bool empty() const noexcept { return _root_idx == static_cast<index_type>( 0 ); }

    /// @brief Получить количество элементов в векторе за O(1).
    /// @return Количество элементов (из weight корня).
    std::size_t size() const noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
            return 0;
        node_pptr root( _root_idx );
        return static_cast<std::size_t>( root.tree_node().get_weight() );
    }

    // ─── Операции с вектором ──────────────────────────────────────────────────

    /**
     * @brief Добавить элемент в конец вектора за O(log n).
     *
     * Создаёт новый узел в ПАП и вставляет его в крайнюю правую позицию AVL-дерева.
     *
     * @param val   Значение для добавления.
     * @return pptr на добавленный узел. Нулевой pptr при ошибке аллокации.
     */
    node_pptr push_back( const T& val ) noexcept
    {
        node_pptr new_node = ManagerT::template allocate_typed<node_type>();
        if ( new_node.is_null() )
            return node_pptr();

        node_type* obj = ManagerT::template resolve<node_type>( new_node );
        if ( obj == nullptr )
            return node_pptr();

        obj->value = val;

        // Инициализируем поля нового узла.
        auto& tn = new_node.tree_node();
        tn.set_left( no_block );
        tn.set_right( no_block );
        tn.set_parent( no_block );
        tn.set_height( static_cast<std::int16_t>( 1 ) );
        tn.set_weight( static_cast<index_type>( 1 ) ); // поддерево размером 1

        _avl_insert_rightmost( new_node );

        return new_node;
    }

    /**
     * @brief Получить элемент по индексу за O(log n).
     *
     * Использует размеры поддеревьев (weight) для O(log n) поиска k-го элемента.
     *
     * @param index Индекс элемента (0-based).
     * @return pptr на узел с данным индексом, или нулевой pptr если индекс вне диапазона.
     */
    node_pptr at( std::size_t index ) const noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
            return node_pptr();

        node_pptr root( _root_idx );
        if ( index >= static_cast<std::size_t>( root.tree_node().get_weight() ) )
            return node_pptr();

        return _avl_find_by_index( node_pptr( _root_idx ), index );
    }

    /**
     * @brief Получить первый элемент вектора за O(log n).
     *
     * @return pptr на первый узел, или нулевой pptr если вектор пуст.
     */
    node_pptr front() const noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
            return node_pptr();
        // Самый левый узел
        node_pptr cur( _root_idx );
        while ( true )
        {
            auto left_idx = cur.tree_node().get_left();
            if ( left_idx == no_block )
                break;
            cur = node_pptr( left_idx );
        }
        return cur;
    }

    /**
     * @brief Получить последний элемент вектора за O(log n).
     *
     * @return pptr на последний узел, или нулевой pptr если вектор пуст.
     */
    node_pptr back() const noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
            return node_pptr();
        // Самый правый узел
        node_pptr cur( _root_idx );
        while ( true )
        {
            auto right_idx = cur.tree_node().get_right();
            if ( right_idx == no_block )
                break;
            cur = node_pptr( right_idx );
        }
        return cur;
    }

    /**
     * @brief Удалить последний элемент вектора за O(log n).
     *
     * Освобождает память узла в ПАП и перебалансирует AVL-дерево.
     *
     * @return true если элемент был удалён, false если вектор пуст.
     */
    bool pop_back() noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
            return false;

        // Находим крайний правый узел.
        node_pptr target( _root_idx );
        while ( true )
        {
            auto right_idx = target.tree_node().get_right();
            if ( right_idx == no_block )
                break;
            target = node_pptr( right_idx );
        }

        _avl_remove( target );
        ManagerT::template deallocate_typed<node_type>( target );
        return true;
    }

    /**
     * @brief Очистить вектор (удалить все элементы).
     *
     * Освобождает память всех узлов в ПАП.
     */
    void clear() noexcept
    {
        while ( !empty() )
        {
            pop_back();
        }
    }

    /**
     * @brief Сбросить вектор (для тестов).
     *
     * Сбрасывает _root_idx, но не освобождает данные в ПАП.
     */
    void reset() noexcept { _root_idx = static_cast<index_type>( 0 ); }

    // ─── Итератор ─────────────────────────────────────────────────────────────

    /**
     * @brief Простой итератор для обхода вектора в порядке индексов (in-order).
     *
     * Реализует in-order обход AVL-дерева (левый → корень → правый),
     * что соответствует порядку элементов вектора.
     */
    struct iterator
    {
        using value_type = node_type;
        using pointer    = node_pptr;

        index_type _current_idx; ///< Текущий узел (no_block/0 = конец).

        iterator() noexcept : _current_idx( static_cast<index_type>( 0 ) ) {}
        explicit iterator( index_type idx ) noexcept : _current_idx( idx ) {}

        bool operator==( const iterator& other ) const noexcept { return _current_idx == other._current_idx; }
        bool operator!=( const iterator& other ) const noexcept { return _current_idx != other._current_idx; }

        /// @brief Разыменование — возвращает pptr на текущий узел.
        node_pptr operator*() const noexcept
        {
            if ( _current_idx == static_cast<index_type>( 0 ) || _current_idx == no_block )
                return node_pptr();
            return node_pptr( _current_idx );
        }

        /// @brief Переход к следующему элементу (in-order successor).
        iterator& operator++() noexcept
        {
            if ( _current_idx == static_cast<index_type>( 0 ) || _current_idx == no_block )
                return *this;

            node_pptr cur( _current_idx );

            // Если есть правый потомок — идём в его крайний левый узел.
            auto right_idx = cur.tree_node().get_right();
            if ( right_idx != no_block )
            {
                node_pptr right( right_idx );
                while ( true )
                {
                    auto left_idx = right.tree_node().get_left();
                    if ( left_idx == no_block )
                        break;
                    right = node_pptr( left_idx );
                }
                _current_idx = right.offset();
                return *this;
            }

            // Иначе — идём вверх, пока не окажемся левым потомком.
            while ( true )
            {
                auto parent_idx = cur.tree_node().get_parent();
                if ( parent_idx == no_block )
                {
                    // Достигли корня снизу справа — конец обхода.
                    _current_idx = static_cast<index_type>( 0 );
                    return *this;
                }
                node_pptr parent( parent_idx );
                auto      parent_left = parent.tree_node().get_left();
                if ( parent_left == cur.offset() )
                {
                    // cur — левый потомок: parent — следующий.
                    _current_idx = parent_idx;
                    return *this;
                }
                cur = parent;
            }
        }
    };

    /// @brief Начало итерации (самый левый узел = первый элемент).
    iterator begin() const noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
            return iterator();
        // Самый левый узел.
        node_pptr cur( _root_idx );
        while ( true )
        {
            auto left_idx = cur.tree_node().get_left();
            if ( left_idx == no_block )
                break;
            cur = node_pptr( left_idx );
        }
        return iterator( cur.offset() );
    }

    /// @brief Конец итерации (sentinel = 0).
    iterator end() const noexcept { return iterator( static_cast<index_type>( 0 ) ); }

  private:
    // ─── AVL order-statistic tree (использует встроенные TreeNode-поля) ────────

    /// @brief Получить размер поддерева (weight), или 0 если узел нулевой.
    static index_type _subtree_size( node_pptr p ) noexcept
    {
        if ( p.is_null() )
            return static_cast<index_type>( 0 );
        auto idx = p.offset();
        if ( idx == no_block )
            return static_cast<index_type>( 0 );
        return p.tree_node().get_weight();
    }

    /// @brief Получить высоту поддерева, или 0 если узел нулевой.
    static std::int16_t _height( node_pptr p ) noexcept
    {
        if ( p.is_null() )
            return 0;
        auto idx = p.offset();
        if ( idx == no_block )
            return 0;
        return p.tree_node().get_height();
    }

    /// @brief Обновить height и weight узла из его детей.
    static void _update_node( node_pptr p ) noexcept
    {
        if ( p.is_null() )
            return;
        auto& tn = p.tree_node();

        auto left_idx  = tn.get_left();
        auto right_idx = tn.get_right();

        node_pptr left_p  = ( left_idx != no_block ) ? node_pptr( left_idx ) : node_pptr();
        node_pptr right_p = ( right_idx != no_block ) ? node_pptr( right_idx ) : node_pptr();

        std::int16_t lh = _height( left_p );
        std::int16_t rh = _height( right_p );
        tn.set_height( static_cast<std::int16_t>( 1 + ( lh > rh ? lh : rh ) ) );

        index_type lw = _subtree_size( left_p );
        index_type rw = _subtree_size( right_p );
        tn.set_weight( static_cast<index_type>( 1 + lw + rw ) );
    }

    /// @brief Обновить связь parent → child (или root если parent нулевой).
    void _set_child( node_pptr parent, node_pptr old_child, node_pptr new_child ) noexcept
    {
        if ( parent.is_null() )
        {
            _root_idx = new_child.offset();
            return;
        }
        auto& ptn      = parent.tree_node();
        auto  left_idx = ptn.get_left();
        if ( left_idx == old_child.offset() )
            ptn.set_left( new_child.is_null() ? no_block : new_child.offset() );
        else
            ptn.set_right( new_child.is_null() ? no_block : new_child.offset() );
    }

    /**
     * @brief Правый поворот вокруг y; обновляет height, weight и parent-ссылки.
     *
     *     y            x
     *    / \          / \
     *   x   C  -->  A    y
     *  / \               / \
     * A   B             B   C
     */
    void _rotate_right( node_pptr y ) noexcept
    {
        auto& y_tn    = y.tree_node();
        auto  x_idx   = y_tn.get_left();
        auto  y_par   = y_tn.get_parent();
        auto  y_p_idx = y_par;

        node_pptr x( x_idx );
        node_pptr y_par_p = ( y_p_idx != no_block ) ? node_pptr( y_p_idx ) : node_pptr();

        auto& x_tn  = x.tree_node();
        auto  b_idx = x_tn.get_right();

        // x.right = y
        x_tn.set_right( y.offset() );
        y_tn.set_parent( x_idx );

        // y.left = B
        y_tn.set_left( b_idx );
        if ( b_idx != no_block )
            node_pptr( b_idx ).tree_node().set_parent( y.offset() );

        // x.parent = y.parent
        x_tn.set_parent( y_p_idx );

        _set_child( y_par_p, y, x );

        _update_node( y );
        _update_node( x );
    }

    /**
     * @brief Левый поворот вокруг x; обновляет height, weight и parent-ссылки.
     *
     *   x               y
     *  / \             / \
     * A   y   -->    x    C
     *    / \        / \
     *   B   C      A   B
     */
    void _rotate_left( node_pptr x ) noexcept
    {
        auto& x_tn    = x.tree_node();
        auto  y_idx   = x_tn.get_right();
        auto  x_p_idx = x_tn.get_parent();

        node_pptr y( y_idx );
        node_pptr x_par_p = ( x_p_idx != no_block ) ? node_pptr( x_p_idx ) : node_pptr();

        auto& y_tn  = y.tree_node();
        auto  b_idx = y_tn.get_left();

        // y.left = x
        y_tn.set_left( x.offset() );
        x_tn.set_parent( y_idx );

        // x.right = B
        x_tn.set_right( b_idx );
        if ( b_idx != no_block )
            node_pptr( b_idx ).tree_node().set_parent( x.offset() );

        // y.parent = x.parent
        y_tn.set_parent( x_p_idx );

        _set_child( x_par_p, x, y );

        _update_node( x );
        _update_node( y );
    }

    /// @brief Перебалансировать дерево снизу вверх от узла p до корня.
    void _rebalance_up( node_pptr p ) noexcept
    {
        while ( !p.is_null() )
        {
            _update_node( p );

            auto& tn = p.tree_node();

            auto left_idx  = tn.get_left();
            auto right_idx = tn.get_right();

            node_pptr left_p  = ( left_idx != no_block ) ? node_pptr( left_idx ) : node_pptr();
            node_pptr right_p = ( right_idx != no_block ) ? node_pptr( right_idx ) : node_pptr();

            std::int16_t bf = static_cast<std::int16_t>( _height( left_p ) - _height( right_p ) );

            if ( bf > 1 )
            {
                // Левый перевес
                auto  ll_idx = left_p.tree_node().get_left();
                auto  lr_idx = left_p.tree_node().get_right();
                auto  ll_h   = ( ll_idx != no_block ) ? _height( node_pptr( ll_idx ) ) : std::int16_t( 0 );
                auto  lr_h   = ( lr_idx != no_block ) ? _height( node_pptr( lr_idx ) ) : std::int16_t( 0 );
                if ( lr_h > ll_h )
                    _rotate_left( left_p );
                _rotate_right( p );
                // После поворота p переместился вниз; его новый родитель — это тот, кто сейчас на его месте.
                // Продолжаем снизу вверх от нового положения p.
                auto p_par = p.tree_node().get_parent();
                p           = ( p_par != no_block ) ? node_pptr( p_par ) : node_pptr();
            }
            else if ( bf < -1 )
            {
                // Правый перевес
                auto  rl_idx = right_p.tree_node().get_left();
                auto  rr_idx = right_p.tree_node().get_right();
                auto  rl_h   = ( rl_idx != no_block ) ? _height( node_pptr( rl_idx ) ) : std::int16_t( 0 );
                auto  rr_h   = ( rr_idx != no_block ) ? _height( node_pptr( rr_idx ) ) : std::int16_t( 0 );
                if ( rl_h > rr_h )
                    _rotate_right( right_p );
                _rotate_left( p );
                auto p_par = p.tree_node().get_parent();
                p           = ( p_par != no_block ) ? node_pptr( p_par ) : node_pptr();
            }
            else
            {
                auto p_par = tn.get_parent();
                p           = ( p_par != no_block ) ? node_pptr( p_par ) : node_pptr();
            }
        }
    }

    /// @brief Вставить new_node в крайнюю правую позицию (конец последовательности).
    void _avl_insert_rightmost( node_pptr new_node ) noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
        {
            _root_idx = new_node.offset();
            return;
        }

        // Идём всегда вправо до листа.
        node_pptr cur( _root_idx );
        while ( true )
        {
            auto right_idx = cur.tree_node().get_right();
            if ( right_idx == no_block )
                break;
            cur = node_pptr( right_idx );
        }

        // Вставляем new_node как правый потомок cur.
        cur.tree_node().set_right( new_node.offset() );
        new_node.tree_node().set_parent( cur.offset() );

        // Перебалансируем снизу вверх.
        _rebalance_up( cur );
    }

    /**
     * @brief Найти k-й по порядку элемент за O(log n).
     *
     * Использует поле weight (размер поддерева) для навигации.
     *
     * @param p     Корень поддерева для поиска.
     * @param index Индекс для поиска (0-based относительно поддерева p).
     * @return pptr на найденный узел.
     */
    static node_pptr _avl_find_by_index( node_pptr p, std::size_t index ) noexcept
    {
        while ( !p.is_null() )
        {
            auto& tn       = p.tree_node();
            auto  left_idx = tn.get_left();

            index_type left_size =
                ( left_idx != no_block ) ? node_pptr( left_idx ).tree_node().get_weight()
                                         : static_cast<index_type>( 0 );

            if ( index < static_cast<std::size_t>( left_size ) )
            {
                // Ищем в левом поддереве.
                p = node_pptr( left_idx );
            }
            else if ( index == static_cast<std::size_t>( left_size ) )
            {
                // Нашли!
                return p;
            }
            else
            {
                // Ищем в правом поддереве с скорректированным индексом.
                index -= static_cast<std::size_t>( left_size ) + 1;
                auto right_idx = tn.get_right();
                if ( right_idx == no_block )
                    return node_pptr();
                p = node_pptr( right_idx );
            }
        }
        return node_pptr();
    }

    /**
     * @brief Удалить узел target из AVL-дерева и перебалансировать.
     *
     * Реализует стандартное удаление из BST с последующей AVL-балансировкой.
     * Для узла с двумя детьми использует in-order successor (крайний левый в правом поддереве).
     */
    void _avl_remove( node_pptr target ) noexcept
    {
        auto& tn        = target.tree_node();
        auto  left_idx  = tn.get_left();
        auto  right_idx = tn.get_right();
        auto  par_idx   = tn.get_parent();

        node_pptr par_p = ( par_idx != no_block ) ? node_pptr( par_idx ) : node_pptr();

        if ( left_idx == no_block && right_idx == no_block )
        {
            // Листовой узел — просто удаляем.
            _set_child( par_p, target, node_pptr() );
            if ( !par_p.is_null() )
                _rebalance_up( par_p );
        }
        else if ( left_idx == no_block )
        {
            // Только правый потомок.
            node_pptr right_p( right_idx );
            right_p.tree_node().set_parent( par_idx );
            _set_child( par_p, target, right_p );
            if ( !par_p.is_null() )
                _rebalance_up( par_p );
            else
                _update_node( right_p );
        }
        else if ( right_idx == no_block )
        {
            // Только левый потомок.
            node_pptr left_p( left_idx );
            left_p.tree_node().set_parent( par_idx );
            _set_child( par_p, target, left_p );
            if ( !par_p.is_null() )
                _rebalance_up( par_p );
            else
                _update_node( left_p );
        }
        else
        {
            // Два потомка — ищем in-order successor (крайний левый в правом поддереве).
            node_pptr successor( right_idx );
            while ( true )
            {
                auto sl = successor.tree_node().get_left();
                if ( sl == no_block )
                    break;
                successor = node_pptr( sl );
            }

            // Запоминаем родителя successor'а до перестановки.
            auto  succ_par_idx = successor.tree_node().get_parent();
            auto  succ_rgt_idx = successor.tree_node().get_right();
            auto& succ_tn      = successor.tree_node();

            node_pptr succ_par_p =
                ( succ_par_idx != static_cast<index_type>( target.offset() ) )
                    ? node_pptr( succ_par_idx )
                    : node_pptr();

            // Отсоединяем successor от его текущего места.
            if ( succ_par_idx == target.offset() )
            {
                // Successor — прямой правый потомок target.
                // ничего не делаем — reconnect ниже
            }
            else
            {
                // Правый потомок successor'а становится левым потомком его родителя.
                node_pptr succ_parent( succ_par_idx );
                if ( succ_rgt_idx != no_block )
                {
                    node_pptr succ_rgt( succ_rgt_idx );
                    succ_rgt.tree_node().set_parent( succ_par_idx );
                    succ_parent.tree_node().set_left( succ_rgt_idx );
                }
                else
                {
                    succ_parent.tree_node().set_left( no_block );
                }
            }

            // Ставим successor на место target.
            succ_tn.set_left( left_idx );
            node_pptr( left_idx ).tree_node().set_parent( successor.offset() );

            if ( succ_par_idx == target.offset() )
            {
                // Правый потомок successor'а остаётся как есть.
                succ_tn.set_right( succ_rgt_idx );
                if ( succ_rgt_idx != no_block )
                    node_pptr( succ_rgt_idx ).tree_node().set_parent( successor.offset() );
            }
            else
            {
                succ_tn.set_right( right_idx );
                node_pptr( right_idx ).tree_node().set_parent( successor.offset() );
            }

            succ_tn.set_parent( par_idx );
            _set_child( par_p, target, successor );

            // Перебалансируем от нижней точки изменений.
            node_pptr rebalance_start =
                ( succ_par_idx == target.offset() ) ? successor : node_pptr( succ_par_idx );
            _rebalance_up( rebalance_start );
        }
    }
};

} // namespace pmm
