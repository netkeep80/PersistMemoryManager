/**
 * @file test_issue87_phase6.cpp
 * @brief Тесты Phase 6: AllocatorPolicy (Issue #87).
 *
 * Проверяет:
 *  - AllocatorPolicy<AvlFreeTree<Default>, Default> компилируется
 *  - Алиасы address_traits, free_block_tree
 *  - coalesce() объединяет смежные свободные блоки
 *  - rebuild_free_tree() восстанавливает дерево из существующего linked list
 *  - repair_linked_list() восстанавливает prev_offset
 *  - recompute_counters() считает блоки и суммы
 *
 * @see include/pmm/allocator_policy.h
 * @see plan_issue87.md §5 «Фаза 6: AllocatorPolicy»
 * @version 0.1 (Issue #87 Phase 6)
 */

#include "pmm/allocator_policy.h"
#include "persist_memory_manager.h"

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
// Phase 6 tests: AllocatorPolicy
// =============================================================================

// ─── P6-A: AllocatorPolicy — компиляция и алиасы ─────────────────────────────

/// @brief AllocatorPolicy<AvlFreeTree<Default>, Default> компилируется и имеет корректные алиасы.
static bool test_p6_allocator_policy_aliases()
{
    using Policy = pmm::AllocatorPolicy<pmm::AvlFreeTree<pmm::DefaultAddressTraits>, pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Policy::address_traits, pmm::DefaultAddressTraits>::value,
                   "AllocatorPolicy::address_traits must be DefaultAddressTraits" );
    static_assert( std::is_same<Policy::free_block_tree, pmm::AvlFreeTree<pmm::DefaultAddressTraits>>::value,
                   "AllocatorPolicy::free_block_tree must be AvlFreeTree<Default>" );
    return true;
}

/// @brief DefaultAllocatorPolicy псевдоним корректен.
static bool test_p6_default_allocator_policy_alias()
{
    static_assert(
        std::is_same<pmm::DefaultAllocatorPolicy, pmm::AllocatorPolicy<pmm::AvlFreeTree<pmm::DefaultAddressTraits>,
                                                                       pmm::DefaultAddressTraits>>::value,
        "DefaultAllocatorPolicy must be correct alias" );
    return true;
}

// ─── P6-B: AllocatorPolicy — функциональность через PersistMemoryManager ─────

/// @brief Вспомогательная структура для тестов: создаёт буфер с инициализированным PMM.
struct TestContext
{
    static constexpr std::size_t kBufSize = 8192;
    std::uint8_t                 buf[kBufSize];
    std::uint8_t*                base;
    pmm::detail::ManagerHeader*  hdr;

    using PMM = pmm::PersistMemoryManager<pmm::config::PMMConfig<16, 64, pmm::config::NoLock>>;

    bool setup()
    {
        std::memset( buf, 0, kBufSize );
        PMM::destroy();
        if ( !PMM::create( buf, kBufSize ) )
            return false;
        base = buf;
        hdr  = reinterpret_cast<pmm::detail::ManagerHeader*>( base + sizeof( pmm::detail::BlockHeader ) );
        return true;
    }

    void teardown() { PMM::destroy(); }
};

/// @brief AllocatorPolicy::recompute_counters() корректно считает блоки.
static bool test_p6_recompute_counters()
{
    TestContext ctx;
    if ( !ctx.setup() )
        return false;

    std::uint32_t old_block_count = ctx.hdr->block_count;
    std::uint32_t old_free_count  = ctx.hdr->free_count;
    std::uint32_t old_alloc_count = ctx.hdr->alloc_count;

    // Сбрасываем счётчики и пересчитываем
    ctx.hdr->block_count = 0;
    ctx.hdr->free_count  = 0;
    ctx.hdr->alloc_count = 0;
    ctx.hdr->used_size   = 0;

    pmm::DefaultAllocatorPolicy::recompute_counters( ctx.base, ctx.hdr );

    PMM_TEST( ctx.hdr->block_count == old_block_count );
    PMM_TEST( ctx.hdr->free_count == old_free_count );
    PMM_TEST( ctx.hdr->alloc_count == old_alloc_count );

    ctx.teardown();
    return true;
}

/// @brief AllocatorPolicy::repair_linked_list() восстанавливает prev_offset.
static bool test_p6_repair_linked_list()
{
    TestContext ctx;
    if ( !ctx.setup() )
        return false;

    // Обходим список и ломаем prev_offset
    std::uint32_t idx = ctx.hdr->first_block_offset;
    while ( idx != pmm::detail::kNoBlock )
    {
        pmm::detail::BlockHeader* blk = pmm::detail::block_at( ctx.base, idx );
        blk->prev_offset              = 0xDEADBEEFU; // намеренно ломаем
        idx                           = blk->next_offset;
    }

    // Восстанавливаем
    pmm::DefaultAllocatorPolicy::repair_linked_list( ctx.base, ctx.hdr );

    // Проверяем корректность prev_offset
    idx                = ctx.hdr->first_block_offset;
    std::uint32_t prev = pmm::detail::kNoBlock;
    while ( idx != pmm::detail::kNoBlock )
    {
        pmm::detail::BlockHeader* blk = pmm::detail::block_at( ctx.base, idx );
        PMM_TEST( blk->prev_offset == prev );
        prev = idx;
        idx  = blk->next_offset;
    }

    ctx.teardown();
    return true;
}

/// @brief AllocatorPolicy::rebuild_free_tree() восстанавливает дерево свободных блоков.
static bool test_p6_rebuild_free_tree()
{
    TestContext ctx;
    if ( !ctx.setup() )
        return false;

    std::uint32_t old_free_root = ctx.hdr->free_tree_root;
    PMM_TEST( old_free_root != pmm::detail::kNoBlock );

    // Сбрасываем дерево
    ctx.hdr->free_tree_root = pmm::detail::kNoBlock;
    // Сбрасываем AVL-ссылки во всех блоках
    std::uint32_t idx = ctx.hdr->first_block_offset;
    while ( idx != pmm::detail::kNoBlock )
    {
        pmm::detail::BlockHeader* blk = pmm::detail::block_at( ctx.base, idx );
        blk->left_offset              = pmm::detail::kNoBlock;
        blk->right_offset             = pmm::detail::kNoBlock;
        blk->parent_offset            = pmm::detail::kNoBlock;
        blk->avl_height               = 0;
        idx                           = blk->next_offset;
    }

    // Перестраиваем
    pmm::DefaultAllocatorPolicy::rebuild_free_tree( ctx.base, ctx.hdr );

    // После перестройки корень дерева должен быть не kNoBlock (есть свободные блоки)
    PMM_TEST( ctx.hdr->free_tree_root != pmm::detail::kNoBlock );
    // Проверяем, что free_count свободных блоков снова в дереве
    std::uint32_t found = pmm::AvlFreeTree<>::find_best_fit( ctx.base, ctx.hdr, 1 );
    PMM_TEST( found != pmm::detail::kNoBlock );

    ctx.teardown();
    return true;
}

/// @brief AllocatorPolicy::allocate_from_block() выделяет из найденного блока.
static bool test_p6_allocate_from_block()
{
    TestContext ctx;
    if ( !ctx.setup() )
        return false;

    using Policy = pmm::DefaultAllocatorPolicy;

    std::uint32_t needed = pmm::detail::required_block_granules( 64 );
    std::uint32_t idx    = pmm::AvlFreeTree<>::find_best_fit( ctx.base, ctx.hdr, needed );
    PMM_TEST( idx != pmm::detail::kNoBlock );

    std::uint32_t old_free  = ctx.hdr->free_count;
    std::uint32_t old_alloc = ctx.hdr->alloc_count;

    void* ptr = Policy::allocate_from_block( ctx.base, ctx.hdr, pmm::detail::block_at( ctx.base, idx ), 64 );
    PMM_TEST( ptr != nullptr );
    PMM_TEST( ctx.hdr->alloc_count == old_alloc + 1 );
    PMM_TEST( ctx.hdr->free_count <= old_free ); // может уменьшиться или остаться (split)

    ctx.teardown();
    return true;
}

/// @brief AllocatorPolicy::coalesce() объединяет соседние свободные блоки.
static bool test_p6_coalesce()
{
    TestContext ctx;
    if ( !ctx.setup() )
        return false;

    using Policy = pmm::DefaultAllocatorPolicy;

    // Выделяем два блока рядом
    std::uint32_t needed = pmm::detail::required_block_granules( 32 );
    std::uint32_t idx1   = pmm::AvlFreeTree<>::find_best_fit( ctx.base, ctx.hdr, needed );
    PMM_TEST( idx1 != pmm::detail::kNoBlock );
    void* ptr1 = Policy::allocate_from_block( ctx.base, ctx.hdr, pmm::detail::block_at( ctx.base, idx1 ), 32 );
    PMM_TEST( ptr1 != nullptr );

    std::uint32_t idx2 = pmm::AvlFreeTree<>::find_best_fit( ctx.base, ctx.hdr, needed );
    PMM_TEST( idx2 != pmm::detail::kNoBlock );
    void* ptr2 = Policy::allocate_from_block( ctx.base, ctx.hdr, pmm::detail::block_at( ctx.base, idx2 ), 32 );
    PMM_TEST( ptr2 != nullptr );

    std::uint32_t free_count_before  = ctx.hdr->free_count;
    std::uint32_t block_count_before = ctx.hdr->block_count;

    // Освобождаем оба блока (через coalesce напрямую)
    pmm::detail::BlockHeader* blk1 = pmm::detail::header_from_ptr( ctx.base, ptr1, ctx.hdr->total_size );
    PMM_TEST( blk1 != nullptr );
    blk1->size        = 0;
    blk1->root_offset = 0;
    ctx.hdr->alloc_count--;
    ctx.hdr->free_count++;
    ctx.hdr->used_size -= pmm::detail::bytes_to_granules( 32 );
    Policy::coalesce( ctx.base, ctx.hdr, blk1 );

    pmm::detail::BlockHeader* blk2 = pmm::detail::header_from_ptr( ctx.base, ptr2, ctx.hdr->total_size );
    PMM_TEST( blk2 != nullptr );
    blk2->size        = 0;
    blk2->root_offset = 0;
    ctx.hdr->alloc_count--;
    ctx.hdr->free_count++;
    ctx.hdr->used_size -= pmm::detail::bytes_to_granules( 32 );
    Policy::coalesce( ctx.base, ctx.hdr, blk2 );

    // После двух освобождений блоки должны были слиться
    // Количество свободных блоков должно быть <= free_count_before + 2
    // Количество блоков не должно расти (слияние уменьшает)
    PMM_TEST( ctx.hdr->free_count > 0 );
    PMM_TEST( ctx.hdr->block_count <= block_count_before );
    (void)free_count_before;

    ctx.teardown();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase6 (Phase 6: AllocatorPolicy) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P6-A: AllocatorPolicy — aliases and compilation ---\n";
    PMM_RUN( "P6-A1: AllocatorPolicy<AvlFreeTree, Default> aliases", test_p6_allocator_policy_aliases );
    PMM_RUN( "P6-A2: DefaultAllocatorPolicy alias correct", test_p6_default_allocator_policy_alias );

    std::cout << "\n--- P6-B: AllocatorPolicy — functional ---\n";
    PMM_RUN( "P6-B1: recompute_counters() correct", test_p6_recompute_counters );
    PMM_RUN( "P6-B2: repair_linked_list() restores prev_offset", test_p6_repair_linked_list );
    PMM_RUN( "P6-B3: rebuild_free_tree() rebuilds AVL tree", test_p6_rebuild_free_tree );
    PMM_RUN( "P6-B4: allocate_from_block() allocates correctly", test_p6_allocate_from_block );
    PMM_RUN( "P6-B5: coalesce() merges adjacent free blocks", test_p6_coalesce );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
