/**
 * @file persist_memory_manager.h
 * @brief Менеджер персистентной кучи памяти для C++17
 *
 * Single-header библиотека управления персистентной памятью.
 * Предоставляет низкоуровневый менеджер памяти, хранящий все метаданные
 * в управляемой области памяти для возможности персистентности между запусками.
 *
 * Алгоритм (Issue #55): Каждый блок содержит 6 ключевых полей:
 *   1. size         — занятый размер данных в гранулах (0 = свободный блок, Issue #75)
 *   2. prev_offset  — индекс предыдущего блока (kNoBlock = нет)
 *   3. next_offset  — индекс следующего блока (kNoBlock = последний)
 *   4. left_offset  — левый дочерний узел AVL-дерева свободных блоков
 *   5. right_offset — правый дочерний узел AVL-дерева свободных блоков
 *   6. parent_offset — родительский узел AVL-дерева
 *
 * Поиск свободного блока: best-fit через AVL-дерево (O(log n)).
 * При освобождении: слияние с соседними свободными блоками.
 *
 * @version 6.0.0
 * Block modernization (Issue #69): removed BlockHeader.magic; validity via is_valid_block()
 * structural invariants (size < total_gran, prev<idx<next, avl_height<32, distinct AVL refs).
 * kMagic updated to "PMM_V040"; load() calls repair_linked_list()+recompute_counters() for recovery.
 *
 * Refactoring (Issue #75): BlockHeader.used_size → size; BlockHeader._reserved → root_offset.
 *   root_offset=0: block belongs to free-blocks tree. root_offset=own_idx: allocated block is
 *   root of its own AVL tree (PAP forest groundwork). kMagic updated to "PMM_V050".
 *   PAP homogenization: ManagerHeader now resides inside BlockHeader_0 (the first allocated block
 *   at granule index 0). Layout: [BlockHeader_0][ManagerHeader][BlockHeader_1][user_data...]
 *   BlockHeader_0 has size=kManagerHeaderGranules, root_offset=0 (own index). kMagic → "PMM_V060".
 *
 * Optimizations (Issue #57): O(1) header_from_ptr, coalesce (max 2 removes + 1 insert),
 *   actual-padding in allocate_from_block, last_block_offset in ManagerHeader.
 *
 * Optimizations (Issue #59): 16-byte granule addressing; 32-bit granule indices (up to 64 GB);
 *   BlockHeader = 32 bytes; total_size via next_offset; pptr<T> = 4 bytes.
 *
 * Refactoring (Issue #61): fully static PersistMemoryManager; pptr<T>-only public API.
 *
 * Code review fixes (Issue #63): inline constexpr, ostream dump, kMagic version encoding,
 *   AVL height assert, header_from_ptr upper-bound, strict-weak-ordering tiebreaker,
 *   overflow guards in bytes_to_granules/allocate_typed/operator[]; constexpr pptr ctors.
 *
 * Bug fix (Issue #67): reallocate_typed re-derives old pointer after possible expand().
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <ostream>
#include <shared_mutex>

namespace pmm
{

class PersistMemoryManager;

inline constexpr std::size_t kGranuleSize      = 16; ///< Issue #59: granule size in bytes
inline constexpr std::size_t kDefaultAlignment = 16;
inline constexpr std::size_t kMinAlignment     = 16;
inline constexpr std::size_t kMaxAlignment     = 16; ///< Issue #59: only 16-byte alignment supported
inline constexpr std::size_t kMinMemorySize    = 4096;
inline constexpr std::size_t kMinBlockSize =
    48; ///< Minimum block size in bytes = BlockHeader (32) + 1 data granule (16)
inline constexpr std::uint64_t kMagic           = 0x504D4D5F56303630ULL; ///< "PMM_V060" (#75: homogeneous PAP)
inline constexpr std::size_t   kGrowNumerator   = 5;
inline constexpr std::size_t   kGrowDenominator = 4;

struct MemoryStats
{
    std::size_t total_blocks;
    std::size_t free_blocks;
    std::size_t allocated_blocks;
    std::size_t largest_free;
    std::size_t smallest_free;
    std::size_t total_fragmentation;
};

struct ManagerInfo
{
    std::uint64_t  magic;
    std::size_t    total_size;
    std::size_t    used_size;
    std::size_t    block_count;
    std::size_t    free_count;
    std::size_t    alloc_count;
    std::ptrdiff_t first_block_offset;
    std::ptrdiff_t first_free_offset; ///< Корень AVL-дерева свободных блоков
    std::ptrdiff_t last_free_offset;  ///< Не используется (совместимость)
    std::size_t    manager_header_size;
};

struct BlockView
{
    std::size_t    index;
    std::ptrdiff_t offset;      ///< Byte offset in PAS
    std::size_t    total_size;  ///< Total block size in bytes
    std::size_t    header_size; ///< BlockHeader size in bytes
    std::size_t    user_size;   ///< User data size in bytes
    std::size_t    alignment;   ///< Always kDefaultAlignment (Issue #59)
    bool           used;
};

/// @brief View of a single free block in the AVL tree for visualisation (Issue #65).
/// All _offset fields are byte offsets in the managed region, or -1 when absent.
struct FreeBlockView
{
    std::ptrdiff_t offset;
    std::size_t    total_size;
    std::size_t    free_size;
    std::ptrdiff_t left_offset;
    std::ptrdiff_t right_offset;
    std::ptrdiff_t parent_offset;
    int            avl_height;
    int            avl_depth;
};

namespace detail
{

/// @brief Block header (32 bytes = 2 granules, Issue #59). size=0: free; size>0: data granules.
/// root_offset=0: free-blocks tree; root_offset=own_idx: allocated block root (Issue #75).
/// Issue #69: magic removed; validity via is_valid_block(). total_size = next_offset - idx.
struct BlockHeader
{
    std::uint32_t size; ///< [1] Занятый размер в гранулах (0 = свободный блок, Issue #75)
    std::uint32_t prev_offset; ///< [2] Предыдущий блок (гранульный индекс)
    std::uint32_t next_offset; ///< [3] Следующий блок (гранульный индекс)
    std::uint32_t left_offset; ///< [4] Левый дочерний узел AVL-дерева (гранульный индекс)
    std::uint32_t right_offset; ///< [5] Правый дочерний узел AVL-дерева (гранульный индекс)
    std::uint32_t parent_offset; ///< [6] Родительский узел AVL-дерева (гранульный индекс)
    std::int16_t  avl_height; ///< Высота AVL-поддерева (0 = не в дереве)
    std::uint16_t _pad;       ///< Зарезервировано (Issue #69: previously held magic[3:2])
    std::uint32_t root_offset; ///< 0=свободный блок (дерево свободных); own_idx=занятый (Issue #75)
};

static_assert( sizeof( BlockHeader ) == 32, "BlockHeader must be exactly 32 bytes (Issue #59)" );
static_assert( sizeof( BlockHeader ) % kGranuleSize == 0, "BlockHeader must be granule-aligned (Issue #59)" );

/// @brief Число гранул в BlockHeader (2 гранулы = 32 байта)
inline constexpr std::uint32_t kBlockHeaderGranules = sizeof( BlockHeader ) / kGranuleSize;

// kBlockMagic removed (Issue #69): block validity now uses is_valid_block() structural invariants.
inline constexpr std::uint32_t kNoBlock = 0xFFFFFFFFU; ///< Sentinel: нет блока (гранульный индекс)

/// @brief Manager header (Issue #59: 64 bytes). All _offset fields are granule indices.
/// prev_base_ptr / prev_total_size are runtime-only (nulled by load() — not persisted).
struct ManagerHeader
{
    std::uint64_t magic;       ///< Магическое число менеджера
    std::uint64_t total_size;  ///< Полный размер управляемой области в байтах
    std::uint32_t used_size;   ///< Занятый размер в гранулах (Issue #59)
    std::uint32_t block_count; ///< Общее число блоков
    std::uint32_t free_count;  ///< Число свободных блоков
    std::uint32_t alloc_count; ///< Число занятых блоков
    std::uint32_t first_block_offset; ///< Первый блок (гранульный индекс)
    std::uint32_t last_block_offset; ///< [Issue #57 opt 4] Последний блок (гранульный индекс)
    std::uint32_t free_tree_root; ///< Корень AVL-дерева свободных блоков (гранульный индекс)
    bool          owns_memory;      ///< Менеджер владеет буфером (runtime-only)
    bool          prev_owns_memory; ///< prev_base_ptr был выделен менеджером (runtime-only)
    std::uint8_t  _pad[2];          ///< Выравнивание
    std::uint64_t prev_total_size;  ///< Размер предыдущего буфера в байтах (runtime-only)
    void* prev_base_ptr; ///< Указатель на предыдущий буфер (runtime-only; nulled on load)
};

static_assert( sizeof( ManagerHeader ) == 64, "ManagerHeader must be exactly 64 bytes (Issue #59)" );
static_assert( sizeof( ManagerHeader ) % kGranuleSize == 0, "ManagerHeader must be granule-aligned (Issue #59)" );

/// @brief Число гранул в ManagerHeader
inline constexpr std::uint32_t kManagerHeaderGranules = sizeof( ManagerHeader ) / kGranuleSize;

// ─── Конвертация байты ↔ гранулы ──────────────────────────────────────────────

/// @brief Перевести байты в гранулы (потолок). Returns 0 on overflow.
inline std::uint32_t bytes_to_granules( std::size_t bytes )
{
    if ( bytes > std::numeric_limits<std::size_t>::max() - ( kGranuleSize - 1 ) )
        return 0;
    std::size_t granules = ( bytes + kGranuleSize - 1 ) / kGranuleSize;
    if ( granules > std::numeric_limits<std::uint32_t>::max() )
        return 0;
    return static_cast<std::uint32_t>( granules );
}

/// @brief Перевести гранулы в байты.
inline std::size_t granules_to_bytes( std::uint32_t granules )
{
    return static_cast<std::size_t>( granules ) * kGranuleSize;
}

/// @brief Получить байтовое смещение из гранульного индекса.
inline std::size_t idx_to_byte_off( std::uint32_t idx )
{
    return static_cast<std::size_t>( idx ) * kGranuleSize;
}

/// @brief Получить гранульный индекс из байтового смещения (кратно kGranuleSize).
inline std::uint32_t byte_off_to_idx( std::size_t byte_off )
{
    assert( byte_off % kGranuleSize == 0 );
    assert( byte_off / kGranuleSize <= std::numeric_limits<std::uint32_t>::max() );
    return static_cast<std::uint32_t>( byte_off / kGranuleSize );
}

/// @brief Returns true only for kGranuleSize (16-byte) alignment.
inline bool is_valid_alignment( std::size_t align )
{
    return align == kGranuleSize;
}

/// @brief Получить указатель на BlockHeader по гранульному индексу.
inline BlockHeader* block_at( std::uint8_t* base, std::uint32_t idx )
{
    assert( idx != kNoBlock );
    return reinterpret_cast<BlockHeader*>( base + idx_to_byte_off( idx ) );
}

/// @brief Получить гранульный индекс BlockHeader.
inline std::uint32_t block_idx( const std::uint8_t* base, const BlockHeader* block )
{
    std::size_t byte_off = reinterpret_cast<const std::uint8_t*>( block ) - base;
    assert( byte_off % kGranuleSize == 0 );
    return static_cast<std::uint32_t>( byte_off / kGranuleSize );
}

/// @brief Вычислить total_size блока в гранулах.
/// Issue #59: total_size больше не хранится — вычисляется через next_offset.
inline std::uint32_t block_total_granules( const std::uint8_t* base, const ManagerHeader* hdr, const BlockHeader* blk )
{
    std::uint32_t this_idx = block_idx( base, blk );
    if ( blk->next_offset != kNoBlock )
        return blk->next_offset - this_idx;
    return byte_off_to_idx( static_cast<std::size_t>( hdr->total_size ) ) - this_idx;
}

/// @brief Issue #69: Structural block validity (replaces magic check).
/// Invariants: size<total_gran, prev<idx<next, avl_height<32, distinct AVL refs.
inline bool is_valid_block( const std::uint8_t* base, const ManagerHeader* hdr, std::uint32_t idx )
{
    if ( idx == kNoBlock )
        return false;
    if ( idx_to_byte_off( idx ) + sizeof( BlockHeader ) > hdr->total_size )
        return false;

    const BlockHeader* blk        = reinterpret_cast<const BlockHeader*>( base + idx_to_byte_off( idx ) );
    std::uint32_t      total_gran = ( blk->next_offset != kNoBlock )
                                        ? ( blk->next_offset - idx )
                                        : ( byte_off_to_idx( static_cast<std::size_t>( hdr->total_size ) ) - idx );
    if ( blk->size >= total_gran )
        return false;
    if ( blk->prev_offset != kNoBlock && blk->prev_offset >= idx )
        return false;
    if ( blk->next_offset != kNoBlock && blk->next_offset <= idx )
        return false;
    if ( blk->avl_height >= 32 )
        return false;
    const bool l = ( blk->left_offset != kNoBlock );
    const bool r = ( blk->right_offset != kNoBlock );
    const bool p = ( blk->parent_offset != kNoBlock );
    if ( ( l || r || p ) && ( ( l && r && blk->left_offset == blk->right_offset ) ||
                              ( l && p && blk->left_offset == blk->parent_offset ) ||
                              ( r && p && blk->right_offset == blk->parent_offset ) ) )
        return false;
    return true;
}

/// @brief Вычислить адрес пользовательских данных для блока (block + sizeof(BlockHeader)).
inline void* user_ptr( BlockHeader* block )
{
    return reinterpret_cast<std::uint8_t*>( block ) + sizeof( BlockHeader );
}

/// @brief O(1) get BlockHeader from user_ptr (ptr - sizeof(BlockHeader)); validated via is_valid_block().
inline BlockHeader* header_from_ptr( std::uint8_t* base, void* ptr, std::size_t total_size )
{
    if ( ptr == nullptr )
        return nullptr;
    std::uint8_t* raw_ptr = reinterpret_cast<std::uint8_t*>( ptr );
    // First user data starts after BlockHeader_0 + ManagerHeader + BlockHeader_1 (Issue #75)
    std::uint8_t* min_addr = base + sizeof( BlockHeader ) + sizeof( ManagerHeader ) + sizeof( BlockHeader );
    if ( raw_ptr < min_addr )
        return nullptr;
    if ( raw_ptr > base + total_size )
        return nullptr;
    std::uint8_t* cand_addr = raw_ptr - sizeof( BlockHeader );
    if ( ( reinterpret_cast<std::size_t>( cand_addr ) - reinterpret_cast<std::size_t>( base ) ) % kGranuleSize != 0 )
        return nullptr;
    std::uint32_t        cand_idx  = static_cast<std::uint32_t>( ( cand_addr - base ) / kGranuleSize );
    const ManagerHeader* hdr_const = reinterpret_cast<const ManagerHeader*>( base );
    if ( !is_valid_block( base, hdr_const, cand_idx ) )
        return nullptr;
    BlockHeader* cand = reinterpret_cast<BlockHeader*>( cand_addr );
    if ( cand->size == 0 )
        return nullptr;
    return cand;
}

/// @brief Minimum block granules for user_bytes (header + data, minimum 1 data granule).
inline std::uint32_t required_block_granules( std::size_t user_bytes )
{
    std::uint32_t data_granules = bytes_to_granules( user_bytes );
    if ( data_granules == 0 )
        data_granules = 1;
    return kBlockHeaderGranules + data_granules;
}

// ─── AVL-дерево свободных блоков (ключ: total_granules, затем index) ─────────

inline std::int32_t avl_height( std::uint8_t* base, std::uint32_t idx )
{
    return ( idx == kNoBlock ) ? 0 : block_at( base, idx )->avl_height;
}

inline void avl_update_height( std::uint8_t* base, BlockHeader* node )
{
    std::int32_t h = 1 + std::max( avl_height( base, node->left_offset ), avl_height( base, node->right_offset ) );
    assert( h <= std::numeric_limits<std::int16_t>::max() ); // tree height must fit in int16_t
    node->avl_height = static_cast<std::int16_t>( h );
}

inline std::int32_t avl_bf( std::uint8_t* base, BlockHeader* node )
{
    return avl_height( base, node->left_offset ) - avl_height( base, node->right_offset );
}

/// @brief Обновить ссылку parent → child в дереве.
inline void avl_set_child( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t parent, std::uint32_t old_child,
                           std::uint32_t new_child )
{
    if ( parent == kNoBlock )
    {
        hdr->free_tree_root = new_child;
        return;
    }
    BlockHeader* p = block_at( base, parent );
    if ( p->left_offset == old_child )
        p->left_offset = new_child;
    else
        p->right_offset = new_child;
}

inline std::uint32_t avl_rotate_right( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t y_idx )
{
    BlockHeader*  y     = block_at( base, y_idx );
    std::uint32_t x_idx = y->left_offset;
    BlockHeader*  x     = block_at( base, x_idx );
    std::uint32_t t2    = x->right_offset;

    x->right_offset  = y_idx;
    y->left_offset   = t2;
    x->parent_offset = y->parent_offset;
    y->parent_offset = x_idx;
    if ( t2 != kNoBlock )
        block_at( base, t2 )->parent_offset = y_idx;
    avl_set_child( base, hdr, x->parent_offset, y_idx, x_idx );
    avl_update_height( base, y );
    avl_update_height( base, x );
    return x_idx;
}

inline std::uint32_t avl_rotate_left( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t x_idx )
{
    BlockHeader*  x     = block_at( base, x_idx );
    std::uint32_t y_idx = x->right_offset;
    BlockHeader*  y     = block_at( base, y_idx );
    std::uint32_t t2    = y->left_offset;

    y->left_offset   = x_idx;
    x->right_offset  = t2;
    y->parent_offset = x->parent_offset;
    x->parent_offset = y_idx;
    if ( t2 != kNoBlock )
        block_at( base, t2 )->parent_offset = x_idx;
    avl_set_child( base, hdr, y->parent_offset, x_idx, y_idx );
    avl_update_height( base, x );
    avl_update_height( base, y );
    return y_idx;
}

inline void avl_rebalance_up( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t node_idx )
{
    std::uint32_t cur = node_idx;
    while ( cur != kNoBlock )
    {
        BlockHeader* node = block_at( base, cur );
        avl_update_height( base, node );
        std::int32_t bf = avl_bf( base, node );
        if ( bf > 1 )
        {
            if ( avl_bf( base, block_at( base, node->left_offset ) ) < 0 )
                avl_rotate_left( base, hdr, node->left_offset );
            cur = avl_rotate_right( base, hdr, cur );
        }
        else if ( bf < -1 )
        {
            if ( avl_bf( base, block_at( base, node->right_offset ) ) > 0 )
                avl_rotate_right( base, hdr, node->right_offset );
            cur = avl_rotate_left( base, hdr, cur );
        }
        cur = block_at( base, cur )->parent_offset;
    }
}

inline void avl_insert( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t blk_idx )
{
    BlockHeader* blk   = block_at( base, blk_idx );
    blk->left_offset   = kNoBlock;
    blk->right_offset  = kNoBlock;
    blk->parent_offset = kNoBlock;
    blk->avl_height    = 1;
    if ( hdr->free_tree_root == kNoBlock )
    {
        hdr->free_tree_root = blk_idx;
        return;
    }
    // Issue #59: cache total_gran once; compute blk size in granules before the traversal loop
    std::uint32_t total_gran = byte_off_to_idx( hdr->total_size );
    std::uint32_t blk_gran =
        ( blk->next_offset != kNoBlock ) ? ( blk->next_offset - blk_idx ) : ( total_gran - blk_idx );
    std::uint32_t cur = hdr->free_tree_root, parent = kNoBlock;
    bool          go_left = false;
    while ( cur != kNoBlock )
    {
        parent                = cur;
        BlockHeader*  n       = block_at( base, cur );
        std::uint32_t n_gran  = ( n->next_offset != kNoBlock ) ? ( n->next_offset - cur ) : ( total_gran - cur );
        bool          smaller = ( blk_gran < n_gran ) || ( blk_gran == n_gran && blk_idx < cur );
        go_left               = smaller;
        cur                   = smaller ? n->left_offset : n->right_offset;
    }
    blk->parent_offset = parent;
    if ( go_left )
        block_at( base, parent )->left_offset = blk_idx;
    else
        block_at( base, parent )->right_offset = blk_idx;
    avl_rebalance_up( base, hdr, parent );
}

inline std::uint32_t avl_min_node( std::uint8_t* base, std::uint32_t node_idx )
{
    while ( node_idx != kNoBlock )
    {
        std::uint32_t left = block_at( base, node_idx )->left_offset;
        if ( left == kNoBlock )
            break;
        node_idx = left;
    }
    return node_idx;
}

inline void avl_remove( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t blk_idx )
{
    BlockHeader*  blk    = block_at( base, blk_idx );
    std::uint32_t parent = blk->parent_offset;
    std::uint32_t left   = blk->left_offset;
    std::uint32_t right  = blk->right_offset;
    std::uint32_t rebal  = kNoBlock;

    if ( left == kNoBlock && right == kNoBlock )
    {
        avl_set_child( base, hdr, parent, blk_idx, kNoBlock );
        rebal = parent;
    }
    else if ( left == kNoBlock || right == kNoBlock )
    {
        std::uint32_t child                    = ( left != kNoBlock ) ? left : right;
        block_at( base, child )->parent_offset = parent;
        avl_set_child( base, hdr, parent, blk_idx, child );
        rebal = parent;
    }
    else
    {
        std::uint32_t succ_idx    = avl_min_node( base, right );
        BlockHeader*  succ        = block_at( base, succ_idx );
        std::uint32_t succ_parent = succ->parent_offset;
        std::uint32_t succ_right  = succ->right_offset;

        if ( succ_parent != blk_idx )
        {
            avl_set_child( base, hdr, succ_parent, succ_idx, succ_right );
            if ( succ_right != kNoBlock )
                block_at( base, succ_right )->parent_offset = succ_parent;
            succ->right_offset                     = right;
            block_at( base, right )->parent_offset = succ_idx;
            rebal                                  = succ_parent;
        }
        else
        {
            rebal = succ_idx;
        }
        succ->left_offset                     = left;
        block_at( base, left )->parent_offset = succ_idx;
        succ->parent_offset                   = parent;
        avl_set_child( base, hdr, parent, blk_idx, succ_idx );
        avl_update_height( base, succ );
    }
    blk->left_offset   = kNoBlock;
    blk->right_offset  = kNoBlock;
    blk->parent_offset = kNoBlock;
    blk->avl_height    = 0;
    avl_rebalance_up( base, hdr, rebal );
}

/// @brief Найти наименьший блок >= needed гранул (best-fit, O(log n)).
/// Issue #59: размеры в гранулах.
inline std::uint32_t avl_find_best_fit( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t needed_granules )
{
    // Issue #59: cache total_gran once to avoid repeated hdr->total_size reads in the hot path
    std::uint32_t total_gran = byte_off_to_idx( hdr->total_size );
    std::uint32_t cur = hdr->free_tree_root, result = kNoBlock;
    while ( cur != kNoBlock )
    {
        BlockHeader*  node     = block_at( base, cur );
        std::uint32_t cur_gran = ( node->next_offset != kNoBlock ) ? ( node->next_offset - cur ) : ( total_gran - cur );
        if ( cur_gran >= needed_granules )
        {
            result = cur;
            cur    = node->left_offset;
        }
        else
        {
            cur = node->right_offset;
        }
    }
    return result;
}

} // namespace detail

// ─── Персистный типизированный указатель ──────────────────────────────────────

/// @brief Persistent typed pointer (Issue #59: 4 bytes, granule index).
/// Index 0 means null. get() resolves via singleton. pptr++/-- are forbidden.
template <class T> class pptr
{
    std::uint32_t _idx; ///< Гранульный индекс пользовательских данных

  public:
    constexpr pptr() noexcept : _idx( 0 ) {}
    constexpr explicit pptr( std::uint32_t idx ) noexcept : _idx( idx ) {}
    constexpr pptr( const pptr<T>& ) noexcept               = default;
    constexpr pptr<T>& operator=( const pptr<T>& ) noexcept = default;
    ~pptr() noexcept                                        = default;

    // Pointer arithmetic is forbidden — pptr is not a random-access iterator
    pptr<T>& operator++()      = delete;
    pptr<T>  operator++( int ) = delete;
    pptr<T>& operator--()      = delete;
    pptr<T>  operator--( int ) = delete;

    constexpr bool          is_null() const noexcept { return _idx == 0; }
    constexpr explicit      operator bool() const noexcept { return _idx != 0; }
    constexpr std::uint32_t offset() const noexcept { return _idx; }

    /// @brief Разыменовать через синглтон PersistMemoryManager (Issue #61).
    inline T* get() const noexcept;
    inline T& operator*() const noexcept { return *get(); }
    inline T* operator->() const noexcept { return get(); }

    /// @brief Доступ к i-му элементу типа T с проверкой размера блока (Issue #59).
    /// Bounds-checked: returns nullptr if index i is out of range.
    inline T* operator[]( std::size_t i ) const noexcept;

    /// @brief Доступ к i-му элементу без проверки границ (внутреннее использование).
    inline T* get_at( std::size_t index ) const noexcept;

    constexpr bool operator==( const pptr<T>& other ) const noexcept { return _idx == other._idx; }
    constexpr bool operator!=( const pptr<T>& other ) const noexcept { return _idx != other._idx; }
};

// Issue #59: pptr<T> is 4 bytes (uint32_t granule index)
static_assert( sizeof( pptr<int> ) == 4, "sizeof(pptr<T>) must be 4 bytes (Issue #59)" );
static_assert( sizeof( pptr<double> ) == 4, "sizeof(pptr<T>) must be 4 bytes (Issue #59)" );

// ─── Основной класс ───────────────────────────────────────────────────────────

inline MemoryStats get_stats();
inline ManagerInfo get_manager_info();

/// @brief Менеджер персистентной памяти — полностью статический класс (Issue #61).
/// Public API: only pptr<T> and PersistMemoryManager::static_method().
class PersistMemoryManager
{
  public:
    // ─── Управление жизненным циклом ─────────────────────────────────────────

    /// @brief Создать менеджер на буфере memory (>= kMinMemorySize, granule-aligned).
    static bool create( void* memory, std::size_t size )
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( memory == nullptr || size < kMinMemorySize )
            return false;
        // Issue #59: max 64 GB (2^32 * kGranuleSize)
        if ( size > static_cast<std::uint64_t>( std::numeric_limits<std::uint32_t>::max() ) * kGranuleSize )
            return false;
        // Size must be a multiple of kGranuleSize
        if ( size % kGranuleSize != 0 )
            size -= ( size % kGranuleSize );

        std::uint8_t* base = static_cast<std::uint8_t*>( memory );
        // Issue #75: PAP homogenization — BlockHeader_0 at granule 0 holds ManagerHeader as user data.
        // Layout: [BlockHeader_0(0)][ManagerHeader(2)][BlockHeader_free(6)][free_space...]
        static constexpr std::uint32_t kHdrBlkIdx  = 0; // BlockHeader_0 granule index
        static constexpr std::uint32_t kFreeBlkIdx =    // first free block
            detail::kBlockHeaderGranules + detail::kManagerHeaderGranules;

        if ( detail::idx_to_byte_off( kFreeBlkIdx ) + sizeof( detail::BlockHeader ) + kMinBlockSize > size )
            return false;

        // Set up BlockHeader_0 — the allocated block that holds ManagerHeader
        detail::BlockHeader* hdr_blk = detail::block_at( base, kHdrBlkIdx );
        std::memset( hdr_blk, 0, sizeof( detail::BlockHeader ) );
        hdr_blk->size          = detail::kManagerHeaderGranules; // holds ManagerHeader (4 gran)
        hdr_blk->prev_offset   = detail::kNoBlock;
        hdr_blk->next_offset   = kFreeBlkIdx;
        hdr_blk->left_offset   = detail::kNoBlock;
        hdr_blk->right_offset  = detail::kNoBlock;
        hdr_blk->parent_offset = detail::kNoBlock;
        hdr_blk->avl_height    = 0;
        hdr_blk->root_offset   = kHdrBlkIdx; // own_idx==0: root of PAP meta-tree (Issue #75)

        // Initialise ManagerHeader inside BlockHeader_0's user data
        detail::ManagerHeader* hdr = reinterpret_cast<detail::ManagerHeader*>( base + sizeof( detail::BlockHeader ) );
        std::memset( hdr, 0, sizeof( detail::ManagerHeader ) );
        hdr->magic              = kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = kHdrBlkIdx;
        hdr->last_block_offset  = detail::kNoBlock; // filled below
        hdr->free_tree_root     = detail::kNoBlock;
        hdr->owns_memory        = false;
        hdr->prev_owns_memory   = false;

        // Set up first free block at kFreeBlkIdx
        detail::BlockHeader* blk = detail::block_at( base, kFreeBlkIdx );
        std::memset( blk, 0, sizeof( detail::BlockHeader ) );
        blk->size          = 0; // free block
        blk->prev_offset   = kHdrBlkIdx;
        blk->next_offset   = detail::kNoBlock;
        blk->left_offset   = detail::kNoBlock;
        blk->right_offset  = detail::kNoBlock;
        blk->parent_offset = detail::kNoBlock;
        blk->avl_height    = 1;
        blk->root_offset   = 0; // free-blocks tree (Issue #75)

        hdr->last_block_offset = kFreeBlkIdx;
        hdr->free_tree_root    = kFreeBlkIdx;
        hdr->block_count       = 2; // BlockHeader_0 + free block
        hdr->free_count        = 1;
        hdr->alloc_count       = 1; // BlockHeader_0 is allocated (holds ManagerHeader)
        // used_size: BlockHeader_0 header (2) + ManagerHeader data (4) + free BlockHeader (2)
        hdr->used_size = kFreeBlkIdx + detail::kBlockHeaderGranules;

        s_instance = reinterpret_cast<PersistMemoryManager*>( base );
        return true;
    }

    /// @brief Загрузить сохранённый менеджер из буфера (magic + size must match).
    static bool load( void* memory, std::size_t size )
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( memory == nullptr || size < kMinMemorySize )
            return false;
        std::uint8_t* base = static_cast<std::uint8_t*>( memory );
        // Issue #75: ManagerHeader at base + sizeof(BlockHeader) (inside BlockHeader_0)
        auto* hdr = reinterpret_cast<detail::ManagerHeader*>( base + sizeof( detail::BlockHeader ) );
        if ( hdr->magic != kMagic || hdr->total_size != size )
            return false;
        hdr->owns_memory = hdr->prev_owns_memory = false;
        hdr->prev_total_size                     = 0;
        hdr->prev_base_ptr                       = nullptr;
        auto* mgr                                = reinterpret_cast<PersistMemoryManager*>( base );
        mgr->repair_linked_list(); // Issue #69: fix prev_offset consistency
        mgr->recompute_counters(); // Issue #69: recompute from actual block state
        mgr->rebuild_free_tree();
        s_instance = mgr;
        return true;
    }

    /// @brief Уничтожить менеджер; освободить expand() буферы (owns_memory=true).
    static void destroy()
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( s_instance == nullptr )
            return;
        detail::ManagerHeader* hdr = s_instance->header();
        hdr->magic                 = 0;
        bool  owns                 = hdr->owns_memory;
        void* buf                  = s_instance->base_ptr();
        void* prev                 = hdr->prev_base_ptr;
        bool  prev_owns            = hdr->prev_owns_memory;
        s_instance                 = nullptr;
        while ( prev != nullptr )
        {
            // Issue #75: ManagerHeader at prev_base + sizeof(BlockHeader)
            auto* ph        = reinterpret_cast<detail::ManagerHeader*>( static_cast<std::uint8_t*>( prev ) +
                                                                        sizeof( detail::BlockHeader ) );
            void* next_prev = ph->prev_base_ptr;
            bool  next_owns = ph->prev_owns_memory;
            if ( prev_owns )
                std::free( prev );
            prev      = next_prev;
            prev_owns = next_owns;
        }
        if ( owns )
            std::free( buf );
    }

    // ─── Типизированное выделение памяти (публичный API — Issue #61) ──────────

    /// @brief Выделить один объект типа T.
    template <class T> static pptr<T> allocate_typed()
    {
        void* raw = s_instance ? s_instance->allocate_raw( sizeof( T ) ) : nullptr;
        if ( raw == nullptr )
            return pptr<T>();
        std::uint8_t* base     = s_instance->base_ptr();
        std::size_t   byte_off = static_cast<std::uint8_t*>( raw ) - base;
        assert( byte_off % kGranuleSize == 0 );
        return pptr<T>( static_cast<std::uint32_t>( byte_off / kGranuleSize ) );
    }

    /// @brief Выделить массив из count объектов типа T.
    template <class T> static pptr<T> allocate_typed( std::size_t count )
    {
        if ( count == 0 )
            return pptr<T>();
        // Guard against sizeof(T) * count overflow
        if ( sizeof( T ) > 0 && count > std::numeric_limits<std::size_t>::max() / sizeof( T ) )
            return pptr<T>();
        void* raw = s_instance ? s_instance->allocate_raw( sizeof( T ) * count ) : nullptr;
        if ( raw == nullptr )
            return pptr<T>();
        std::uint8_t* base     = s_instance->base_ptr();
        std::size_t   byte_off = static_cast<std::uint8_t*>( raw ) - base;
        assert( byte_off % kGranuleSize == 0 );
        return pptr<T>( static_cast<std::uint32_t>( byte_off / kGranuleSize ) );
    }

    /// @brief Освободить блок, выделенный через allocate_typed.
    template <class T> static void deallocate_typed( pptr<T> p )
    {
        if ( !p.is_null() && s_instance != nullptr )
        {
            void* raw = s_instance->base_ptr() + detail::idx_to_byte_off( p.offset() );
            s_instance->deallocate_raw( raw );
        }
    }

    /// @brief Перевыделить массив: если count больше — выделить новый, скопировать, освободить старый.
    template <class T> static pptr<T> reallocate_typed( pptr<T> p, std::size_t count )
    {
        if ( p.is_null() )
            return allocate_typed<T>( count );
        if ( count == 0 )
        {
            deallocate_typed( p );
            return pptr<T>();
        }
        if ( s_instance == nullptr )
            return pptr<T>();

        std::uint8_t*        base    = s_instance->base_ptr();
        void*                old_raw = base + detail::idx_to_byte_off( p.offset() );
        detail::BlockHeader* blk =
            detail::header_from_ptr( base, old_raw, static_cast<std::size_t>( s_instance->header()->total_size ) );
        if ( blk == nullptr || blk->size == 0 )
            return pptr<T>();

        // Guard against sizeof(T) * count overflow
        if ( count > 0 && sizeof( T ) > std::numeric_limits<std::size_t>::max() / count )
            return pptr<T>();
        std::uint32_t new_granules = detail::bytes_to_granules( sizeof( T ) * count );
        if ( new_granules <= blk->size )
            return p;

        std::size_t   old_bytes  = detail::granules_to_bytes( blk->size );
        std::uint32_t old_offset = p.offset();
        pptr<T>       new_p      = allocate_typed<T>( count );
        if ( new_p.is_null() )
            return pptr<T>();

        // Re-derive after possible auto-expand (Issue #67).
        std::uint8_t* cur_base = s_instance->base_ptr();
        void*         new_raw  = cur_base + detail::idx_to_byte_off( new_p.offset() );
        void*         cur_old  = cur_base + detail::idx_to_byte_off( old_offset );
        std::memcpy( new_raw, cur_old, old_bytes );
        deallocate_typed( p );
        return new_p;
    }

    // ─── Статические методы доступа к состоянию (Issue #61) ──────────────────

    /// @brief Конвертировать гранульный индекс в указатель (внутреннее использование).
    static void* offset_to_ptr( std::uint32_t idx ) noexcept
    {
        if ( s_instance == nullptr || idx == 0 )
            return nullptr;
        return s_instance->base_ptr() + detail::idx_to_byte_off( idx );
    }

    /// @brief Получить размер блока в байтах по гранульному индексу данных.
    /// Используется для bounds checking в pptr<T>::operator[].
    static std::size_t block_data_size_bytes( std::uint32_t data_idx ) noexcept
    {
        if ( s_instance == nullptr || data_idx == 0 )
            return 0;
        if ( data_idx < detail::kBlockHeaderGranules )
            return 0;
        std::uint8_t* base    = s_instance->base_ptr();
        std::uint32_t blk_idx = data_idx - detail::kBlockHeaderGranules;
        // Block 0 holds ManagerHeader (Issue #75); exclude it from user data (Issue #75)
        if ( blk_idx == 0 )
            return 0;
        const detail::ManagerHeader* hdr = s_instance->header();
        if ( !detail::is_valid_block( base, hdr, blk_idx ) )
            return 0;
        const detail::BlockHeader* blk = detail::block_at( base, blk_idx );
        if ( blk->size == 0 )
            return 0;
        return detail::granules_to_bytes( blk->size );
    }

    static std::size_t total_size() noexcept
    {
        return s_instance ? static_cast<std::size_t>( s_instance->header()->total_size ) : 0;
    }

    // Issue #75: returns total reserved zone: BlockHeader_0 header + ManagerHeader data
    static std::size_t manager_header_size() noexcept
    {
        return sizeof( detail::BlockHeader ) + sizeof( detail::ManagerHeader );
    }

    /// @brief Использованный размер в байтах (Issue #59: внутри хранится в гранулах).
    static std::size_t used_size() noexcept
    {
        return s_instance ? detail::granules_to_bytes( s_instance->header()->used_size ) : 0;
    }

    static std::size_t free_size() noexcept
    {
        if ( s_instance == nullptr )
            return 0;
        const detail::ManagerHeader* hdr        = s_instance->header();
        std::size_t                  used_bytes = detail::granules_to_bytes( hdr->used_size );
        return ( hdr->total_size > used_bytes ) ? ( hdr->total_size - used_bytes ) : 0;
    }

    static std::size_t fragmentation() noexcept
    {
        if ( s_instance == nullptr )
            return 0;
        const detail::ManagerHeader* hdr = s_instance->header();
        return ( hdr->free_count > 1 ) ? ( hdr->free_count - 1 ) : 0;
    }

    static bool validate()
    {
        if ( s_instance == nullptr )
            return false;
        std::shared_lock<std::shared_mutex> lock( s_mutex );
        const std::uint8_t*                 base = s_instance->const_base_ptr();
        const detail::ManagerHeader*        hdr  = s_instance->header();

        if ( hdr->magic != kMagic )
            return false;
        std::size_t   block_count = 0, free_count = 0, alloc_count = 0;
        std::uint32_t idx = hdr->first_block_offset;
        while ( idx != detail::kNoBlock )
        {
            if ( !detail::is_valid_block( base, hdr, idx ) )
                return false;
            const detail::BlockHeader* blk = detail::block_at( const_cast<std::uint8_t*>( base ), idx );
            block_count++;
            if ( blk->size > 0 )
                alloc_count++;
            else
                free_count++;
            if ( blk->next_offset != detail::kNoBlock )
            {
                const detail::BlockHeader* nxt =
                    detail::block_at( const_cast<std::uint8_t*>( base ), blk->next_offset );
                if ( nxt->prev_offset != idx )
                    return false;
            }
            idx = blk->next_offset;
        }
        std::size_t tree_free = 0;
        if ( !s_instance->validate_avl( base, hdr, hdr->free_tree_root, tree_free ) )
            return false;
        if ( tree_free != free_count )
            return false;
        return ( block_count == hdr->block_count && free_count == hdr->free_count && alloc_count == hdr->alloc_count );
    }

    /// @brief Dump diagnostics to the provided output stream.
    /// @param os  Output stream to write to (e.g. std::cout).
    static void dump_stats( std::ostream& os )
    {
        if ( s_instance == nullptr )
        {
            os << "=== PersistMemoryManager: no instance ===\n";
            return;
        }
        std::shared_lock<std::shared_mutex> lock( s_mutex );
        const detail::ManagerHeader*        hdr = s_instance->header();
        os << "=== PersistMemoryManager stats ===\n"
           << "  total_size  : " << hdr->total_size << " bytes\n"
           << "  used_size   : " << detail::granules_to_bytes( hdr->used_size ) << " bytes\n"
           << "  free_size   : "
           << ( hdr->total_size > detail::granules_to_bytes( hdr->used_size )
                    ? hdr->total_size - detail::granules_to_bytes( hdr->used_size )
                    : 0ULL )
           << " bytes\n"
           << "  blocks      : " << hdr->block_count << " (free=" << hdr->free_count << ", alloc=" << hdr->alloc_count
           << ")\n"
           << "  fragmentation: " << ( ( hdr->free_count > 1 ) ? ( hdr->free_count - 1 ) : 0U )
           << " extra free segments\n"
           << "==================================\n";
    }

    /// @brief Проверить, инициализирован ли менеджер.
    static bool is_initialized() noexcept { return s_instance != nullptr; }

    friend MemoryStats                       get_stats();
    friend ManagerInfo                       get_manager_info();
    template <typename Callback> friend void for_each_block( Callback&& cb );
    template <typename Callback> friend void for_each_free_block_avl( Callback&& cb );

    // ─── Внутренний доступ для demo и других модулей ─────────────────────────

    /// @brief Доступ к внутреннему синглтону (только для demo/IO модулей).
    /// @note Не используйте в прикладном коде — используйте статические методы.
    static PersistMemoryManager* instance() noexcept { return s_instance; }

  private:
    static PersistMemoryManager* s_instance;
    static std::shared_mutex     s_mutex;

    std::uint8_t*       base_ptr() { return reinterpret_cast<std::uint8_t*>( this ); }
    const std::uint8_t* const_base_ptr() const { return reinterpret_cast<const std::uint8_t*>( this ); }
    // Issue #75: ManagerHeader lives inside BlockHeader_0 (granule 2, after BlockHeader at granule 0)
    detail::ManagerHeader* header()
    {
        return reinterpret_cast<detail::ManagerHeader*>( base_ptr() + sizeof( detail::BlockHeader ) );
    }
    const detail::ManagerHeader* header() const
    {
        return reinterpret_cast<const detail::ManagerHeader*>( const_base_ptr() + sizeof( detail::BlockHeader ) );
    }

    /// @brief Выделить сырую память (внутреннее использование).
    void* allocate_raw( std::size_t user_size )
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( user_size == 0 )
            return nullptr;

        std::uint8_t*          base    = base_ptr();
        detail::ManagerHeader* hdr     = header();
        std::uint32_t          needed  = detail::required_block_granules( user_size );
        std::uint32_t          blk_idx = detail::avl_find_best_fit( base, hdr, needed );

        if ( blk_idx != detail::kNoBlock )
            return allocate_from_block( detail::block_at( base, blk_idx ), user_size );

        if ( !expand( user_size ) )
            return nullptr;

        PersistMemoryManager* new_mgr = s_instance;
        if ( new_mgr == nullptr )
            return nullptr;
        std::uint8_t*          nb      = new_mgr->base_ptr();
        detail::ManagerHeader* nh      = new_mgr->header();
        std::uint32_t          new_idx = detail::avl_find_best_fit( nb, nh, needed );
        if ( new_idx != detail::kNoBlock )
            return new_mgr->allocate_from_block( detail::block_at( nb, new_idx ), user_size );
        return nullptr;
    }

    /// @brief Освободить сырую память (внутреннее использование).
    void deallocate_raw( void* ptr )
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( ptr == nullptr )
            return;
        ptr                         = translate_ptr( ptr );
        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();
        detail::BlockHeader*   blk  = detail::header_from_ptr( base, ptr, static_cast<std::size_t>( hdr->total_size ) );
        if ( blk == nullptr || blk->size == 0 )
            return;

        std::uint32_t freed = blk->size; // in granules (Issue #75: was used_size)
        blk->size           = 0;         // mark free
        blk->root_offset    = 0;         // rejoin free-blocks tree (Issue #75)
        hdr->alloc_count--;
        hdr->free_count++;
        if ( hdr->used_size >= freed )
            hdr->used_size -= freed;
        // Issue #57 opt 2: do NOT insert into AVL yet; coalesce() will do a single insert after merging.
        coalesce( blk );
    }

    /// @pre Must be called while holding s_mutex as a unique_lock (called only from allocate_raw).
    bool expand( std::size_t user_size )
    {
        detail::ManagerHeader* hdr      = header();
        std::size_t            old_size = hdr->total_size;
        std::uint32_t          needed   = detail::required_block_granules( user_size );

        std::size_t growth   = ( old_size / kGrowDenominator ) * kGrowNumerator; // 5/4 growth
        std::size_t new_size = ( ( growth + kGranuleSize - 1 ) / kGranuleSize ) * kGranuleSize;
        if ( new_size <= old_size )
            new_size = old_size + kGranuleSize; // at minimum grow by one granule
        if ( new_size < old_size + detail::granules_to_bytes( needed ) )
        {
            std::size_t extra = detail::granules_to_bytes( needed + detail::kBlockHeaderGranules );
            if ( extra > std::numeric_limits<std::size_t>::max() - old_size - ( kGranuleSize - 1 ) )
                return false;
            new_size = ( ( old_size + extra + kGranuleSize - 1 ) / kGranuleSize ) * kGranuleSize;
        }

        if ( new_size > static_cast<std::uint64_t>( std::numeric_limits<std::uint32_t>::max() ) * kGranuleSize )
            return false;

        void* new_memory = std::malloc( new_size );
        if ( new_memory == nullptr )
            return false;

        std::memcpy( new_memory, base_ptr(), old_size );
        std::uint8_t* nb = static_cast<std::uint8_t*>( new_memory );
        // Issue #75: ManagerHeader at nb + sizeof(BlockHeader) (inside BlockHeader_0)
        detail::ManagerHeader* nh      = reinterpret_cast<detail::ManagerHeader*>( nb + sizeof( detail::BlockHeader ) );
        nh->owns_memory                = true;
        std::uint32_t        extra_idx = detail::byte_off_to_idx( old_size );
        std::size_t          extra_size = new_size - old_size;
        detail::BlockHeader* last_blk =
            ( nh->last_block_offset != detail::kNoBlock ) ? detail::block_at( nb, nh->last_block_offset ) : nullptr;

        if ( last_blk != nullptr && last_blk->size == 0 )
        {
            std::uint32_t loff = detail::block_idx( nb, last_blk );
            detail::avl_remove( nb, nh, loff );
            nh->total_size = new_size;
            detail::avl_insert( nb, nh, loff );
        }
        else
        {
            if ( extra_size < sizeof( detail::BlockHeader ) + kMinBlockSize )
            {
                std::free( new_memory );
                return false;
            }
            detail::BlockHeader* nb_blk = detail::block_at( nb, extra_idx );
            std::memset( nb_blk, 0, sizeof( detail::BlockHeader ) );
            nb_blk->size          = 0; // free block
            nb_blk->left_offset   = detail::kNoBlock;
            nb_blk->right_offset  = detail::kNoBlock;
            nb_blk->parent_offset = detail::kNoBlock;
            nb_blk->avl_height    = 1;
            nb_blk->root_offset   = 0; // free-blocks tree (Issue #75)
            if ( last_blk != nullptr )
            {
                std::uint32_t loff    = detail::block_idx( nb, last_blk );
                nb_blk->prev_offset   = loff;
                nb_blk->next_offset   = detail::kNoBlock;
                last_blk->next_offset = extra_idx;
            }
            else
            {
                nb_blk->prev_offset    = detail::kNoBlock;
                nb_blk->next_offset    = detail::kNoBlock;
                nh->first_block_offset = extra_idx;
            }
            nh->last_block_offset = extra_idx;
            nh->block_count++;
            nh->free_count++;
            nh->total_size = new_size;
            detail::avl_insert( nb, nh, extra_idx );
        }
        nh->prev_base_ptr    = base_ptr();
        nh->prev_total_size  = old_size;
        nh->prev_owns_memory = hdr->owns_memory;
        s_instance           = reinterpret_cast<PersistMemoryManager*>( new_memory );
        return true;
    }

    void rebuild_free_tree()
    {
        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();
        hdr->free_tree_root         = detail::kNoBlock;
        hdr->last_block_offset      = detail::kNoBlock;
        std::uint32_t idx           = hdr->first_block_offset;
        while ( idx != detail::kNoBlock )
        {
            detail::BlockHeader* blk = detail::block_at( base, idx );
            blk->left_offset         = detail::kNoBlock;
            blk->right_offset        = detail::kNoBlock;
            blk->parent_offset       = detail::kNoBlock;
            blk->avl_height          = 0;
            if ( blk->size == 0 )
                detail::avl_insert( base, hdr, idx );
            if ( blk->next_offset == detail::kNoBlock )
                hdr->last_block_offset = idx;
            idx = blk->next_offset;
        }
    }

    /// @brief Issue #69: Fix prev_offset consistency after crash (re-derives from forward traversal).
    void repair_linked_list()
    {
        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();
        std::uint32_t          idx  = hdr->first_block_offset;
        std::uint32_t          prev = detail::kNoBlock;
        while ( idx != detail::kNoBlock )
        {
            if ( detail::idx_to_byte_off( idx ) + sizeof( detail::BlockHeader ) > hdr->total_size )
                break;
            detail::BlockHeader* blk = detail::block_at( base, idx );
            blk->prev_offset         = prev;
            prev                     = idx;
            idx                      = blk->next_offset;
        }
    }

    /// @brief Issue #69: Recompute counters and used_size from actual block list (crash recovery).
    void recompute_counters()
    {
        std::uint8_t*          base        = base_ptr();
        detail::ManagerHeader* hdr         = header();
        std::uint32_t          block_count = 0, free_count = 0, alloc_count = 0;
        // Issue #75: no separate ManagerHeader overhead; BlockHeader_0 is in the block list
        std::uint32_t used_gran = 0;
        std::uint32_t idx       = hdr->first_block_offset;
        while ( idx != detail::kNoBlock )
        {
            if ( detail::idx_to_byte_off( idx ) + sizeof( detail::BlockHeader ) > hdr->total_size )
                break;
            detail::BlockHeader* blk = detail::block_at( base, idx );
            block_count++;
            used_gran += detail::kBlockHeaderGranules;
            if ( blk->size > 0 )
            {
                alloc_count++;
                used_gran += blk->size;
            }
            else
            {
                free_count++;
            }
            idx = blk->next_offset;
        }
        hdr->block_count = block_count;
        hdr->free_count  = free_count;
        hdr->alloc_count = alloc_count;
        hdr->used_size   = used_gran;
    }

    void* translate_ptr( void* ptr ) const noexcept
    {
        std::uint8_t*                base     = const_cast<PersistMemoryManager*>( this )->base_ptr();
        const detail::ManagerHeader* hdr      = header();
        std::uint8_t*                raw      = static_cast<std::uint8_t*>( ptr );
        void*                        prev_buf = hdr->prev_base_ptr;
        std::size_t                  prev_sz  = hdr->prev_total_size;
        while ( prev_buf != nullptr && prev_sz > 0 )
        {
            auto* prev = static_cast<std::uint8_t*>( prev_buf );
            if ( raw >= prev && raw < prev + prev_sz )
                return base + ( raw - prev );
            // Issue #75: ManagerHeader at prev + sizeof(BlockHeader)
            auto* ph = reinterpret_cast<detail::ManagerHeader*>( prev + sizeof( detail::BlockHeader ) );
            prev_buf = ph->prev_base_ptr;
            prev_sz  = ph->prev_total_size;
        }
        return ptr;
    }

    /// @brief Слить блок с соседними свободными блоками и вставить результат в AVL один раз.
    /// Issue #57 opt 2: max 2 avl_remove + 1 avl_insert вместо insert + remove/remove/insert.
    /// Issue #57 opt 4: поддерживает last_block_offset.
    /// Issue #59: размеры в гранулах.
    void coalesce( detail::BlockHeader* blk )
    {
        std::uint8_t*          base  = base_ptr();
        detail::ManagerHeader* hdr   = header();
        std::uint32_t          b_idx = detail::block_idx( base, blk );

        // Слияние со следующим свободным соседом
        if ( blk->next_offset != detail::kNoBlock )
        {
            detail::BlockHeader* nxt = detail::block_at( base, blk->next_offset );
            if ( nxt->size == 0 )
            {
                std::uint32_t nxt_idx = blk->next_offset;
                detail::avl_remove( base, hdr, nxt_idx );
                blk->next_offset = nxt->next_offset;
                if ( nxt->next_offset != detail::kNoBlock )
                    detail::block_at( base, nxt->next_offset )->prev_offset = b_idx;
                else
                    hdr->last_block_offset = b_idx;
                std::memset( nxt, 0, sizeof( detail::BlockHeader ) );
                hdr->block_count--;
                hdr->free_count--;
                if ( hdr->used_size >= detail::kBlockHeaderGranules )
                    hdr->used_size -= detail::kBlockHeaderGranules;
            }
        }

        // Слияние с предыдущим свободным соседом
        if ( blk->prev_offset != detail::kNoBlock )
        {
            detail::BlockHeader* prv = detail::block_at( base, blk->prev_offset );
            if ( prv->size == 0 )
            {
                std::uint32_t prv_idx = blk->prev_offset;
                detail::avl_remove( base, hdr, prv_idx );
                prv->next_offset = blk->next_offset;
                if ( blk->next_offset != detail::kNoBlock )
                    detail::block_at( base, blk->next_offset )->prev_offset = prv_idx;
                else
                    hdr->last_block_offset = prv_idx;
                std::memset( blk, 0, sizeof( detail::BlockHeader ) );
                hdr->block_count--;
                hdr->free_count--;
                if ( hdr->used_size >= detail::kBlockHeaderGranules )
                    hdr->used_size -= detail::kBlockHeaderGranules;
                detail::avl_insert( base, hdr, prv_idx );
                return;
            }
        }

        detail::avl_insert( base, hdr, b_idx );
    }

    void* allocate_from_block( detail::BlockHeader* blk, std::size_t user_size )
    {
        std::uint8_t*          base    = base_ptr();
        detail::ManagerHeader* hdr     = header();
        std::uint32_t          blk_idx = detail::block_idx( base, blk );
        detail::avl_remove( base, hdr, blk_idx );

        std::uint32_t blk_total_gran = detail::block_total_granules( base, hdr, blk );
        std::uint32_t data_gran      = detail::bytes_to_granules( user_size );
        std::uint32_t needed_gran    = detail::kBlockHeaderGranules + data_gran;
        std::uint32_t min_rem_gran   = detail::kBlockHeaderGranules + 1;
        bool          can_split      = ( blk_total_gran >= needed_gran + min_rem_gran );

        if ( can_split )
        {
            std::uint32_t        new_idx = blk_idx + needed_gran;
            detail::BlockHeader* new_blk = detail::block_at( base, new_idx );
            std::memset( new_blk, 0, sizeof( detail::BlockHeader ) );
            new_blk->size          = 0; // free block
            new_blk->prev_offset   = blk_idx;
            new_blk->next_offset   = blk->next_offset;
            new_blk->left_offset   = detail::kNoBlock;
            new_blk->right_offset  = detail::kNoBlock;
            new_blk->parent_offset = detail::kNoBlock;
            new_blk->avl_height    = 1;
            new_blk->root_offset   = 0; // free-blocks tree (Issue #75)
            if ( blk->next_offset != detail::kNoBlock )
                detail::block_at( base, blk->next_offset )->prev_offset = new_idx;
            else
                hdr->last_block_offset = new_idx;
            blk->next_offset = new_idx;
            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += detail::kBlockHeaderGranules;
            detail::avl_insert( base, hdr, new_idx );
        }

        blk->size          = data_gran; // store in granules (Issue #75: was used_size)
        blk->root_offset   = blk_idx;   // allocated block is root of its own tree (Issue #75)
        blk->left_offset   = detail::kNoBlock;
        blk->right_offset  = detail::kNoBlock;
        blk->parent_offset = detail::kNoBlock;
        blk->avl_height    = 0;
        hdr->alloc_count++;
        hdr->free_count--;
        hdr->used_size += data_gran;
        return detail::user_ptr( blk );
    }

    bool validate_avl( const std::uint8_t* base, const detail::ManagerHeader* hdr, std::uint32_t node_idx,
                       std::size_t& count ) const
    {
        if ( node_idx == detail::kNoBlock )
            return true;
        if ( detail::idx_to_byte_off( node_idx ) >= hdr->total_size )
            return false;
        if ( !detail::is_valid_block( base, hdr, node_idx ) )
            return false;
        const detail::BlockHeader* node = detail::block_at( const_cast<std::uint8_t*>( base ), node_idx );
        if ( node->size != 0 )
            return false;
        count++;
        if ( !validate_avl( base, hdr, node->left_offset, count ) )
            return false;
        if ( !validate_avl( base, hdr, node->right_offset, count ) )
            return false;
        if ( node->left_offset != detail::kNoBlock )
        {
            const detail::BlockHeader* lc = detail::block_at( const_cast<std::uint8_t*>( base ), node->left_offset );
            if ( lc->parent_offset != node_idx )
                return false;
        }
        if ( node->right_offset != detail::kNoBlock )
        {
            const detail::BlockHeader* rc = detail::block_at( const_cast<std::uint8_t*>( base ), node->right_offset );
            if ( rc->parent_offset != node_idx )
                return false;
        }
        return true;
    }
};

inline PersistMemoryManager* PersistMemoryManager::s_instance = nullptr;
inline std::shared_mutex     PersistMemoryManager::s_mutex;

// ─── Реализация методов pptr<T> ───────────────────────────────────────────────

/// @brief Разыменовать через синглтон PersistMemoryManager (Issue #61).
template <class T> inline T* pptr<T>::get() const noexcept
{
    if ( _idx == 0 )
        return nullptr;
    return static_cast<T*>( PersistMemoryManager::offset_to_ptr( _idx ) );
}

template <class T> inline T* pptr<T>::get_at( std::size_t index ) const noexcept
{
    T* base_elem = get();
    return ( base_elem == nullptr ) ? nullptr : base_elem + index;
}

template <class T> inline T* pptr<T>::operator[]( std::size_t i ) const noexcept
{
    if ( _idx == 0 )
        return nullptr;
    std::size_t block_bytes = PersistMemoryManager::block_data_size_bytes( _idx );
    std::size_t capacity    = ( sizeof( T ) > 0 ) ? ( block_bytes / sizeof( T ) ) : 0;
    if ( i >= capacity )
        return nullptr;
    T* base_elem = static_cast<T*>( PersistMemoryManager::offset_to_ptr( _idx ) );
    return base_elem + i;
}

// ─── Реализация свободных функций ─────────────────────────────────────────────

/// @brief Получить статистику менеджера.
inline MemoryStats get_stats()
{
    MemoryStats           stats{};
    PersistMemoryManager* mgr = PersistMemoryManager::instance();
    if ( mgr == nullptr )
        return stats;
    const std::uint8_t*          base = mgr->const_base_ptr();
    const detail::ManagerHeader* hdr  = mgr->header();
    stats.total_blocks                = hdr->block_count;
    stats.free_blocks                 = hdr->free_count;
    stats.allocated_blocks            = hdr->alloc_count;
    bool          first_free          = true;
    std::uint32_t idx                 = hdr->first_block_offset;
    while ( idx != detail::kNoBlock )
    {
        const detail::BlockHeader* blk = detail::block_at( const_cast<std::uint8_t*>( base ), idx );
        if ( blk->size == 0 )
        {
            std::uint32_t gran     = ( blk->next_offset != detail::kNoBlock )
                                         ? ( blk->next_offset - idx )
                                         : ( detail::byte_off_to_idx( hdr->total_size ) - idx );
            std::size_t   blk_size = detail::granules_to_bytes( gran );
            if ( first_free )
            {
                stats.largest_free  = blk_size;
                stats.smallest_free = blk_size;
                first_free          = false;
            }
            else
            {
                stats.largest_free  = std::max( stats.largest_free, blk_size );
                stats.smallest_free = std::min( stats.smallest_free, blk_size );
            }
            stats.total_fragmentation += blk_size;
        }
        idx = blk->next_offset;
    }
    return stats;
}

/// @brief Получить детальную информацию о менеджере.
inline ManagerInfo get_manager_info()
{
    ManagerInfo           info{};
    PersistMemoryManager* mgr = PersistMemoryManager::instance();
    if ( mgr == nullptr )
        return info;
    const detail::ManagerHeader* hdr = mgr->header();
    info.magic                       = hdr->magic;
    info.total_size                  = hdr->total_size;
    info.used_size                   = detail::granules_to_bytes( hdr->used_size );
    info.block_count                 = hdr->block_count;
    info.free_count                  = hdr->free_count;
    info.alloc_count                 = hdr->alloc_count;
    info.first_block_offset          = ( hdr->first_block_offset != detail::kNoBlock )
                                           ? static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( hdr->first_block_offset ) )
                                           : -1;
    info.first_free_offset           = ( hdr->free_tree_root != detail::kNoBlock )
                                           ? static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( hdr->free_tree_root ) )
                                           : -1;
    info.last_free_offset            = -1;
    // Issue #75: total reserved zone = BlockHeader_0 + ManagerHeader
    info.manager_header_size = sizeof( detail::BlockHeader ) + sizeof( detail::ManagerHeader );
    return info;
}

/// @brief Итерировать по всем блокам. @warning Callback must not call mutating PMM methods.
template <typename Callback> inline void for_each_block( Callback&& cb )
{
    PersistMemoryManager* mgr = PersistMemoryManager::instance();
    if ( mgr == nullptr )
        return;
    std::shared_lock<std::shared_mutex> lock( PersistMemoryManager::s_mutex );
    const std::uint8_t*                 base  = mgr->const_base_ptr();
    const detail::ManagerHeader*        hdr   = mgr->header();
    std::uint32_t                       idx   = hdr->first_block_offset;
    std::size_t                         index = 0;
    while ( idx != detail::kNoBlock )
    {
        if ( detail::idx_to_byte_off( idx ) >= hdr->total_size )
            break;
        const detail::BlockHeader* blk  = detail::block_at( const_cast<std::uint8_t*>( base ), idx );
        std::uint32_t              gran = ( blk->next_offset != detail::kNoBlock )
                                              ? ( blk->next_offset - idx )
                                              : ( detail::byte_off_to_idx( hdr->total_size ) - idx );
        BlockView                  view;
        view.index       = index;
        view.offset      = static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( idx ) );
        view.total_size  = detail::granules_to_bytes( gran );
        view.header_size = sizeof( detail::BlockHeader );
        view.user_size   = detail::granules_to_bytes( blk->size );
        view.alignment   = kDefaultAlignment;
        view.used        = ( blk->size > 0 );
        cb( view );
        ++index;
        idx = blk->next_offset;
    }
}

/// @brief Iterate over all free blocks in the AVL tree. @warning Callback must not call mutating PMM methods.
template <typename Callback> inline void for_each_free_block_avl( Callback&& cb )
{
    PersistMemoryManager* mgr = PersistMemoryManager::instance();
    if ( mgr == nullptr )
        return;
    std::shared_lock<std::shared_mutex> lock( PersistMemoryManager::s_mutex );
    const std::uint8_t*                 base   = mgr->const_base_ptr();
    const detail::ManagerHeader*        hdr    = mgr->header();
    auto                                to_off = []( std::uint32_t i ) -> std::ptrdiff_t
    { return ( i != detail::kNoBlock ) ? static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( i ) ) : -1; };
    for ( std::uint32_t idx = hdr->first_block_offset; idx != detail::kNoBlock; )
    {
        if ( detail::idx_to_byte_off( idx ) >= hdr->total_size )
            break;
        const detail::BlockHeader* blk = detail::block_at( const_cast<std::uint8_t*>( base ), idx );
        if ( blk->size == 0 )
        {
            std::uint32_t gran = ( blk->next_offset != detail::kNoBlock )
                                     ? ( blk->next_offset - idx )
                                     : ( detail::byte_off_to_idx( hdr->total_size ) - idx );
            FreeBlockView view;
            view.offset        = to_off( idx );
            view.total_size    = detail::granules_to_bytes( gran );
            view.free_size     = ( gran > detail::kBlockHeaderGranules )
                                     ? detail::granules_to_bytes( gran - detail::kBlockHeaderGranules )
                                     : 0;
            view.left_offset   = to_off( blk->left_offset );
            view.right_offset  = to_off( blk->right_offset );
            view.parent_offset = to_off( blk->parent_offset );
            view.avl_height    = static_cast<int>( blk->avl_height );
            view.avl_depth     = 0;
            cb( view );
        }
        idx = blk->next_offset;
    }
}

} // namespace pmm
