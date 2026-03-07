/**
 * @file pmm/manager_concept.h
 * @brief PersistMemoryManagerConcept — концепция для проверки типов менеджеров ПАП (Issue #100).
 *
 * Определяет концепцию `PersistMemoryManagerConcept` и вспомогательные type traits,
 * позволяющие проверить во время компиляции, является ли тип корректным менеджером
 * персистентной памяти (удовлетворяет ли интерфейсу AbstractPersistMemoryManager).
 *
 * Требования к типу менеджера:
 *   - `manager_type` — typedef на собственный тип (self-type)
 *   - `address_traits` — тип адресных traits
 *   - `storage_backend` — тип бэкенда хранилища
 *   - `free_block_tree` — тип политики дерева свободных блоков
 *   - `thread_policy` — тип политики многопоточности
 *   - `create()` / `create(size_t)` — инициализация
 *   - `load()` — загрузка состояния
 *   - `destroy()` — сброс состояния
 *   - `is_initialized()` — проверка инициализации
 *   - `allocate(size_t)` → void* — выделение сырой памяти
 *   - `deallocate(void*)` — освобождение сырой памяти
 *   - `allocate_typed<T>()` → pptr<T, manager_type> — типизированное выделение
 *   - `deallocate_typed(pptr<T>)` — типизированное освобождение
 *   - `resolve<T>(pptr<T>)` → T* — разыменование
 *   - `total_size()`, `used_size()`, `free_size()` — статистика
 *
 * Использование:
 * @code
 *   // Проверка во время компиляции
 *   static_assert(pmm::is_persist_memory_manager_v<pmm::presets::SingleThreadedHeap>,
 *                 "SingleThreadedHeap must be a valid PersistMemoryManager");
 *
 *   // Ограничение шаблонного параметра
 *   template <typename MgrT>
 *   void process(MgrT& mgr) {
 *       static_assert(pmm::is_persist_memory_manager_v<MgrT>);
 *       // ... работа с менеджером
 *   }
 * @endcode
 *
 * @see abstract_pmm.h — AbstractPersistMemoryManager (реализация)
 * @see pptr.h — pptr<T, ManagerT>
 * @version 0.1 (Issue #100 — Phase 1: Infrastructure Preparation)
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

// ─── SFINAE-детекторы для интерфейса менеджера ────────────────────────────────

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

template <typename T, typename = void> struct has_is_initialized : std::false_type
{
};
template <typename T>
struct has_is_initialized<T, std::void_t<decltype( std::declval<const T&>().is_initialized() )>> : std::true_type
{
};

template <typename T, typename = void> struct has_allocate_method : std::false_type
{
};
template <typename T>
struct has_allocate_method<T, std::void_t<decltype( std::declval<T&>().allocate( std::size_t{} ) )>> : std::true_type
{
};

template <typename T, typename = void> struct has_deallocate_method : std::false_type
{
};
template <typename T>
struct has_deallocate_method<T, std::void_t<decltype( std::declval<T&>().deallocate( static_cast<void*>( nullptr ) ) )>>
    : std::true_type
{
};

template <typename T, typename = void> struct has_total_size_method : std::false_type
{
};
template <typename T>
struct has_total_size_method<T, std::void_t<decltype( std::declval<const T&>().total_size() )>> : std::true_type
{
};

template <typename T, typename = void> struct has_destroy_method : std::false_type
{
};
template <typename T>
struct has_destroy_method<T, std::void_t<decltype( std::declval<T&>().destroy() )>> : std::true_type
{
};

/// @endcond

} // namespace mgr_concept
} // namespace detail

/**
 * @brief Проверить, является ли T корректным типом менеджера персистентной памяти.
 *
 * Тип считается менеджером ПАП, если он предоставляет:
 *   - typedef `manager_type`
 *   - typedef `address_traits`
 *   - typedef `storage_backend`
 *   - метод `is_initialized() const → bool`
 *   - метод `allocate(size_t) → void*`
 *   - метод `deallocate(void*)`
 *   - метод `total_size() const → size_t`
 *   - метод `destroy()`
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
