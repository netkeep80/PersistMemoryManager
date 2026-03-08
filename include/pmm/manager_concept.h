/**
 * @file pmm/manager_concept.h
 * @brief PersistMemoryManagerConcept — концепция для проверки типов менеджеров ПАП (Issue #100, #110).
 *
 * Определяет концепцию `PersistMemoryManagerConcept` и вспомогательные type traits,
 * позволяющие проверить во время компиляции, является ли тип корректным менеджером
 * персистентной памяти (удовлетворяет ли статическому интерфейсу PersistMemoryManager).
 *
 * Требования к типу менеджера (Issue #110 — статический интерфейс):
 *   - `manager_type` — typedef на собственный тип (self-type)
 *   - `address_traits` — тип адресных traits
 *   - `storage_backend` — тип бэкенда хранилища
 *   - `free_block_tree` — тип политики дерева свободных блоков
 *   - `thread_policy` — тип политики многопоточности
 *   - статический `create(size_t)` — инициализация
 *   - статический `load()` — загрузка состояния
 *   - статический `destroy()` — сброс состояния
 *   - статический `is_initialized()` — проверка инициализации
 *   - статический `allocate(size_t)` → void* — выделение сырой памяти
 *   - статический `deallocate(void*)` — освобождение сырой памяти
 *   - статический `total_size()` → size_t — статистика
 *
 * Использование:
 * @code
 *   // Проверка во время компиляции
 *   static_assert(pmm::is_persist_memory_manager_v<pmm::PersistMemoryManager<pmm::CacheManagerConfig>>,
 *                 "PersistMemoryManager<CacheManagerConfig> must be a valid PersistMemoryManager");
 *
 *   // Ограничение шаблонного параметра
 *   template <typename MgrT>
 *   void process() {
 *       static_assert(pmm::is_persist_memory_manager_v<MgrT>);
 *       MgrT::create(64 * 1024);
 *       // ...
 *   }
 * @endcode
 *
 * @see persist_memory_manager.h — PersistMemoryManager (Issue #110)
 * @see pptr.h — pptr<T, ManagerT>
 * @version 0.2 (Issue #110 — обновлено для статического интерфейса PersistMemoryManager)
 */

#pragma once

#include <cstddef>
#include <type_traits>

namespace pmm
{

namespace detail
{
namespace mgr_concept
{

// ─── SFINAE-детекторы для статического интерфейса менеджера ──────────────────

/// @cond INTERNAL

template <typename T, typename = void> struct has_manager_type : std::false_type
{
};
template <typename T> struct has_manager_type<T, std::void_t<typename T::manager_type>> : std::true_type
{
};

template <typename T, typename = void> struct has_address_traits : std::false_type
{
};
template <typename T> struct has_address_traits<T, std::void_t<typename T::address_traits>> : std::true_type
{
};

template <typename T, typename = void> struct has_storage_backend_type : std::false_type
{
};
template <typename T> struct has_storage_backend_type<T, std::void_t<typename T::storage_backend>> : std::true_type
{
};

// Проверка статических методов через указатели на статические функции-члены
template <typename T, typename = void> struct has_is_initialized : std::false_type
{
};
template <typename T>
struct has_is_initialized<T, std::void_t<decltype( T::is_initialized() )>> : std::true_type
{
};

template <typename T, typename = void> struct has_allocate_method : std::false_type
{
};
template <typename T>
struct has_allocate_method<T, std::void_t<decltype( T::allocate( std::size_t{} ) )>> : std::true_type
{
};

template <typename T, typename = void> struct has_deallocate_method : std::false_type
{
};
template <typename T>
struct has_deallocate_method<T, std::void_t<decltype( T::deallocate( static_cast<void*>( nullptr ) ) )>>
    : std::true_type
{
};

template <typename T, typename = void> struct has_total_size_method : std::false_type
{
};
template <typename T>
struct has_total_size_method<T, std::void_t<decltype( T::total_size() )>> : std::true_type
{
};

template <typename T, typename = void> struct has_destroy_method : std::false_type
{
};
template <typename T>
struct has_destroy_method<T, std::void_t<decltype( T::destroy() )>> : std::true_type
{
};

/// @endcond

} // namespace mgr_concept
} // namespace detail

/**
 * @brief Проверить, является ли T корректным типом менеджера персистентной памяти.
 *
 * Тип считается менеджером ПАП, если он предоставляет (Issue #110 — статический интерфейс):
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
struct is_persist_memory_manager
    : std::bool_constant<
          detail::mgr_concept::has_manager_type<T>::value && detail::mgr_concept::has_address_traits<T>::value &&
          detail::mgr_concept::has_storage_backend_type<T>::value &&
          detail::mgr_concept::has_is_initialized<T>::value && detail::mgr_concept::has_allocate_method<T>::value &&
          detail::mgr_concept::has_deallocate_method<T>::value &&
          detail::mgr_concept::has_total_size_method<T>::value && detail::mgr_concept::has_destroy_method<T>::value>
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
template <typename T> inline constexpr bool is_persist_memory_manager_v = is_persist_memory_manager<T>::value;

} // namespace pmm
