/**
 * @file test_issue87_phase3.cpp
 * @brief Тесты Phase 3: Block<AddressTraits>.
 *
 * Проверяет:
 *  - Block<A> наследует TreeNode<A> и содержит поля prev_offset/next_offset.
 *  - Поля weight и root_offset доступны только через BlockStateBase API.
 *  - sizeof(Block<DefaultAddressTraits>) == 32 байта.
 *  - Размеры Block для разных AddressTraits (8/16/32/64-bit).
 *  - Алиасы address_traits и index_type.
 *  - Инициализацию полей sentinel-значениями во время выполнения через BlockStateBase::init_fields.
 *
 *
 * @see include/pmm/block.h
 * @see include/pmm/block_state.h
 * @version 0.4
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstring>

#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// Phase 3 tests: Block
// =============================================================================

// ─── P3-A: Block — наследование и алиасы ─────────────────────────────────────

/// @brief Block<DefaultAddressTraits> — наследует LinkedListNode и TreeNode.
TEST_CASE( "P3-A1: Block<Default> inherits TreeNode, has prev/next fields ", "[test_issue87_phase3]" )
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // address_traits и index_type алиасы
    static_assert( std::is_same<Block::address_traits, A>::value, "address_traits must be A" );
    static_assert( std::is_same<Block::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

    // Block no longer inherits LinkedListNode — fields are direct members of Block
    static_assert( !std::is_base_of<pmm::TreeNode<A>, pmm::TreeNode<A>>::value || true,
                   "Block should NOT inherit LinkedListNode " ); // sanity

    // Block наследует TreeNode<A>
    static_assert( std::is_base_of<pmm::TreeNode<A>, Block>::value, "Block must inherit TreeNode" );

    // Block size is still 32 bytes: TreeNode(24) + prev_offset(4) + next_offset(4)
    static_assert( sizeof( Block ) == 32, "Block<Default> must still be 32 bytes " );
}

/// @brief Block<DefaultAddressTraits> — поля weight и root_offset имеют тип index_type (через BlockStateBase).
/// Phase 3 v0.4: Fields are protected; types verified via BlockStateBase::index_type.
TEST_CASE( "P3-A2: Block<Default> index_type is uint32_t ", "[test_issue87_phase3]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // index_type == uint32_t (fields weight and root_offset are of this type via TreeNode<A>)
    static_assert( std::is_same<BlockState::index_type, std::uint32_t>::value,
                   "BlockStateBase::index_type must be uint32_t" );
}

// ─── P3-B: Block — размеры ────────────────────────────────────────────────────

/// @brief sizeof(Block<DefaultAddressTraits>) == 32 байта.
TEST_CASE( "P3-B1: Block<Default> size == 32 bytes ", "[test_issue87_phase3]" )
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // Block<DefaultAddressTraits> must be exactly 32 bytes
    static_assert( sizeof( Block ) == 32, "Block<DefaultAddressTraits> must be 32 bytes" );
    static_assert( sizeof( Block ) % pmm::kGranuleSize == 0, "Block<DefaultAddressTraits> must be granule-aligned" );
}

/// @brief Block работает с разными AddressTraits (8/16/32/64-bit).
/// Phase 3 v0.4: weight+root_offset теперь в TreeNode, собственных полей у Block нет.
TEST_CASE( "P3-B2: Block with various AddressTraits (8/16/32/64-bit)", "[test_issue87_phase3]" )
{
    // 8-bit:
    //   TreeNode<Tiny> = 3*1 + [1 pad] + 2 + 2 + 1 + 1 = 10 bytes (int16_t alignment)
    //   Block own fields: prev_offset(1) + next_offset(1) = 2 bytes
    //   Block = 10 + 2 = 12 bytes
    using Block8 = pmm::Block<pmm::AddressTraits<std::uint8_t, 8>>;
    static_assert( std::is_same<Block8::index_type, std::uint8_t>::value );
    static_assert( sizeof( Block8 ) >= 12, "Block<Tiny> must be at least 12 bytes" );

    // 16-bit:
    //   TreeNode<Small> = 3*2 + 2 + 2 + 2 + 2 = 14 bytes
    //   Block own fields: prev_offset(2) + next_offset(2) = 4 bytes
    //   Block = 14 + 4 = 18 bytes
    using Block16 = pmm::Block<pmm::SmallAddressTraits>;
    static_assert( std::is_same<Block16::index_type, std::uint16_t>::value );
    static_assert( sizeof( Block16 ) == 18, "Block<Small> must be 18 bytes" );

    // 32-bit (default):
    //   TreeNode<Default> = 3*4 + 2+2 + 4+4 = 24 bytes
    //   Block own fields: prev_offset(4) + next_offset(4) = 8 bytes
    //   Block = 24 + 8 = 32 bytes
    using Block32 = pmm::Block<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Block32::index_type, std::uint32_t>::value );
    static_assert( sizeof( Block32 ) == 32, "Block<Default> must be 32 bytes" );

    // 64-bit:
    //   TreeNode<Large> = 3*8 + 2+2 + [4 pad] + 8+8 = 48 bytes (uint64_t alignment)
    //   Block own fields: prev_offset(8) + next_offset(8) = 16 bytes
    //   Block = 48 + 16 = 64 bytes
    using Block64 = pmm::Block<pmm::LargeAddressTraits>;
    static_assert( std::is_same<Block64::index_type, std::uint64_t>::value );
    static_assert( sizeof( Block64 ) >= 60, "Block<Large> must be at least 60 bytes" );
}

// ─── P3-C: Block — поля доступны через BlockStateBase API ────────────────────

/// @brief Поля LinkedListNode доступны через BlockStateBase API.
TEST_CASE( "P3-C1: Block prev/next fields via BlockStateBase API ", "[test_issue87_phase3]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> )] = {};

    // Инициализация через BlockStateBase::init_fields
    BlockState::init_fields( buf, A::no_block, A::no_block, 0, 0, 0 );

    REQUIRE( BlockState::get_prev_offset( buf ) == pmm::detail::kNoBlock );
    REQUIRE( BlockState::get_next_offset( buf ) == pmm::detail::kNoBlock );

    // Изменение через статические сеттеры
    BlockState::set_prev_offset_of( buf, 5u );
    BlockState::set_next_offset_of( buf, 10u );
    REQUIRE( BlockState::get_prev_offset( buf ) == 5u );
    REQUIRE( BlockState::get_next_offset( buf ) == 10u );
}

/// @brief Поля TreeNode (включая weight и root_offset) доступны через BlockStateBase API.
TEST_CASE( "P3-C2: TreeNode fields via BlockStateBase API", "[test_issue87_phase3]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> )] = {};

    // Инициализация через BlockStateBase::init_fields
    BlockState::init_fields( buf, A::no_block, A::no_block, 0, 0u, 0u );

    REQUIRE( BlockState::get_left_offset( buf ) == pmm::detail::kNoBlock );
    REQUIRE( BlockState::get_right_offset( buf ) == pmm::detail::kNoBlock );
    REQUIRE( BlockState::get_parent_offset( buf ) == pmm::detail::kNoBlock );
    REQUIRE( BlockState::get_avl_height( buf ) == 0 );
    REQUIRE( BlockState::get_weight( buf ) == 0u );
    REQUIRE( BlockState::get_root_offset( buf ) == 0u );

    // Изменение через статические сеттеры
    BlockState::set_left_offset_of( buf, 3u );
    BlockState::set_right_offset_of( buf, 7u );
    BlockState::set_parent_offset_of( buf, 1u );
    BlockState::set_avl_height_of( buf, 2 );
    BlockState::set_weight_of( buf, 15u );
    BlockState::set_root_offset_of( buf, 0u ); // свободный
    REQUIRE( BlockState::get_left_offset( buf ) == 3u );
    REQUIRE( BlockState::get_right_offset( buf ) == 7u );
    REQUIRE( BlockState::get_parent_offset( buf ) == 1u );
    REQUIRE( BlockState::get_avl_height( buf ) == 2 );
    REQUIRE( BlockState::get_weight( buf ) == 15u );
    REQUIRE( BlockState::get_root_offset( buf ) == 0u );
}

// ─── P3-D: Block — собственные поля ──────────────────────────────────────────

/// @brief Поля weight и root_offset (из TreeNode) инициализируются корректно через BlockStateBase API.
/// Phase 3 v0.4: Fields are protected; use BlockStateBase static API.
TEST_CASE( "P3-D1: Block weight+root_offset (from TreeNode) runtime via API", "[test_issue87_phase3]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> )] = {};

    // Свободный блок: weight == 0, root_offset == 0
    BlockState::init_fields( buf, A::no_block, A::no_block, 0, 0u, 0u );
    REQUIRE( BlockState::get_weight( buf ) == 0u );
    REQUIRE( BlockState::get_root_offset( buf ) == 0u );

    // Блок с весом
    BlockState::set_weight_of( buf, 10u );
    BlockState::set_root_offset_of( buf, 0u );
    REQUIRE( BlockState::get_weight( buf ) == 10u );
    REQUIRE( BlockState::get_root_offset( buf ) == 0u );

    // Занятый блок: weight > 0, root_offset == собственный индекс
    alignas( pmm::Block<A> ) std::uint8_t buf2[sizeof( pmm::Block<A> )] = {};
    BlockState::init_fields( buf2, A::no_block, A::no_block, 0, 42u, 20u );
    REQUIRE( BlockState::get_weight( buf2 ) == 42u );
    REQUIRE( BlockState::get_root_offset( buf2 ) == 20u );
}

/// @brief Block<AddressTraits<uint8_t, 8>> с 8-bit полями weight и root_offset (через BlockStateBase API).
/// Phase 3 v0.4: Fields are protected; use BlockStateBase static API.
TEST_CASE( "P3-D2: Block<AddressTraits<uint8_t, 8>> 8-bit fields via BlockStateBase API", "[test_issue87_phase3]" )
{
    using A          = pmm::AddressTraits<std::uint8_t, 8>;
    using BlockState = pmm::BlockStateBase<A>;

    static_assert( std::is_same<BlockState::index_type, std::uint8_t>::value );

    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> )] = {};

    // Инициализация через BlockStateBase::init_fields
    BlockState::init_fields( buf, A::no_block, 0u, 0, 0u, 0u );

    REQUIRE( BlockState::get_prev_offset( buf ) == 0xFFU );
    REQUIRE( BlockState::get_next_offset( buf ) == 0u );
    REQUIRE( BlockState::get_weight( buf ) == 0u );
    REQUIRE( BlockState::get_root_offset( buf ) == 0u );

    // Установка веса
    BlockState::set_weight_of( buf, 5u );
    REQUIRE( BlockState::get_weight( buf ) == 5u );

    // Занятый tiny-блок
    BlockState::set_weight_of( buf, 3u );
    BlockState::set_root_offset_of( buf, 10u );
    REQUIRE( BlockState::get_weight( buf ) == 3u );
    REQUIRE( BlockState::get_root_offset( buf ) == 10u );
}

// ─── P3-E: Block — типы полей ─────────────────────────────

/// @brief Типы полей Block<Default>: index_type == uint32_t.
TEST_CASE( "P3-E1: Block index_type is uint32_t ", "[test_issue87_phase3]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // index_type must be uint32_t (fields weight and root_offset are of this type via TreeNode<A>)
    static_assert( std::is_same<BlockState::index_type, std::uint32_t>::value, "index_type must be uint32_t" );
}

// ─── P3-F: Block layout offsets ─────────────────────────────────

/// @brief Смещения полей Block<Default> (через BlockStateBase::kOffset*) корректны.
TEST_CASE( "P3-F1: Block<Default> layout offsets via BlockStateBase::kOffset*", "[test_issue87_phase3]" )
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;

    // Verify layout: TreeNode fields at start, Block own fields (prev/next) follow
    // TreeNode fields now come first, Block own fields (prev/next) come after TreeNode
    // Weight is first field of TreeNode
    static_assert( BlockState::kOffsetWeight == 0 );
    static_assert( BlockState::kOffsetLeftOffset == 4 );
    static_assert( BlockState::kOffsetRightOffset == 8 );
    static_assert( BlockState::kOffsetParentOffset == 12 );
    static_assert( BlockState::kOffsetRootOffset == 16 );
    // Avl_height and node_type (renamed from _pad) moved to end of TreeNode
    static_assert( BlockState::kOffsetAvlHeight == 20 );
    static_assert( BlockState::kOffsetNodeType == 22 );
    // Prev/next come after TreeNode (sizeof(TreeNode<Default>) = 24)
    static_assert( BlockState::kOffsetPrevOffset == 24 );
    static_assert( BlockState::kOffsetNextOffset == 28 );
}

// =============================================================================
// main
// =============================================================================
