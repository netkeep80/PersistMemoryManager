/**
 * @file pmm/address_traits.h
 * @brief AddressTraits — адресное пространство ПАП (Issue #87 Phase 1).
 *
 * Параметризует три взаимосвязанные характеристики адресного пространства:
 *   - `index_type`   — тип гранульного индекса (uint8_t / uint16_t / uint32_t / uint64_t)
 *   - `granule_size` — размер гранулы в байтах (степень двойки, 1..N)
 *   - `no_block`     — sentinel «нет блока» = максимальное значение index_type
 *
 * Обратная совместимость:
 *   pmm::kGranuleSize и pmm::detail::kNoBlock остаются в persist_memory_types.h,
 *   но теперь выводятся из DefaultAddressTraits через static_assert.
 *
 * @see plan_issue87.md §5 «Фаза 1: AddressTraits»
 * @version 0.1 (Issue #87 Phase 1)
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace pmm
{

/**
 * @brief Traits адресного пространства ПАП.
 *
 * @tparam IndexT     Тип гранульного индекса (uint8_t, uint16_t, uint32_t, uint64_t).
 * @tparam GranuleSz  Размер гранулы в байтах (степень двойки ≥ 1).
 *
 * Статические проверки (static_assert):
 *  - IndexT — беззнаковый целый тип.
 *  - GranuleSz — степень двойки (GranuleSz & (GranuleSz-1) == 0).
 *  - GranuleSz ≥ 1.
 */
template <typename IndexT, std::size_t GranuleSz> struct AddressTraits
{
    static_assert( std::is_unsigned<IndexT>::value, "AddressTraits: IndexT must be an unsigned integer type" );
    static_assert( GranuleSz >= 1, "AddressTraits: GranuleSz must be >= 1" );
    static_assert( ( GranuleSz & ( GranuleSz - 1 ) ) == 0, "AddressTraits: GranuleSz must be a power of 2" );

    /// Тип гранульного индекса.
    using index_type = IndexT;

    /// Размер гранулы в байтах.
    static constexpr std::size_t granule_size = GranuleSz;

    /// Sentinel «нет блока» — максимальное значение index_type.
    static constexpr index_type no_block = std::numeric_limits<IndexT>::max();

    // ─── Вспомогательные функции ───────────────────────────────────────────

    /**
     * @brief Перевести байты в гранулы (потолок).
     *
     * Возвращает 0 при переполнении.
     */
    static constexpr index_type bytes_to_granules( std::size_t bytes ) noexcept
    {
        if ( bytes == 0 )
            return static_cast<index_type>( 0 );
        // Overflow-safe ceiling division: (bytes + granule_size - 1) / granule_size
        if ( bytes > std::numeric_limits<std::size_t>::max() - ( granule_size - 1 ) )
            return static_cast<index_type>( 0 ); // overflow
        std::size_t granules = ( bytes + granule_size - 1 ) / granule_size;
        if ( granules > static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) )
            return static_cast<index_type>( 0 ); // overflow for IndexT
        return static_cast<index_type>( granules );
    }

    /**
     * @brief Перевести гранулы в байты.
     */
    static constexpr std::size_t granules_to_bytes( index_type granules ) noexcept
    {
        return static_cast<std::size_t>( granules ) * granule_size;
    }

    /**
     * @brief Получить байтовое смещение из гранульного индекса.
     */
    static constexpr std::size_t idx_to_byte_off( index_type idx ) noexcept
    {
        return static_cast<std::size_t>( idx ) * granule_size;
    }

    /**
     * @brief Получить гранульный индекс из байтового смещения (кратно granule_size).
     */
    static index_type byte_off_to_idx( std::size_t byte_off ) noexcept
    {
        assert( byte_off % granule_size == 0 );
        assert( byte_off / granule_size <= static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) );
        return static_cast<index_type>( byte_off / granule_size );
    }
};

// ─── Стандартные алиасы ────────────────────────────────────────────────────────

/// Минимальный 8-bit вариант (до 255 гранул по 8 байт = 2040 байт, embedded).
using TinyAddressTraits = AddressTraits<std::uint8_t, 8>;

/// 16-bit вариант (до 65535 гранул по 16 байт = ~1 МБ, small embedded).
using SmallAddressTraits = AddressTraits<std::uint16_t, 16>;

/// 32-bit вариант, 16-байтная гранула — текущий дефолт.
using DefaultAddressTraits = AddressTraits<std::uint32_t, 16>;

/// 64-bit вариант, 64-байтная гранула (для крупных промышленных БД).
using LargeAddressTraits = AddressTraits<std::uint64_t, 64>;

} // namespace pmm
