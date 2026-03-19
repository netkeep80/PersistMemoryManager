/**
 * @file test_issue87_phase1.cpp
 * @brief Тесты Phase 1: AddressTraits<IndexType, GranuleSize> (Issue #87).
 *
 * Проверяет:
 *  - Корректность static-полей для 8/16/32/64-bit адресации.
 *  - Конвертационные функции bytes_to_granules/granules_to_bytes/idx_to_byte_off/byte_off_to_idx.
 *  - static_assert'ы: степень двойки, беззнаковый тип.
 *  - DefaultAddressTraits идентичен текущим константам (обратная совместимость).
 *  - Стандартные алиасы: SmallAddressTraits, LargeAddressTraits.
 *
 * @see include/pmm/address_traits.h
 * @see plan_issue87.md §5 «Фаза 1: AddressTraits»
 * @version 0.1 (Issue #87 Phase 1)
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// Phase 1 tests: AddressTraits
// =============================================================================

// ─── P1-A: Статические поля и типы ──────────────────────────────────────────

/// @brief Проверяем 8-bit адресацию с 8-байтной гранулой (AddressTraits<uint8_t, 8>).
TEST_CASE( "P1-A1: AddressTraits<uint8_t, 8>", "[test_issue87_phase1]" )
{
    using A = pmm::AddressTraits<std::uint8_t, 8>;

    // Тип индекса
    static_assert( std::is_same<A::index_type, std::uint8_t>::value, "index_type must be uint8_t" );

    // Размер гранулы
    static_assert( A::granule_size == 8, "granule_size must be 8" );

    // no_block = max(uint8_t) = 0xFF
    static_assert( A::no_block == 0xFFU, "no_block must be 0xFF for uint8_t" );
    static_assert( A::no_block == std::numeric_limits<std::uint8_t>::max(), "no_block must be max(index_type)" );
}

/// @brief Проверяем 16-bit адресацию с 16-байтной гранулой (SmallAddressTraits).
TEST_CASE( "P1-A2: SmallAddressTraits (uint16_t, 16)", "[test_issue87_phase1]" )
{
    using A = pmm::AddressTraits<std::uint16_t, 16>;

    static_assert( std::is_same<A::index_type, std::uint16_t>::value, "index_type must be uint16_t" );
    static_assert( A::granule_size == 16, "granule_size must be 16" );

    // no_block = max(uint16_t) = 0xFFFF
    static_assert( A::no_block == 0xFFFFU, "no_block must be 0xFFFF for uint16_t" );
    static_assert( A::no_block == std::numeric_limits<std::uint16_t>::max(), "no_block must be max(index_type)" );

    static_assert( std::is_same<pmm::SmallAddressTraits, A>::value,
                   "SmallAddressTraits must be AddressTraits<uint16_t,16>" );
}

/// @brief Проверяем 32-bit адресацию с 16-байтной гранулой (DefaultAddressTraits).
TEST_CASE( "P1-A3: DefaultAddressTraits (uint32_t, 16)", "[test_issue87_phase1]" )
{
    using A32 = pmm::AddressTraits<std::uint32_t, 16>;

    static_assert( std::is_same<A32::index_type, std::uint32_t>::value, "index_type must be uint32_t" );
    static_assert( A32::granule_size == 16, "granule_size must be 16" );

    // no_block = max(uint32_t) = 0xFFFFFFFF
    static_assert( A32::no_block == 0xFFFFFFFFU, "no_block must be 0xFFFFFFFF for uint32_t" );

    // DefaultAddressTraits — алиас для A32
    static_assert( std::is_same<pmm::DefaultAddressTraits, A32>::value,
                   "DefaultAddressTraits must be AddressTraits<uint32_t,16>" );
}

/// @brief Проверяем 64-bit адресацию с 64-байтной гранулой (LargeAddressTraits).
TEST_CASE( "P1-A4: LargeAddressTraits (uint64_t, 64)", "[test_issue87_phase1]" )
{
    using A = pmm::AddressTraits<std::uint64_t, 64>;

    static_assert( std::is_same<A::index_type, std::uint64_t>::value, "index_type must be uint64_t" );
    static_assert( A::granule_size == 64, "granule_size must be 64" );

    static_assert( A::no_block == std::numeric_limits<std::uint64_t>::max(), "no_block must be max(uint64_t)" );

    static_assert( std::is_same<pmm::LargeAddressTraits, A>::value,
                   "LargeAddressTraits must be AddressTraits<uint64_t,64>" );
}

// ─── P1-B: Функция bytes_to_granules ────────────────────────────────────────

/// @brief Проверяем bytes_to_granules для 8-bit адресации (8-байтная гранула).
TEST_CASE( "P1-B1: bytes_to_granules (tiny, 8-byte granule)", "[test_issue87_phase1]" )
{
    using A = pmm::AddressTraits<std::uint8_t, 8>;

    // Основные случаи
    REQUIRE( A::bytes_to_granules( 0 ) == 0 );
    REQUIRE( A::bytes_to_granules( 1 ) == 1 );  // ceiling(1/8) = 1
    REQUIRE( A::bytes_to_granules( 8 ) == 1 );  // ceiling(8/8) = 1
    REQUIRE( A::bytes_to_granules( 9 ) == 2 );  // ceiling(9/8) = 2
    REQUIRE( A::bytes_to_granules( 16 ) == 2 ); // ceiling(16/8) = 2
    REQUIRE( A::bytes_to_granules( 17 ) == 3 ); // ceiling(17/8) = 3

    // Максимум без переполнения: 254*8 = 2032 байт → 254 гранулы
    REQUIRE( A::bytes_to_granules( 254 * 8 ) == 254 );
}

/// @brief Проверяем bytes_to_granules для 32-bit адресации (16-байтная гранула).
TEST_CASE( "P1-B2: bytes_to_granules (default, 16-byte granule)", "[test_issue87_phase1]" )
{
    using A = pmm::DefaultAddressTraits;

    REQUIRE( A::bytes_to_granules( 0 ) == 0 );
    REQUIRE( A::bytes_to_granules( 1 ) == 1 );  // ceiling(1/16) = 1
    REQUIRE( A::bytes_to_granules( 16 ) == 1 ); // ceiling(16/16) = 1
    REQUIRE( A::bytes_to_granules( 17 ) == 2 ); // ceiling(17/16) = 2
    REQUIRE( A::bytes_to_granules( 32 ) == 2 ); // ceiling(32/16) = 2
    REQUIRE( A::bytes_to_granules( 33 ) == 3 ); // ceiling(33/16) = 3

    // Совместимость с текущими pmm::detail::bytes_to_granules
    REQUIRE( A::bytes_to_granules( 16 ) == pmm::detail::bytes_to_granules( 16 ) );
    REQUIRE( A::bytes_to_granules( 17 ) == pmm::detail::bytes_to_granules( 17 ) );
    REQUIRE( A::bytes_to_granules( 128 ) == pmm::detail::bytes_to_granules( 128 ) );
}

// ─── P1-C: Функция granules_to_bytes ────────────────────────────────────────

/// @brief Проверяем granules_to_bytes для разных AddressTraits.
TEST_CASE( "P1-C:  granules_to_bytes", "[test_issue87_phase1]" )
{
    using A8  = pmm::AddressTraits<std::uint8_t, 8>;
    using A32 = pmm::DefaultAddressTraits;

    // 8-байтная гранула
    REQUIRE( A8::granules_to_bytes( 0 ) == 0 );
    REQUIRE( A8::granules_to_bytes( 1 ) == 8 );
    REQUIRE( A8::granules_to_bytes( 2 ) == 16 );
    REQUIRE( A8::granules_to_bytes( 10 ) == 80 );

    // 16-байтная гранула (совместимость с текущими)
    REQUIRE( A32::granules_to_bytes( 1 ) == 16 );
    REQUIRE( A32::granules_to_bytes( 2 ) == 32 );
    REQUIRE( A32::granules_to_bytes( 1 ) == pmm::detail::granules_to_bytes( 1 ) );
    REQUIRE( A32::granules_to_bytes( 100 ) == pmm::detail::granules_to_bytes( 100 ) );
}

// ─── P1-D: Функции idx_to_byte_off / byte_off_to_idx ────────────────────────

/// @brief Проверяем idx_to_byte_off и byte_off_to_idx (обратность).
TEST_CASE( "P1-D:  idx_to_byte_off / byte_off_to_idx roundtrip", "[test_issue87_phase1]" )
{
    using A8  = pmm::AddressTraits<std::uint8_t, 8>;
    using A16 = pmm::SmallAddressTraits;
    using A32 = pmm::DefaultAddressTraits;

    // 8-bit
    REQUIRE( A8::idx_to_byte_off( 0 ) == 0 );
    REQUIRE( A8::idx_to_byte_off( 1 ) == 8 );
    REQUIRE( A8::idx_to_byte_off( 10 ) == 80 );
    REQUIRE( A8::byte_off_to_idx( 0 ) == 0 );
    REQUIRE( A8::byte_off_to_idx( 8 ) == 1 );
    REQUIRE( A8::byte_off_to_idx( 80 ) == 10 );

    // 16-bit
    REQUIRE( A16::idx_to_byte_off( 5 ) == 80 );
    REQUIRE( A16::byte_off_to_idx( 80 ) == 5 );

    // 32-bit (совместимость с текущими)
    for ( std::uint32_t idx : { 0u, 1u, 10u, 100u, 1000u } )
    {
        REQUIRE( A32::idx_to_byte_off( idx ) == pmm::detail::idx_to_byte_off( idx ) );
        std::size_t byte_off = A32::idx_to_byte_off( idx );
        REQUIRE( A32::byte_off_to_idx( byte_off ) == pmm::detail::byte_off_to_idx( byte_off ) );
        REQUIRE( A32::byte_off_to_idx( byte_off ) == idx ); // roundtrip
    }
}

// ─── P1-E: Обратная совместимость с persist_memory_types.h ──────────────────

/// @brief Проверяем, что DefaultAddressTraits совместим с текущими константами.
TEST_CASE( "P1-E: DefaultAddressTraits matches current constants", "[test_issue87_phase1]" )
{
    using A = pmm::DefaultAddressTraits;

    // granule_size соответствует pmm::kGranuleSize
    static_assert( A::granule_size == pmm::kGranuleSize,
                   "DefaultAddressTraits::granule_size must match pmm::kGranuleSize" );

    // no_block соответствует pmm::detail::kNoBlock
    static_assert( A::no_block == pmm::detail::kNoBlock,
                   "DefaultAddressTraits::no_block must match pmm::detail::kNoBlock" );

    // index_type = uint32_t (как в BlockHeader)
    static_assert( std::is_same<A::index_type, std::uint32_t>::value,
                   "DefaultAddressTraits::index_type must be uint32_t" );

    // Все конвертационные функции совпадают с текущими
    for ( std::size_t bytes :
          { std::size_t( 1 ), std::size_t( 16 ), std::size_t( 17 ), std::size_t( 256 ), std::size_t( 1024 ) } )
    {
        REQUIRE( A::bytes_to_granules( bytes ) == pmm::detail::bytes_to_granules( bytes ) );
        REQUIRE( A::granules_to_bytes( static_cast<std::uint32_t>( bytes / 16 ) ) ==
                 pmm::detail::granules_to_bytes( static_cast<std::uint32_t>( bytes / 16 ) ) );
    }
}

// ─── P1-F: Разнообразные размеры гранул ─────────────────────────────────────

/// @brief Проверяем, что AddressTraits работает с разными степенями двойки (Issue #146: min 4 bytes).
TEST_CASE( "P1-F: Various power-of-2 granule sizes (1, 4, 32, 512)", "[test_issue87_phase1]" )
{
    // 4-байтная гранула — минимальный допустимый размер (Issue #146: kMinGranuleSize = 4)
    using A4 = pmm::AddressTraits<std::uint32_t, 4>;
    static_assert( A4::granule_size == 4 );
    REQUIRE( A4::bytes_to_granules( 4 ) == 1 );
    REQUIRE( A4::bytes_to_granules( 5 ) == 2 ); // ceiling(5/4) = 2

    // 32-байтная гранула
    using A32bytes = pmm::AddressTraits<std::uint32_t, 32>;
    static_assert( A32bytes::granule_size == 32 );
    REQUIRE( A32bytes::bytes_to_granules( 32 ) == 1 );
    REQUIRE( A32bytes::bytes_to_granules( 33 ) == 2 );

    // 512-байтная гранула (сектор диска)
    using A512 = pmm::AddressTraits<std::uint32_t, 512>;
    static_assert( A512::granule_size == 512 );
    REQUIRE( A512::bytes_to_granules( 512 ) == 1 );
    REQUIRE( A512::bytes_to_granules( 513 ) == 2 );
    REQUIRE( A512::granules_to_bytes( 1 ) == 512 );
}

// =============================================================================
// main
// =============================================================================
