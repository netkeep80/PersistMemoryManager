/**
 * @file pmm/pmap.h
 * @brief pmap<_K,_V,ManagerT> — персистентный словарь на основе AVL-дерева (Issue #153, #196).
 *
 * Реализует шаблонный ассоциативный контейнер в персистентном адресном пространстве (ПАП).
 * Каждый узел словаря — это блок в ПАП, хранящий пару ключ-значение (_K, _V).
 * Узлы используют встроенные поля TreeNode (left_offset, right_offset, parent_offset,
 * avl_height) для организации AVL-дерева, как это делает pstringview (Issue #151).
 *
 * Ключевые особенности:
 *   - Персистентный: гранульные индексы адресно-независимы при перезагрузке ПАП.
 *   - AVL-балансировка: O(log n) для вставки, поиска и удаления.
 *   - Встроенный AVL: узлы используют встроенные TreeNode-поля Block<AT> без
 *     дополнительных аллокаций структур дерева.
 *   - Не дублирует ключи: повторная вставка по существующему ключу обновляет значение.
 *   - Узлы НЕ блокируются навечно (в отличие от pstringview — Issue #155).
 *   - Тип ключа _K должен поддерживать operator< и operator==.
 *   - erase(key) — удаление узла по ключу с деаллокацией памяти (Issue #196).
 *   - size() — количество элементов за O(n) (Issue #196).
 *   - begin()/end() — итератор для обхода в порядке ключей (Issue #196).
 *   - clear() — удаление всех элементов с деаллокацией (Issue #196).
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
 *   // Удаление по ключу
 *   bool removed = map.erase(42);  // true
 *
 *   // Итерация в порядке ключей
 *   for (auto it = map.begin(); it != map.end(); ++it) {
 *       auto node = *it;
 *       // node->key, node->value
 *   }
 *
 *   // Количество элементов
 *   std::size_t n = map.size();  // 1
 *
 *   map.clear();  // удалить все элементы
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
 * @see avl_tree_mixin.h — общие AVL-операции (Issue #155)
 * @see pptr.h — pptr<T, ManagerT> (персистентный указатель)
 * @see tree_node.h — TreeNode<AT> (встроенные AVL-поля каждого блока)
 * @version 0.4 (Issue #196 — erase, size, iterator, clear)
 */

#pragma once

#include "pmm/avl_tree_mixin.h"

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
 *   - Вставка/поиск/удаление за O(log n).
 *   - Повторная вставка по существующему ключу обновляет значение.
 *   - erase(key) удаляет узел и освобождает память в ПАП (Issue #196).
 *   - size() возвращает количество элементов за O(n) (Issue #196).
 *   - begin()/end() — итератор для обхода в порядке ключей (Issue #196).
 *   - clear() — удаление всех элементов с деаллокацией (Issue #196).
 *   - Узлы НЕ блокируются навечно (Issue #155): в отличие от pstringview, узлы
 *     pmap могут быть освобождены после удаления из дерева.
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

    /// @brief Sentinel value for "no node" in TreeNode fields.
    static constexpr index_type no_block = ManagerT::address_traits::no_block;

    /// @brief Гранульный индекс корня AVL-дерева; 0 = пустое дерево.
    index_type _root_idx;

    // ─── Конструктор ──────────────────────────────────────────────────────────

    /// @brief Создать пустой словарь.
    pmap() noexcept : _root_idx( static_cast<index_type>( 0 ) ) {}

    // ─── Методы доступа ───────────────────────────────────────────────────────

    /// @brief Проверить, пуст ли словарь.
    bool empty() const noexcept { return _root_idx == static_cast<index_type>( 0 ); }

    /// @brief Получить количество элементов в словаре за O(n).
    /// @return Количество элементов (подсчитывается обходом дерева).
    std::size_t size() const noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
            return 0;
        return _subtree_count( node_pptr( _root_idx ) );
    }

    // ─── Операции со словарём ─────────────────────────────────────────────────

    /**
     * @brief Вставить или обновить пару ключ-значение.
     *
     * Если ключ уже существует в словаре — обновляет значение.
     * Если ключ новый — создаёт новый узел в ПАП и вставляет в AVL-дерево.
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

        // Инициализируем AVL-поля нового узла (no_block = нет связи).
        auto& tn = new_node.tree_node();
        tn.set_left( no_block );
        tn.set_right( no_block );
        tn.set_parent( no_block );
        tn.set_height( static_cast<std::int16_t>( 1 ) );

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
     * @brief Удалить узел по ключу (Issue #196).
     *
     * Находит узел с заданным ключом, удаляет его из AVL-дерева и
     * освобождает память блока в ПАП.
     *
     * @param key Ключ для удаления.
     * @return true если ключ был найден и удалён, false если ключ не найден.
     */
    bool erase( const _K& key ) noexcept
    {
        node_pptr target = _avl_find( key );
        if ( target.is_null() )
            return false;

        detail::avl_remove( target, _root_idx );
        ManagerT::template deallocate_typed<node_type>( target );
        return true;
    }

    /**
     * @brief Очистить словарь (удалить все элементы) (Issue #196).
     *
     * Освобождает память всех узлов в ПАП.
     */
    void clear() noexcept
    {
        if ( _root_idx != static_cast<index_type>( 0 ) )
            _clear_subtree( node_pptr( _root_idx ) );
        _root_idx = static_cast<index_type>( 0 );
    }

    /**
     * @brief Сбросить словарь (для тестов).
     *
     * Сбрасывает _root_idx, но не освобождает данные в ПАП.
     */
    void reset() noexcept { _root_idx = static_cast<index_type>( 0 ); }

    // ─── Итератор (Issue #196) ───────────────────────────────────────────────

    /**
     * @brief Итератор для обхода словаря в порядке ключей (in-order).
     *
     * Реализует in-order обход AVL-дерева (левый -> корень -> правый),
     * что соответствует порядку возрастания ключей.
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

    /// @brief Начало итерации (самый левый узел = наименьший ключ).
    iterator begin() const noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
            return iterator();
        node_pptr min = detail::avl_min_node( node_pptr( _root_idx ) );
        return iterator( min.offset() );
    }

    /// @brief Конец итерации (sentinel = 0).
    iterator end() const noexcept { return iterator( static_cast<index_type>( 0 ) ); }

    // ─── AVL-дерево (использует встроенные TreeNode-поля каждого узла) ────────

  private:
    /// @brief Найти узел AVL-дерева с заданным ключом. Возвращает null если не найден.
    node_pptr _avl_find( const _K& key ) const noexcept
    {
        return detail::avl_find<node_pptr>(
            _root_idx,
            [&]( node_pptr cur ) -> int
            {
                node_type* obj = ManagerT::template resolve<node_type>( cur );
                if ( obj == nullptr )
                    return 0;
                if ( key == obj->key )
                    return 0;
                return ( key < obj->key ) ? -1 : 1;
            },
            []( node_pptr p ) -> node_type* { return ManagerT::template resolve<node_type>( p ); } );
    }

    /// @brief Вставить новый узел в AVL-дерево. Предполагается, что ключ ещё не в дереве.
    void _avl_insert( node_pptr new_node ) noexcept
    {
        node_type* new_obj = ManagerT::template resolve<node_type>( new_node );
        detail::avl_insert(
            new_node, _root_idx,
            [&]( node_pptr cur ) -> bool
            {
                node_type* obj = ManagerT::template resolve<node_type>( cur );
                return ( obj != nullptr ) && ( new_obj->key < obj->key );
            },
            []( node_pptr p ) -> node_type* { return ManagerT::template resolve<node_type>( p ); } );
    }

    /// @brief Подсчитать количество узлов в поддереве (Issue #196).
    static std::size_t _subtree_count( node_pptr p ) noexcept
    {
        if ( p.is_null() )
            return 0;
        std::size_t count   = 1;
        auto        left_p  = detail::pptr_get_left( p );
        auto        right_p = detail::pptr_get_right( p );
        count += _subtree_count( left_p );
        count += _subtree_count( right_p );
        return count;
    }

    /// @brief Рекурсивно деаллоцировать все узлы поддерева (Issue #196).
    static void _clear_subtree( node_pptr p ) noexcept
    {
        if ( p.is_null() )
            return;
        auto left_p  = detail::pptr_get_left( p );
        auto right_p = detail::pptr_get_right( p );
        _clear_subtree( left_p );
        _clear_subtree( right_p );
        ManagerT::template deallocate_typed<node_type>( p );
    }
};

} // namespace pmm
