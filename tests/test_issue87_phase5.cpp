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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

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

// =============================================================================
// Phase 5 tests: StorageBackend concept and implementations
// =============================================================================

// ─── P5-A: Концепт is_storage_backend_v ──────────────────────────────────────

/// @brief StaticStorage<4096> соответствует концепту StorageBackend.
static bool test_p5_static_storage_satisfies_concept()
{
    static_assert( pmm::is_storage_backend_v<pmm::StaticStorage<4096>>,
                   "StaticStorage<4096> must satisfy StorageBackendConcept" );
    return true;
}

/// @brief HeapStorage<> соответствует концепту StorageBackend.
static bool test_p5_heap_storage_satisfies_concept()
{
    static_assert( pmm::is_storage_backend_v<pmm::HeapStorage<>>, "HeapStorage<> must satisfy StorageBackendConcept" );
    return true;
}

/// @brief MMapStorage<> соответствует концепту StorageBackend.
static bool test_p5_mmap_storage_satisfies_concept()
{
    static_assert( pmm::is_storage_backend_v<pmm::MMapStorage<>>, "MMapStorage<> must satisfy StorageBackendConcept" );
    return true;
}

/// @brief Тип без интерфейса не соответствует концепту.
static bool test_p5_non_backend_type_fails_concept()
{
    struct NotABackend
    {
        int x;
    };
    static_assert( !pmm::is_storage_backend_v<NotABackend>, "NotABackend must not satisfy StorageBackendConcept" );
    static_assert( !pmm::is_storage_backend_v<int>, "int must not satisfy StorageBackendConcept" );
    return true;
}

// ─── P5-B: StaticStorage — функциональность ───────────────────────────────────

/// @brief StaticStorage<4096>: base_ptr(), total_size(), expand(), owns_memory().
static bool test_p5_static_storage_interface()
{
    pmm::StaticStorage<4096> storage;

    PMM_TEST( storage.base_ptr() != nullptr );
    PMM_TEST( storage.total_size() == 4096 );
    PMM_TEST( storage.expand( 1024 ) == false ); // статический буфер нельзя расширить
    PMM_TEST( storage.owns_memory() == false );  // не владеет динамической памятью

    return true;
}

/// @brief StaticStorage<4096>: буфер выровнен по granule_size.
static bool test_p5_static_storage_alignment()
{
    pmm::StaticStorage<4096> storage;
    std::uintptr_t           addr = reinterpret_cast<std::uintptr_t>( storage.base_ptr() );
    PMM_TEST( addr % pmm::DefaultAddressTraits::granule_size == 0 );
    return true;
}

/// @brief StaticStorage с TinyAddressTraits (8-byte granula).
static bool test_p5_static_storage_tiny_traits()
{
    pmm::StaticStorage<512, pmm::TinyAddressTraits> storage;
    PMM_TEST( storage.base_ptr() != nullptr );
    PMM_TEST( storage.total_size() == 512 );
    std::uintptr_t addr = reinterpret_cast<std::uintptr_t>( storage.base_ptr() );
    PMM_TEST( addr % pmm::TinyAddressTraits::granule_size == 0 );
    return true;
}

// ─── P5-C: HeapStorage — функциональность ─────────────────────────────────────

/// @brief HeapStorage<>: начальное состояние (пустой, без буфера).
static bool test_p5_heap_storage_initial_state()
{
    pmm::HeapStorage<> storage;
    PMM_TEST( storage.base_ptr() == nullptr );
    PMM_TEST( storage.total_size() == 0 );
    PMM_TEST( storage.owns_memory() == false );
    return true;
}

/// @brief HeapStorage<>: expand() из нулевого состояния.
static bool test_p5_heap_storage_expand_from_zero()
{
    pmm::HeapStorage<> storage;
    PMM_TEST( storage.expand( 4096 ) == true );
    PMM_TEST( storage.base_ptr() != nullptr );
    PMM_TEST( storage.total_size() >= 4096 );
    PMM_TEST( storage.owns_memory() == true );
    return true;
}

/// @brief HeapStorage<>: expand() из существующего буфера.
static bool test_p5_heap_storage_expand_existing()
{
    pmm::HeapStorage<> storage;
    PMM_TEST( storage.expand( 4096 ) == true );
    std::size_t old_size = storage.total_size();

    // Записываем данные и проверяем после расширения
    storage.base_ptr()[0] = 0xAB;
    storage.base_ptr()[1] = 0xCD;

    PMM_TEST( storage.expand( 4096 ) == true );
    PMM_TEST( storage.total_size() >= old_size );
    PMM_TEST( storage.base_ptr()[0] == 0xAB ); // данные скопированы
    PMM_TEST( storage.base_ptr()[1] == 0xCD );

    return true;
}

/// @brief HeapStorage<>: конструктор с начальным размером.
static bool test_p5_heap_storage_constructor_with_size()
{
    pmm::HeapStorage<> storage( 8192 );
    PMM_TEST( storage.base_ptr() != nullptr );
    PMM_TEST( storage.total_size() == 8192 );
    PMM_TEST( storage.owns_memory() == true );
    return true;
}

/// @brief HeapStorage<>: attach() внешнего буфера.
static bool test_p5_heap_storage_attach()
{
    static std::uint8_t extern_buf[4096] = {};
    pmm::HeapStorage<>  storage;
    storage.attach( extern_buf, 4096 );
    PMM_TEST( storage.base_ptr() == extern_buf );
    PMM_TEST( storage.total_size() == 4096 );
    PMM_TEST( storage.owns_memory() == false );
    return true;
}

// ─── P5-D: MMapStorage — функциональность ─────────────────────────────────────

/// @brief MMapStorage<>: начальное состояние (не открыт).
static bool test_p5_mmap_storage_initial_state()
{
    pmm::MMapStorage<> storage;
    PMM_TEST( storage.base_ptr() == nullptr );
    PMM_TEST( storage.total_size() == 0 );
    PMM_TEST( storage.is_open() == false );
    PMM_TEST( storage.owns_memory() == false );
    PMM_TEST( storage.expand( 1024 ) == false ); // не поддерживается
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase5 (Phase 5: StorageBackend) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P5-A: StorageBackend concept ---\n";
    PMM_RUN( "P5-A1: StaticStorage<4096> satisfies StorageBackendConcept", test_p5_static_storage_satisfies_concept );
    PMM_RUN( "P5-A2: HeapStorage<> satisfies StorageBackendConcept", test_p5_heap_storage_satisfies_concept );
    PMM_RUN( "P5-A3: MMapStorage<> satisfies StorageBackendConcept", test_p5_mmap_storage_satisfies_concept );
    PMM_RUN( "P5-A4: Non-backend type fails concept check", test_p5_non_backend_type_fails_concept );

    std::cout << "\n--- P5-B: StaticStorage functional ---\n";
    PMM_RUN( "P5-B1: StaticStorage<4096> interface methods", test_p5_static_storage_interface );
    PMM_RUN( "P5-B2: StaticStorage<4096> buffer alignment", test_p5_static_storage_alignment );
    PMM_RUN( "P5-B3: StaticStorage<512, TinyAddressTraits>", test_p5_static_storage_tiny_traits );

    std::cout << "\n--- P5-C: HeapStorage functional ---\n";
    PMM_RUN( "P5-C1: HeapStorage<> initial state (empty)", test_p5_heap_storage_initial_state );
    PMM_RUN( "P5-C2: HeapStorage<> expand from zero", test_p5_heap_storage_expand_from_zero );
    PMM_RUN( "P5-C3: HeapStorage<> expand existing buffer", test_p5_heap_storage_expand_existing );
    PMM_RUN( "P5-C4: HeapStorage<> constructor with initial size", test_p5_heap_storage_constructor_with_size );
    PMM_RUN( "P5-C5: HeapStorage<> attach external buffer", test_p5_heap_storage_attach );

    std::cout << "\n--- P5-D: MMapStorage functional ---\n";
    PMM_RUN( "P5-D1: MMapStorage<> initial state (not open)", test_p5_mmap_storage_initial_state );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
