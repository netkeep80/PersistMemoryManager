/**
 * @file pmm/pptr.h
 * @brief pptr<T, ManagerT> — персистентный типизированный указатель.
 *
 * pptr<T, ManagerT> хранит гранульный индекс вместо адреса,
 * что делает его адресно-независимым и пригодным для персистентных хранилищ:
 *   - Хранит индекс типа ManagerT::address_traits::index_type
 *   - Нет смещения при повторной загрузке по другому адресу
 *   - Запрет адресной арифметики (pptr++ запрещён)
 *   - Index 0 означает null
 *
 * Поддерживаемые режимы разыменования:
 *   - Статическая модель: `*p`, `p->field` — через статический метод менеджера
 *
 * Доступ к узлу AVL-дерева:
 *   - `p.tree_node()` — прямой доступ к TreeNode через ссылку
 *
 * Пример использования с PersistMemoryManager (статическая модель):
 * @code
 *   using MyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *   MyMgr::create(64 * 1024);
 *
 *   MyMgr::pptr<int> p = MyMgr::allocate_typed<int>();
 *   if (p) {
 *       *p = 42;        // operator* — разыменование без аргументов
 *       p->some_field;  // operator-> — доступ к полям
 *   }
 *   MyMgr::deallocate_typed(p);
 *   MyMgr::destroy();
 * @endcode
 *
 * @see persist_memory_manager.h — PersistMemoryManager (статическая модель)
 * @see tree_node.h — TreeNode<A> с публичными методами
 * @version 0.9
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

namespace detail
{

/// @cond INTERNAL

/// @brief Вспомогательный trait: извлекает index_type из менеджера через address_traits.
/// Если ManagerT::address_traits::index_type существует, использует его;
/// иначе — uint32_t (для совместимости).
template <typename ManagerT> struct manager_index_type
{
    using type = std::uint32_t;
};

template <typename ManagerT>
    requires requires { typename ManagerT::address_traits::index_type; }
struct manager_index_type<ManagerT>
{
    using type = typename ManagerT::address_traits::index_type;
};

/// @endcond

} // namespace detail

/**
 * @brief Персистентный типизированный указатель (гранульный индекс).
 *
 * Хранит гранульный индекс пользовательских данных, а не адрес.
 * Адресно-независим: корректно загружается из файла по другому базовому адресу.
 *
 * Тип индекса определяется через `ManagerT::address_traits::index_type`.
 *
 * @tparam T Тип данных, на который указывает pptr.
 * @tparam ManagerT Тип менеджера (обязателен, void не допускается).
 *
 * Поддерживается статическая модель разыменования:
 *   - `*p`, `p->field` — через статический метод менеджера
 */
template <class T, class ManagerT>
    requires( !std::is_void_v<ManagerT> )
class pptr
{

  public:
    /// @brief Тип данных, на который ссылается pptr.
    using element_type = T;

    /// @brief Тип менеджера, к которому привязан pptr.
    using manager_type = ManagerT;

    /// @brief Тип гранульного индекса (берётся из ManagerT::address_traits::index_type).
    using index_type = typename detail::manager_index_type<ManagerT>::type;

  private:
    index_type _idx; ///< Гранульный индекс пользовательских данных (0 = null)

  public:
    constexpr pptr() noexcept : _idx( 0 ) {}
    constexpr explicit pptr( index_type idx ) noexcept : _idx( idx ) {}
    constexpr pptr( const pptr& ) noexcept            = default;
    constexpr pptr& operator=( const pptr& ) noexcept = default;
    ~pptr() noexcept                                  = default;

    // Адресная арифметика запрещена — pptr не является итератором
    pptr& operator++()      = delete;
    pptr  operator++( int ) = delete;
    pptr& operator--()      = delete;
    pptr  operator--( int ) = delete;

    /// @brief Проверить, является ли указатель нулевым.
    constexpr bool is_null() const noexcept { return _idx == 0; }

    /// @brief Явное преобразование в bool: true если не null.
    constexpr explicit operator bool() const noexcept { return _idx != 0; }

    /// @brief Получить гранульный индекс (для сохранения/восстановления).
    constexpr index_type offset() const noexcept { return _idx; }

    /// @brief Получить байтовое смещение из гранульного индекса.
    ///
    /// Возвращает `offset() * granule_size`. Упрощает интеграцию с внешними системами,
    /// работающими с байтовыми адресами (например, BinDiffSynchronizer pam_adapter).
    ///
    /// @return Байтовое смещение в управляемой области. Для null pptr возвращает 0.
    constexpr std::size_t byte_offset() const noexcept
    {
        return static_cast<std::size_t>( _idx ) * ManagerT::address_traits::granule_size;
    }

    /// @brief Сравнение персистентных указателей одного типа по индексу.
    constexpr bool operator==( const pptr& other ) const noexcept { return _idx == other._idx; }
    constexpr bool operator!=( const pptr& other ) const noexcept { return _idx != other._idx; }

    /**
     * @brief Упорядочивание персистентных указателей для использования как ключ в pmap.
     *
     * Сравнивает указываемые объекты через `*this < *other`, если оба указателя не null.
     * Null pptr считается меньше любого ненулевого указателя.
     *
     * @note Требует, чтобы тип T поддерживал `operator<`.
     * @return true если `*this` должен быть перед `other` в упорядоченной последовательности.
     */
    bool operator<( const pptr& other ) const noexcept
    {
        // Compile-time check — T must support operator< for pptr ordering.
        static_assert(
            requires( const T& a, const T& b ) {
                { a < b } -> std::convertible_to<bool>;
            }, "pptr<T>::operator< requires T to support operator<. "
               "Provide bool operator<(const T&, const T&) or use pptr::offset() for index-based ordering." );
        // Null pptr меньше любого ненулевого
        if ( is_null() && !other.is_null() )
            return true;
        if ( !is_null() && other.is_null() )
            return false;
        if ( is_null() && other.is_null() )
            return false;
        // Оба ненулевые — сравниваем указываемые объекты
        return **this < *other;
    }

    // ─── Разыменование через статический менеджер (статическая модель) ────────

    /**
     * @brief Разыменование указателя (статическая модель).
     *
     * Вызывает `ManagerT::resolve<T>(*this)` без аргументов.
     * Доступно только для менеджеров со статическим API (например, PersistMemoryManager).
     *
     * @return T& — ссылка на данные.
     */
    T& operator*() const noexcept { return *ManagerT::template resolve<T>( *this ); }

    /**
     * @brief Доступ к членам через персистентный указатель (статическая модель).
     *
     * Вызывает `ManagerT::resolve<T>(*this)` без аргументов.
     * Доступно только для менеджеров со статическим API.
     *
     * @return T* — указатель на данные.
     */
    T* operator->() const noexcept { return ManagerT::template resolve<T>( *this ); }

    /**
     * @brief Получить сырой указатель (низкоуровневый доступ).
     *
     * Вызывает `ManagerT::resolve<T>(*this)`.
     * Используйте `*p` или `p->field` вместо этого метода для обычных операций.
     * Для доступа к элементам массива используйте `ManagerT::resolve_at(p, i)`.
     *
     * @warning Сырой указатель действителен только пока менеджер инициализирован
     *          и блок не освобождён. Не храните его дольше необходимого.
     *
     * @return T* — указатель на данные или nullptr если is_null().
     */
    T* resolve() const noexcept { return ManagerT::template resolve<T>( *this ); }

    // ─── Доступ к узлу AVL-дерева ────────────────────────────────

    /**
     * @brief Получить ссылку на узел AVL-дерева в заголовке блока.
     *
     * Позволяет работать с узлом дерева напрямую через методы TreeNode:
     * get_left(), set_left(), get_right(), set_right(), get_parent(), set_parent(),
     * get_weight(), set_weight(), get_height(), set_height(), get_node_type(), set_node_type().
     *
     * Использование:
     * @code
     *   auto& tn = p.tree_node();
     *   auto left_idx = tn.get_left();  // no_block если нет левого потомка
     *   tn.set_left(other_p.offset());
     * @endcode
     *
     * @warning Ссылка действительна только пока менеджер инициализирован и блок не освобождён.
     *
     * @return TreeNode& — ссылка на узел AVL-дерева в заголовке выделенного блока.
     */
    auto& tree_node() const noexcept { return ManagerT::tree_node( *this ); }
};

// pptr<T, ManagerT> хранит только гранульный индекс — ManagerT не хранится.
// Размер зависит от ManagerT::address_traits::index_type.
// Для DefaultAddressTraits (uint32_t) размер равен 4 байтам.

} // namespace pmm
