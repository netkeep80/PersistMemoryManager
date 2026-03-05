/**
 * @file pmm/storage_backend.h
 * @brief StorageBackend — концепт бэкенда хранилища ПАП (Issue #87 Phase 5).
 *
 * Определяет C++17 SFINAE-концепт `StorageBackendConcept<Backend>`,
 * которому должны соответствовать все реализации бэкендов:
 *   - `StaticStorage<Size, AddressTraitsT>` — буфер на стеке/глобально (static_storage.h)
 *   - `HeapStorage<AddressTraitsT>`         — динамическая память через malloc (heap_storage.h)
 *   - `MMapStorage<AddressTraitsT>`         — mmap/MapViewOfFile (mmap_storage.h)
 *
 * Минимальный интерфейс бэкенда:
 *   - `uint8_t* base_ptr()`         — указатель на начало управляемой области
 *   - `size_t   total_size() const` — текущий размер управляемой области в байтах
 *   - `bool     expand(size_t)`     — расширить область на указанное число байт
 *   - `bool     owns_memory() const`— true, если бэкенд владеет памятью и освободит её
 *
 * @see plan_issue87.md §5 «Фаза 5: StorageBackend — три бэкенда»
 * @version 0.1 (Issue #87 Phase 5)
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

// ─── SFINAE-утилиты для проверки концепта ─────────────────────────────────────

namespace detail
{

template <typename Backend, typename = void> struct has_base_ptr : std::false_type
{
};
template <typename Backend>
struct has_base_ptr<Backend, std::void_t<decltype( std::declval<Backend&>().base_ptr() )>> : std::true_type
{
};

template <typename Backend, typename = void> struct has_total_size : std::false_type
{
};
template <typename Backend>
struct has_total_size<Backend, std::void_t<decltype( std::declval<const Backend&>().total_size() )>> : std::true_type
{
};

template <typename Backend, typename = void> struct has_expand : std::false_type
{
};
template <typename Backend>
struct has_expand<Backend, std::void_t<decltype( std::declval<Backend&>().expand( std::declval<std::size_t>() ) )>>
    : std::true_type
{
};

template <typename Backend, typename = void> struct has_owns_memory : std::false_type
{
};
template <typename Backend>
struct has_owns_memory<Backend, std::void_t<decltype( std::declval<const Backend&>().owns_memory() )>> : std::true_type
{
};

} // namespace detail

/**
 * @brief Проверка, соответствует ли Backend концепту StorageBackend.
 *
 * Требует наличия:
 *   - `base_ptr()   -> uint8_t*`
 *   - `total_size() -> size_t` (const)
 *   - `expand(size_t) -> bool`
 *   - `owns_memory()  -> bool` (const)
 *
 * @tparam Backend  Тип, проверяемый на соответствие концепту.
 */
template <typename Backend>
inline constexpr bool is_storage_backend_v =
    detail::has_base_ptr<Backend>::value && detail::has_total_size<Backend>::value &&
    detail::has_expand<Backend>::value && detail::has_owns_memory<Backend>::value;

} // namespace pmm
