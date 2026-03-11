/**
 * @file pmm/pvector.h
 * @brief pvector<T,ManagerT> — персистентный вектор для менеджера ПАП (Issue #186).
 *
 * Реализует шаблонный последовательный контейнер в персистентном адресном пространстве (ПАП).
 * Каждый элемент вектора — это блок в ПАП, хранящий значение типа T.
 * Вектор использует связный список через встроенные поля TreeNode (left_offset, right_offset)
 * для поддержания порядка элементов.
 *
 * Ключевые особенности:
 *   - Персистентный: гранульные индексы адресно-независимы при перезагрузке ПАП.
 *   - O(1) для push_back и доступа по индексу (при наличии указателя на tail).
 *   - O(n) для size() — линейный обход списка.
 *   - Узлы НЕ блокируются навечно — они могут быть освобождены после удаления из вектора.
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
 *   auto p = vec.at(1);  // доступ по индексу
 *   if (!p.is_null()) {
 *       int val = p->value;  // 20
 *   }
 *
 *   std::size_t n = vec.size();  // 3
 *
 *   Mgr::destroy();
 * @endcode
 *
 * @see pmap.h — аналогичный персистентный контейнер (Issue #153)
 * @see pptr.h — pptr<T, ManagerT> (персистентный указатель)
 * @see tree_node.h — TreeNode<AT> (встроенные поля каждого блока)
 * @version 0.1 (Issue #186 — pvector<T>)
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
 * используются для организации связного списка:
 *   - left_offset  → prev (предыдущий элемент)
 *   - right_offset → next (следующий элемент)
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
 * содержащим гранульные индексы head и tail связного списка элементов.
 * Элементы (pvector_node) хранятся в ПАП и используют встроенные TreeNode-поля
 * для организации двусвязного списка.
 *
 * Особенности:
 *   - push_back() за O(1) (есть указатель на tail).
 *   - at(i) за O(i) — линейный обход от head.
 *   - size() за O(n) — подсчёт элементов.
 *   - Узлы НЕ блокируются навечно — можно освобождать через clear().
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

    /// @brief Гранульный индекс первого элемента списка; 0 = пустой вектор.
    index_type _head_idx;

    /// @brief Гранульный индекс последнего элемента списка; 0 = пустой вектор.
    index_type _tail_idx;

    /// @brief Кэшированный размер вектора (чтобы не считать каждый раз).
    index_type _size;

    // ─── Конструктор ──────────────────────────────────────────────────────────

    /// @brief Создать пустой вектор.
    pvector() noexcept
        : _head_idx( static_cast<index_type>( 0 ) ), _tail_idx( static_cast<index_type>( 0 ) ),
          _size( static_cast<index_type>( 0 ) )
    {
    }

    // ─── Методы доступа ───────────────────────────────────────────────────────

    /// @brief Проверить, пуст ли вектор.
    bool empty() const noexcept { return _head_idx == static_cast<index_type>( 0 ); }

    /// @brief Получить количество элементов в векторе.
    /// @return Количество элементов (кэшированное значение).
    std::size_t size() const noexcept { return static_cast<std::size_t>( _size ); }

    // ─── Операции с вектором ──────────────────────────────────────────────────

    /**
     * @brief Добавить элемент в конец вектора.
     *
     * Создаёт новый узел в ПАП и добавляет его в конец связного списка.
     *
     * @param val   Значение для добавления.
     * @return pptr на добавленный узел. Нулевой pptr при ошибке аллокации.
     */
    node_pptr push_back( const T& val ) noexcept
    {
        // Создаём новый узел в ПАП.
        node_pptr new_node = ManagerT::template allocate_typed<node_type>();
        if ( new_node.is_null() )
            return node_pptr();

        node_type* obj = ManagerT::template resolve<node_type>( new_node );
        if ( obj == nullptr )
            return node_pptr();

        obj->value = val;

        // Инициализируем поля связного списка нового узла.
        // Используем TreeNode: left = prev, right = next
        auto& tn = new_node.tree_node();
        tn.set_left( _tail_idx );                            // prev = текущий tail
        tn.set_right( static_cast<index_type>( 0 ) );        // next = null (это новый tail)
        tn.set_parent( static_cast<index_type>( 0 ) );       // не используется
        tn.set_height( static_cast<std::int16_t>( 0 ) );     // не используется

        // Связываем новый узел со старым tail.
        if ( _tail_idx != static_cast<index_type>( 0 ) )
        {
            node_pptr tail_ptr( _tail_idx );
            auto&     tail_tn = tail_ptr.tree_node();
            tail_tn.set_right( new_node.offset() );  // old_tail.next = new_node
        }
        else
        {
            // Список был пуст — новый узел становится head.
            _head_idx = new_node.offset();
        }

        // Обновляем tail и размер.
        _tail_idx = new_node.offset();
        ++_size;

        return new_node;
    }

    /**
     * @brief Получить элемент по индексу.
     *
     * @param index Индекс элемента (0-based).
     * @return pptr на узел с данным индексом, или нулевой pptr если индекс вне диапазона.
     */
    node_pptr at( std::size_t index ) const noexcept
    {
        if ( index >= static_cast<std::size_t>( _size ) )
            return node_pptr();

        // Линейный обход от head.
        index_type current_idx = _head_idx;
        for ( std::size_t i = 0; i < index && current_idx != static_cast<index_type>( 0 ); ++i )
        {
            node_pptr current_ptr( current_idx );
            auto&     tn = current_ptr.tree_node();
            current_idx  = tn.get_right();  // next
        }

        if ( current_idx == static_cast<index_type>( 0 ) )
            return node_pptr();

        return node_pptr( current_idx );
    }

    /**
     * @brief Получить первый элемент вектора.
     *
     * @return pptr на первый узел, или нулевой pptr если вектор пуст.
     */
    node_pptr front() const noexcept
    {
        if ( _head_idx == static_cast<index_type>( 0 ) )
            return node_pptr();
        return node_pptr( _head_idx );
    }

    /**
     * @brief Получить последний элемент вектора.
     *
     * @return pptr на последний узел, или нулевой pptr если вектор пуст.
     */
    node_pptr back() const noexcept
    {
        if ( _tail_idx == static_cast<index_type>( 0 ) )
            return node_pptr();
        return node_pptr( _tail_idx );
    }

    /**
     * @brief Удалить последний элемент вектора.
     *
     * Освобождает память узла в ПАП и обновляет tail.
     *
     * @return true если элемент был удалён, false если вектор пуст.
     */
    bool pop_back() noexcept
    {
        if ( _tail_idx == static_cast<index_type>( 0 ) )
            return false;

        node_pptr tail_ptr( _tail_idx );
        auto&     tail_tn     = tail_ptr.tree_node();
        index_type prev_idx   = tail_tn.get_left();  // prev

        // Освобождаем узел.
        ManagerT::template deallocate_typed<node_type>( tail_ptr );

        // Обновляем tail.
        _tail_idx = prev_idx;
        --_size;

        if ( prev_idx != static_cast<index_type>( 0 ) )
        {
            // Обновляем next предыдущего элемента.
            node_pptr prev_ptr( prev_idx );
            auto&     prev_tn = prev_ptr.tree_node();
            prev_tn.set_right( static_cast<index_type>( 0 ) );
        }
        else
        {
            // Вектор стал пустым.
            _head_idx = static_cast<index_type>( 0 );
        }

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
     * Сбрасывает head/tail/size, но не освобождает данные в ПАП.
     */
    void reset() noexcept
    {
        _head_idx = static_cast<index_type>( 0 );
        _tail_idx = static_cast<index_type>( 0 );
        _size     = static_cast<index_type>( 0 );
    }

    // ─── Итератор ─────────────────────────────────────────────────────────────

    /**
     * @brief Простой итератор для обхода вектора.
     */
    struct iterator
    {
        using value_type = node_type;
        using pointer    = node_pptr;

        index_type _current_idx;

        iterator() noexcept : _current_idx( static_cast<index_type>( 0 ) ) {}
        explicit iterator( index_type idx ) noexcept : _current_idx( idx ) {}

        bool operator==( const iterator& other ) const noexcept { return _current_idx == other._current_idx; }
        bool operator!=( const iterator& other ) const noexcept { return _current_idx != other._current_idx; }

        /// @brief Разыменование — возвращает pptr на текущий узел.
        node_pptr operator*() const noexcept
        {
            if ( _current_idx == static_cast<index_type>( 0 ) )
                return node_pptr();
            return node_pptr( _current_idx );
        }

        /// @brief Переход к следующему элементу.
        iterator& operator++() noexcept
        {
            if ( _current_idx != static_cast<index_type>( 0 ) )
            {
                node_pptr current_ptr( _current_idx );
                auto&     tn    = current_ptr.tree_node();
                _current_idx    = tn.get_right();  // next
            }
            return *this;
        }
    };

    /// @brief Начало итерации.
    iterator begin() const noexcept { return iterator( _head_idx ); }

    /// @brief Конец итерации (sentinel).
    iterator end() const noexcept { return iterator( static_cast<index_type>( 0 ) ); }
};

} // namespace pmm
