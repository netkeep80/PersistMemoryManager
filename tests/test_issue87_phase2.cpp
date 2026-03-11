/**
 * @file test_issue87_phase2.cpp
 * @brief Тесты Phase 2: TreeNode<A> (Issue #87, #112, #120, #138).
 *
 * Проверяет:
 *  - Типы полей TreeNode зависят от AddressTraits::index_type.
 *  - Размеры структур для разных AddressTraits (8/16/32/64-bit).
 *  - Смещения полей внутри Block<A> (через BlockStateBase::kOffset* — Issue #120).
 *  - Layout Block<DefaultAddressTraits> как составного типа (Issue #112).
 *  - Алиасы address_traits и index_type.
 *  - Наличие полей weight и root_offset в TreeNode (Phase 2 v0.2).
 *  - Доступ к полям только через state machine (Issue #120).
 *  - Issue #138: LinkedListNode удалена; prev_offset/next_offset прямо в Block.
 *
 * Note (Issue #120): Поля TreeNode и Block теперь protected.
 * Доступ к ним осуществляется только через BlockStateBase и его наследников.
 *
 * @see include/pmm/block.h
 * @see include/pmm/tree_node.h
 * @see include/pmm/block_state.h
 * @see plan_issue87.md §5 «Фаза 2: LinkedListNode и TreeNode»
 * @version 0.4 (Issue #120 — поля защищены, тесты обновлены для state machine API)
 */

#include "pmm_single_threaded_heap.h"

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
// Phase 2 tests: Block prev/next fields (Issue #138: LinkedListNode merged into Block)
// =============================================================================

// ─── P2-A: Block prev/next fields — типы и размеры ───────────────────────────

/// @brief Block<DefaultAddressTraits> — поля prev_offset/next_offset прямо в Block (Issue #138).
/// Issue #138: LinkedListNode удалена; prev_offset/next_offset теперь прямые поля Block.
/// Note (Issue #120): Поля protected, тип проверяется через index_type.
static bool test_p2_list_node_default_types()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // address_traits и index_type алиасы
    static_assert( std::is_same<Block::address_traits, A>::value, "address_traits must be A" );
    static_assert( std::is_same<Block::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

    // Block contains prev/next as direct fields (2 * sizeof(uint32_t) = 8 bytes)
    // Issue #138: Block = TreeNode(24) + prev(4) + next(4) = 32 bytes
    static_assert( sizeof( Block ) == sizeof( pmm::TreeNode<A> ) + 2 * sizeof( std::uint32_t ),
                   "Block<Default> must be TreeNode + 2 index_type fields" );

    return true;
}

/// @brief Block prev/next fields have correct size for разные AddressTraits (Issue #138).
static bool test_p2_list_node_various_traits()
{
    // 8-bit: TreeNode(10+) + 2*1 = at least 12 bytes
    using Block8 = pmm::Block<pmm::AddressTraits<std::uint8_t, 8>>;
    static_assert( std::is_same<Block8::index_type, std::uint8_t>::value );
    static_assert( sizeof( Block8 ) >= 12, "Block<Tiny> must be at least 12 bytes" );

    // 16-bit: TreeNode(14) + 2*2 = 18 bytes
    using Block16 = pmm::Block<pmm::SmallAddressTraits>;
    static_assert( std::is_same<Block16::index_type, std::uint16_t>::value );
    static_assert( sizeof( Block16 ) == 18, "Block<Small> must be 18 bytes (Issue #138)" );

    // 32-bit (default): TreeNode(24) + 2*4 = 32 bytes
    using Block32 = pmm::Block<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Block32::index_type, std::uint32_t>::value );
    static_assert( sizeof( Block32 ) == 32, "Block<Default> must be 32 bytes (Issue #138)" );

    // 64-bit: TreeNode(48+) + 2*8 >= 64 bytes
    using Block64 = pmm::Block<pmm::LargeAddressTraits>;
    static_assert( std::is_same<Block64::index_type, std::uint64_t>::value );
    static_assert( sizeof( Block64 ) >= 64, "Block<Large> must be at least 64 bytes (Issue #138)" );

    return true;
}

/// @brief Смещения полей prev/next в Block<DefaultAddressTraits> (Issue #138).
/// Issue #120: поля protected, смещения проверяются через BlockStateBase::kOffset*.
/// Issue #138: prev/next come AFTER TreeNode fields (TreeNode is base class, fields come first).
static bool test_p2_list_node_offsets()
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;

    // Issue #138: TreeNode fields come first (TreeNode is base class)
    // prev_offset comes after TreeNode (sizeof(TreeNode<Default>) = 24)
    static_assert( BlockState::kOffsetPrevOffset == 24,
                   "prev_offset must be at offset 24 (after TreeNode, Issue #138)" );
    // next_offset follows prev_offset
    static_assert( BlockState::kOffsetNextOffset == 28, "next_offset must be at offset 28 (Issue #138)" );

    return true;
}

/// @brief Layout Block<DefaultAddressTraits>: prev/next fields after TreeNode (Issue #112, #138).
/// Issue #120: поля protected, проверяем layout через BlockStateBase::kOffset*.
static bool test_p2_list_node_blockheader_compat()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Тип полей = index_type (Issue #120: поля protected)
    static_assert( std::is_same<BlockState::index_type, std::uint32_t>::value );

    // Issue #138: prev/next are Block own fields, come after TreeNode base class
    // sizeof(TreeNode<Default>) = 24
    static_assert( BlockState::kOffsetPrevOffset == 24, "Block::prev_offset must be at offset 24 (Issue #138)" );
    static_assert( BlockState::kOffsetNextOffset == 28, "Block::next_offset must be at offset 28 (Issue #138)" );

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
    using Node8 = pmm::TreeNode<pmm::AddressTraits<std::uint8_t, 8>>;
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
/// Issue #126: новый порядок полей — weight первым, avl_height/node_type в конце.
/// Issue #120: поля protected, смещения через BlockStateBase::kOffset*.
static bool test_p2_tree_node_offsets()
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;
    using Node       = pmm::TreeNode<pmm::DefaultAddressTraits>;

    // Issue #138: TreeNode is base class of Block, so TreeNode fields start at offset 0
    // Issue #126: weight — первое поле TreeNode (offset 0 в Block, Issue #138)
    static_assert( BlockState::kOffsetWeight == 0, "weight must be at offset 0 (Issue #138)" );
    static_assert( BlockState::kOffsetLeftOffset == sizeof( std::uint32_t ),
                   "left_offset must be at offset 4 (Issue #138)" );
    static_assert( BlockState::kOffsetRightOffset == 2 * sizeof( std::uint32_t ),
                   "right_offset must be at offset 8 (Issue #138)" );
    static_assert( BlockState::kOffsetParentOffset == 3 * sizeof( std::uint32_t ),
                   "parent_offset must be at offset 12 (Issue #138)" );
    static_assert( BlockState::kOffsetRootOffset == 4 * sizeof( std::uint32_t ),
                   "root_offset must be at offset 16 (Issue #138)" );
    // Issue #126: avl_height follows weight+left+right+parent+root (5 x uint32_t = 20 bytes)
    static_assert( BlockState::kOffsetAvlHeight == 5 * sizeof( std::uint32_t ),
                   "avl_height must be at offset 20 (Issue #138)" );
    // Issue #126: node_type (renamed from _pad) follows avl_height (2 bytes)
    static_assert( BlockState::kOffsetNodeType == 5 * sizeof( std::uint32_t ) + 2,
                   "node_type must be at offset 22 (Issue #138)" );

    // Also verify TreeNode size
    static_assert( sizeof( Node ) == 24, "TreeNode<Default> must be 24 bytes" );

    return true;
}

/// @brief Layout TreeNode<DefaultAddressTraits> в составе Block<A> (Issue #112).
/// Issue #120: поля protected, layout через BlockStateBase::kOffset*.
/// Issue #126: новый порядок полей — weight первым, avl_height/node_type в конце.
static bool test_p2_tree_node_blockheader_compat()
{
    using A          = pmm::DefaultAddressTraits;
    using Node       = pmm::TreeNode<A>;
    using BlockState = pmm::BlockStateBase<A>;

    // Тип полей = index_type = uint32_t (Issue #120: поля protected)
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value );

    // Issue #138: Block inherits TreeNode — TreeNode fields come first in Block layout.
    // Проверяем через BlockStateBase::kOffset* (Issue #126: новый порядок, #138: TreeNode first):
    static_assert( BlockState::kOffsetWeight == 0, "weight position in Block (Issue #138)" );
    static_assert( BlockState::kOffsetLeftOffset == sizeof( std::uint32_t ),
                   "left_offset position in Block (Issue #138)" );
    static_assert( BlockState::kOffsetRightOffset == 2 * sizeof( std::uint32_t ),
                   "right_offset position in Block (Issue #138)" );
    static_assert( BlockState::kOffsetParentOffset == 3 * sizeof( std::uint32_t ),
                   "parent_offset position in Block (Issue #138)" );
    static_assert( BlockState::kOffsetRootOffset == 4 * sizeof( std::uint32_t ),
                   "root_offset position in Block (Issue #138)" );
    static_assert( BlockState::kOffsetAvlHeight == 5 * sizeof( std::uint32_t ),
                   "avl_height position in Block (Issue #138)" );
    static_assert( BlockState::kOffsetNodeType == 5 * sizeof( std::uint32_t ) + 2,
                   "node_type position in Block (Issue #138)" );

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

/// @brief Проверяем AddressTraits<uint8_t, 8>: LinkedListNode и TreeNode с 8-bit индексами.
/// Phase 2 v0.2: включает поля weight и root_offset (8-bit для AddressTraits<uint8_t, 8>).
/// Issue #120: доступ через BlockStateBase<AddressTraits<uint8_t, 8>>.
static bool test_p2_tiny_traits_nodes()
{
    using A          = pmm::AddressTraits<std::uint8_t, 8>;
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
    std::cout << "=== test_issue87_phase2 (Phase 2: TreeNode + Block prev/next fields, Issue #138) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P2-A: Block prev/next fields (Issue #138: LinkedListNode merged into Block) ---\n";
    PMM_RUN( "P2-A1: Block<Default> prev/next field types and size (Issue #138)", test_p2_list_node_default_types );
    PMM_RUN( "P2-A2: Block prev/next with various AddressTraits (8/16/32/64-bit, Issue #138)",
             test_p2_list_node_various_traits );
    PMM_RUN( "P2-A3: Block<Default> prev/next offsets (via BlockStateBase::kOffset*, Issue #138)",
             test_p2_list_node_offsets );
    PMM_RUN( "P2-A4: Block<Default> prev/next layout (Issue #112, #138)", test_p2_list_node_blockheader_compat );

    std::cout << "\n--- P2-B: TreeNode ---\n";
    PMM_RUN( "P2-B1: TreeNode<Default> types and size (incl. weight+root_offset)", test_p2_tree_node_default_types );
    PMM_RUN( "P2-B2: TreeNode with various AddressTraits (8/16/32/64-bit)", test_p2_tree_node_various_traits );
    PMM_RUN( "P2-B3: TreeNode<Default> field offsets (via BlockStateBase::kOffset*)", test_p2_tree_node_offsets );
    PMM_RUN( "P2-B4: TreeNode<Default> layout in Block<A> (Issue #112)", test_p2_tree_node_blockheader_compat );

    std::cout << "\n--- P2-C: Runtime initialization via state machine (Issue #120) ---\n";
    PMM_RUN( "P2-C1: Block prev/next runtime init via state machine (Issue #138)", test_p2_list_node_runtime_init );
    PMM_RUN( "P2-C2: TreeNode runtime init via state machine (incl. weight+root_offset)",
             test_p2_tree_node_runtime_init );
    PMM_RUN( "P2-C3: AddressTraits<uint8_t, 8> nodes (8-bit indices) via state machine", test_p2_tiny_traits_nodes );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
