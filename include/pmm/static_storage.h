/**
 * @file pmm/static_storage.h
 * @brief StaticStorage — статический бэкенд хранилища ПАП (: phase 5).
 *
 * Хранит управляемую область в фиксированном буфере размером `Size` байт,
 * расположенном внутри объекта (подходит для глобальных объектов и стека).
 *
 * Ограничения:
 *   - Размер фиксирован на этапе компиляции — expand() всегда возвращает false.
 *   - Не владеет памятью в смысле dynamic allocation — owns_memory() == false.
 *   - Требует выравнивания буфера по `AddressTraitsT::granule_size`.
 *
 * Применение: embedded-системы, тесты с маленьким фиксированным пулом,
 * быстрые unit-тесты без динамической памяти.
 *
 * @tparam Size           Размер буфера в байтах (compile-time константа).
 * @tparam AddressTraitsT Traits адресного пространства (из address_traits.h).
 *
 * @see storage_backend.h — концепт StorageBackend
 * @version 0.1
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/storage_backend.h"

#include <cstddef>
#include <cstdint>

namespace pmm
{

/**
 * @brief Статический бэкенд хранилища — фиксированный буфер внутри объекта.
 *
 * @tparam Size           Размер буфера в байтах.
 * @tparam AddressTraitsT Traits адресного пространства (определяет выравнивание).
 */
template <std::size_t Size, typename AddressTraitsT = DefaultAddressTraits> class StaticStorage
{
    static_assert( Size > 0, "StaticStorage: Size must be > 0" );
    static_assert( Size % AddressTraitsT::granule_size == 0, "StaticStorage: Size must be a multiple of granule_size" );

  public:
    using address_traits = AddressTraitsT;

    StaticStorage() noexcept                         = default;
    StaticStorage( const StaticStorage& )            = delete;
    StaticStorage& operator=( const StaticStorage& ) = delete;
    StaticStorage( StaticStorage&& )                 = delete;
    StaticStorage& operator=( StaticStorage&& )      = delete;

    /// @brief Указатель на начало буфера.
    std::uint8_t*       base_ptr() noexcept { return _buffer; }
    const std::uint8_t* base_ptr() const noexcept { return _buffer; }

    /// @brief Текущий размер буфера (фиксированный = Size).
    constexpr std::size_t total_size() const noexcept { return Size; }

    /// @brief Расширить хранилище — невозможно для статического буфера.
    /// @return Всегда false.
    bool expand( std::size_t /*additional_bytes*/ ) noexcept { return false; }

    /// @brief StaticStorage не владеет динамически выделенной памятью.
    /// @return Всегда false.
    constexpr bool owns_memory() const noexcept { return false; }

  private:
    alignas( AddressTraitsT::granule_size ) std::uint8_t _buffer[Size]{};
};

// ─── static_assert: StaticStorage соответствует концепту StorageBackend ────────

static_assert( is_storage_backend_v<StaticStorage<64>>, "StaticStorage must satisfy StorageBackendConcept" );

} // namespace pmm
