/**
 * @file pmm/pptr.h
 * @brief pptr<T, ManagerT> — персистентный типизированный указатель (Issue #97, #100).
 *
 * Выделен из legacy_manager.h для использования с AbstractPersistMemoryManager.
 *
 * pptr<T> хранит гранульный индекс (4 байта, uint32_t) вместо адреса,
 * что делает его адресно-независимым и пригодным для персистентных хранилищ:
 *   - 4 байта вместо 8 (для 64-bit)
 *   - Нет смещения при повторной загрузке по другому адресу
 *   - Запрет адресной арифметики (pptr++ запрещён)
 *   - Index 0 означает null
 *
 * Два режима использования (Issue #100):
 *   - `pptr<T>` (ManagerT=void) — обратная совместимость, синглтон-разыменование (устарело)
 *   - `pptr<T, ManagerT>` — привязан к типу менеджера, разыменование через p.resolve(mgr)
 *
 * Разыменование:
 *   - Для AbstractPersistMemoryManager (рекомендуется): mgr.resolve<T>(p) или p.resolve(mgr)
 *   - Для PersistMemoryManager<> (синглтон, устарело): p.get() → T*
 *
 * Пример использования с AbstractPersistMemoryManager (Issue #97, обратно совместимо):
 * @code
 *   pmm::presets::SingleThreadedHeap pmm;
 *   pmm.create(64 * 1024);
 *
 *   // Выделение (возвращает Manager::pptr<int>)
 *   pmm::pptr<int> p = pmm.allocate_typed<int>(); // через неявное преобразование
 *   if (p) {
 *       *pmm.resolve(p) = 42;  // разыменование через экземпляр
 *   }
 *   pmm.deallocate_typed(p);
 * @endcode
 *
 * Пример использования с привязкой к типу менеджера (Issue #100, новый API):
 * @code
 *   using MyMgr = pmm::presets::SingleThreadedHeap;
 *   MyMgr pmm;
 *   pmm.create(64 * 1024);
 *
 *   // pptr привязан к типу менеджера через nested alias Manager::pptr<T>
 *   MyMgr::pptr<int> p = pmm.allocate_typed<int>();
 *   if (p) {
 *       *p.resolve(pmm) = 42;  // разыменование через экземпляр менеджера
 *   }
 *   pmm.deallocate_typed(p);
 * @endcode
 *
 * @see abstract_pmm.h — AbstractPersistMemoryManager::allocate_typed()
 * @see abstract_pmm.h — AbstractPersistMemoryManager::resolve()
 * @see legacy_manager.h — PersistMemoryManager<> (устаревший синглтон API)
 * @version 0.2 (Issue #100 — ManagerT parameter for type-safe manager binding)
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

/**
 * @brief Персистентный типизированный указатель (4 байта, гранульный индекс).
 *
 * Хранит гранульный индекс пользовательских данных, а не адрес.
 * Адресно-независим: корректно загружается из файла по другому базовому адресу.
 *
 * @tparam T Тип данных, на который указывает pptr.
 * @tparam ManagerT Тип менеджера (void = без привязки, обратная совместимость).
 *
 * Для разыменования требуется базовый указатель менеджера:
 *   - `mgr.resolve<T>(p)` или `p.resolve(mgr)` — для AbstractPersistMemoryManager (рекомендуется)
 *   - `p.get()`                                — для PersistMemoryManager<> (устарело, через синглтон)
 */
template <class T, class ManagerT = void> class pptr
{
    std::uint32_t _idx; ///< Гранульный индекс пользовательских данных (0 = null)

  public:
    /// @brief Тип данных, на который ссылается pptr.
    using element_type = T;

    /// @brief Тип менеджера, к которому привязан pptr (void = без привязки).
    using manager_type = ManagerT;

    constexpr pptr() noexcept : _idx( 0 ) {}
    constexpr explicit pptr( std::uint32_t idx ) noexcept : _idx( idx ) {}
    constexpr pptr( const pptr& ) noexcept           = default;
    constexpr pptr& operator=( const pptr& ) noexcept = default;
    ~pptr() noexcept                                   = default;

    /**
     * @brief Неявное преобразование из pptr<T, void> (обратная совместимость, Issue #100).
     *
     * Позволяет передавать старый pmm::pptr<T> в методы, ожидающие pptr<T, ManagerT>.
     * Только для ManagerT != void (в противном случае это был бы copy-конструктор).
     *
     * Пример:
     * @code
     *   pmm::pptr<int> old_ptr = ...; // pptr<int, void>
     *   Manager::pptr<int> new_ptr = old_ptr; // конвертируется
     * @endcode
     */
    template <typename M = ManagerT, typename std::enable_if<!std::is_void<M>::value, int>::type = 0>
    constexpr pptr( const pptr<T, void>& other ) noexcept : _idx( other.offset() )
    {
    }

    /**
     * @brief Неявное преобразование в pptr<T, void> (обратная совместимость, Issue #100).
     *
     * Позволяет хранить результат allocate_typed() (возвращает pptr<T, ManagerT>)
     * в переменной типа pmm::pptr<T> = pmm::pptr<T, void> без явного приведения.
     * Только для ManagerT != void (иначе это было бы самоприведение).
     *
     * Пример:
     * @code
     *   pmm::pptr<int> p = pmm.allocate_typed<int>(); // работает через неявное преобразование
     * @endcode
     */
    template <typename M = ManagerT, typename std::enable_if<!std::is_void<M>::value, int>::type = 0>
    constexpr operator pptr<T, void>() const noexcept
    {
        return pptr<T, void>( _idx );
    }

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
    constexpr std::uint32_t offset() const noexcept { return _idx; }

    /// @brief Сравнение персистентных указателей одного типа.
    constexpr bool operator==( const pptr& other ) const noexcept { return _idx == other._idx; }
    constexpr bool operator!=( const pptr& other ) const noexcept { return _idx != other._idx; }

    // ─── Разыменование через экземпляр менеджера (Issue #100) ─────────────────
    // Доступно только для pptr<T, ManagerT> где ManagerT != void.

    /**
     * @brief Разыменовать через экземпляр менеджера (Issue #100).
     *
     * Только для ManagerT != void. Эквивалентно mgr.resolve<T>(*this).
     *
     * @param mgr Экземпляр менеджера, с которым связан данный pptr.
     * @return T* — указатель на данные или nullptr если is_null().
     */
    template <typename M = ManagerT, typename std::enable_if<!std::is_void<M>::value, int>::type = 0>
    T* resolve( M& mgr ) const noexcept
    {
        return mgr.template resolve<T>( *this );
    }

    // ─── Устаревший API через синглтон (для PersistMemoryManager<>) ───────────
    // Для AbstractPersistMemoryManager используйте mgr.resolve<T>(p) или p.resolve(mgr).

    /// @brief Разыменовать через синглтон PersistMemoryManager<> (устарело).
    /// @deprecated Используйте mgr.resolve<T>(p) с AbstractPersistMemoryManager.
    inline T* get() const noexcept;

    /// @brief Доступ через синглтон (устарело).
    inline T& operator*() const noexcept { return *get(); }
    inline T* operator->() const noexcept { return get(); }

    /// @brief Доступ к i-му элементу с проверкой размера блока через синглтон (устарело).
    inline T* operator[]( std::size_t i ) const noexcept;

    /// @brief Доступ к i-му элементу без проверки границ через синглтон (устарело).
    inline T* get_at( std::size_t index ) const noexcept;
};

// pptr<T> должен быть 4 байта (uint32_t гранульный индекс) — ManagerT не хранится
static_assert( sizeof( pptr<int> ) == 4, "sizeof(pptr<T>) must be 4 bytes (Issue #59)" );
static_assert( sizeof( pptr<double> ) == 4, "sizeof(pptr<T>) must be 4 bytes (Issue #59)" );
static_assert( sizeof( pptr<int, void> ) == 4, "sizeof(pptr<T,void>) must be 4 bytes" );

} // namespace pmm
