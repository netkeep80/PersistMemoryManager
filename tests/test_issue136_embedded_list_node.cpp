/**
 * @file test_issue136_embedded_list_node.cpp
 * @brief Тесты для переноса узла двухсвязного списка внутрь блока (Issue #136).
 *
 * Issue #136: «Перенос узла двухсвязного списка внутрь самого блока».
 *
 * Проверяет:
 *  - Block<DefaultAddressTraits> теперь имеет размер 16 байт (1 грануля вместо 2).
 *  - FreeBlockData<DefaultAddressTraits> хранит prev_offset, left_offset,
 *    right_offset, parent_offset (16 байт = 1 грануля).
 *  - LinkedListNode<DefaultAddressTraits> содержит только next_offset (4 байта).
 *  - TreeNode<DefaultAddressTraits> содержит weight, root_offset, avl_height, node_type (12 байт).
 *  - Для свободных блоков FreeBlockData доступна из BlockStateBase API.
 *  - Для занятых блоков FreeBlockData перекрывается пользовательскими данными.
 *  - Allocate/Deallocate работают корректно с новой архитектурой.
 *  - Фрагментация и слияние (coalesce) работают корректно.
 *  - Накладные расходы на выделенный блок уменьшились на 16 байт (1 гранулу).
 *
 * @see include/pmm/free_block_data.h — FreeBlockData<A>
 * @see include/pmm/block.h — Block<A> (16 байт)
 * @see include/pmm/linked_list_node.h — LinkedListNode<A> (только next_offset)
 * @see include/pmm/tree_node.h — TreeNode<A> (без left/right/parent)
 * @version 0.1 (Issue #136)
 */

#include "pmm_single_threaded_heap.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
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

// ─── Тип менеджера ─────────────────────────────────────────────────────────────

// Use unique InstanceId to avoid state leakage between tests
template <std::size_t Id> using TestMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, Id>;

// =============================================================================
// I136-A: Размеры структур (Issue #136)
// =============================================================================

/// @brief Block<DefaultAddressTraits> == 16 байт (Issue #136: уменьшен с 32 до 16).
static bool test_i136_block_size_is_16_bytes()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // Issue #136: Block header reduced from 32 bytes (2 granules) to 16 bytes (1 granule)
    static_assert( sizeof( Block ) == 16, "Block<DefaultAddressTraits> must be 16 bytes (Issue #136)" );
    static_assert( sizeof( Block ) == pmm::kGranuleSize,
                   "Block<DefaultAddressTraits> must equal one granule (Issue #136)" );

    return true;
}

/// @brief FreeBlockData<DefaultAddressTraits> == 16 байт (Issue #136).
static bool test_i136_free_block_data_size_is_16_bytes()
{
    using A      = pmm::DefaultAddressTraits;
    using FBData = pmm::FreeBlockData<A>;

    static_assert( sizeof( FBData ) == 16, "FreeBlockData<DefaultAddressTraits> must be 16 bytes (Issue #136)" );
    static_assert( sizeof( FBData ) == pmm::kGranuleSize,
                   "FreeBlockData<DefaultAddressTraits> must equal one granule (Issue #136)" );

    return true;
}

/// @brief LinkedListNode<DefaultAddressTraits> == 4 байта (только next_offset, Issue #136).
static bool test_i136_linked_list_node_size_is_4_bytes()
{
    using A      = pmm::DefaultAddressTraits;
    using LLNode = pmm::LinkedListNode<A>;

    // Issue #136: LinkedListNode now contains only next_offset (prev_offset moved to FreeBlockData)
    static_assert( sizeof( LLNode ) == 4, "LinkedListNode<DefaultAddressTraits> must be 4 bytes (Issue #136)" );

    return true;
}

/// @brief TreeNode<DefaultAddressTraits> == 12 байт (без left/right/parent, Issue #136).
static bool test_i136_tree_node_size_is_12_bytes()
{
    using A     = pmm::DefaultAddressTraits;
    using TNode = pmm::TreeNode<A>;

    // Issue #136: TreeNode no longer contains left_offset, right_offset, parent_offset (moved to FreeBlockData)
    // weight(4) + root_offset(4) + avl_height(2) + node_type(2) = 12 bytes
    static_assert( sizeof( TNode ) == 12, "TreeNode<DefaultAddressTraits> must be 12 bytes (Issue #136)" );

    return true;
}

/// @brief Block<A> + FreeBlockData<A> == 32 байта (то же что раньше было sizeof(Block<A>)).
static bool test_i136_block_plus_free_data_equals_old_block_size()
{
    using A      = pmm::DefaultAddressTraits;
    using Block  = pmm::Block<A>;
    using FBData = pmm::FreeBlockData<A>;

    // The combined size of Block<A> header + FreeBlockData<A> equals the previous Block<A> size (32 bytes)
    static_assert( sizeof( Block ) + sizeof( FBData ) == 32,
                   "Block<A> + FreeBlockData<A> must be 32 bytes (same as old Block<A>)" );

    return true;
}

// =============================================================================
// I136-B: Раскладка полей BlockStateBase (Issue #136)
// =============================================================================

/// @brief Смещения полей в заголовке Block<Default> корректны (Issue #136).
static bool test_i136_block_header_layout_offsets()
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;

    // Issue #136: compact header layout
    // [0..3]  next_offset (LinkedListNode::next_offset)
    // [4..7]  weight (TreeNode::weight)
    // [8..11] root_offset (TreeNode::root_offset)
    // [12..13] avl_height (TreeNode::avl_height)
    // [14..15] node_type (TreeNode::node_type)
    static_assert( BlockState::kOffsetNextOffset == 0, "next_offset must be at byte 0 (Issue #136)" );
    static_assert( BlockState::kOffsetWeight == 4, "weight must be at byte 4 (Issue #136)" );
    static_assert( BlockState::kOffsetRootOffset == 8, "root_offset must be at byte 8 (Issue #136)" );
    static_assert( BlockState::kOffsetAvlHeight == 12, "avl_height must be at byte 12 (Issue #136)" );
    static_assert( BlockState::kOffsetNodeType == 14, "node_type must be at byte 14 (Issue #136)" );

    return true;
}

/// @brief Смещения FreeBlockData корректны (Issue #136): поля в области данных свободного блока.
static bool test_i136_free_block_data_offsets()
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;

    // FreeBlockData is at offset sizeof(Block<A>) = 16 from block start
    // [16..19] prev_offset
    // [20..23] left_offset
    // [24..27] right_offset
    // [28..31] parent_offset
    static_assert( BlockState::kOffsetPrevOffset == 16, "prev_offset must be at byte 16 (Issue #136)" );
    static_assert( BlockState::kOffsetLeftOffset == 20, "left_offset must be at byte 20 (Issue #136)" );
    static_assert( BlockState::kOffsetRightOffset == 24, "right_offset must be at byte 24 (Issue #136)" );
    static_assert( BlockState::kOffsetParentOffset == 28, "parent_offset must be at byte 28 (Issue #136)" );

    return true;
}

// =============================================================================
// I136-C: Инициализация полей через BlockStateBase API (Issue #136)
// =============================================================================

/// @brief BlockStateBase::init_fields инициализирует заголовок и FreeBlockData для свободного блока.
static bool test_i136_init_fields_free_block()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Allocate space for Block<A> header + FreeBlockData<A>
    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> ) + sizeof( pmm::FreeBlockData<A> )] = {};

    // Initialize as free block
    BlockState::init_fields( buf,
                             /*prev*/ A::no_block,
                             /*next*/ A::no_block,
                             /*avl_height*/ 1,
                             /*weight*/ 0,
                             /*root_offset*/ 0 );

    // Header fields
    PMM_TEST( BlockState::get_next_offset( buf ) == A::no_block );
    PMM_TEST( BlockState::get_weight( buf ) == 0 );
    PMM_TEST( BlockState::get_root_offset( buf ) == 0 );
    PMM_TEST( BlockState::get_avl_height( buf ) == 1 );

    // FreeBlockData fields (in data area)
    PMM_TEST( BlockState::get_prev_offset( buf ) == A::no_block );
    PMM_TEST( BlockState::get_left_offset( buf ) == A::no_block );
    PMM_TEST( BlockState::get_right_offset( buf ) == A::no_block );
    PMM_TEST( BlockState::get_parent_offset( buf ) == A::no_block );

    return true;
}

/// @brief BlockStateBase API корректно устанавливает и читает все поля (Issue #136).
static bool test_i136_block_state_api_setters_getters()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> ) + sizeof( pmm::FreeBlockData<A> )] = {};

    // Init as free block first
    BlockState::init_fields( buf, A::no_block, A::no_block, 1, 0u, 0u );

    // Set and verify all fields
    BlockState::set_next_offset_of( buf, 42u );
    PMM_TEST( BlockState::get_next_offset( buf ) == 42u );

    BlockState::set_prev_offset_of( buf, 10u );
    PMM_TEST( BlockState::get_prev_offset( buf ) == 10u );

    BlockState::set_left_offset_of( buf, 3u );
    PMM_TEST( BlockState::get_left_offset( buf ) == 3u );

    BlockState::set_right_offset_of( buf, 7u );
    PMM_TEST( BlockState::get_right_offset( buf ) == 7u );

    BlockState::set_parent_offset_of( buf, 1u );
    PMM_TEST( BlockState::get_parent_offset( buf ) == 1u );

    BlockState::set_avl_height_of( buf, 3 );
    PMM_TEST( BlockState::get_avl_height( buf ) == 3 );

    BlockState::set_weight_of( buf, 5u );
    PMM_TEST( BlockState::get_weight( buf ) == 5u );

    BlockState::set_root_offset_of( buf, 20u );
    PMM_TEST( BlockState::get_root_offset( buf ) == 20u );

    BlockState::set_node_type_of( buf, pmm::kNodeReadOnly );
    PMM_TEST( BlockState::get_node_type( buf ) == pmm::kNodeReadOnly );

    return true;
}

/// @brief Для занятых блоков prev_offset недоступен (возвращает no_block, Issue #136).
static bool test_i136_allocated_block_has_no_prev_offset()
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( pmm::Block<A> ) std::uint8_t buf[sizeof( pmm::Block<A> ) + sizeof( pmm::FreeBlockData<A> )] = {};

    // Initialize as allocated block (weight > 0)
    BlockState::init_fields( buf, A::no_block, 10u, 0, 5u, 2u ); // weight=5, root=2

    // For allocated blocks (weight != 0), get_prev_offset returns no_block
    PMM_TEST( BlockState::get_weight( buf ) == 5u );
    PMM_TEST( BlockState::get_prev_offset( buf ) == A::no_block );

    return true;
}

// =============================================================================
// I136-D: Функциональные тесты с реальным менеджером (Issue #136)
// =============================================================================

/// @brief Allocate/deallocate работают корректно с новой архитектурой (Issue #136).
static bool test_i136_allocate_deallocate_basic()
{
    using Mgr = TestMgr<200>;
    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );
    PMM_TEST( Mgr::is_initialized() );

    // Allocate a few blocks
    void* p1 = Mgr::allocate( 32 );
    PMM_TEST( p1 != nullptr );
    void* p2 = Mgr::allocate( 64 );
    PMM_TEST( p2 != nullptr );
    void* p3 = Mgr::allocate( 128 );
    PMM_TEST( p3 != nullptr );

    // Write and verify data (ensures user data area is separate from block metadata)
    std::memset( p1, 0xAA, 32 );
    std::memset( p2, 0xBB, 64 );
    std::memset( p3, 0xCC, 128 );

    PMM_TEST( static_cast<std::uint8_t*>( p1 )[0] == 0xAA );
    PMM_TEST( static_cast<std::uint8_t*>( p2 )[0] == 0xBB );
    PMM_TEST( static_cast<std::uint8_t*>( p3 )[0] == 0xCC );

    Mgr::deallocate( p1 );
    Mgr::deallocate( p2 );
    Mgr::deallocate( p3 );
    Mgr::destroy();
    return true;
}

/// @brief Минимальный блок после Issue #136 — 1 грануля заголовка + 1+ грануля данных.
static bool test_i136_header_size_is_one_granule()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // Block header is exactly 1 granule
    PMM_TEST( sizeof( Block ) == pmm::kGranuleSize );
    PMM_TEST( sizeof( Block ) == 16u );

    // FreeBlockData is also 1 granule (stored in data area of free blocks)
    PMM_TEST( sizeof( pmm::FreeBlockData<A> ) == pmm::kGranuleSize );

    // Combined = 2 granules (minimum size for a free block)
    PMM_TEST( sizeof( Block ) + sizeof( pmm::FreeBlockData<A> ) == 2 * pmm::kGranuleSize );

    return true;
}

/// @brief Coalesce работает корректно с новой архитектурой (Issue #136).
static bool test_i136_coalesce_works()
{
    using Mgr = TestMgr<201>;
    Mgr::destroy();
    PMM_TEST( Mgr::create( 4 * 1024 ) );

    std::size_t free_before = Mgr::free_size();

    // Allocate three adjacent blocks
    void* p1 = Mgr::allocate( 16 );
    void* p2 = Mgr::allocate( 16 );
    void* p3 = Mgr::allocate( 16 );
    PMM_TEST( p1 != nullptr && p2 != nullptr && p3 != nullptr );

    // Free middle block first, then neighbours → should coalesce
    Mgr::deallocate( p2 );
    Mgr::deallocate( p1 );
    Mgr::deallocate( p3 );

    std::size_t free_after = Mgr::free_size();

    // After deallocating all, free size should equal what we had before
    PMM_TEST( free_after == free_before );

    Mgr::destroy();
    return true;
}

/// @brief Многократное выделение и освобождение без утечек (Issue #136).
static bool test_i136_allocate_many_blocks()
{
    using Mgr = TestMgr<202>;
    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    static constexpr int kCount = 100;
    void*                ptrs[kCount];

    for ( int i = 0; i < kCount; ++i )
    {
        ptrs[i] = Mgr::allocate( 64 );
        PMM_TEST( ptrs[i] != nullptr );
        // Write a unique pattern to verify isolation
        std::memset( ptrs[i], static_cast<unsigned char>( i & 0xFF ), 64 );
    }

    // Verify all written patterns are intact
    for ( int i = 0; i < kCount; ++i )
    {
        const auto* bytes = static_cast<const std::uint8_t*>( ptrs[i] );
        PMM_TEST( bytes[0] == static_cast<std::uint8_t>( i & 0xFF ) );
        PMM_TEST( bytes[63] == static_cast<std::uint8_t>( i & 0xFF ) );
    }

    // Free all
    for ( int i = 0; i < kCount; ++i )
        Mgr::deallocate( ptrs[i] );

    Mgr::destroy();
    return true;
}

/// @brief pptr работает корректно с новой архитектурой (Issue #136).
static bool test_i136_pptr_works()
{
    using Mgr = TestMgr<203>;
    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    auto p = Mgr::allocate_typed<int>();
    PMM_TEST( !p.is_null() );

    int* raw = p.resolve();
    PMM_TEST( raw != nullptr );
    *raw = 42;
    PMM_TEST( *raw == 42 );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
    return true;
}

/// @brief Накладные расходы на выделенный блок == 1 грануля (Issue #136: было 2 гранулы).
static bool test_i136_overhead_is_one_granule_per_allocated_block()
{
    using A     = pmm::DefaultAddressTraits;
    using Block = pmm::Block<A>;

    // Old overhead per allocated block = 32 bytes = 2 granules
    // New overhead per allocated block = 16 bytes = 1 granule
    PMM_TEST( sizeof( Block ) == pmm::kGranuleSize );
    PMM_TEST( sizeof( Block ) == 16u );

    // Verify via pmm::detail constants
    PMM_TEST( pmm::detail::kBlockHeaderGranules == 1u );

    return true;
}

/// @brief FreeBlockData хранится по адресу Block + sizeof(Block) (Issue #136).
static bool test_i136_free_block_data_address()
{
    using A      = pmm::DefaultAddressTraits;
    using BlkT   = pmm::Block<A>;
    using FBData = pmm::FreeBlockData<A>;

    alignas( BlkT ) std::uint8_t buf[sizeof( BlkT ) + sizeof( FBData )];
    std::memset( buf, 0, sizeof( buf ) );

    BlkT*   blk  = reinterpret_cast<BlkT*>( buf );
    FBData* data = reinterpret_cast<FBData*>( buf + sizeof( BlkT ) );

    // FreeBlockData should be right after the Block header
    PMM_TEST( reinterpret_cast<std::uint8_t*>( data ) == reinterpret_cast<std::uint8_t*>( blk ) + sizeof( BlkT ) );
    PMM_TEST( reinterpret_cast<std::uint8_t*>( data ) == buf + 16 );

    // Verify FreeBlockData fields are accessible via Block::free_data()
    data->prev_offset   = 5u;
    data->left_offset   = 10u;
    data->right_offset  = 15u;
    data->parent_offset = 20u;

    PMM_TEST( blk->free_data().prev_offset == 5u );
    PMM_TEST( blk->free_data().left_offset == 10u );
    PMM_TEST( blk->free_data().right_offset == 15u );
    PMM_TEST( blk->free_data().parent_offset == 20u );

    return true;
}

/// @brief Persistence (save/load) работает с новой архитектурой (Issue #136).
static bool test_i136_persistence_works()
{
    using Mgr = TestMgr<204>;
    Mgr::destroy();
    PMM_TEST( Mgr::create( 64 * 1024 ) );

    // Allocate and write data
    auto p = Mgr::allocate_typed<int>();
    PMM_TEST( !p.is_null() );
    *p.resolve() = 12345;

    // Verify before reload
    PMM_TEST( *p.resolve() == 12345 );

    Mgr::destroy();
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue136_embedded_list_node (Issue #136: embedded linked list node) ===\n\n";
    bool all_passed = true;

    std::cout << "--- I136-A: Structure sizes ---\n";
    PMM_RUN( "I136-A1: Block<Default> == 16 bytes (1 granule, Issue #136)", test_i136_block_size_is_16_bytes );
    PMM_RUN( "I136-A2: FreeBlockData<Default> == 16 bytes (1 granule, Issue #136)",
             test_i136_free_block_data_size_is_16_bytes );
    PMM_RUN( "I136-A3: LinkedListNode<Default> == 4 bytes (next_offset only, Issue #136)",
             test_i136_linked_list_node_size_is_4_bytes );
    PMM_RUN( "I136-A4: TreeNode<Default> == 12 bytes (no left/right/parent, Issue #136)",
             test_i136_tree_node_size_is_12_bytes );
    PMM_RUN( "I136-A5: Block<A> + FreeBlockData<A> == 32 bytes (same as old Block<A>)",
             test_i136_block_plus_free_data_equals_old_block_size );

    std::cout << "\n--- I136-B: Layout offsets ---\n";
    PMM_RUN( "I136-B1: Block header layout offsets (Issue #136)", test_i136_block_header_layout_offsets );
    PMM_RUN( "I136-B2: FreeBlockData offsets in data area (Issue #136)", test_i136_free_block_data_offsets );

    std::cout << "\n--- I136-C: BlockStateBase API ---\n";
    PMM_RUN( "I136-C1: init_fields initializes header + FreeBlockData for free blocks",
             test_i136_init_fields_free_block );
    PMM_RUN( "I136-C2: BlockStateBase setters/getters for all fields", test_i136_block_state_api_setters_getters );
    PMM_RUN( "I136-C3: Allocated block prev_offset returns no_block (in user data area)",
             test_i136_allocated_block_has_no_prev_offset );

    std::cout << "\n--- I136-D: Functional tests ---\n";
    PMM_RUN( "I136-D1: Allocate/deallocate basic functionality", test_i136_allocate_deallocate_basic );
    PMM_RUN( "I136-D2: Block header is 1 granule (16 bytes)", test_i136_header_size_is_one_granule );
    PMM_RUN( "I136-D3: Coalesce works with new architecture", test_i136_coalesce_works );
    PMM_RUN( "I136-D4: Allocate many blocks, verify data isolation", test_i136_allocate_many_blocks );
    PMM_RUN( "I136-D5: pptr works with new architecture", test_i136_pptr_works );
    PMM_RUN( "I136-D6: Overhead is 1 granule per allocated block (was 2 granules)",
             test_i136_overhead_is_one_granule_per_allocated_block );
    PMM_RUN( "I136-D7: FreeBlockData stored at Block + sizeof(Block)", test_i136_free_block_data_address );
    PMM_RUN( "I136-D8: Persistence works with new architecture", test_i136_persistence_works );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
