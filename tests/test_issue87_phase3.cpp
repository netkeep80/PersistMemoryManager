/**
 * @file test_issue87_phase3.cpp
 * @brief Тесты Phase 3: Block<AddressTraits> (Issue #87).
 *
 * Проверяет:
 *  - Block<A> наследует LinkedListNode<A> и TreeNode<A>.
 *  - Поля weight и root_offset доступны через TreeNode<A> (не собственные поля Block).
 *  - sizeof(Block<DefaultAddressTraits>) == sizeof(BlockHeader) == 32 байта.
 *  - Размеры Block для разных AddressTraits (8/16/32/64-bit).
 *  - Алиасы address_traits и index_type.
 *  - Инициализацию полей sentinel-значениями во время выполнения.
 *
 * @see include/pmm/block.h
 * @see plan_issue87.md §5 «Фаза 3: Block — блок как составной тип»
 * @version 0.2 (Issue #87 Phase 3 — weight+root_offset moved to TreeNode)
 */

#include "pmm/block.h"
#include "pmm/types.h" // для BlockHeader, kNoBlock

#include <cassert>
#include <cstddef>
#include <cstdlib>
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
// Phase 3 tests: Block
// =============================================================================

// ─── P3-A: Block — наследование и алиасы ─────────────────────────────────────

/// @brief Block<DefaultAddressTraits> — наследует LinkedListNode и TreeNode.
static bool test_p3_block_inherits_nodes()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // address_traits и index_type алиасы
    static_assert( std::is_same<Block::address_traits, A>::value, "address_traits must be A" );
    static_assert( std::is_same<Block::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

    // Block наследует LinkedListNode<A>
    static_assert( std::is_base_of<pmm::LinkedListNode<A>, Block>::value, "Block must inherit LinkedListNode" );

    // Block наследует TreeNode<A>
    static_assert( std::is_base_of<pmm::TreeNode<A>, Block>::value, "Block must inherit TreeNode" );

    return true;
}

/// @brief Block<DefaultAddressTraits> — поля weight и root_offset доступны через TreeNode.
/// Phase 3 v0.2: Block не имеет собственных полей size/root_offset; они живут в TreeNode.
static bool test_p3_block_treenode_field_types()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // Поле weight — тип index_type = uint32_t (через TreeNode<A>)
    static_assert( std::is_same<decltype( Block::weight ), std::uint32_t>::value,
                   "weight (via TreeNode) must be uint32_t" );

    // Поле root_offset — тип index_type = uint32_t (через TreeNode<A>)
    static_assert( std::is_same<decltype( Block::root_offset ), std::uint32_t>::value,
                   "root_offset (via TreeNode) must be uint32_t" );

    return true;
}

// ─── P3-B: Block — размеры ────────────────────────────────────────────────────

/// @brief sizeof(Block<DefaultAddressTraits>) == sizeof(BlockHeader) == 32 байта.
static bool test_p3_block_default_size_equals_blockheader()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;
    using BH    = pmm::detail::BlockHeader;

    // Обе структуры должны быть по 32 байта
    static_assert( sizeof( Block ) == 32, "Block<DefaultAddressTraits> must be 32 bytes" );
    static_assert( sizeof( BH ) == 32, "BlockHeader must be 32 bytes" );
    static_assert( sizeof( Block ) == sizeof( BH ),
                   "Block<DefaultAddressTraits> and BlockHeader must have the same size" );

    return true;
}

/// @brief Block работает с разными AddressTraits (8/16/32/64-bit).
/// Phase 3 v0.2: weight+root_offset теперь в TreeNode, собственных полей у Block нет.
static bool test_p3_block_various_traits()
{
    // 8-bit:
    //   LinkedListNode<Tiny> = 2 bytes
    //   TreeNode<Tiny> = 3*1 + [1 pad] + 2 + 2 + 1 + 1 = 10 bytes (int16_t alignment)
    //   Block = 2 + 10 = 12 bytes
    using Block8 = pmm::Block<pmm::TinyAddressTraits>;
    static_assert( std::is_same<Block8::index_type, std::uint8_t>::value );
    static_assert( sizeof( Block8 ) >= 12, "Block<Tiny> must be at least 12 bytes" );

    // 16-bit:
    //   LinkedListNode<Small> = 4 bytes
    //   TreeNode<Small> = 3*2 + 2 + 2 + 2 + 2 = 14 bytes
    //   Block = 4 + 14 = 18 bytes
    using Block16 = pmm::Block<pmm::SmallAddressTraits>;
    static_assert( std::is_same<Block16::index_type, std::uint16_t>::value );
    static_assert( sizeof( Block16 ) == 18, "Block<Small> must be 18 bytes" );

    // 32-bit (default):
    //   LinkedListNode<Default> = 8 bytes
    //   TreeNode<Default> = 3*4 + 2+2 + 4+4 = 24 bytes
    //   Block = 8 + 24 = 32 bytes
    using Block32 = pmm::Block<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Block32::index_type, std::uint32_t>::value );
    static_assert( sizeof( Block32 ) == 32, "Block<Default> must be 32 bytes" );

    // 64-bit:
    //   LinkedListNode<Large> = 16 bytes
    //   TreeNode<Large> = 3*8 + 2+2 + [4 pad] + 8+8 = 48 bytes (uint64_t alignment)
    //   Block = 16 + 48 = 64 bytes
    using Block64 = pmm::Block<pmm::LargeAddressTraits>;
    static_assert( std::is_same<Block64::index_type, std::uint64_t>::value );
    static_assert( sizeof( Block64 ) >= 60, "Block<Large> must be at least 60 bytes" );

    return true;
}

// ─── P3-C: Block — смещения базовых классов ──────────────────────────────────

/// @brief Поля LinkedListNode доступны через Block (наследование).
static bool test_p3_block_list_node_fields()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // Поля LinkedListNode доступны напрямую через Block
    Block b{};
    b.prev_offset = A::no_block;
    b.next_offset = A::no_block;

    PMM_TEST( b.prev_offset == pmm::detail::kNoBlock );
    PMM_TEST( b.next_offset == pmm::detail::kNoBlock );

    b.prev_offset = 5u;
    b.next_offset = 10u;
    PMM_TEST( b.prev_offset == 5u );
    PMM_TEST( b.next_offset == 10u );

    return true;
}

/// @brief Поля TreeNode (включая weight и root_offset) доступны через Block (наследование).
static bool test_p3_block_tree_node_fields()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    Block b{};
    b.left_offset   = A::no_block;
    b.right_offset  = A::no_block;
    b.parent_offset = A::no_block;
    b.avl_height    = 0;
    b._pad          = 0;
    b.weight        = 0u; // свободный блок: начальный вес
    b.root_offset   = 0u; // дерево свободных блоков

    PMM_TEST( b.left_offset == pmm::detail::kNoBlock );
    PMM_TEST( b.right_offset == pmm::detail::kNoBlock );
    PMM_TEST( b.parent_offset == pmm::detail::kNoBlock );
    PMM_TEST( b.avl_height == 0 );
    PMM_TEST( b.weight == 0u );
    PMM_TEST( b.root_offset == 0u );

    b.left_offset   = 3u;
    b.right_offset  = 7u;
    b.parent_offset = 1u;
    b.avl_height    = 2;
    b.weight        = 15u;
    b.root_offset   = 0u; // всё ещё свободный
    PMM_TEST( b.left_offset == 3u );
    PMM_TEST( b.right_offset == 7u );
    PMM_TEST( b.parent_offset == 1u );
    PMM_TEST( b.avl_height == 2 );
    PMM_TEST( b.weight == 15u );
    PMM_TEST( b.root_offset == 0u );

    return true;
}

// ─── P3-D: Block — собственные поля ──────────────────────────────────────────

/// @brief Поля weight и root_offset (из TreeNode) инициализируются корректно через Block.
/// Phase 3 v0.2: weight и root_offset живут в TreeNode, доступны через наследование Block.
static bool test_p3_block_treenode_fields_runtime()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // Свободный блок: weight == N (кол-во свободных гранул за блоком), root_offset == 0
    Block free_blk{};
    free_blk.weight      = 10u; // 10 свободных гранул за этим блоком
    free_blk.root_offset = 0u;  // принадлежит дереву свободных блоков

    PMM_TEST( free_blk.weight == 10u );
    PMM_TEST( free_blk.root_offset == 0u );

    // Занятый блок: weight — произвольный пользовательский вес, root_offset == собственный индекс
    Block alloc_blk{};
    alloc_blk.weight = 42u; // произвольный вес (определяется пользовательским деревом)
    alloc_blk.root_offset = 20u; // корень дерева, которому принадлежит этот узел

    PMM_TEST( alloc_blk.weight == 42u );
    PMM_TEST( alloc_blk.root_offset == 20u );

    return true;
}

/// @brief Block<TinyAddressTraits> с 8-bit полями weight и root_offset (через TreeNode).
/// Phase 3 v0.2: weight и root_offset живут в TreeNode.
static bool test_p3_block_tiny_traits()
{
    using A     = pmm::TinyAddressTraits;
    using Block = pmm::Block<A>;

    static_assert( std::is_same<Block::index_type, std::uint8_t>::value );
    // weight и root_offset доступны через TreeNode<A>
    static_assert( std::is_same<decltype( Block::weight ), std::uint8_t>::value,
                   "weight must be uint8_t via TreeNode<Tiny>" );
    static_assert( std::is_same<decltype( Block::root_offset ), std::uint8_t>::value,
                   "root_offset must be uint8_t via TreeNode<Tiny>" );

    Block b{};
    b.prev_offset   = A::no_block; // 0xFF
    b.next_offset   = 0u;
    b.left_offset   = A::no_block;
    b.right_offset  = A::no_block;
    b.parent_offset = A::no_block;
    b.avl_height    = 0;
    b._pad          = 0;
    b.weight        = 5u; // 5 свободных гранул за блоком
    b.root_offset   = 0u; // дерево свободных блоков

    PMM_TEST( b.prev_offset == 0xFFU );
    PMM_TEST( b.weight == 5U );
    PMM_TEST( b.root_offset == 0U );

    // Занятый tiny-блок
    b.weight      = 3u;
    b.root_offset = 10u; // принадлежит дереву с корнем 10
    PMM_TEST( b.weight == 3u );
    PMM_TEST( b.root_offset == 10u );

    return true;
}

// ─── P3-E: Block — совместимость типов с BlockHeader ─────────────────────────

/// @brief Тип weight/root_offset в Block<Default> совпадает с типами size/root_offset в BlockHeader.
/// Phase 3 v0.2: Block::weight (через TreeNode) ↔ BlockHeader::size; оба uint32_t.
static bool test_p3_block_weight_type_matches_blockheader()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;
    using BH    = pmm::detail::BlockHeader;

    // weight (через TreeNode<A>) имеет тот же тип, что BlockHeader::size
    static_assert( std::is_same<decltype( Block::weight ), decltype( BH::size )>::value,
                   "Block::weight type must match BlockHeader::size type" );
    static_assert( std::is_same<decltype( Block::root_offset ), decltype( BH::root_offset )>::value,
                   "Block::root_offset type must match BlockHeader::root_offset type" );

    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase3 (Phase 3: Block<AddressTraits>) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P3-A: Block — inheritance and aliases ---\n";
    PMM_RUN( "P3-A1: Block<Default> inherits LinkedListNode and TreeNode", test_p3_block_inherits_nodes );
    PMM_RUN( "P3-A2: Block<Default> TreeNode field types (weight, root_offset)", test_p3_block_treenode_field_types );

    std::cout << "\n--- P3-B: Block — sizes ---\n";
    PMM_RUN( "P3-B1: Block<Default> size == BlockHeader size == 32 bytes",
             test_p3_block_default_size_equals_blockheader );
    PMM_RUN( "P3-B2: Block with various AddressTraits (8/16/32/64-bit)", test_p3_block_various_traits );

    std::cout << "\n--- P3-C: Block — base class fields accessible ---\n";
    PMM_RUN( "P3-C1: LinkedListNode fields accessible via Block", test_p3_block_list_node_fields );
    PMM_RUN( "P3-C2: TreeNode fields accessible via Block", test_p3_block_tree_node_fields );

    std::cout << "\n--- P3-D: Block — TreeNode fields (weight+root_offset) ---\n";
    PMM_RUN( "P3-D1: Block weight+root_offset (from TreeNode) runtime init", test_p3_block_treenode_fields_runtime );
    PMM_RUN( "P3-D2: Block<TinyAddressTraits> 8-bit weight+root_offset via TreeNode", test_p3_block_tiny_traits );

    std::cout << "\n--- P3-E: Block — type compatibility with BlockHeader ---\n";
    PMM_RUN( "P3-E1: Block::weight (TreeNode) and root_offset types match BlockHeader",
             test_p3_block_weight_type_matches_blockheader );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
