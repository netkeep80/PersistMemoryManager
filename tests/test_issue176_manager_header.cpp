/**
 * @file test_issue176_manager_header.cpp
 * @brief Tests: removal of obsolete ManagerHeader fields.
 *
 * Verifies:
 *  - #176-R1: prev_owns_memory and prev_base_ptr are no longer members of ManagerHeader.
 *  - #176-R2: ManagerHeader still compiles and has the correct 64-byte size.
 *  - #176-R3: image_version and root_offset occupy bytes previously held by removed/reserved fields.
 *  - #176-R4: load() correctly resets runtime-only fields (owns_memory, prev_total_size).
 *
 * @see include/pmm/types.h
 * @version 0.1
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

// ─── Test macros ──────────────────────────────────────────────────────────────

// ─── #176-R1: prev_owns_memory and prev_base_ptr removed ─────────────────────

// Verify at compile time that the removed fields no longer exist in ManagerHeader.
// This is checked via negative type-trait tests (SFINAE).
template <typename T, typename = void> struct has_prev_owns_memory : std::false_type
{
};
template <typename T>
struct has_prev_owns_memory<T, std::void_t<decltype( std::declval<T>().prev_owns_memory )>> : std::true_type
{
};

template <typename T, typename = void> struct has_prev_base_ptr : std::false_type
{
};
template <typename T>
struct has_prev_base_ptr<T, std::void_t<decltype( std::declval<T>().prev_base_ptr )>> : std::true_type
{
};

static_assert( !has_prev_owns_memory<pmm::detail::ManagerHeader<>>::value,
               "#176-R1: prev_owns_memory must be removed from ManagerHeader" );
static_assert( !has_prev_base_ptr<pmm::detail::ManagerHeader<>>::value,
               "#176-R1: prev_base_ptr must be removed from ManagerHeader" );

// ─── #176-R2: ManagerHeader is still 64 bytes ─────────────────────────────────

static_assert( sizeof( pmm::detail::ManagerHeader<> ) == 64,
               "#176-R2: ManagerHeader must still be exactly 64 bytes after field removal" );

// ─── #176-R3: image_version and root_offset presence ──────────────────────────

template <typename T, typename = void> struct has_image_version : std::false_type
{
};
template <typename T>
struct has_image_version<T, std::void_t<decltype( std::declval<T>().image_version )>> : std::true_type
{
};

template <typename T, typename = void> struct has_root_offset : std::false_type
{
};
template <typename T> struct has_root_offset<T, std::void_t<decltype( std::declval<T>().root_offset )>> : std::true_type
{
};

static_assert( has_image_version<pmm::detail::ManagerHeader<>>::value,
               "#329: image_version field must be present in ManagerHeader" );
static_assert( has_root_offset<pmm::detail::ManagerHeader<>>::value,
               "#200: root_offset field must be present in ManagerHeader (replaced _reserved)" );

// 4 bytes of _reserved were repurposed for crc32 field.
// Remaining 4 bytes of _reserved repurposed for root_offset (index_type).
// crc32 is 4 bytes, root_offset is 4 bytes (for DefaultAddressTraits) — total 8 bytes preserved.
static_assert( sizeof( pmm::detail::ManagerHeader<>::root_offset ) == 4,
               "#200: root_offset is 4 bytes for DefaultAddressTraits" );
static_assert( sizeof( pmm::detail::ManagerHeader<>::crc32 ) == 4, "#43-P2.1: crc32 must be exactly 4 bytes" );
static_assert( sizeof( pmm::detail::ManagerHeader<>::crc32 ) + sizeof( pmm::detail::ManagerHeader<>::root_offset ) == 8,
               "#200: crc32 + root_offset must total 8 bytes for DefaultAddressTraits" );

// ─── Manager alias for runtime tests ──────────────────────────────────────────

using M = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 176>;

// ─── #176-R4: load() resets runtime fields ─────────────────────────────────────

/// @brief After save/load round-trip, runtime-only fields are reset and manager is usable.
TEST_CASE( "#176-R4: load() resets runtime fields (save/load round-trip)", "[test_issue176_manager_header]" )
{
    const char* TEST_FILE = "test_issue176_load.dat";

    M::destroy();
    REQUIRE( M::create( 64 * 1024 ) );
    auto p = M::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    REQUIRE( pmm::save_manager<M>( TEST_FILE ) );
    M::destroy();

    REQUIRE( M::create( 64 * 1024 ) );
    {
        pmm::VerifyResult vr_;
        REQUIRE( pmm::load_manager_from_file<M>( TEST_FILE, vr_ ) );
    }
    REQUIRE( M::is_initialized() );

    M::destroy();
    std::remove( TEST_FILE );
}

/// @brief ManagerHeader fields have correct types and sizes (compile-time checks).
TEST_CASE( "#176-R3: ManagerHeader field types are correct", "[test_issue176_manager_header]" )
{
    static_assert( std::is_same_v<decltype( pmm::detail::ManagerHeader<>::owns_memory ), bool>,
                   "owns_memory must be bool" );
    static_assert( std::is_same_v<decltype( pmm::detail::ManagerHeader<>::image_version ), std::uint8_t>,
                   "image_version must be uint8_t" );
    static_assert( std::is_same_v<decltype( pmm::detail::ManagerHeader<>::granule_size ), std::uint16_t>,
                   "granule_size must be uint16_t" );
    static_assert( std::is_same_v<decltype( pmm::detail::ManagerHeader<>::prev_total_size ), std::uint64_t>,
                   "prev_total_size must be uint64_t" );
}

/// @brief create() and basic allocation work correctly after field removal.
TEST_CASE( "#176: basic alloc/dealloc after field removal", "[test_issue176_manager_header]" )
{
    M::destroy();
    REQUIRE( M::create( 64 * 1024 ) );
    REQUIRE( M::is_initialized() );

    auto p = M::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    int* ptr = M::resolve( p );
    REQUIRE( ptr != nullptr );
    *ptr = 42;
    REQUIRE( *ptr == 42 );

    M::deallocate_typed( p );
    M::destroy();
}
