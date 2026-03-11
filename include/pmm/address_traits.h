/**
 * @file pmm/address_traits.h
 * @brief AddressTraits — адресное пространство ПАП (Issue #87 Phase 1, #146).
 *
 * Параметризует три взаимосвязанные характеристики адресного пространства:
 *   - `index_type`   — тип гранульного индекса (uint16_t / uint32_t / uint64_t)
 *   - `granule_size` — размер гранулы в байтах (степень двойки, минимум 4)
 *   - `no_block`     — sentinel «нет блока» = максимальное значение index_type
 *
 * Поддерживаемые размеры индекса (Issue #146):
 *   - uint16_t (SmallAddressTraits,   16B гранула) — до ~1 МБ, малые embedded-системы.
 *   - uint32_t (DefaultAddressTraits, 16B гранула) — до 64 ГБ, основной вариант.
 *   - uint64_t (LargeAddressTraits,   64B гранула) — до петабайт, крупные БД.
 *
 * Правила выбора конфигурации (Issue #146):
 *   1. granule_size >= 4 (минимальный размер слова архитектуры = kMinGranuleSize в manager_configs.h).
 *   2. granule_size — степень двойки.
 *   3. Для минимального расхода памяти выбирайте конфигурации без потерь:
 *      DefaultAddressTraits (Block=32B, granule=16B, 0 байт потерь на блок),
 *      LargeAddressTraits   (Block=64B, granule=64B, 0 байт потерь на блок).
 *      SmallAddressTraits допустима, но с потерями (Block=18B, granule=16B,
 *      ceil(18/16)=2 гранулы выделяется под заголовок = 14 байт потерь на блок).
 *
 * Недопустимые конфигурации (Issue #146):
 *   - uint8_t индекс (TinyAddressTraits удалена): максимум 255 гранул — практически
 *     непригодно для реальных сценариев использования менеджера ПАП.
 *
 * Обратная совместимость:
 *   pmm::kGranuleSize и pmm::detail::kNoBlock остаются в persist_memory_types.h,
 *   но теперь выводятся из DefaultAddressTraits через static_assert.
 *
 * @see manager_configs.h — ValidPmmAddressTraits концепт и правила конфигурации
 * @see plan_issue87.md §5 «Фаза 1: AddressTraits»
 * @version 0.2 (Issue #146 — removed TinyAddressTraits; documented valid index types and granule rules)
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
 * @tparam IndexT     Тип гранульного индекса (uint16_t, uint32_t, uint64_t).
 *                    Определяет максимально адресуемое пространство (2^bits * granule_size).
 *                    Рекомендуемые типы: uint16_t (embedded), uint32_t (desktop), uint64_t (large DB).
 * @tparam GranuleSz  Размер гранулы в байтах (степень двойки ≥ 4).
 *                    Минимальная единица адресации ПАП-менеджера.
 *
 * Статические проверки (static_assert):
 *  - IndexT — беззнаковый целый тип.
 *  - GranuleSz — степень двойки (GranuleSz & (GranuleSz-1) == 0).
 *  - GranuleSz ≥ 4 (минимальный размер слова архитектуры).
 */
template <typename IndexT, std::size_t GranuleSz> struct AddressTraits
{
    static_assert( std::is_unsigned<IndexT>::value, "AddressTraits: IndexT must be an unsigned integer type" );
    static_assert( GranuleSz >= 4, "AddressTraits: GranuleSz must be >= 4 (minimum architecture word size)" );
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

// ─── Стандартные алиасы (Issue #146) ──────────────────────────────────────────
//
// Допустимые конфигурации для менеджеров ПАП (Issue #146):
//   - SmallAddressTraits   — uint16_t, 16B гранула: до ~1 МБ, малые embedded-системы.
//   - DefaultAddressTraits — uint32_t, 16B гранула: до 64 ГБ, основной вариант (без потерь).
//   - LargeAddressTraits   — uint64_t, 64B гранула: петабайтный масштаб (без потерь).
//
// Удалено (Issue #146):
//   - TinyAddressTraits (uint8_t, 8B): максимум 255 гранул — практически
//     непригодно; uint8_t-индекс не поддерживается менеджерами ПАП.

/// 16-bit вариант (до 65535 гранул по 16 байт = ~1 МБ, small embedded).
/// Допустима с потерями: Block<SmallAddressTraits>=18B, ceil(18/16)=2 гранулы на заголовок.
using SmallAddressTraits = AddressTraits<std::uint16_t, 16>;

/// 32-bit вариант, 16-байтная гранула — текущий дефолт (без потерь: Block=32B=2*16B).
using DefaultAddressTraits = AddressTraits<std::uint32_t, 16>;

/// 64-bit вариант, 64-байтная гранула (без потерь: Block=64B=1*64B; для крупных БД).
using LargeAddressTraits = AddressTraits<std::uint64_t, 64>;

} // namespace pmm
