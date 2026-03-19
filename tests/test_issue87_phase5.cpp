/**
 * @file test_issue87_phase5.cpp
 * @brief Тесты Phase 5: StorageBackend — концепт и реализации (Issue #87).
 *
 * Проверяет:
 *  - is_storage_backend_v<StaticStorage<4096>> == true
 *  - is_storage_backend_v<HeapStorage<>> == true
 *  - is_storage_backend_v<MMapStorage<>> == true
 *  - Функциональность StaticStorage: base_ptr(), total_size(), expand(), owns_memory()
 *  - Функциональность HeapStorage: expand() из нулевого состояния
 *
 * @see include/pmm/storage_backend.h
 * @see include/pmm/static_storage.h
 * @see include/pmm/heap_storage.h
 * @see include/pmm/mmap_storage.h
 * @see plan_issue87.md §5 «Фаза 5: StorageBackend — три бэкенда»
 * @version 0.1 (Issue #87 Phase 5)
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// Phase 5 tests: StorageBackend concept and implementations
// =============================================================================

// ─── P5-A: Концепт is_storage_backend_v ──────────────────────────────────────

/// @brief StaticStorage<4096> соответствует концепту StorageBackend.
TEST_CASE( "P5-A1: StaticStorage<4096> satisfies StorageBackendConcept", "[test_issue87_phase5]" )
{
    static_assert( pmm::is_storage_backend_v<pmm::StaticStorage<4096>>,
                   "StaticStorage<4096> must satisfy StorageBackendConcept" );
}

/// @brief HeapStorage<> соответствует концепту StorageBackend.
TEST_CASE( "P5-A2: HeapStorage<> satisfies StorageBackendConcept", "[test_issue87_phase5]" )
{
    static_assert( pmm::is_storage_backend_v<pmm::HeapStorage<>>, "HeapStorage<> must satisfy StorageBackendConcept" );
}

/// @brief MMapStorage<> соответствует концепту StorageBackend.
TEST_CASE( "P5-A3: MMapStorage<> satisfies StorageBackendConcept", "[test_issue87_phase5]" )
{
    static_assert( pmm::is_storage_backend_v<pmm::MMapStorage<>>, "MMapStorage<> must satisfy StorageBackendConcept" );
}

/// @brief Тип без интерфейса не соответствует концепту.
TEST_CASE( "P5-A4: Non-backend type fails concept check", "[test_issue87_phase5]" )
{
    struct NotABackend
    {
        int x;
    };
    static_assert( !pmm::is_storage_backend_v<NotABackend>, "NotABackend must not satisfy StorageBackendConcept" );
    static_assert( !pmm::is_storage_backend_v<int>, "int must not satisfy StorageBackendConcept" );
}

// ─── P5-B: StaticStorage — функциональность ───────────────────────────────────

/// @brief StaticStorage<4096>: base_ptr(), total_size(), expand(), owns_memory().
TEST_CASE( "P5-B1: StaticStorage<4096> interface methods", "[test_issue87_phase5]" )
{
    pmm::StaticStorage<4096> storage;

    REQUIRE( storage.base_ptr() != nullptr );
    REQUIRE( storage.total_size() == 4096 );
    REQUIRE( storage.expand( 1024 ) == false ); // статический буфер нельзя расширить
    REQUIRE( storage.owns_memory() == false );  // не владеет динамической памятью
}

/// @brief StaticStorage<4096>: буфер выровнен по granule_size.
TEST_CASE( "P5-B2: StaticStorage<4096> buffer alignment", "[test_issue87_phase5]" )
{
    pmm::StaticStorage<4096> storage;
    std::uintptr_t           addr = reinterpret_cast<std::uintptr_t>( storage.base_ptr() );
    REQUIRE( addr % pmm::DefaultAddressTraits::granule_size == 0 );
}

/// @brief StaticStorage с AddressTraits<uint8_t, 8> (8-byte granula).
TEST_CASE( "P5-B3: StaticStorage<512, AddressTraits<uint8_t, 8>>", "[test_issue87_phase5]" )
{
    using AT8 = pmm::AddressTraits<std::uint8_t, 8>;
    pmm::StaticStorage<512, AT8> storage;
    REQUIRE( storage.base_ptr() != nullptr );
    REQUIRE( storage.total_size() == 512 );
    std::uintptr_t addr = reinterpret_cast<std::uintptr_t>( storage.base_ptr() );
    REQUIRE( addr % AT8::granule_size == 0 );
}

// ─── P5-C: HeapStorage — функциональность ─────────────────────────────────────

/// @brief HeapStorage<>: начальное состояние (пустой, без буфера).
TEST_CASE( "P5-C1: HeapStorage<> initial state (empty)", "[test_issue87_phase5]" )
{
    pmm::HeapStorage<> storage;
    REQUIRE( storage.base_ptr() == nullptr );
    REQUIRE( storage.total_size() == 0 );
    REQUIRE( storage.owns_memory() == false );
}

/// @brief HeapStorage<>: expand() из нулевого состояния.
TEST_CASE( "P5-C2: HeapStorage<> expand from zero", "[test_issue87_phase5]" )
{
    pmm::HeapStorage<> storage;
    REQUIRE( storage.expand( 4096 ) == true );
    REQUIRE( storage.base_ptr() != nullptr );
    REQUIRE( storage.total_size() >= 4096 );
    REQUIRE( storage.owns_memory() == true );
}

/// @brief HeapStorage<>: expand() из существующего буфера.
TEST_CASE( "P5-C3: HeapStorage<> expand existing buffer", "[test_issue87_phase5]" )
{
    pmm::HeapStorage<> storage;
    REQUIRE( storage.expand( 4096 ) == true );
    std::size_t old_size = storage.total_size();

    // Записываем данные и проверяем после расширения
    storage.base_ptr()[0] = 0xAB;
    storage.base_ptr()[1] = 0xCD;

    REQUIRE( storage.expand( 4096 ) == true );
    REQUIRE( storage.total_size() >= old_size );
    REQUIRE( storage.base_ptr()[0] == 0xAB ); // данные скопированы
    REQUIRE( storage.base_ptr()[1] == 0xCD );
}

/// @brief HeapStorage<>: конструктор с начальным размером.
TEST_CASE( "P5-C4: HeapStorage<> constructor with initial size", "[test_issue87_phase5]" )
{
    pmm::HeapStorage<> storage( 8192 );
    REQUIRE( storage.base_ptr() != nullptr );
    REQUIRE( storage.total_size() == 8192 );
    REQUIRE( storage.owns_memory() == true );
}

/// @brief HeapStorage<>: attach() внешнего буфера.
TEST_CASE( "P5-C5: HeapStorage<> attach external buffer", "[test_issue87_phase5]" )
{
    static std::uint8_t extern_buf[4096] = {};
    pmm::HeapStorage<>  storage;
    storage.attach( extern_buf, 4096 );
    REQUIRE( storage.base_ptr() == extern_buf );
    REQUIRE( storage.total_size() == 4096 );
    REQUIRE( storage.owns_memory() == false );
}

// ─── P5-D: MMapStorage — функциональность ─────────────────────────────────────

/// @brief MMapStorage<>: начальное состояние (не открыт).
TEST_CASE( "P5-D1: MMapStorage<> initial state (not open)", "[test_issue87_phase5]" )
{
    pmm::MMapStorage<> storage;
    REQUIRE( storage.base_ptr() == nullptr );
    REQUIRE( storage.total_size() == 0 );
    REQUIRE( storage.is_open() == false );
    REQUIRE( storage.owns_memory() == false );
    REQUIRE( storage.expand( 1024 ) == false ); // не поддерживается
}

// =============================================================================
// main
// =============================================================================
