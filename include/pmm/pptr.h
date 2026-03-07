/**
 * @file pmm/pptr.h
 * @brief pptr<T> — персистентный типизированный указатель (Issue #97).
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
 * Разыменование:
 *   - Для AbstractPersistMemoryManager: mgr.resolve<T>(p) → T*
 *   - Для PersistMemoryManager<> (синглтон, устарело): p.get() → T*
 *
 * Пример использования с AbstractPersistMemoryManager (Issue #97):
 * @code
 *   pmm::presets::SingleThreadedHeap pmm;
 *   pmm.create(64 * 1024);
 *
 *   // Выделение (возвращает pptr<int>)
 *   pmm::pptr<int> p = pmm.allocate_typed<int>();
 *   if (p) {
 *       *pmm.resolve(p) = 42;  // разыменование через экземпляр
 *   }
 *   pmm.deallocate_typed(p);
 * @endcode
 *
 * @see abstract_pmm.h — AbstractPersistMemoryManager::allocate_typed()
 * @see abstract_pmm.h — AbstractPersistMemoryManager::resolve()
 * @see legacy_manager.h — PersistMemoryManager<> (устаревший синглтон API)
 * @version 0.1 (Issue #97 — extracted from legacy_manager.h)
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace pmm
{

/**
 * @brief Персистентный типизированный указатель (4 байта, гранульный индекс).
 *
 * Хранит гранульный индекс пользовательских данных, а не адрес.
 * Адресно-независим: корректно загружается из файла по другому базовому адресу.
 *
 * Для разыменования требуется базовый указатель менеджера:
 *   - `mgr.resolve<T>(p)`  — для AbstractPersistMemoryManager (рекомендуется)
 *   - `p.get()`            — для PersistMemoryManager<> (устарело, через синглтон)
 *
 * @tparam T Тип данных, на который указывает pptr.
 */
template <class T> class pptr
{
    std::uint32_t _idx; ///< Гранульный индекс пользовательских данных (0 = null)

  public:
    constexpr pptr() noexcept : _idx( 0 ) {}
    constexpr explicit pptr( std::uint32_t idx ) noexcept : _idx( idx ) {}
    constexpr pptr( const pptr<T>& ) noexcept               = default;
    constexpr pptr<T>& operator=( const pptr<T>& ) noexcept = default;
    ~pptr() noexcept                                        = default;

    // Адресная арифметика запрещена — pptr не является итератором
    pptr<T>& operator++()      = delete;
    pptr<T>  operator++( int ) = delete;
    pptr<T>& operator--()      = delete;
    pptr<T>  operator--( int ) = delete;

    /// @brief Проверить, является ли указатель нулевым.
    constexpr bool is_null() const noexcept { return _idx == 0; }

    /// @brief Явное преобразование в bool: true если не null.
    constexpr explicit operator bool() const noexcept { return _idx != 0; }

    /// @brief Получить гранульный индекс (для сохранения/восстановления).
    constexpr std::uint32_t offset() const noexcept { return _idx; }

    /// @brief Сравнение персистентных указателей.
    constexpr bool operator==( const pptr<T>& other ) const noexcept { return _idx == other._idx; }
    constexpr bool operator!=( const pptr<T>& other ) const noexcept { return _idx != other._idx; }

    // ─── Устаревший API через синглтон (для PersistMemoryManager<>) ───────────
    // Для AbstractPersistMemoryManager используйте mgr.resolve<T>(p).

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

// pptr<T> должен быть 4 байта (uint32_t гранульный индекс)
static_assert( sizeof( pptr<int> ) == 4, "sizeof(pptr<T>) must be 4 bytes (Issue #59)" );
static_assert( sizeof( pptr<double> ) == 4, "sizeof(pptr<T>) must be 4 bytes (Issue #59)" );

} // namespace pmm
