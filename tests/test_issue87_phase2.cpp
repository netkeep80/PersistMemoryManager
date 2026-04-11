/**
 * @file test_issue87_phase2.cpp
 * @brief Тесты Phase 2: TreeNode<A>.
 *
 * Проверяет:
 *  - Типы полей TreeNode зависят от AddressTraits::index_type.
 *  - Размеры структур для разных AddressTraits (8/16/32/64-bit).
 *  - Смещения полей внутри Block<A> (через BlockStateBase::kOffset*).
 *  - Layout Block<DefaultAddressTraits> как составного типа.
 *  - Алиасы address_traits и index_type.
 *  - Наличие полей weight и root_offset в TreeNode (Phase 2 v0.2).
 *  - Доступ к полям только через state machine.
 *  - LinkedListNode удалена; prev_offset/next_offset прямо в Block.
 *
 * Note: Поля TreeNode и Block теперь protected.
 * Доступ к ним осуществляется только через BlockStateBase и его наследников.
 *
 * @see include/pmm/block.h
 * @see include/pmm/tree_node.h
 * @see include/pmm/block_state.h
 * @version 0.4
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

#include <limits>
#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// Phase 2 tests: Block prev/next fields
// =============================================================================

// ─── P2-A: Block prev/next fields — типы и размеры ───────────────────────────

/// @brief Block<DefaultAddressTraits> — поля prev_offset/next_offset прямо в Block.
/// LinkedListNode удалена; prev_offset/next_offset теперь прямые поля Block.
/// Note: Поля protected, тип проверяется через index_type.
TEST_CASE( "P2-A1: Block<Default> prev/next field types and size ", "[test_issue87_phase2]" )
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // address_traits и index_type алиасы
    static_assert( std::is_same<Block::address_traits, A>::value, "address_traits must be A" );
    static_assert( std::is_same<Block::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

    // Block contains prev/next as direct fields (2 * sizeof(uint32_t) = 8 bytes)
    // Block = TreeNode(24) + prev(4) + next(4) = 32 bytes
    static_assert( sizeof( Block ) == sizeof( pmm::TreeNode<A> ) + 2 * sizeof( std::uint32_t ),
                   "Block<Default> must be TreeNode + 2 index_type fields" );
}

/// @brief Block prev/next fields have correct size for разные AddressTraits.
TEST_CASE( "P2-A2: Block prev/next with various AddressTraits (8/16/32/64-bit, Issue #138)", "[test_issue87_phase2]" )
{
    // 8-bit: TreeNode(10+) + 2*1 = at least 12 bytes
    using Block8 = pmm::Block<pmm::AddressTraits<std::uint8_t, 8>>;
    static_assert( std::is_same<Block8::index_type, std::uint8_t>::value );
    static_assert( sizeof( Block8 ) >= 12, "Block<Tiny> must be at least 12 bytes" );

    // 16-bit: TreeNode(14) + 2*2 = 18 bytes
    using Block16 = pmm::Block<pmm::SmallAddressTraits>;
    static_assert( std::is_same<Block16::index_type, std::uint16_t>::value );
    static_assert( sizeof( Block16 ) == 18, "Block<Small> must be 18 bytes " );

    // 32-bit (default): TreeNode(24) + 2*4 = 32 bytes
    using Block32 = pmm::Block<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Block32::index_type, std::uint32_t>::value );
    static_assert( sizeof( Block32 ) == 32, "Block<Default> must be 32 bytes " );

    // 64-bit: TreeNode(48+) + 2*8 >= 64 bytes
    using Block64 = pmm::Block<pmm::LargeAddressTraits>;
    static_assert( std::is_same<Block64::index_type, std::uint64_t>::value );
    static_assert( sizeof( Block64 ) >= 64, "Block<Large> must be at least 64 bytes " );
}

/// @brief Смещения полей prev/next в Block<DefaultAddressTraits>.
/// Поля protected, смещения проверяются через BlockStateBase::kOffset*.
/// Prev/next come AFTER TreeNode fields (TreeNode is base class, fields come first).
TEST_CASE( "P2-A3: Block<Default> prev/next offsets (via BlockStateBase::kOffset*, Issue #138)",
           "[test_issue87_phase2]" )
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;

    // TreeNode fields come first (TreeNode is base class)
    // prev_offset comes after TreeNode (sizeof(TreeNode<Default>) = 24)
    static_assert( BlockState::kOffsetPrevOffset == 24, "prev_offset must be at offset 24 (after TreeNode)" );
    // next_offset follows prev_offset
    static_assert( BlockState::kOffsetNextOffset == 28, "next_offset must be at offset 28 " );
}

/// @brief Layout Block<DefaultAddressTraits>: prev/next fields after TreeNode.
/// Поля protected, проверяем layout через BlockStateBase::kOffset*.
TEST_CASE( "P2-A4: Block<Default> prev/next layout ", "[test_issue87_phase2]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Тип полей = index_type
    static_assert( std::is_same<BlockState::index_type, std::uint32_t>::value );

    // Prev/next are Block own fields, come after TreeNode base class
    // sizeof(TreeNode<Default>) = 24
    static_assert( BlockState::kOffsetPrevOffset == 24, "Block::prev_offset must be at offset 24 " );
    static_assert( BlockState::kOffsetNextOffset == 28, "Block::next_offset must be at offset 28 " );
}

// =============================================================================
// Phase 2 tests: TreeNode
// =============================================================================

// ─── P2-B: TreeNode — типы и размеры ────────────────────────────────────────

/// @brief TreeNode<DefaultAddressTraits> — типы полей и размер.
/// Phase 2 v0.2: TreeNode теперь содержит поля weight и root_offset.
/// Поля protected, тип проверяется через index_type.
TEST_CASE( "P2-B1: TreeNode<Default> types and size (incl. weight+root_offset)", "[test_issue87_phase2]" )
{
    using A    = pmm::DefaultAddressTraits;
    using Node = pmm::TreeNode<A>;

    // address_traits и index_type алиасы
    static_assert( std::is_same<Node::address_traits, A>::value, "address_traits must be A" );
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

    // Тип AVL-полей = index_type = uint32_t
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value,
                   "TreeNode::index_type must be uint32_t for DefaultAddressTraits" );

    // Размер: 3 * sizeof(uint32_t) + sizeof(int16_t) + sizeof(uint16_t)
    //         + sizeof(uint32_t) + sizeof(uint32_t)
    //       = 12 + 2 + 2 + 4 + 4 = 24 байта
    static_assert( sizeof( Node ) == 24, "TreeNode<Default> must be 24 bytes" );
}

/// @brief TreeNode работает с разными AddressTraits.
/// Phase 2 v0.2: учитывает дополнительные поля weight и root_offset.
/// Поля protected, тип проверяется через index_type.
TEST_CASE( "P2-B2: TreeNode with various AddressTraits (8/16/32/64-bit)", "[test_issue87_phase2]" )
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
}

/// @brief Смещения полей TreeNode<DefaultAddressTraits>.
/// Новый порядок полей — weight первым, avl_height/node_type в конце.
/// Поля protected, смещения через BlockStateBase::kOffset*.
TEST_CASE( "P2-B3: TreeNode<Default> field offsets (via BlockStateBase::kOffset*)", "[test_issue87_phase2]" )
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;
    using Node       = pmm::TreeNode<pmm::DefaultAddressTraits>;

    // TreeNode is base class of Block, so TreeNode fields start at offset 0
    // Weight — первое поле TreeNode (offset 0 в Block)
    static_assert( BlockState::kOffsetWeight == 0, "weight must be at offset 0 " );
    static_assert( BlockState::kOffsetLeftOffset == sizeof( std::uint32_t ), "left_offset must be at offset 4 " );
    static_assert( BlockState::kOffsetRightOffset == 2 * sizeof( std::uint32_t ), "right_offset must be at offset 8 " );
    static_assert( BlockState::kOffsetParentOffset == 3 * sizeof( std::uint32_t ),
                   "parent_offset must be at offset 12 " );
    static_assert( BlockState::kOffsetRootOffset == 4 * sizeof( std::uint32_t ), "root_offset must be at offset 16 " );
    // Avl_height follows weight+left+right+parent+root (5 x uint32_t = 20 bytes)
    static_assert( BlockState::kOffsetAvlHeight == 5 * sizeof( std::uint32_t ), "avl_height must be at offset 20 " );
    // Node_type (renamed from _pad) follows avl_height (2 bytes)
    static_assert( BlockState::kOffsetNodeType == 5 * sizeof( std::uint32_t ) + 2, "node_type must be at offset 22 " );

    // Also verify TreeNode size
    static_assert( sizeof( Node ) == 24, "TreeNode<Default> must be 24 bytes" );
}

/// @brief Layout TreeNode<DefaultAddressTraits> в составе Block<A>.
/// Поля protected, layout через BlockStateBase::kOffset*.
/// Новый порядок полей — weight первым, avl_height/node_type в конце.
TEST_CASE( "P2-B4: TreeNode<Default> layout in Block<A> ", "[test_issue87_phase2]" )
{
    using A          = pmm::DefaultAddressTraits;
    using Node       = pmm::TreeNode<A>;
    using BlockState = pmm::BlockStateBase<A>;

    // Тип полей = index_type = uint32_t
    static_assert( std::is_same<Node::index_type, std::uint32_t>::value );

    // Block inherits TreeNode — TreeNode fields come first in Block layout.
    // Проверяем через BlockStateBase::kOffset*:
    static_assert( BlockState::kOffsetWeight == 0, "weight position in Block " );
    static_assert( BlockState::kOffsetLeftOffset == sizeof( std::uint32_t ), "left_offset position in Block " );
    static_assert( BlockState::kOffsetRightOffset == 2 * sizeof( std::uint32_t ), "right_offset position in Block " );
    static_assert( BlockState::kOffsetParentOffset == 3 * sizeof( std::uint32_t ), "parent_offset position in Block " );
    static_assert( BlockState::kOffsetRootOffset == 4 * sizeof( std::uint32_t ), "root_offset position in Block " );
    static_assert( BlockState::kOffsetAvlHeight == 5 * sizeof( std::uint32_t ), "avl_height position in Block " );
    static_assert( BlockState::kOffsetNodeType == 5 * sizeof( std::uint32_t ) + 2, "node_type position in Block " );
}

// =============================================================================
// Phase 2 tests: runtime field initialization via state machine
// =============================================================================

/// @brief Проверяем, что поля LinkedListNode инициализируются через state machine.
/// Прямой доступ к полям запрещён, используем BlockStateBase.
TEST_CASE( "P2-C1: Block prev/next runtime init via state machine ", "[test_issue87_phase2]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Выделяем буфер размером Block<A> и инициализируем через state machine
    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> )] = {};
    BlockState::init_fields( buf, A::no_block, A::no_block, 0, 0, 0 );

    REQUIRE( BlockState::get_prev_offset( buf ) == pmm::detail::kNoBlock );
    REQUIRE( BlockState::get_next_offset( buf ) == pmm::detail::kNoBlock );

    // Установить конкретные индексы
    BlockState::repair_prev_offset( buf, 10u );
    BlockState::set_next_offset_of( buf, 20u );
    REQUIRE( BlockState::get_prev_offset( buf ) == 10u );
    REQUIRE( BlockState::get_next_offset( buf ) == 20u );
}

/// @brief Проверяем, что поля TreeNode инициализируются через state machine.
/// Прямой доступ к полям запрещён, используем BlockStateBase.
TEST_CASE( "P2-C2: TreeNode runtime init via state machine (incl. weight+root_offset)", "[test_issue87_phase2]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Выделяем буфер и инициализируем через state machine
    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> )] = {};

    // Свободный блок: weight=0, root_offset=0
    BlockState::init_fields( buf, A::no_block, A::no_block, 0, 0, 0 );

    REQUIRE( BlockState::get_left_offset( buf ) == pmm::detail::kNoBlock );
    REQUIRE( BlockState::get_right_offset( buf ) == pmm::detail::kNoBlock );
    REQUIRE( BlockState::get_parent_offset( buf ) == pmm::detail::kNoBlock );
    REQUIRE( BlockState::get_avl_height( buf ) == 0 );
    REQUIRE( BlockState::get_weight( buf ) == 0u );

    // Проверка через BlockStateBase instance getters
    auto* bs = reinterpret_cast<BlockState*>( buf );
    REQUIRE( bs->weight() == 0u );
    REQUIRE( bs->root_offset() == 0u );
    REQUIRE( bs->is_free() );

    // Установить поля через reset_avl_fields_of / set methods
    BlockState::reset_avl_fields_of( buf );
    REQUIRE( BlockState::get_left_offset( buf ) == pmm::detail::kNoBlock );
    REQUIRE( BlockState::get_avl_height( buf ) == 0 );
}

/// @brief Проверяем AddressTraits<uint8_t, 8>: LinkedListNode и TreeNode с 8-bit индексами.
/// Phase 2 v0.2: включает поля weight и root_offset (8-bit для AddressTraits<uint8_t, 8>).
/// Доступ через BlockStateBase<AddressTraits<uint8_t, 8>>.
TEST_CASE( "P2-C3: AddressTraits<uint8_t, 8> nodes (8-bit indices) via state machine", "[test_issue87_phase2]" )
{
    using A          = pmm::AddressTraits<std::uint8_t, 8>;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> )] = {};

    // Инициализация через state machine
    BlockState::init_fields( buf, A::no_block, 0u, 1, 0, 0 );

    REQUIRE( BlockState::get_prev_offset( buf ) == 0xFFU ); // A::no_block
    REQUIRE( BlockState::get_next_offset( buf ) == 0U );
    REQUIRE( BlockState::get_avl_height( buf ) == 1 );
    REQUIRE( BlockState::get_weight( buf ) == 0U );

    auto* bs = reinterpret_cast<BlockState*>( buf );
    REQUIRE( bs->root_offset() == 0U );
    REQUIRE( bs->is_free() );
}

// =============================================================================
// main
// =============================================================================
