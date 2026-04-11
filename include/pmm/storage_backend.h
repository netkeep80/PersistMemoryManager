/**
 * @file pmm/storage_backend.h
 * @brief StorageBackend — концепт бэкенда хранилища ПАП (: phase 5, #129).
 *
 * Определяет C++20 концепт `StorageBackendConcept<Backend>`,
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
 * @version 0.2
 */

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

/**
 * @brief C++20 концепт: проверяет, соответствует ли Backend концепту StorageBackend.
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
concept StorageBackendConcept = requires( Backend& b, const Backend& cb, std::size_t n ) {
    { b.base_ptr() } -> std::convertible_to<std::uint8_t*>;
    { cb.total_size() } -> std::convertible_to<std::size_t>;
    { b.expand( n ) } -> std::convertible_to<bool>;
    { cb.owns_memory() } -> std::convertible_to<bool>;
};

/**
 * @brief Вспомогательная переменная: true если Backend удовлетворяет StorageBackendConcept.
 *
 * @tparam Backend  Тип, проверяемый на соответствие концепту.
 */
template <typename Backend> inline constexpr bool is_storage_backend_v = StorageBackendConcept<Backend>;

} // namespace pmm
