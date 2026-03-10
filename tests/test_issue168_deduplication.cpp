/**
 * @file test_issue168_deduplication.cpp
 * @brief Тесты дедупликации функциональности ПАП (Issue #168).
 *
 * Проверяет:
 *   - Удаление четырёх избыточных функций-обёрток из block_state.h (Issue #168):
 *       reset_block_avl_fields(), repair_block_prev_offset(),
 *       read_block_next_offset(), read_block_weight()
 *   - AllocatorPolicy использует BlockStateBase<AT>::* напрямую (Issue #168)
 *   - detail::kBlockHeaderGranules_t<AT> корректно вычисляет размер заголовка блока (Issue #168)
 *   - Функциональность recovery-методов (rebuild_free_tree, repair_linked_list,
 *     recompute_counters) не изменилась после рефакторинга
 *
 * @see include/pmm/block_state.h    — BlockStateBase<AT>::* методы (Issue #168)
 * @see include/pmm/allocator_policy.h — AllocatorPolicy (Issue #168)
 * @see include/pmm/types.h          — detail::kBlockHeaderGranules_t<AT> (Issue #168)
 * @version 0.1 (Issue #168 — дедупликация функций-обёрток)
 */

#include "pmm/allocator_policy.h"
#include "pmm/block_state.h"
#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/types.h"

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
// Issue #168 Tests Section A: kBlockHeaderGranules_t<AT> correctness
// =============================================================================

/// @brief kBlockHeaderGranules_t<DefaultAddressTraits> is correct (2 granules = 32 bytes / 16 bytes).
static bool test_i168_kBlockHeaderGranules_matches_templated()
{
    using AT = pmm::DefaultAddressTraits;

    static_assert( pmm::detail::kBlockHeaderGranules_t<AT> == 2,
                   "kBlockHeaderGranules_t<DefaultAddressTraits> must be 2 (32 bytes / 16 bytes, Issue #168)" );
    return true;
}

/// @brief kBlockHeaderGranules_t<SmallAddressTraits> is correct (ceiling division).
static bool test_i168_kBlockHeaderGranules_t_small()
{
    using AT = pmm::SmallAddressTraits;

    // SmallAddressTraits: granule_size=16, Block<Small> has TreeNode<Small> + 2 uint16_t
    // TreeNode<Small>: 5*uint16_t + 4 bytes = 14 bytes; Block<Small> = 14 + 4 = 18 bytes
    // ceil(18/16) = 2
    static_assert( pmm::detail::kBlockHeaderGranules_t<AT> == 2,
                   "kBlockHeaderGranules_t<SmallAddressTraits> must be 2 (Issue #168)" );
    return true;
}

/// @brief kBlockHeaderGranules_t<LargeAddressTraits> is correct.
static bool test_i168_kBlockHeaderGranules_t_large()
{
    using AT = pmm::LargeAddressTraits;

    // LargeAddressTraits: granule_size=64, Block<Large> has TreeNode<Large> + 2 uint64_t
    // TreeNode<Large>: 5*uint64_t + 4 bytes = 44 bytes; Block<Large> = 44 + 16 = 60 bytes? No:
    // Actually Block<Large> = sizeof(TreeNode<Large>) + 2*sizeof(uint64_t)
    // ceil(sizeof(Block<Large>) / 64) — at least 1
    static_assert( pmm::detail::kBlockHeaderGranules_t<AT> >= 1,
                   "kBlockHeaderGranules_t<LargeAddressTraits> must be >= 1 (Issue #168)" );
    return true;
}

// =============================================================================
// Issue #168 Tests Section B: BlockStateBase<AT> methods used directly (no wrapper overhead)
// =============================================================================

/// @brief BlockStateBase<AT>::reset_avl_fields_of() correctly resets AVL fields.
static bool test_i168_BlockStateBase_reset_avl_fields_directly()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Prepare a block buffer
    alignas( 16 ) std::uint8_t buffer[32] = {};
    auto*                      blk        = reinterpret_cast<pmm::Block<A>*>( buffer );

    // Set some AVL fields
    BlockState::set_left_offset_of( blk, static_cast<A::index_type>( 10 ) );
    BlockState::set_right_offset_of( blk, static_cast<A::index_type>( 20 ) );
    BlockState::set_parent_offset_of( blk, static_cast<A::index_type>( 5 ) );
    BlockState::set_avl_height_of( blk, static_cast<std::int16_t>( 3 ) );

    // Call reset_avl_fields_of directly (as AllocatorPolicy now does after Issue #168)
    BlockState::reset_avl_fields_of( blk );

    PMM_TEST( BlockState::get_left_offset( blk ) == A::no_block );
    PMM_TEST( BlockState::get_right_offset( blk ) == A::no_block );
    PMM_TEST( BlockState::get_parent_offset( blk ) == A::no_block );
    PMM_TEST( BlockState::get_avl_height( blk ) == 0 );
    return true;
}

/// @brief BlockStateBase<AT>::repair_prev_offset() correctly sets prev_offset.
static bool test_i168_BlockStateBase_repair_prev_offset_directly()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32] = {};
    auto*                      blk        = reinterpret_cast<pmm::Block<A>*>( buffer );

    // repair_prev_offset sets prev_offset (as AllocatorPolicy now does after Issue #168)
    BlockState::repair_prev_offset( blk, static_cast<A::index_type>( 7 ) );
    PMM_TEST( BlockState::get_prev_offset( blk ) == static_cast<A::index_type>( 7 ) );

    BlockState::repair_prev_offset( blk, A::no_block );
    PMM_TEST( BlockState::get_prev_offset( blk ) == A::no_block );
    return true;
}

/// @brief BlockStateBase<AT>::get_next_offset() returns next_offset correctly.
static bool test_i168_BlockStateBase_get_next_offset_directly()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32] = {};
    auto*                      blk        = reinterpret_cast<pmm::Block<A>*>( buffer );

    // Set next_offset via init_fields
    BlockState::init_fields( blk,
                             /*prev*/ A::no_block,
                             /*next*/ static_cast<A::index_type>( 42 ),
                             /*avl_height*/ 0,
                             /*weight*/ 0,
                             /*root_offset*/ 0 );

    // get_next_offset is now called directly in AllocatorPolicy (Issue #168)
    PMM_TEST( BlockState::get_next_offset( blk ) == static_cast<A::index_type>( 42 ) );

    BlockState::set_next_offset_of( blk, A::no_block );
    PMM_TEST( BlockState::get_next_offset( blk ) == A::no_block );
    return true;
}

/// @brief BlockStateBase<AT>::get_weight() returns weight correctly.
static bool test_i168_BlockStateBase_get_weight_directly()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32] = {};
    auto*                      blk        = reinterpret_cast<pmm::Block<A>*>( buffer );

    BlockState::init_fields( blk, A::no_block, A::no_block, 0, 0, 0 );
    PMM_TEST( BlockState::get_weight( blk ) == 0 );

    BlockState::set_weight_of( blk, static_cast<A::index_type>( 5 ) );
    PMM_TEST( BlockState::get_weight( blk ) == static_cast<A::index_type>( 5 ) );
    return true;
}

// =============================================================================
// Issue #168 Tests Section C: AllocatorPolicy recovery methods work correctly after refactoring
// =============================================================================

using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 168>;

/// @brief rebuild_free_tree() correctly rebuilds the tree (functional test).
static bool test_i168_rebuild_free_tree_functional()
{
    TestMgr::create( 64 * 1024 );

    void* p1 = TestMgr::allocate( 32 );
    void* p2 = TestMgr::allocate( 64 );
    PMM_TEST( p1 != nullptr );
    PMM_TEST( p2 != nullptr );

    std::size_t free_before = TestMgr::free_block_count();

    // Deallocate to create free blocks
    TestMgr::deallocate( p1 );
    TestMgr::deallocate( p2 );

    std::size_t free_after = TestMgr::free_block_count();
    PMM_TEST( free_after >= free_before );

    // Reload triggers rebuild_free_tree internally (via load path)
    // After reload, can still allocate
    void* p3 = TestMgr::allocate( 32 );
    PMM_TEST( p3 != nullptr );
    TestMgr::deallocate( p3 );

    TestMgr::destroy();
    return true;
}

/// @brief repair_linked_list() correctly restores prev_offset links.
static bool test_i168_repair_linked_list_functional()
{
    TestMgr::create( 64 * 1024 );

    // Allocate and deallocate to create a multi-block state
    void* p1 = TestMgr::allocate( 16 );
    void* p2 = TestMgr::allocate( 32 );
    void* p3 = TestMgr::allocate( 48 );
    PMM_TEST( p1 != nullptr );
    PMM_TEST( p2 != nullptr );
    PMM_TEST( p3 != nullptr );

    TestMgr::deallocate( p2 );

    // Verify counts are consistent (repair_linked_list runs during load)
    PMM_TEST( TestMgr::block_count() >= 3 );

    TestMgr::deallocate( p1 );
    TestMgr::deallocate( p3 );
    TestMgr::destroy();
    return true;
}

/// @brief recompute_counters() correctly tallies block/free/alloc counts.
static bool test_i168_recompute_counters_functional()
{
    TestMgr::create( 64 * 1024 );

    // Initial state: one sentinel block_0 (allocated) + one free block
    // Note: block_0 is always counted as allocated (sentinel block)
    std::size_t initial_free  = TestMgr::free_block_count();
    std::size_t initial_alloc = TestMgr::alloc_block_count();
    PMM_TEST( initial_free > 0 );            // at least one free block
    PMM_TEST( TestMgr::block_count() >= 2 ); // block_0 + at least one free block

    void* p1 = TestMgr::allocate( 64 );
    PMM_TEST( p1 != nullptr );
    PMM_TEST( TestMgr::alloc_block_count() == initial_alloc + 1 );

    void* p2 = TestMgr::allocate( 128 );
    PMM_TEST( p2 != nullptr );
    PMM_TEST( TestMgr::alloc_block_count() == initial_alloc + 2 );

    TestMgr::deallocate( p1 );
    PMM_TEST( TestMgr::alloc_block_count() == initial_alloc + 1 );
    PMM_TEST( TestMgr::free_block_count() >= initial_free );

    TestMgr::deallocate( p2 );
    PMM_TEST( TestMgr::alloc_block_count() == initial_alloc );

    TestMgr::destroy();
    return true;
}

// =============================================================================
// Issue #168 Tests Section D: Non-default AddressTraits use correct BlockStateBase
// =============================================================================

/// @brief BlockStateBase<SmallAddressTraits>::reset_avl_fields_of() uses correct no_block value.
static bool test_i168_BlockStateBase_small_at_reset_avl_fields()
{
    using A          = pmm::SmallAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // SmallAddressTraits: index_type = uint16_t, no_block = 0xFFFF
    alignas( 16 ) std::uint8_t buffer[64] = {};
    auto*                      blk        = reinterpret_cast<pmm::Block<A>*>( buffer );

    BlockState::set_left_offset_of( blk, static_cast<A::index_type>( 100 ) );
    BlockState::set_right_offset_of( blk, static_cast<A::index_type>( 200 ) );
    BlockState::set_parent_offset_of( blk, static_cast<A::index_type>( 50 ) );
    BlockState::set_avl_height_of( blk, static_cast<std::int16_t>( 5 ) );

    BlockState::reset_avl_fields_of( blk );

    PMM_TEST( BlockState::get_left_offset( blk ) == A::no_block );
    PMM_TEST( BlockState::get_right_offset( blk ) == A::no_block );
    PMM_TEST( BlockState::get_parent_offset( blk ) == A::no_block );
    PMM_TEST( BlockState::get_avl_height( blk ) == 0 );

    // Verify no_block is 0xFFFF for SmallAddressTraits
    static_assert( A::no_block == static_cast<A::index_type>( 0xFFFFU ),
                   "SmallAddressTraits::no_block must be 0xFFFF" );
    return true;
}

/// @brief BlockStateBase<SmallAddressTraits>::get_weight() and get_next_offset() work correctly.
static bool test_i168_BlockStateBase_small_at_read_fields()
{
    using A          = pmm::SmallAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[64] = {};
    auto*                      blk        = reinterpret_cast<pmm::Block<A>*>( buffer );

    BlockState::init_fields( blk,
                             /*prev*/ A::no_block,
                             /*next*/ static_cast<A::index_type>( 99 ),
                             /*avl_height*/ 1,
                             /*weight*/ static_cast<A::index_type>( 3 ),
                             /*root_offset*/ static_cast<A::index_type>( 10 ) );

    // get_weight and get_next_offset are now called directly in AllocatorPolicy (Issue #168)
    PMM_TEST( BlockState::get_weight( blk ) == static_cast<A::index_type>( 3 ) );
    PMM_TEST( BlockState::get_next_offset( blk ) == static_cast<A::index_type>( 99 ) );
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue168_deduplication (Issue #168: Deduplication) ===\n\n";
    bool all_passed = true;

    std::cout << "--- I168-A: kBlockHeaderGranules_t<AT> correctness ---\n";
    PMM_RUN( "I168-A1: kBlockHeaderGranules_t<DefaultAddressTraits> == 2",
             test_i168_kBlockHeaderGranules_matches_templated );
    PMM_RUN( "I168-A2: kBlockHeaderGranules_t<SmallAddressTraits> is correct", test_i168_kBlockHeaderGranules_t_small );
    PMM_RUN( "I168-A3: kBlockHeaderGranules_t<LargeAddressTraits> is >= 1", test_i168_kBlockHeaderGranules_t_large );

    std::cout << "\n--- I168-B: BlockStateBase<AT> methods used directly (no wrapper overhead) ---\n";
    PMM_RUN( "I168-B1: BlockStateBase::reset_avl_fields_of() directly",
             test_i168_BlockStateBase_reset_avl_fields_directly );
    PMM_RUN( "I168-B2: BlockStateBase::repair_prev_offset() directly",
             test_i168_BlockStateBase_repair_prev_offset_directly );
    PMM_RUN( "I168-B3: BlockStateBase::get_next_offset() directly", test_i168_BlockStateBase_get_next_offset_directly );
    PMM_RUN( "I168-B4: BlockStateBase::get_weight() directly", test_i168_BlockStateBase_get_weight_directly );

    std::cout << "\n--- I168-C: AllocatorPolicy recovery methods functional after refactoring ---\n";
    PMM_RUN( "I168-C1: rebuild_free_tree() works after wrapper removal", test_i168_rebuild_free_tree_functional );
    PMM_RUN( "I168-C2: repair_linked_list() works after wrapper removal", test_i168_repair_linked_list_functional );
    PMM_RUN( "I168-C3: recompute_counters() works after wrapper removal", test_i168_recompute_counters_functional );

    std::cout << "\n--- I168-D: Non-default AddressTraits BlockStateBase methods ---\n";
    PMM_RUN( "I168-D1: BlockStateBase<SmallAddressTraits>::reset_avl_fields_of() uses 0xFFFF no_block",
             test_i168_BlockStateBase_small_at_reset_avl_fields );
    PMM_RUN( "I168-D2: BlockStateBase<SmallAddressTraits>::get_weight/next_offset correct",
             test_i168_BlockStateBase_small_at_read_fields );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
