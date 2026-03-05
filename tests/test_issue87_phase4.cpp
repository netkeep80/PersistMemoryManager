/**
 * @file test_issue87_phase4.cpp
 * @brief Тесты Phase 4: FreeBlockTree как шаблонная политика (Issue #87).
 *
 * Проверяет:
 *  - is_free_block_tree_policy_v<AvlFreeTree<DefaultAddressTraits>> == true
 *  - AvlFreeTree корректно делегирует PersistentAvlTree
 *  - Концепт применим (SFINAE): несоответствующие типы возвращают false
 *
 * @see include/pmm/free_block_tree.h
 * @see plan_issue87.md §5 «Фаза 4: FreeBlockTree как шаблонная политика»
 * @version 0.1 (Issue #87 Phase 4)
 */

#include "pmm/free_block_tree.h"
#include "pmm/legacy_manager.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
// Phase 4 tests: FreeBlockTree policy concept
// =============================================================================

// ─── P4-A: Концепт ────────────────────────────────────────────────────────────

/// @brief AvlFreeTree<DefaultAddressTraits> соответствует концепту.
static bool test_p4_avl_free_tree_satisfies_concept()
{
    using Policy = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;
    static_assert( pmm::is_free_block_tree_policy_v<Policy>,
                   "AvlFreeTree<DefaultAddressTraits> must satisfy FreeBlockTreePolicy" );
    return true;
}

/// @brief Тип без статических методов не соответствует концепту.
static bool test_p4_non_policy_type_fails_concept()
{
    struct NotAPolicy
    {
        int x;
    };
    static_assert( !pmm::is_free_block_tree_policy_v<NotAPolicy>, "NotAPolicy must not satisfy FreeBlockTreePolicy" );
    static_assert( !pmm::is_free_block_tree_policy_v<int>, "int must not satisfy FreeBlockTreePolicy" );
    return true;
}

/// @brief Тип с только insert() не соответствует концепту (нет remove/find_best_fit).
static bool test_p4_partial_policy_fails_concept()
{
    struct PartialPolicy
    {
        static void insert( std::uint8_t*, pmm::detail::ManagerHeader*, std::uint32_t ) {}
    };
    static_assert( !pmm::is_free_block_tree_policy_v<PartialPolicy>,
                   "PartialPolicy (insert only) must not satisfy FreeBlockTreePolicy" );
    return true;
}

// ─── P4-B: AvlFreeTree — алиасы типов ────────────────────────────────────────

/// @brief AvlFreeTree имеет корректные алиасы address_traits и index_type.
static bool test_p4_avl_free_tree_aliases()
{
    using Policy = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Policy::address_traits, pmm::DefaultAddressTraits>::value,
                   "AvlFreeTree::address_traits must be DefaultAddressTraits" );
    static_assert( std::is_same<Policy::index_type, std::uint32_t>::value, "AvlFreeTree::index_type must be uint32_t" );
    return true;
}

// ─── P4-C: AvlFreeTree — функциональность ────────────────────────────────────

/// @brief AvlFreeTree корректно вставляет/ищет/удаляет блоки (через PersistentAvlTree).
static bool test_p4_avl_free_tree_functional()
{
    using Policy = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;

    // Создаём минимальный менеджер через PersistMemoryManager для наполнения буфера
    constexpr std::size_t kSize = 4096;
    static std::uint8_t   buf[kSize];
    std::memset( buf, 0, kSize );

    // Используем PersistMemoryManager для создания валидного контекста
    using PMM = pmm::PersistMemoryManager<pmm::config::PMMConfig<16, 64, pmm::config::NoLock>>;
    PMM::destroy(); // сброс на всякий случай
    PMM_TEST( PMM::create( buf, kSize ) );

    // После create() в буфере есть один свободный блок и дерево уже инициализировано
    std::uint8_t*               base = buf;
    pmm::detail::ManagerHeader* hdr =
        reinterpret_cast<pmm::detail::ManagerHeader*>( base + sizeof( pmm::detail::BlockHeader ) );

    std::uint32_t root_before = hdr->free_tree_root;
    PMM_TEST( root_before != pmm::detail::kNoBlock );

    // find_best_fit через AvlFreeTree
    std::uint32_t found = Policy::find_best_fit( base, hdr, 3 ); // 3 гранулы
    PMM_TEST( found != pmm::detail::kNoBlock );

    // remove + insert
    Policy::remove( base, hdr, found );
    std::uint32_t after_remove = hdr->free_tree_root;
    // После удаления корня дерево может стать пустым (если был один свободный блок)
    (void)after_remove;

    Policy::insert( base, hdr, found );
    // После повторной вставки блок снова в дереве
    std::uint32_t found2 = Policy::find_best_fit( base, hdr, 3 );
    PMM_TEST( found2 != pmm::detail::kNoBlock );

    PMM::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase4 (Phase 4: FreeBlockTree policy) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P4-A: FreeBlockTree concept ---\n";
    PMM_RUN( "P4-A1: AvlFreeTree<Default> satisfies FreeBlockTreePolicy", test_p4_avl_free_tree_satisfies_concept );
    PMM_RUN( "P4-A2: Non-policy type fails concept check", test_p4_non_policy_type_fails_concept );
    PMM_RUN( "P4-A3: Partial policy (insert only) fails concept check", test_p4_partial_policy_fails_concept );

    std::cout << "\n--- P4-B: AvlFreeTree — type aliases ---\n";
    PMM_RUN( "P4-B1: AvlFreeTree<Default> has correct type aliases", test_p4_avl_free_tree_aliases );

    std::cout << "\n--- P4-C: AvlFreeTree — functional ---\n";
    PMM_RUN( "P4-C1: AvlFreeTree insert/remove/find_best_fit functional", test_p4_avl_free_tree_functional );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
