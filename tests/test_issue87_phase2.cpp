/**
 * @file test_issue87_phase2.cpp
 * @brief Тесты Phase 2: LinkedListNode<A> и TreeNode<A> (Issue #87, #112).
 *
 * Проверяет:
 *  - Типы полей LinkedListNode и TreeNode зависят от AddressTraits::index_type.
 *  - Размеры структур для разных AddressTraits (8/16/32/64-bit).
 *  - Смещения полей внутри LinkedListNode и TreeNode.
 *  - Layout Block<DefaultAddressTraits> как составного типа (Issue #112).
 *  - Алиасы address_traits и index_type.
 *  - Наличие полей weight и root_offset в TreeNode (Phase 2 v0.2).
 *
 * @see include/pmm/linked_list_node.h
 * @see include/pmm/tree_node.h
 * @see plan_issue87.md §5 «Фаза 2: LinkedListNode и TreeNode»
 * @version 0.3 (Issue #112 — BlockHeader removed, tests updated to Block<A>)
 */

#include "pmm/block.h"
#include "pmm/linked_list_node.h"
#include "pmm/tree_node.h"
#include "pmm/types.h" // kNoBlock и другие константы

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
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
// Phase 2 tests: LinkedListNode
// =============================================================================

// ─── P2-A: LinkedListNode — типы и размеры ───────────────────────────────────

/// @brief LinkedListNode<DefaultAddressTraits> — типы полей и размер.
static bool test_p2_list_node_default_types()
{
    using A    = pmm::DefaultAddressTraits;
    using Node = pmm::LinkedListNode<A>;

    // address_traits и index_type алиасы
    static_assert( std::is_same<Node::address_traits, A>::value, "address_traits must be A" );
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

    // Типы полей
    static_assert( std::is_same<decltype( Node::prev_offset ), std::uint32_t>::value, "prev_offset must be uint32_t" );
    static_assert( std::is_same<decltype( Node::next_offset ), std::uint32_t>::value, "next_offset must be uint32_t" );

    // Размер: 2 * sizeof(uint32_t) = 8 байт
    static_assert( sizeof( Node ) == 8, "LinkedListNode<Default> must be 8 bytes" );

    return true;
}

/// @brief LinkedListNode работает с разными AddressTraits.
static bool test_p2_list_node_various_traits()
{
    // 8-bit
    using Node8 = pmm::LinkedListNode<pmm::TinyAddressTraits>;
    static_assert( std::is_same<Node8::index_type, std::uint8_t>::value );
    static_assert( sizeof( Node8 ) == 2, "LinkedListNode<Tiny> must be 2 bytes" );

    // 16-bit
    using Node16 = pmm::LinkedListNode<pmm::SmallAddressTraits>;
    static_assert( std::is_same<Node16::index_type, std::uint16_t>::value );
    static_assert( sizeof( Node16 ) == 4, "LinkedListNode<Small> must be 4 bytes" );

    // 32-bit (default)
    using Node32 = pmm::LinkedListNode<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Node32::index_type, std::uint32_t>::value );
    static_assert( sizeof( Node32 ) == 8, "LinkedListNode<Default> must be 8 bytes" );

    // 64-bit
    using Node64 = pmm::LinkedListNode<pmm::LargeAddressTraits>;
    static_assert( std::is_same<Node64::index_type, std::uint64_t>::value );
    static_assert( sizeof( Node64 ) == 16, "LinkedListNode<Large> must be 16 bytes" );

    return true;
}

/// @brief Смещения полей LinkedListNode<DefaultAddressTraits>.
static bool test_p2_list_node_offsets()
{
    using Node = pmm::LinkedListNode<pmm::DefaultAddressTraits>;

    // prev_offset — первое поле (offset 0)
    static_assert( offsetof( Node, prev_offset ) == 0, "prev_offset must be at offset 0" );
    // next_offset — второе поле (offset 4 = sizeof(uint32_t))
    static_assert( offsetof( Node, next_offset ) == sizeof( std::uint32_t ), "next_offset must be at offset 4" );

    return true;
}

/// @brief Layout LinkedListNode<DefaultAddressTraits> в составе Block<A> (Issue #112).
static bool test_p2_list_node_blockheader_compat()
{
    using A     = pmm::DefaultAddressTraits;
    using Node  = pmm::LinkedListNode<A>;
    using Block = pmm::Block<A>;

    // Размер узла списка = 8 байт (два uint32_t поля)
    static_assert( sizeof( Node ) == 8 );

    // Типы полей
    static_assert( std::is_same<decltype( Node::prev_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( Node::next_offset ), std::uint32_t>::value );

    // В Block<A> LinkedListNode идёт первым — prev_offset и next_offset с offset 0 и 4
    static_assert( offsetof( Block, prev_offset ) == 0, "Block::prev_offset must be at offset 0" );
    static_assert( offsetof( Block, next_offset ) == sizeof( std::uint32_t ),
                   "Block::next_offset must be at offset 4" );

    // Смещения внутри Node
    static_assert( offsetof( Node, prev_offset ) == 0 );
    static_assert( offsetof( Node, next_offset ) == sizeof( std::uint32_t ) );

    return true;
}

// =============================================================================
// Phase 2 tests: TreeNode
// =============================================================================

// ─── P2-B: TreeNode — типы и размеры ────────────────────────────────────────

/// @brief TreeNode<DefaultAddressTraits> — типы полей и размер.
/// Phase 2 v0.2: TreeNode теперь содержит поля weight и root_offset.
static bool test_p2_tree_node_default_types()
{
    using A    = pmm::DefaultAddressTraits;
    using Node = pmm::TreeNode<A>;

    // address_traits и index_type алиасы
    static_assert( std::is_same<Node::address_traits, A>::value, "address_traits must be A" );
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

    // Типы AVL-полей
    static_assert( std::is_same<decltype( Node::left_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( Node::right_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( Node::parent_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( Node::avl_height ), std::int16_t>::value );
    static_assert( std::is_same<decltype( Node::_pad ), std::uint16_t>::value );

    // Типы полей weight и root_offset (index_type = uint32_t)
    static_assert( std::is_same<decltype( Node::weight ), std::uint32_t>::value,
                   "weight must be index_type = uint32_t" );
    static_assert( std::is_same<decltype( Node::root_offset ), std::uint32_t>::value,
                   "root_offset must be index_type = uint32_t" );

    // Размер: 3 * sizeof(uint32_t) + sizeof(int16_t) + sizeof(uint16_t)
    //         + sizeof(uint32_t) + sizeof(uint32_t)
    //       = 12 + 2 + 2 + 4 + 4 = 24 байта
    static_assert( sizeof( Node ) == 24, "TreeNode<Default> must be 24 bytes" );

    return true;
}

/// @brief TreeNode работает с разными AddressTraits.
/// Phase 2 v0.2: учитывает дополнительные поля weight и root_offset.
static bool test_p2_tree_node_various_traits()
{
    // 8-bit: 3*1 + 2+2 + 1+1 = 10 байт (может быть паддинг из-за выравнивания int16_t)
    using Node8 = pmm::TreeNode<pmm::TinyAddressTraits>;
    static_assert( std::is_same<Node8::index_type, std::uint8_t>::value );
    static_assert( std::is_same<decltype( Node8::weight ), std::uint8_t>::value );
    static_assert( std::is_same<decltype( Node8::root_offset ), std::uint8_t>::value );
    static_assert( sizeof( Node8 ) >= 10, "TreeNode<Tiny> must be at least 10 bytes" );

    // 16-bit: 3*2 + 2+2 + 2+2 = 14 байт
    using Node16 = pmm::TreeNode<pmm::SmallAddressTraits>;
    static_assert( std::is_same<Node16::index_type, std::uint16_t>::value );
    static_assert( std::is_same<decltype( Node16::weight ), std::uint16_t>::value );
    static_assert( std::is_same<decltype( Node16::root_offset ), std::uint16_t>::value );
    static_assert( sizeof( Node16 ) == 14, "TreeNode<Small> must be 14 bytes" );

    // 32-bit (default): 3*4 + 2+2 + 4+4 = 24 байта
    using Node32 = pmm::TreeNode<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Node32::index_type, std::uint32_t>::value );
    static_assert( sizeof( Node32 ) == 24, "TreeNode<Default> must be 24 bytes" );

    // 64-bit: 3*8 + 2+2 + 8+8 = 44 байта (может быть паддинг до 48 для 8-байтного выравнивания)
    using Node64 = pmm::TreeNode<pmm::LargeAddressTraits>;
    static_assert( std::is_same<Node64::index_type, std::uint64_t>::value );
    static_assert( std::is_same<decltype( Node64::weight ), std::uint64_t>::value );
    static_assert( std::is_same<decltype( Node64::root_offset ), std::uint64_t>::value );
    static_assert( sizeof( Node64 ) >= 44, "TreeNode<Large> must be at least 44 bytes" );

    return true;
}

/// @brief Смещения полей TreeNode<DefaultAddressTraits>.
/// Phase 2 v0.2: включает смещения weight (16) и root_offset (20).
static bool test_p2_tree_node_offsets()
{
    using Node = pmm::TreeNode<pmm::DefaultAddressTraits>;

    static_assert( offsetof( Node, left_offset ) == 0, "left_offset must be at offset 0" );
    static_assert( offsetof( Node, right_offset ) == sizeof( std::uint32_t ), "right_offset must be at offset 4" );
    static_assert( offsetof( Node, parent_offset ) == 2 * sizeof( std::uint32_t ),
                   "parent_offset must be at offset 8" );
    static_assert( offsetof( Node, avl_height ) == 3 * sizeof( std::uint32_t ), "avl_height must be at offset 12" );
    static_assert( offsetof( Node, _pad ) == 3 * sizeof( std::uint32_t ) + sizeof( std::int16_t ),
                   "_pad must be at offset 14" );
    // weight follows _pad at offset 16
    static_assert( offsetof( Node, weight ) == 3 * sizeof( std::uint32_t ) + 4, "weight must be at offset 16" );
    // root_offset follows weight at offset 20
    static_assert( offsetof( Node, root_offset ) == 4 * sizeof( std::uint32_t ) + 4,
                   "root_offset must be at offset 20" );

    return true;
}

/// @brief Layout TreeNode<DefaultAddressTraits> в составе Block<A> (Issue #112).
static bool test_p2_tree_node_blockheader_compat()
{
    using A     = pmm::DefaultAddressTraits;
    using Node  = pmm::TreeNode<A>;
    using Block = pmm::Block<A>;

    // Типы AVL-полей (все uint32_t кроме avl_height=int16_t, _pad=uint16_t)
    static_assert( std::is_same<decltype( Node::left_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( Node::right_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( Node::parent_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( Node::avl_height ), std::int16_t>::value );
    static_assert( std::is_same<decltype( Node::_pad ), std::uint16_t>::value );
    static_assert( std::is_same<decltype( Node::weight ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( Node::root_offset ), std::uint32_t>::value );

    // В Block<A> TreeNode следует после LinkedListNode (8 байт),
    // поэтому смещения в Block = 8 + смещения в TreeNode:
    //   left_offset@8,   right_offset@12, parent_offset@16,
    //   avl_height@20,   weight@24,        root_offset@28
    constexpr std::size_t tree_base = sizeof( pmm::LinkedListNode<A> ); // = 8
    static_assert( tree_base == 8 );
    static_assert( tree_base + offsetof( Node, left_offset ) == offsetof( Block, left_offset ),
                   "left_offset position in Block" );
    static_assert( tree_base + offsetof( Node, right_offset ) == offsetof( Block, right_offset ),
                   "right_offset position in Block" );
    static_assert( tree_base + offsetof( Node, parent_offset ) == offsetof( Block, parent_offset ),
                   "parent_offset position in Block" );
    static_assert( tree_base + offsetof( Node, avl_height ) == offsetof( Block, avl_height ),
                   "avl_height position in Block" );
    static_assert( tree_base + offsetof( Node, weight ) == offsetof( Block, weight ), "weight position in Block" );
    static_assert( tree_base + offsetof( Node, root_offset ) == offsetof( Block, root_offset ),
                   "root_offset position in Block" );

    return true;
}

// =============================================================================
// Phase 2 tests: runtime field initialization
// =============================================================================

/// @brief Проверяем, что поля LinkedListNode можно инициализировать sentinel-значениями.
static bool test_p2_list_node_runtime_init()
{
    using A    = pmm::DefaultAddressTraits;
    using Node = pmm::LinkedListNode<A>;

    Node n;
    n.prev_offset = A::no_block;
    n.next_offset = A::no_block;

    PMM_TEST( n.prev_offset == pmm::detail::kNoBlock );
    PMM_TEST( n.next_offset == pmm::detail::kNoBlock );

    // Установить конкретные индексы
    n.prev_offset = 10u;
    n.next_offset = 20u;
    PMM_TEST( n.prev_offset == 10u );
    PMM_TEST( n.next_offset == 20u );

    return true;
}

/// @brief Проверяем, что поля TreeNode можно инициализировать sentinel-значениями.
/// Phase 2 v0.2: включает инициализацию полей weight и root_offset.
static bool test_p2_tree_node_runtime_init()
{
    using A    = pmm::DefaultAddressTraits;
    using Node = pmm::TreeNode<A>;

    Node n;
    n.left_offset   = A::no_block;
    n.right_offset  = A::no_block;
    n.parent_offset = A::no_block;
    n.avl_height    = 0;
    n._pad          = 0;
    n.weight = 0u; // свободный блок: вес будет вычислен при добавлении в дерево
    n.root_offset = 0u; // 0 = дерево свободных блоков

    PMM_TEST( n.left_offset == pmm::detail::kNoBlock );
    PMM_TEST( n.right_offset == pmm::detail::kNoBlock );
    PMM_TEST( n.parent_offset == pmm::detail::kNoBlock );
    PMM_TEST( n.avl_height == 0 );
    PMM_TEST( n.weight == 0u );
    PMM_TEST( n.root_offset == 0u );

    // Занятый узел: root_offset == собственный индекс, weight — произвольный
    n.weight      = 42u;
    n.root_offset = 5u; // принадлежит дереву с корнем 5

    PMM_TEST( n.weight == 42u );
    PMM_TEST( n.root_offset == 5u );

    // Установить конкретные значения AVL-полей
    n.left_offset   = 5u;
    n.right_offset  = 10u;
    n.parent_offset = 2u;
    n.avl_height    = 3;

    PMM_TEST( n.left_offset == 5u );
    PMM_TEST( n.right_offset == 10u );
    PMM_TEST( n.parent_offset == 2u );
    PMM_TEST( n.avl_height == 3 );

    return true;
}

/// @brief Проверяем TinyAddressTraits: LinkedListNode и TreeNode с 8-bit индексами.
/// Phase 2 v0.2: включает поля weight и root_offset (8-bit для TinyAddressTraits).
static bool test_p2_tiny_traits_nodes()
{
    using A  = pmm::TinyAddressTraits;
    using LN = pmm::LinkedListNode<A>;
    using TN = pmm::TreeNode<A>;

    LN list_node;
    list_node.prev_offset = A::no_block; // 0xFF
    list_node.next_offset = 0;

    PMM_TEST( list_node.prev_offset == 0xFFU );
    PMM_TEST( list_node.next_offset == 0U );

    TN tree_node;
    tree_node.left_offset   = A::no_block; // 0xFF
    tree_node.right_offset  = A::no_block;
    tree_node.parent_offset = 0;
    tree_node.avl_height    = 1;
    tree_node._pad          = 0;
    tree_node.weight        = 0u; // свободный блок
    tree_node.root_offset   = 0u; // дерево свободных блоков

    PMM_TEST( tree_node.left_offset == 0xFFU );
    PMM_TEST( tree_node.parent_offset == 0U );
    PMM_TEST( tree_node.avl_height == 1 );
    PMM_TEST( tree_node.weight == 0U );
    PMM_TEST( tree_node.root_offset == 0U );

    // Занятый tiny-узел
    tree_node.weight      = 3u;  // вес
    tree_node.root_offset = 10u; // принадлежит дереву с корнем 10
    PMM_TEST( tree_node.weight == 3u );
    PMM_TEST( tree_node.root_offset == 10u );

    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase2 (Phase 2: LinkedListNode + TreeNode) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P2-A: LinkedListNode ---\n";
    PMM_RUN( "P2-A1: LinkedListNode<Default> types and size", test_p2_list_node_default_types );
    PMM_RUN( "P2-A2: LinkedListNode with various AddressTraits (8/16/32/64-bit)", test_p2_list_node_various_traits );
    PMM_RUN( "P2-A3: LinkedListNode<Default> field offsets", test_p2_list_node_offsets );
    PMM_RUN( "P2-A4: LinkedListNode<Default> layout in Block<A> (Issue #112)", test_p2_list_node_blockheader_compat );

    std::cout << "\n--- P2-B: TreeNode ---\n";
    PMM_RUN( "P2-B1: TreeNode<Default> types and size (incl. weight+root_offset)", test_p2_tree_node_default_types );
    PMM_RUN( "P2-B2: TreeNode with various AddressTraits (8/16/32/64-bit)", test_p2_tree_node_various_traits );
    PMM_RUN( "P2-B3: TreeNode<Default> field offsets (incl. weight+root_offset)", test_p2_tree_node_offsets );
    PMM_RUN( "P2-B4: TreeNode<Default> layout in Block<A> (Issue #112)", test_p2_tree_node_blockheader_compat );

    std::cout << "\n--- P2-C: Runtime initialization ---\n";
    PMM_RUN( "P2-C1: LinkedListNode runtime sentinel init", test_p2_list_node_runtime_init );
    PMM_RUN( "P2-C2: TreeNode runtime sentinel init (incl. weight+root_offset)", test_p2_tree_node_runtime_init );
    PMM_RUN( "P2-C3: TinyAddressTraits nodes (8-bit indices, incl. weight+root_offset)", test_p2_tiny_traits_nodes );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
