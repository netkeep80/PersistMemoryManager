/**
 * @file test_issue87_phase3.cpp
 * @brief Тесты Phase 3: Block<AddressTraits> (Issue #87, #112, #120).
 *
 * Проверяет:
 *  - Block<A> наследует LinkedListNode<A> и TreeNode<A>.
 *  - Поля weight и root_offset доступны только через BlockStateBase API (Issue #120).
 *  - sizeof(Block<DefaultAddressTraits>) == 32 байта.
 *  - Размеры Block для разных AddressTraits (8/16/32/64-bit).
 *  - Алиасы address_traits и index_type.
 *  - Инициализацию полей sentinel-значениями во время выполнения через BlockStateBase::init_fields.
 *
 * Issue #112: BlockHeader struct removed — Block<A> is the sole block type.
 * Issue #120: Block fields are protected — access only via BlockStateBase static API.
 *
 * @see include/pmm/block.h
 * @see include/pmm/block_state.h
 * @see plan_issue87.md §5 «Фаза 3: Block — блок как составной тип»
 * @version 0.4 (Issue #120 — fields protected, use BlockStateBase API)
 */

#include "pmm_single_threaded_heap.h"

#include <cassert>
#include <cstddef>
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

/// @brief Block<DefaultAddressTraits> — поля weight и root_offset имеют тип index_type (через BlockStateBase).
/// Phase 3 v0.4: Fields are protected (Issue #120); types verified via BlockStateBase::index_type.
static bool test_p3_block_treenode_field_types()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // index_type == uint32_t (fields weight and root_offset are of this type via TreeNode<A>)
    static_assert( std::is_same<BlockState::index_type, std::uint32_t>::value,
                   "BlockStateBase::index_type must be uint32_t" );

    return true;
}

// ─── P3-B: Block — размеры ────────────────────────────────────────────────────

/// @brief sizeof(Block<DefaultAddressTraits>) == 16 байт (Issue #136).
static bool test_p3_block_default_size_equals_blockheader()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // Issue #136: Block<DefaultAddressTraits> reduced from 32 to 16 bytes (1 granule).
    // FreeBlockData (prev/left/right/parent) stored after block header in data area.
    static_assert( sizeof( Block ) == 16, "Block<DefaultAddressTraits> must be 16 bytes (Issue #136)" );
    static_assert( sizeof( Block ) % pmm::kGranuleSize == 0, "Block<DefaultAddressTraits> must be granule-aligned" );

    return true;
}

/// @brief Block работает с разными AddressTraits (8/16/32/64-bit).
/// Phase 3 v0.4: weight+root_offset теперь в TreeNode, собственных полей у Block нет.
static bool test_p3_block_various_traits()
{
    // Issue #136: prev/left/right/parent_offset moved from block header to FreeBlockData.
    // Block = LinkedListNode + TreeNode only.
    // 8-bit:
    //   LinkedListNode<Tiny> = 1 byte (only next_offset)
    //   TreeNode<Tiny> = weight(1)+root_offset(1)+avl_height(2)+node_type(2) = 6 bytes
    //   Block = 1 + 6 = 7 bytes (+ possible padding to alignment)
    using Block8 = pmm::Block<pmm::TinyAddressTraits>;
    static_assert( std::is_same<Block8::index_type, std::uint8_t>::value );
    static_assert( sizeof( Block8 ) >= 7, "Block<Tiny> must be at least 7 bytes (Issue #136)" );

    // 16-bit:
    //   LinkedListNode<Small> = 2 bytes (only next_offset)
    //   TreeNode<Small> = weight(2)+root_offset(2)+avl_height(2)+node_type(2) = 8 bytes
    //   Block = 2 + 8 = 10 bytes
    using Block16 = pmm::Block<pmm::SmallAddressTraits>;
    static_assert( std::is_same<Block16::index_type, std::uint16_t>::value );
    static_assert( sizeof( Block16 ) == 10, "Block<Small> must be 10 bytes (Issue #136)" );

    // 32-bit (default):
    //   LinkedListNode<Default> = 4 bytes (only next_offset)
    //   TreeNode<Default> = weight(4)+root_offset(4)+avl_height(2)+node_type(2) = 12 bytes
    //   Block = 4 + 12 = 16 bytes
    using Block32 = pmm::Block<pmm::DefaultAddressTraits>;
    static_assert( std::is_same<Block32::index_type, std::uint32_t>::value );
    static_assert( sizeof( Block32 ) == 16, "Block<Default> must be 16 bytes (Issue #136)" );

    // 64-bit:
    //   LinkedListNode<Large> = 8 bytes (only next_offset)
    //   TreeNode<Large> = weight(8)+root_offset(8)+avl_height(2)+node_type(2)+[4 pad] = 24 bytes
    //   Block = 8 + 24 = 32 bytes
    using Block64 = pmm::Block<pmm::LargeAddressTraits>;
    static_assert( std::is_same<Block64::index_type, std::uint64_t>::value );
    static_assert( sizeof( Block64 ) >= 28, "Block<Large> must be at least 28 bytes (Issue #136)" );

    return true;
}

// ─── P3-C: Block — поля доступны через BlockStateBase API ────────────────────

/// @brief Поля LinkedListNode доступны через BlockStateBase API (Issue #120).
/// Issue #136: для свободных блоков нужен буфер Block<A> + FreeBlockData<A>.
static bool test_p3_block_list_node_fields()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Issue #136: буфер Block<A> + FreeBlockData<A> для free blocks.
    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> ) + sizeof( pmm::FreeBlockData<A> )] = {};

    // Инициализация через BlockStateBase::init_fields
    BlockState::init_fields( buf, A::no_block, A::no_block, 0, 0, 0 );

    PMM_TEST( BlockState::get_prev_offset( buf ) == pmm::detail::kNoBlock );
    PMM_TEST( BlockState::get_next_offset( buf ) == pmm::detail::kNoBlock );

    // Изменение через статические сеттеры
    BlockState::set_prev_offset_of( buf, 5u );
    BlockState::set_next_offset_of( buf, 10u );
    PMM_TEST( BlockState::get_prev_offset( buf ) == 5u );
    PMM_TEST( BlockState::get_next_offset( buf ) == 10u );

    return true;
}

/// @brief Поля TreeNode (включая weight и root_offset) доступны через BlockStateBase API (Issue #120).
/// Issue #136: left/right/parent_offset перемещены в FreeBlockData.
static bool test_p3_block_tree_node_fields()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Issue #136: буфер Block<A> + FreeBlockData<A> для free blocks.
    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> ) + sizeof( pmm::FreeBlockData<A> )] = {};

    // Инициализация через BlockStateBase::init_fields (weight=0 => free block => FreeBlockData initialized)
    BlockState::init_fields( buf, A::no_block, A::no_block, 0, 0u, 0u );

    PMM_TEST( BlockState::get_left_offset( buf ) == pmm::detail::kNoBlock );
    PMM_TEST( BlockState::get_right_offset( buf ) == pmm::detail::kNoBlock );
    PMM_TEST( BlockState::get_parent_offset( buf ) == pmm::detail::kNoBlock );
    PMM_TEST( BlockState::get_avl_height( buf ) == 0 );
    PMM_TEST( BlockState::get_weight( buf ) == 0u );
    PMM_TEST( BlockState::get_root_offset( buf ) == 0u );

    // Изменение через статические сеттеры
    BlockState::set_left_offset_of( buf, 3u );
    BlockState::set_right_offset_of( buf, 7u );
    BlockState::set_parent_offset_of( buf, 1u );
    BlockState::set_avl_height_of( buf, 2 );
    BlockState::set_weight_of( buf, 15u );
    BlockState::set_root_offset_of( buf, 0u ); // свободный
    PMM_TEST( BlockState::get_left_offset( buf ) == 3u );
    PMM_TEST( BlockState::get_right_offset( buf ) == 7u );
    PMM_TEST( BlockState::get_parent_offset( buf ) == 1u );
    PMM_TEST( BlockState::get_avl_height( buf ) == 2 );
    PMM_TEST( BlockState::get_weight( buf ) == 15u );
    PMM_TEST( BlockState::get_root_offset( buf ) == 0u );

    return true;
}

// ─── P3-D: Block — собственные поля ──────────────────────────────────────────

/// @brief Поля weight и root_offset (из TreeNode) инициализируются корректно через BlockStateBase API.
/// Phase 3 v0.4: Fields are protected (Issue #120); use BlockStateBase static API.
static bool test_p3_block_treenode_fields_runtime()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Issue #136: буфер Block<A> + FreeBlockData<A> для free blocks (weight=0).
    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> ) + sizeof( pmm::FreeBlockData<A> )] = {};

    // Свободный блок: weight == 0, root_offset == 0
    BlockState::init_fields( buf, A::no_block, A::no_block, 0, 0u, 0u );
    PMM_TEST( BlockState::get_weight( buf ) == 0u );
    PMM_TEST( BlockState::get_root_offset( buf ) == 0u );

    // Блок с весом
    BlockState::set_weight_of( buf, 10u );
    BlockState::set_root_offset_of( buf, 0u );
    PMM_TEST( BlockState::get_weight( buf ) == 10u );
    PMM_TEST( BlockState::get_root_offset( buf ) == 0u );

    // Занятый блок: weight > 0 — только block header needed
    alignas( pmm::Block<A> ) std::uint8_t buf2[sizeof( pmm::Block<A> )] = {};
    BlockState::init_fields( buf2, A::no_block, A::no_block, 0, 42u, 20u );
    PMM_TEST( BlockState::get_weight( buf2 ) == 42u );
    PMM_TEST( BlockState::get_root_offset( buf2 ) == 20u );

    return true;
}

/// @brief Block<TinyAddressTraits> с 8-bit полями weight и root_offset (через BlockStateBase API).
/// Phase 3 v0.4: Fields are protected (Issue #120); use BlockStateBase static API.
static bool test_p3_block_tiny_traits()
{
    using A          = pmm::TinyAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    static_assert( std::is_same<BlockState::index_type, std::uint8_t>::value );

    // Issue #136: буфер Block<A> + FreeBlockData<A> для free blocks (weight=0).
    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> ) + sizeof( pmm::FreeBlockData<A> )] = {};

    // Инициализация через BlockStateBase::init_fields
    BlockState::init_fields( buf, A::no_block, 0u, 0, 0u, 0u );

    PMM_TEST( BlockState::get_prev_offset( buf ) == 0xFFU );
    PMM_TEST( BlockState::get_next_offset( buf ) == 0u );
    PMM_TEST( BlockState::get_weight( buf ) == 0u );
    PMM_TEST( BlockState::get_root_offset( buf ) == 0u );

    // Установка веса
    BlockState::set_weight_of( buf, 5u );
    PMM_TEST( BlockState::get_weight( buf ) == 5u );

    // Занятый tiny-блок
    BlockState::set_weight_of( buf, 3u );
    BlockState::set_root_offset_of( buf, 10u );
    PMM_TEST( BlockState::get_weight( buf ) == 3u );
    PMM_TEST( BlockState::get_root_offset( buf ) == 10u );

    return true;
}

// ─── P3-E: Block — типы полей (Issue #112, #120) ─────────────────────────────

/// @brief Типы полей Block<Default>: index_type == uint32_t (Issue #112, #120).
static bool test_p3_block_weight_type_matches_blockheader()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // index_type must be uint32_t (fields weight and root_offset are of this type via TreeNode<A>)
    static_assert( std::is_same<BlockState::index_type, std::uint32_t>::value, "index_type must be uint32_t" );

    return true;
}

// ─── P3-F: Block layout offsets (Issue #120) ─────────────────────────────────

/// @brief Смещения полей Block<Default> (через BlockStateBase::kOffset*) корректны (Issue #120, #136).
static bool test_p3_block_layout_offsets()
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;

    // Issue #136: Block header reduced from 32 to 16 bytes.
    // LinkedListNode has only next_offset (4 bytes); prev_offset moved to FreeBlockData.
    static_assert( BlockState::kOffsetNextOffset == 0 );
    // Issue #126: weight moved to first field of TreeNode; Issue #136: now at offset 4 (after LLNode)
    static_assert( BlockState::kOffsetWeight == 4 );
    static_assert( BlockState::kOffsetRootOffset == 8 );
    // Issue #126: avl_height and node_type (renamed from _pad) at end of TreeNode header
    static_assert( BlockState::kOffsetAvlHeight == 12 );
    static_assert( BlockState::kOffsetNodeType == 14 );
    // Issue #136: prev/left/right/parent_offset in FreeBlockData, stored after block header
    static_assert( BlockState::kOffsetPrevOffset == 16 );
    static_assert( BlockState::kOffsetLeftOffset == 20 );
    static_assert( BlockState::kOffsetRightOffset == 24 );
    static_assert( BlockState::kOffsetParentOffset == 28 );

    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_phase3 (Phase 3: Block<AddressTraits>, Issue #120) ===\n\n";
    bool all_passed = true;

    std::cout << "--- P3-A: Block — inheritance and aliases ---\n";
    PMM_RUN( "P3-A1: Block<Default> inherits LinkedListNode and TreeNode", test_p3_block_inherits_nodes );
    PMM_RUN( "P3-A2: Block<Default> index_type is uint32_t (Issue #120)", test_p3_block_treenode_field_types );

    std::cout << "\n--- P3-B: Block — sizes ---\n";
    PMM_RUN( "P3-B1: Block<Default> size == 32 bytes (Issue #112)", test_p3_block_default_size_equals_blockheader );
    PMM_RUN( "P3-B2: Block with various AddressTraits (8/16/32/64-bit)", test_p3_block_various_traits );

    std::cout << "\n--- P3-C: Block — fields accessible via BlockStateBase API (Issue #120) ---\n";
    PMM_RUN( "P3-C1: LinkedListNode fields via BlockStateBase API", test_p3_block_list_node_fields );
    PMM_RUN( "P3-C2: TreeNode fields via BlockStateBase API", test_p3_block_tree_node_fields );

    std::cout << "\n--- P3-D: Block — TreeNode fields (weight+root_offset) ---\n";
    PMM_RUN( "P3-D1: Block weight+root_offset (from TreeNode) runtime via API", test_p3_block_treenode_fields_runtime );
    PMM_RUN( "P3-D2: Block<TinyAddressTraits> 8-bit fields via BlockStateBase API", test_p3_block_tiny_traits );

    std::cout << "\n--- P3-E: Block — field types (Issue #112, #120) ---\n";
    PMM_RUN( "P3-E1: Block index_type is uint32_t (Issue #112, #120)", test_p3_block_weight_type_matches_blockheader );

    std::cout << "\n--- P3-F: Block — layout offsets (Issue #120) ---\n";
    PMM_RUN( "P3-F1: Block<Default> layout offsets via BlockStateBase::kOffset*", test_p3_block_layout_offsets );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
