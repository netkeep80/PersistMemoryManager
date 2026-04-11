/**
 * @file pmm/manager_concept.h
 * @brief PersistMemoryManagerConcept — концепция для проверки типов менеджеров ПАП.
 *
 * Определяет C++20 концепт `PersistMemoryManagerConcept<T>` и вспомогательный тип-трейт
 * `is_persist_memory_manager<T>`, позволяющие проверить во время компиляции, является ли
 * тип корректным менеджером персистентной памяти (удовлетворяет ли статическому
 * интерфейсу PersistMemoryManager).
 *
 * Требования к типу менеджера:
 *   - `manager_type` — typedef на собственный тип (self-type)
 *   - `address_traits` — тип адресных traits
 *   - `storage_backend` — тип бэкенда хранилища
 *   - статический `is_initialized()` — проверка инициализации
 *   - статический `allocate(size_t)` → void* — выделение сырой памяти
 *   - статический `deallocate(void*)` — освобождение сырой памяти
 *   - статический `total_size()` → size_t — статистика
 *   - статический `destroy()` — сброс состояния
 *
 * Использование:
 * @code
 *   // Проверка во время компиляции
 *   static_assert(pmm::is_persist_memory_manager_v<pmm::PersistMemoryManager<pmm::CacheManagerConfig>>,
 *                 "PersistMemoryManager<CacheManagerConfig> must be a valid PersistMemoryManager");
 *
 *   // Ограничение шаблонного параметра через C++20 концепт
 *   template <pmm::PersistMemoryManagerConcept MgrT>
 *   void process() {
 *       MgrT::create(64 * 1024);
 *       // ...
 *   }
 * @endcode
 *
 * @see persist_memory_manager.h — PersistMemoryManager
 * @see pptr.h — pptr<T, ManagerT>
 * @version 0.3
 */

#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace pmm
{

/**
 * @brief C++20 концепт: проверяет, является ли T корректным менеджером персистентной памяти.
 *
 * Тип считается менеджером ПАП, если он предоставляет:
 *   - typedef `manager_type`
 *   - typedef `address_traits`
 *   - typedef `storage_backend`
 *   - статический метод `is_initialized() → bool`
 *   - статический метод `allocate(size_t) → void*`
 *   - статический метод `deallocate(void*)`
 *   - статический метод `total_size() → size_t`
 *   - статический метод `destroy()`
 *
 * @tparam T Проверяемый тип.
 */
template <typename T>
concept PersistMemoryManagerConcept = requires {
    typename T::manager_type;
    typename T::address_traits;
    typename T::storage_backend;
    { T::is_initialized() };
    { T::allocate( std::size_t{} ) } -> std::convertible_to<void*>;
    { T::deallocate( static_cast<void*>( nullptr ) ) };
    { T::total_size() } -> std::convertible_to<std::size_t>;
    { T::destroy() };
};

/**
 * @brief Проверить, является ли T корректным типом менеджера персистентной памяти.
 *
 * Совместимый с C++17 тип-трейт на основе C++20 концепта PersistMemoryManagerConcept.
 *
 * @tparam T Проверяемый тип.
 */
template <typename T> struct is_persist_memory_manager : std::bool_constant<PersistMemoryManagerConcept<T>>
{
};

/**
 * @brief Вспомогательная переменная для is_persist_memory_manager.
 *
 * @tparam T Проверяемый тип.
 *
 * @code
 *   if constexpr (pmm::is_persist_memory_manager_v<MyType>) { ... }
 * @endcode
 */
template <typename T> inline constexpr bool is_persist_memory_manager_v = PersistMemoryManagerConcept<T>;

} // namespace pmm
