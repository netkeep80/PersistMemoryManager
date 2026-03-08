/**
 * @file pmm/pptr.h
 * @brief pptr<T, ManagerT> — персистентный типизированный указатель (Issue #102).
 *
 * pptr<T, ManagerT> хранит гранульный индекс вместо адреса,
 * что делает его адресно-независимым и пригодным для персистентных хранилищ:
 *   - Хранит индекс типа ManagerT::address_traits::index_type
 *   - Нет смещения при повторной загрузке по другому адресу
 *   - Запрет адресной арифметики (pptr++ запрещён)
 *   - Index 0 означает null
 *
 * Поддерживаемые режимы разыменования (Issue #102, #108):
 *   - Статическая модель: `p.resolve()`, `*p`, `p->field` — через статический метод менеджера
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
 *       int* raw = p.resolve();  // явный вызов resolve() без аргументов
 *   }
 *   MyMgr::deallocate_typed(p);
 *   MyMgr::destroy();
 * @endcode
 *
 * @see persist_memory_manager.h — PersistMemoryManager (статическая модель, Issue #110)
 * @version 0.5 (Issue #110 — index_type из ManagerT::address_traits::index_type)
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
/// иначе пробует ManagerT::index_type; иначе — uint32_t (для совместимости).
template <typename ManagerT, typename = void> struct manager_index_type
{
    using type = std::uint32_t;
};

template <typename ManagerT>
struct manager_index_type<ManagerT, std::void_t<typename ManagerT::address_traits::index_type>>
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
 * Тип индекса определяется через `ManagerT::address_traits::index_type` (Issue #110).
 *
 * @tparam T Тип данных, на который указывает pptr.
 * @tparam ManagerT Тип менеджера (обязателен, void не допускается).
 *
 * Поддерживается статическая модель разыменования:
 *   - `p.resolve()`, `*p`, `p->field` — через статический метод менеджера
 */
template <class T, class ManagerT> class pptr
{
    static_assert( !std::is_void<ManagerT>::value,
                   "pptr<T, void> is no longer supported. Use pptr<T, ManagerT> with a concrete manager type. "
                   "See pmm_presets.h for available presets." );

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

    /// @brief Сравнение персистентных указателей одного типа.
    constexpr bool operator==( const pptr& other ) const noexcept { return _idx == other._idx; }
    constexpr bool operator!=( const pptr& other ) const noexcept { return _idx != other._idx; }

    // ─── Разыменование через статический менеджер (статическая модель) ────────

    /**
     * @brief Разыменовать через статический метод менеджера (статическая модель).
     *
     * Вызывает `ManagerT::resolve<T>(*this)` без аргументов.
     * Доступно только для менеджеров со статическим API (например, StaticMemoryManager).
     *
     * @return T* — указатель на данные или nullptr если is_null().
     */
    T* resolve() const noexcept { return ManagerT::template resolve<T>( *this ); }

    /**
     * @brief Разыменование указателя (статическая модель).
     *
     * Эквивалентно `*resolve()`.
     * Доступно только для менеджеров со статическим API.
     *
     * @return T& — ссылка на данные.
     */
    T& operator*() const noexcept { return *resolve(); }

    /**
     * @brief Доступ к членам через персистентный указатель (статическая модель).
     *
     * Эквивалентно `resolve()`.
     * Доступно только для менеджеров со статическим API.
     *
     * @return T* — указатель на данные.
     */
    T* operator->() const noexcept { return resolve(); }
};

// pptr<T, ManagerT> хранит только гранульный индекс — ManagerT не хранится.
// Размер зависит от ManagerT::address_traits::index_type (Issue #110).
// Для DefaultAddressTraits (uint32_t) размер равен 4 байтам.

} // namespace pmm
