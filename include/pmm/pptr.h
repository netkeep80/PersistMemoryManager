/**
 * @file pmm/pptr.h
 * @brief pptr<T, ManagerT> — персистентный типизированный указатель (Issue #102).
 *
 * Выделен из legacy_manager.h для использования с AbstractPersistMemoryManager.
 *
 * pptr<T, ManagerT> хранит гранульный индекс вместо адреса,
 * что делает его адресно-независимым и пригодным для персистентных хранилищ:
 *   - Хранит индекс типа ManagerT::address_traits::index_type (обычно uint32_t = 4 байта)
 *   - Нет смещения при повторной загрузке по другому адресу
 *   - Запрет адресной арифметики (pptr++ запрещён)
 *   - Index 0 означает null
 *
 * Поддерживаемые режимы разыменования (Issue #102, #108):
 *   - Объектная модель: `p.resolve(mgr)` или `mgr.resolve<T>(p)` — через экземпляр менеджера
 *   - Статическая модель: `p.resolve()`, `*p`, `p->field` — через статический метод менеджера
 *     (только для менеджеров с статическим API, например StaticMemoryManager)
 *
 * Пример использования с AbstractPersistMemoryManager (объектная модель):
 * @code
 *   using MyMgr = pmm::presets::SingleThreadedHeap;
 *   MyMgr pmm;
 *   pmm.create(64 * 1024);
 *
 *   MyMgr::pptr<int> p = pmm.allocate_typed<int>();
 *   if (p) {
 *       *p.resolve(pmm) = 42;  // разыменование через экземпляр менеджера
 *   }
 *   pmm.deallocate_typed(p);
 * @endcode
 *
 * Пример использования с StaticMemoryManager (статическая модель):
 * @code
 *   using MyMgr = pmm::StaticMemoryManager<pmm::CacheManagerConfig>;
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
 * @see abstract_pmm.h — AbstractPersistMemoryManager::allocate_typed()
 * @see static_memory_manager.h — StaticMemoryManager (статическая модель)
 * @version 0.4 (Issue #108 — добавлен беззаргументный resolve(), operator*, operator->)
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

/// @brief Вспомогательный trait: извлекает index_type из менеджера (через address_traits).
/// Если ManagerT::index_type существует, использует его; иначе — uint32_t.
template <typename ManagerT, typename = void> struct manager_index_type
{
    using type = std::uint32_t;
};

template <typename ManagerT> struct manager_index_type<ManagerT, std::void_t<typename ManagerT::index_type>>
{
    using type = typename ManagerT::index_type;
};

/// @endcond

} // namespace detail

/**
 * @brief Персистентный типизированный указатель (гранульный индекс).
 *
 * Хранит гранульный индекс пользовательских данных, а не адрес.
 * Адресно-независим: корректно загружается из файла по другому базовому адресу.
 *
 * Тип индекса определяется через `ManagerT::index_type` (если менеджер его предоставляет),
 * иначе используется `uint32_t` (Issue #108).
 *
 * @tparam T Тип данных, на который указывает pptr.
 * @tparam ManagerT Тип менеджера (обязателен, void не допускается).
 *
 * Поддерживаются два режима разыменования:
 *   - Объектная модель: `p.resolve(mgr)` или `mgr.resolve<T>(p)`
 *   - Статическая модель: `p.resolve()`, `*p`, `p->field` (если ManagerT имеет статический resolve)
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

    /// @brief Тип гранульного индекса (берётся из ManagerT::index_type, иначе uint32_t).
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

    // ─── Разыменование через экземпляр менеджера (объектная модель) ──────────

    /**
     * @brief Разыменовать через экземпляр менеджера (объектная модель).
     *
     * Эквивалентно mgr.resolve<T>(*this).
     *
     * @param mgr Экземпляр менеджера, с которым связан данный pptr.
     * @return T* — указатель на данные или nullptr если is_null().
     */
    T* resolve( ManagerT& mgr ) const noexcept { return mgr.template resolve<T>( *this ); }

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

// pptr<T, ManagerT> должен быть 4 байта (uint32_t гранульный индекс) — ManagerT не хранится
static_assert( sizeof( pptr<int, struct DummyMgr> ) == 4, "sizeof(pptr<T,ManagerT>) must be 4 bytes (Issue #59)" );
static_assert( sizeof( pptr<double, struct DummyMgr2> ) == 4, "sizeof(pptr<T,ManagerT>) must be 4 bytes (Issue #59)" );

} // namespace pmm
