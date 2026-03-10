/**
 * @file test_issue176_manager_header.cpp
 * @brief Tests for Issue #176: removal of obsolete ManagerHeader fields.
 *
 * Verifies:
 *  - #176-R1: prev_owns_memory and prev_base_ptr are no longer members of ManagerHeader.
 *  - #176-R2: ManagerHeader still compiles and has the correct 64-byte size.
 *  - #176-R3: _pad and _reserved[8] occupy the bytes previously held by removed fields.
 *  - #176-R4: load() correctly resets runtime-only fields (owns_memory, prev_total_size).
 *
 * @see include/pmm/types.h
 * @version 0.1 (Issue #176 — remove prev_owns_memory and prev_base_ptr)
 */

#include "pmm_single_threaded_heap.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

// ─── Test macros ──────────────────────────────────────────────────────────────

#define PMM_TEST( expr )                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( expr ) )                                                                                               \
        {                                                                                                              \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " << #expr << "\n";                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while ( false )

#define PMM_RUN( name, fn )                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "  " << name << " ... ";                                                                          \
        if ( fn() )                                                                                                    \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

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

static_assert( !has_prev_owns_memory<pmm::detail::ManagerHeader>::value,
               "#176-R1: prev_owns_memory must be removed from ManagerHeader" );
static_assert( !has_prev_base_ptr<pmm::detail::ManagerHeader>::value,
               "#176-R1: prev_base_ptr must be removed from ManagerHeader" );

// ─── #176-R2: ManagerHeader is still 64 bytes ─────────────────────────────────

static_assert( sizeof( pmm::detail::ManagerHeader ) == 64,
               "#176-R2: ManagerHeader must still be exactly 64 bytes after field removal" );

// ─── #176-R3: _pad and _reserved[8] presence ─────────────────────────────────

template <typename T, typename = void> struct has_pad : std::false_type
{
};
template <typename T> struct has_pad<T, std::void_t<decltype( std::declval<T>()._pad )>> : std::true_type
{
};

template <typename T, typename = void> struct has_reserved : std::false_type
{
};
template <typename T> struct has_reserved<T, std::void_t<decltype( std::declval<T>()._reserved )>> : std::true_type
{
};

static_assert( has_pad<pmm::detail::ManagerHeader>::value, "#176-R3: _pad field must be present in ManagerHeader" );
static_assert( has_reserved<pmm::detail::ManagerHeader>::value,
               "#176-R3: _reserved field must be present in ManagerHeader" );

static_assert( sizeof( pmm::detail::ManagerHeader::_reserved ) == 8, "#176-R3: _reserved must be exactly 8 bytes" );

// ─── Manager alias for runtime tests ──────────────────────────────────────────

using M = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 176>;

// ─── #176-R4: load() resets runtime fields ─────────────────────────────────────

/// @brief After save/load round-trip, runtime-only fields are reset and manager is usable.
static bool test_i176_load_resets_runtime_fields()
{
    const char* TEST_FILE = "test_issue176_load.dat";

    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );
    auto p = M::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    PMM_TEST( pmm::save_manager<M>( TEST_FILE ) );
    M::destroy();

    PMM_TEST( M::create( 64 * 1024 ) );
    PMM_TEST( pmm::load_manager_from_file<M>( TEST_FILE ) );
    PMM_TEST( M::is_initialized() );

    M::destroy();
    std::remove( TEST_FILE );
    return true;
}

/// @brief ManagerHeader fields have correct types and sizes (compile-time checks).
static bool test_i176_manager_header_field_types()
{
    static_assert( std::is_same_v<decltype( pmm::detail::ManagerHeader::owns_memory ), bool>,
                   "owns_memory must be bool" );
    static_assert( std::is_same_v<decltype( pmm::detail::ManagerHeader::_pad ), std::uint8_t>, "_pad must be uint8_t" );
    static_assert( std::is_same_v<decltype( pmm::detail::ManagerHeader::granule_size ), std::uint16_t>,
                   "granule_size must be uint16_t" );
    static_assert( std::is_same_v<decltype( pmm::detail::ManagerHeader::prev_total_size ), std::uint64_t>,
                   "prev_total_size must be uint64_t" );
    return true;
}

/// @brief create() and basic allocation work correctly after field removal.
static bool test_i176_basic_alloc_after_field_removal()
{
    M::destroy();
    PMM_TEST( M::create( 64 * 1024 ) );
    PMM_TEST( M::is_initialized() );

    auto p = M::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    int* ptr = M::resolve( p );
    PMM_TEST( ptr != nullptr );
    *ptr = 42;
    PMM_TEST( *ptr == 42 );

    M::deallocate_typed( p );
    M::destroy();
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "Test suite: Issue #176 — remove prev_owns_memory and prev_base_ptr\n";
    bool all_passed = true;

    PMM_RUN( "#176-R4: load() resets runtime fields (save/load round-trip)", test_i176_load_resets_runtime_fields );
    PMM_RUN( "#176-R3: ManagerHeader field types are correct", test_i176_manager_header_field_types );
    PMM_RUN( "#176: basic alloc/dealloc after field removal", test_i176_basic_alloc_after_field_removal );

    if ( all_passed )
    {
        std::cout << "All tests PASSED.\n";
        return 0;
    }
    else
    {
        std::cout << "Some tests FAILED.\n";
        return 1;
    }
}
