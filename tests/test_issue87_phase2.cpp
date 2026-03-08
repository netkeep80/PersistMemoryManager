/**
 * @file test_issue87_phase2.cpp
 * @brief Тесты Phase 2: LinkedListNode<A> и TreeNode<A> (Issue #87, #112, #120).
 *
 * Проверяет:
 *  - Типы полей LinkedListNode и TreeNode зависят от AddressTraits::index_type.
 *  - Размеры структур для разных AddressTraits (8/16/32/64-bit).
 *  - Смещения полей внутри Block<A> (через BlockStateBase::kOffset* — Issue #120).
 *  - Layout Block<DefaultAddressTraits> как составного типа (Issue #112).
 *  - Алиасы address_traits и index_type.
 *  - Наличие полей weight и root_offset в TreeNode (Phase 2 v0.2).
 *  - Доступ к полям только через state machine (Issue #120).
 *
 * Note (Issue #120): Поля LinkedListNode и TreeNode теперь protected.
 * Доступ к ним осуществляется только через BlockStateBase и его наследников.
 *
 * @see include/pmm/linked_list_node.h
 * @see include/pmm/tree_node.h
 * @see include/pmm/block_state.h
 * @see plan_issue87.md §5 «Фаза 2: LinkedListNode и TreeNode»
 * @version 0.4 (Issue #120 — поля защищены, тесты обновлены для state machine API)
 */

#include "pmm/block.h"
#include "pmm/block_state.h"
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
/// Note (Issue #120): Поля protected, тип проверяется через index_type.
static bool test_p2_list_node_default_types()
{
    using A    = pmm::DefaultAddressTraits;
    using Node = pmm::LinkedListNode<A>;

    // address_traits и index_type алиасы
    static_assert( std::is_same<Node::address_traits, A>::value, "address_traits must be A" );
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

    // Тип полей = index_type (Issue #120: поля protected, проверяем через index_type)
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

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
/// Issue #120: поля protected, смещения проверяются через BlockStateBase::kOffset*.
static bool test_p2_list_node_offsets()
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;

    // prev_offset — первое поле (offset 0)
    static_assert( BlockState::kOffsetPrevOffset == 0, "prev_offset must be at offset 0" );
    // next_offset — второе поле (offset 4 = sizeof(uint32_t))
    static_assert( BlockState::kOffsetNextOffset == sizeof( std::uint32_t ), "next_offset must be at offset 4" );

    return true;
}

/// @brief Layout LinkedListNode<DefaultAddressTraits> в составе Block<A> (Issue #112).
/// Issue #120: поля protected, проверяем layout через BlockStateBase::kOffset*.
static bool test_p2_list_node_blockheader_compat()
{
    using A          = pmm::DefaultAddressTraits;
    using Node       = pmm::LinkedListNode<A>;
    using BlockState = pmm::BlockStateBase<A>;

    // Размер узла списка = 8 байт (два index_type поля)
    static_assert( sizeof( Node ) == 8 );

    // Тип полей = index_type (Issue #120: поля protected)
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value );

    // В Block<A> LinkedListNode идёт первым — prev_offset и next_offset с offset 0 и 4
    static_assert( BlockState::kOffsetPrevOffset == 0, "Block::prev_offset must be at offset 0" );
    static_assert( BlockState::kOffsetNextOffset == sizeof( std::uint32_t ),
                   "Block::next_offset must be at offset 4" );

    return true;
}

// =============================================================================
// Phase 2 tests: TreeNode
// =============================================================================

// ─── P2-B: TreeNode — типы и размеры ────────────────────────────────────────

/// @brief TreeNode<DefaultAddressTraits> — типы полей и размер.
/// Phase 2 v0.2: TreeNode теперь содержит поля weight и root_offset.
/// Issue #120: Поля protected, тип проверяется через index_type.
static bool test_p2_tree_node_default_types()
{
    using A    = pmm::DefaultAddressTraits;
    using Node = pmm::TreeNode<A>;

    // address_traits и index_type алиасы
    static_assert( std::is_same<Node::address_traits, A>::value, "address_traits must be A" );
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

    // Тип AVL-полей = index_type = uint32_t (Issue #120: поля protected, тип через index_type)
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value,
                   "TreeNode::index_type must be uint32_t for DefaultAddressTraits" );

    // Размер: 3 * sizeof(uint32_t) + sizeof(int16_t) + sizeof(uint16_t)
    //         + sizeof(uint32_t) + sizeof(uint32_t)
    //       = 12 + 2 + 2 + 4 + 4 = 24 байта
    static_assert( sizeof( Node ) == 24, "TreeNode<Default> must be 24 bytes" );

    return true;
}

/// @brief TreeNode работает с разными AddressTraits.
/// Phase 2 v0.2: учитывает дополнительные поля weight и root_offset.
/// Issue #120: Поля protected, тип проверяется через index_type.
static bool test_p2_tree_node_various_traits()
{
    // 8-bit: 3*1 + 2+2 + 1+1 = 10 байт (может быть паддинг из-за выравнивания int16_t)
    using Node8 = pmm::TreeNode<pmm::TinyAddressTraits>;
    static_assert( std::is_same<Node8::index_type, std::uint8_t>::value );
    static_assert( sizeof( Node8 ) >= 10, "TreeNode<Tiny> must be at least 10 bytes" );

    // 16-bit: 3*2 + 2+2 + 2+2 = 14 байт
    using Node16 = pmm::TreeNode<pmm::SmallAddressTraits>;
    static_assert( std::is_same<Node16::index_type, std::uint16_t>::value );
    static_assert( sizeof( Node16 ) == 14, "TreeNode<Small> must be 14 bytes" );

    // 32-bit (default): 3*4 + 2+2 + 4+4 = 24 байта
    using Node32 = pmm::TreeNode<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Node32::index_type, std::uint32_t>::value );
    static_assert( sizeof( Node32 ) == 24, "TreeNode<Default> must be 24 bytes" );

    // 64-bit: 3*8 + 2+2 + 8+8 = 44 байта (может быть паддинг до 48 для 8-байтного выравнивания)
    using Node64 = pmm::TreeNode<pmm::LargeAddressTraits>;
    static_assert( std::is_same<Node64::index_type, std::uint64_t>::value );
    static_assert( sizeof( Node64 ) >= 44, "TreeNode<Large> must be at least 44 bytes" );

    return true;
}

/// @brief Смещения полей TreeNode<DefaultAddressTraits>.
/// Phase 2 v0.2: включает смещения weight (16) и root_offset (20).
/// Issue #120: поля protected, смещения через BlockStateBase::kOffset*.
static bool test_p2_tree_node_offsets()
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;
    using Node       = pmm::TreeNode<pmm::DefaultAddressTraits>;

    // TreeNode начинается после LinkedListNode (sizeof(LLNode) = 8)
    constexpr std::size_t tree_base = sizeof( pmm::LinkedListNode<pmm::DefaultAddressTraits> );
    static_assert( tree_base == 8 );

    static_assert( BlockState::kOffsetLeftOffset == tree_base + 0, "left_offset must be at offset 8" );
    static_assert( BlockState::kOffsetRightOffset == tree_base + sizeof( std::uint32_t ),
                   "right_offset must be at offset 12" );
    static_assert( BlockState::kOffsetParentOffset == tree_base + 2 * sizeof( std::uint32_t ),
                   "parent_offset must be at offset 16" );
    static_assert( BlockState::kOffsetAvlHeight == tree_base + 3 * sizeof( std::uint32_t ),
                   "avl_height must be at offset 20" );
    // weight follows avl_height(2) + _pad(2) at +4
    static_assert( BlockState::kOffsetWeight == tree_base + 3 * sizeof( std::uint32_t ) + 4,
                   "weight must be at offset 24" );
    static_assert( BlockState::kOffsetRootOffset == tree_base + 4 * sizeof( std::uint32_t ) + 4,
                   "root_offset must be at offset 28" );

    // Also verify TreeNode size
    static_assert( sizeof( Node ) == 24, "TreeNode<Default> must be 24 bytes" );

    return true;
}

/// @brief Layout TreeNode<DefaultAddressTraits> в составе Block<A> (Issue #112).
/// Issue #120: поля protected, layout через BlockStateBase::kOffset*.
static bool test_p2_tree_node_blockheader_compat()
{
    using A          = pmm::DefaultAddressTraits;
    using Node       = pmm::TreeNode<A>;
    using BlockState = pmm::BlockStateBase<A>;

    // Тип полей = index_type = uint32_t (Issue #120: поля protected)
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value );

    // В Block<A> TreeNode следует после LinkedListNode (8 байт),
    // поэтому смещения в Block = 8 + смещения в TreeNode.
    // Проверяем через BlockStateBase::kOffset*:
    constexpr std::size_t tree_base = sizeof( pmm::LinkedListNode<A> ); // = 8
    static_assert( tree_base == 8 );
    static_assert( BlockState::kOffsetLeftOffset == tree_base + 0, "left_offset position in Block" );
    static_assert( BlockState::kOffsetRightOffset == tree_base + sizeof( std::uint32_t ),
                   "right_offset position in Block" );
    static_assert( BlockState::kOffsetParentOffset == tree_base + 2 * sizeof( std::uint32_t ),
                   "parent_offset position in Block" );
    static_assert( BlockState::kOffsetAvlHeight == tree_base + 3 * sizeof( std::uint32_t ),
                   "avl_height position in Block" );
    static_assert( BlockState::kOffsetWeight == tree_base + 3 * sizeof( std::uint32_t ) + 4,
                   "weight position in Block" );
    static_assert( BlockState::kOffsetRootOffset == tree_base + 4 * sizeof( std::uint32_t ) + 4,
                   "root_offset position in Block" );

    return true;
}

// =============================================================================
// Phase 2 tests: runtime field initialization via state machine
// =============================================================================

/// @brief Проверяем, что поля LinkedListNode инициализируются через state machine.
/// Issue #120: прямой доступ к полям запрещён, используем BlockStateBase.
static bool test_p2_list_node_runtime_init()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Выделяем буфер размером Block<A> и инициализируем через state machine
    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> )] = {};
    BlockState::init_fields( buf, A::no_block, A::no_block, 0, 0, 0 );

    PMM_TEST( BlockState::get_prev_offset( buf ) == pmm::detail::kNoBlock );
    PMM_TEST( BlockState::get_next_offset( buf ) == pmm::detail::kNoBlock );

    // Установить конкретные индексы
    BlockState::repair_prev_offset( buf, 10u );
    BlockState::set_next_offset_of( buf, 20u );
    PMM_TEST( BlockState::get_prev_offset( buf ) == 10u );
    PMM_TEST( BlockState::get_next_offset( buf ) == 20u );

    return true;
}

/// @brief Проверяем, что поля TreeNode инициализируются через state machine.
/// Issue #120: прямой доступ к полям запрещён, используем BlockStateBase.
static bool test_p2_tree_node_runtime_init()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Выделяем буфер и инициализируем через state machine
    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> )] = {};

    // Свободный блок: weight=0, root_offset=0
    BlockState::init_fields( buf, A::no_block, A::no_block, 0, 0, 0 );

    PMM_TEST( BlockState::get_left_offset( buf ) == pmm::detail::kNoBlock );
    PMM_TEST( BlockState::get_right_offset( buf ) == pmm::detail::kNoBlock );
    PMM_TEST( BlockState::get_parent_offset( buf ) == pmm::detail::kNoBlock );
    PMM_TEST( BlockState::get_avl_height( buf ) == 0 );
    PMM_TEST( BlockState::get_weight( buf ) == 0u );

    // Проверка через BlockStateBase instance getters
    auto* bs = reinterpret_cast<BlockState*>( buf );
    PMM_TEST( bs->weight() == 0u );
    PMM_TEST( bs->root_offset() == 0u );
    PMM_TEST( bs->is_free() );

    // Установить поля через reset_avl_fields_of / set methods
    BlockState::reset_avl_fields_of( buf );
    PMM_TEST( BlockState::get_left_offset( buf ) == pmm::detail::kNoBlock );
    PMM_TEST( BlockState::get_avl_height( buf ) == 0 );

    return true;
}

/// @brief Проверяем TinyAddressTraits: LinkedListNode и TreeNode с 8-bit индексами.
/// Phase 2 v0.2: включает поля weight и root_offset (8-bit для TinyAddressTraits).
/// Issue #120: доступ через BlockStateBase<TinyAddressTraits>.
static bool test_p2_tiny_traits_nodes()
{
    using A          = pmm::TinyAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> )] = {};

    // Инициализация через state machine
    BlockState::init_fields( buf, A::no_block, 0u, 1, 0, 0 );

    PMM_TEST( BlockState::get_prev_offset( buf ) == 0xFFU ); // A::no_block
    PMM_TEST( BlockState::get_next_offset( buf ) == 0U );
    PMM_TEST( BlockState::get_avl_height( buf ) == 1 );
    PMM_TEST( BlockState::get_weight( buf ) == 0U );

    auto* bs = reinterpret_cast<BlockState*>( buf );
    PMM_TEST( bs->root_offset() == 0U );
    PMM_TEST( bs->is_free() );

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
    PMM_RUN( "P2-A3: LinkedListNode<Default> field offsets (via BlockStateBase::kOffset*)", test_p2_list_node_offsets );
    PMM_RUN( "P2-A4: LinkedListNode<Default> layout in Block<A> (Issue #112)", test_p2_list_node_blockheader_compat );

    std::cout << "\n--- P2-B: TreeNode ---\n";
    PMM_RUN( "P2-B1: TreeNode<Default> types and size (incl. weight+root_offset)", test_p2_tree_node_default_types );
    PMM_RUN( "P2-B2: TreeNode with various AddressTraits (8/16/32/64-bit)", test_p2_tree_node_various_traits );
    PMM_RUN( "P2-B3: TreeNode<Default> field offsets (via BlockStateBase::kOffset*)", test_p2_tree_node_offsets );
    PMM_RUN( "P2-B4: TreeNode<Default> layout in Block<A> (Issue #112)", test_p2_tree_node_blockheader_compat );

    std::cout << "\n--- P2-C: Runtime initialization via state machine (Issue #120) ---\n";
    PMM_RUN( "P2-C1: LinkedListNode runtime init via state machine", test_p2_list_node_runtime_init );
    PMM_RUN( "P2-C2: TreeNode runtime init via state machine (incl. weight+root_offset)", test_p2_tree_node_runtime_init );
    PMM_RUN( "P2-C3: TinyAddressTraits nodes (8-bit indices) via state machine", test_p2_tiny_traits_nodes );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
