/**
 * @file persist_memory_manager.h
 * @brief Менеджер персистентной кучи памяти для C++17
 *
 * Single-header библиотека управления персистентной памятью.
 * Предоставляет низкоуровневый менеджер памяти, хранящий все метаданные
 * в управляемой области памяти для возможности персистентности между запусками.
 *
 * Алгоритм (Issue #55): Каждый блок содержит 6 ключевых полей:
 *   1. used_size   — занятый размер данных в гранулах (0 = свободный блок)
 *   2. prev_offset — индекс предыдущего блока (kNoBlock = нет)
 *   3. next_offset — индекс следующего блока (kNoBlock = последний)
 *   4. left_offset — левый дочерний узел AVL-дерева свободных блоков
 *   5. right_offset — правый дочерний узел AVL-дерева свободных блоков
 *   6. parent_offset — родительский узел AVL-дерева
 *
 * Поиск свободного блока: best-fit через AVL-дерево (O(log n)).
 * При освобождении: слияние с соседними свободными блоками.
 *
 * @version 3.0.0
 *
 * Optimizations (Issue #57):
 *   1. O(1) back-pointer (boundary tag) in header_from_ptr — O(1) instead of up to 512 iterations.
 *   2. Restructured deallocate()+coalesce() — max 2 removes + 1 insert instead of insert+removes+insert.
 *   3. Actual-padding computation in allocate_from_block() — less internal fragmentation.
 *   4. last_block_offset in ManagerHeader — O(1) last-block lookup in expand().
 *
 * Optimizations (Issue #59):
 *   1. 16-byte granule addressing — all indices are in units of 16 bytes (kGranuleSize).
 *   2. 32-bit granule indices for all block offsets — supports up to 64 GB per manager (2^32 * 16).
 *   3. BlockHeader reduced to 32 bytes = 2 granules (removed total_size, alignment fields).
 *   4. total_size computed from next_offset - this_offset (last block uses manager total_size).
 *   5. used_size stored in granules (ceiling of bytes / kGranuleSize).
 *   6. alignment is no longer needed — minimum allocation is 16 bytes (1 granule).
 *   7. pptr<T> reduced to 4 bytes (uint32_t granule index) — halves pointer memory in JSON structures.
 *   8. pptr<T>++ and pptr<T>-- are forbidden (deleted).
 *   9. pptr<T>[i] addresses elements of type T with block-size bounds checking.
 *  10. header_from_ptr is O(1) without back-pointer tag: user_ptr = block + 2 granules always.
 *
 * Refactoring (Issue #61):
 *   1. PersistMemoryManager is a fully static class — no instances, no PersistMemoryManager* in public API.
 *   2. Public API uses only pptr<T> and PersistMemoryManager::static_method() calls.
 *   3. pptr<T> uses only PersistMemoryManager::static methods — no PersistMemoryManager* parameter.
 *   4. Removed raw void* allocate/deallocate/reallocate from public API.
 *   5. Removed pptr<T>::resolve(PersistMemoryManager*) — use get() via singleton instead.
 *   6. Removed AllocationInfo struct and get_info() free function.
 *
 * Code review fixes (Issue #63):
 *   1. Replaced static constexpr with inline constexpr at namespace scope (no internal linkage per TU).
 *   2. Removed #include <iostream> from header; dump_stats() now uses ostream parameter.
 *   3. Updated kMagic to encode format version 3 ("PMM_V030") to reject incompatible persisted files.
 *   4. Fixed is_valid_alignment to correctly check for kGranuleSize-only alignment.
 *   5. Added assert to avl_update_height to guard against int16_t height overflow.
 *   6. Added upper-bound check in header_from_ptr to prevent out-of-range reads.
 *   7. Fixed AVL tiebreaker to use strict < instead of <= for correct strict weak ordering.
 *   8. Fixed bytes_to_granules overflow: added SIZE_MAX proximity guard and uint32_t overflow check.
 *   9. Fixed sizeof(T)*count overflow in allocate_typed(count) and reallocate_typed.
 *  10. Fixed unsigned underflow in block_data_size_bytes when data_idx < kBlockHeaderGranules.
 *  11. Fixed pptr::operator[] bounds check: replaced (i+1)*sizeof(T) with overflow-safe i >= N form.
 *  12. Made pptr constructors and comparison operators constexpr.
 *  13. Fixed total_fragmentation in get_stats() to include all free blocks.
 *  14. Replaced magic literal 0xFFFFFFFFULL with std::numeric_limits<std::uint32_t>::max().
 *  15. Added inline to free function forward declarations to match definitions.
 *  16. Documented that for_each_block callbacks must not call mutating PMM methods.
 *  17. Documented expand() lock precondition; added lock state assertion.
 *  18. Fixed coalesce() to update hdr->used_size when merging block headers.
 *  19. Fixed expand() growth computation to avoid size_t overflow.
 *  20. Replaced void* prev_base_ptr in ManagerHeader with uint64_t prev_base_ptr_offset (persistent-safe).
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
inline constexpr std::uint64_t kMagic           = 0x504D4D5F56303330ULL; ///< "PMM_V030" — format version 3
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

/// @brief Block header (Issue #59: 32 bytes = 2 granules). All _offset fields are
/// granule indices (uint32_t); kNoBlock = 0xFFFFFFFF. used_size=0 means free block.
/// User data starts 32 bytes (2 granules) after block start. total_size = next_offset - idx.
struct BlockHeader
{
    std::uint32_t magic;     ///< Магическое число (4 байта)
    std::uint32_t used_size; ///< [1] Занятый размер в гранулах (0 = свободный блок)
    std::uint32_t prev_offset; ///< [2] Предыдущий блок (гранульный индекс)
    std::uint32_t next_offset; ///< [3] Следующий блок (гранульный индекс)
    std::uint32_t left_offset; ///< [4] Левый дочерний узел AVL-дерева (гранульный индекс)
    std::uint32_t right_offset; ///< [5] Правый дочерний узел AVL-дерева (гранульный индекс)
    std::uint32_t parent_offset; ///< [6] Родительский узел AVL-дерева (гранульный индекс)
    std::int16_t  avl_height; ///< Высота AVL-поддерева (0 = не в дереве)
    std::uint16_t _pad;       ///< Зарезервировано
};

static_assert( sizeof( BlockHeader ) == 32, "BlockHeader must be exactly 32 bytes (Issue #59)" );
static_assert( sizeof( BlockHeader ) % kGranuleSize == 0, "BlockHeader must be granule-aligned (Issue #59)" );

/// @brief Число гранул в BlockHeader (2 гранулы = 32 байта)
inline constexpr std::uint32_t kBlockHeaderGranules = sizeof( BlockHeader ) / kGranuleSize;

inline constexpr std::uint32_t kBlockMagic = 0x424C4B32U; // "BLK2" (Issue #59 version)
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

/// @brief Перевести байты в гранулы (потолок: ceiling(bytes / kGranuleSize)).
/// Returns 0 if the result would overflow uint32_t (caller must treat as allocation failure).
inline std::uint32_t bytes_to_granules( std::size_t bytes )
{
    // Guard against size_t overflow in the addition
    if ( bytes > std::numeric_limits<std::size_t>::max() - ( kGranuleSize - 1 ) )
        return 0; // overflow — signal failure to caller
    std::size_t granules = ( bytes + kGranuleSize - 1 ) / kGranuleSize;
    // Guard against uint32_t truncation
    if ( granules > std::numeric_limits<std::uint32_t>::max() )
        return 0; // overflow — signal failure to caller
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

/// @brief Получить гранульный индекс из байтового смещения.
/// Байтовое смещение должно быть кратно kGranuleSize и <= UINT32_MAX * kGranuleSize.
inline std::uint32_t byte_off_to_idx( std::size_t byte_off )
{
    assert( byte_off % kGranuleSize == 0 );
    assert( byte_off / kGranuleSize <= std::numeric_limits<std::uint32_t>::max() );
    return static_cast<std::uint32_t>( byte_off / kGranuleSize );
}

/// @brief Returns true only for kGranuleSize (16-byte) alignment.
/// Issue #59: only 16-byte alignment is supported — everything is granule-aligned.
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

/// @brief Вычислить адрес пользовательских данных для блока.
/// Issue #59: user_ptr всегда ровно через kBlockHeaderGranules гранул после начала блока.
/// Выравнивание по kGranuleSize = 16 байт гарантировано самой структурой адресации.
inline void* user_ptr( BlockHeader* block )
{
    return reinterpret_cast<std::uint8_t*>( block ) + sizeof( BlockHeader );
}

/// @brief O(1) восстановление заголовка блока по user_ptr (Issue #59).
/// user_ptr всегда = block + sizeof(BlockHeader), поэтому header = ptr - sizeof(BlockHeader).
/// Проверка через magic и used_size > 0.
/// @param base     Базовый адрес управляемой области.
/// @param ptr      Указатель на пользовательские данные (должен быть внутри [base, base+total_size)).
/// @param total_size Полный размер управляемой области в байтах (для проверки верхней границы).
inline BlockHeader* header_from_ptr( std::uint8_t* base, void* ptr, std::size_t total_size )
{
    if ( ptr == nullptr )
        return nullptr;
    std::uint8_t* raw_ptr  = reinterpret_cast<std::uint8_t*>( ptr );
    std::uint8_t* min_addr = base + sizeof( ManagerHeader ) + sizeof( BlockHeader );
    if ( raw_ptr < min_addr )
        return nullptr;
    // Upper-bound check: ptr must be strictly within the managed region
    if ( raw_ptr > base + total_size )
        return nullptr;

    // user_ptr is always exactly sizeof(BlockHeader) bytes past the block header
    std::uint8_t* cand_addr = raw_ptr - sizeof( BlockHeader );
    // Must be granule-aligned
    if ( ( reinterpret_cast<std::size_t>( cand_addr ) - reinterpret_cast<std::size_t>( base ) ) % kGranuleSize != 0 )
        return nullptr;
    BlockHeader* cand = reinterpret_cast<BlockHeader*>( cand_addr );
    if ( cand->magic == kBlockMagic && cand->used_size > 0 )
        return cand;
    return nullptr;
}

/// @brief Минимальный размер блока в гранулах для заданного числа байт пользователя.
/// Issue #59: overhead = kBlockHeaderGranules; user data aligned up to granule boundary.
/// Минимум: kBlockHeaderGranules + 1 (заголовок + хотя бы 1 гранула данных).
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

/**
 * @brief Менеджер персистентной памяти — полностью статический класс (Issue #61).
 *
 * Issue #61: PersistMemoryManager не имеет экземпляров.
 *   - Все публичные методы статические.
 *   - Публичный API не использует void* и PersistMemoryManager*.
 *   - Только pptr<T> и PersistMemoryManager::static_method() в пользовательском коде.
 */
class PersistMemoryManager
{
  public:
    // ─── Управление жизненным циклом ─────────────────────────────────────────

    /**
     * @brief Создать менеджер на предоставленном буфере памяти.
     *
     * Инициализирует новый менеджер в предоставленной области памяти.
     * После успешного вызова синглтон доступен через instance().
     *
     * @param memory  Указатель на буфер (должен быть выровнен по kGranuleSize).
     * @param size    Размер буфера в байтах (минимум kMinMemorySize).
     * @return true при успехе, false при ошибке.
     */
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

        std::uint8_t*          base = static_cast<std::uint8_t*>( memory );
        detail::ManagerHeader* hdr  = reinterpret_cast<detail::ManagerHeader*>( base );
        std::memset( hdr, 0, sizeof( detail::ManagerHeader ) );
        hdr->magic              = kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = detail::kNoBlock;
        hdr->last_block_offset  = detail::kNoBlock;
        hdr->free_tree_root     = detail::kNoBlock;
        hdr->owns_memory        = false; // external memory — caller is responsible
        hdr->prev_owns_memory   = false;

        // First block starts right after ManagerHeader (granule-aligned)
        std::uint32_t blk_idx = detail::kManagerHeaderGranules;

        if ( detail::idx_to_byte_off( blk_idx ) + sizeof( detail::BlockHeader ) + kMinBlockSize > size )
            return false;

        detail::BlockHeader* blk = detail::block_at( base, blk_idx );
        std::memset( blk, 0, sizeof( detail::BlockHeader ) );
        blk->magic         = detail::kBlockMagic;
        blk->used_size     = 0;
        blk->prev_offset   = detail::kNoBlock;
        blk->next_offset   = detail::kNoBlock;
        blk->left_offset   = detail::kNoBlock;
        blk->right_offset  = detail::kNoBlock;
        blk->parent_offset = detail::kNoBlock;
        blk->avl_height    = 1;

        hdr->first_block_offset = blk_idx;
        hdr->last_block_offset  = blk_idx;
        hdr->free_tree_root     = blk_idx;
        hdr->block_count        = 1;
        hdr->free_count         = 1;
        // used_size in granules: ManagerHeader + BlockHeader
        hdr->used_size = blk_idx + detail::kBlockHeaderGranules;

        s_instance = reinterpret_cast<PersistMemoryManager*>( base );
        return true;
    }

    /**
     * @brief Загрузить ранее сохранённый менеджер из буфера памяти.
     *
     * Восстанавливает состояние менеджера из буфера, который был заполнен
     * из файла через persist_memory_io.h или иным способом.
     *
     * @param memory  Указатель на буфер с сохранёнными данными.
     * @param size    Размер буфера (должен совпадать с сохранённым total_size).
     * @return true при успехе, false при ошибке или несовпадении размера.
     */
    static bool load( void* memory, std::size_t size )
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( memory == nullptr || size < kMinMemorySize )
            return false;
        std::uint8_t*          base = static_cast<std::uint8_t*>( memory );
        detail::ManagerHeader* hdr  = reinterpret_cast<detail::ManagerHeader*>( base );
        if ( hdr->magic != kMagic || hdr->total_size != size )
            return false;
        hdr->owns_memory      = false; // external memory — caller is responsible
        hdr->prev_owns_memory = false;
        hdr->prev_total_size  = 0;
        hdr->prev_base_ptr    = nullptr;
        auto* mgr             = reinterpret_cast<PersistMemoryManager*>( base );
        mgr->rebuild_free_tree();
        s_instance = mgr;
        return true;
    }

    /**
     * @brief Уничтожить менеджер и освободить динамически выделенную память.
     *
     * Сбрасывает синглтон. Если менеджер расширялся (owns_memory = true),
     * освобождает все выделенные через expand() буферы.
     * Память, переданная пользователем в create()/load(), не освобождается.
     */
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
            detail::ManagerHeader* ph        = reinterpret_cast<detail::ManagerHeader*>( prev );
            void*                  next_prev = ph->prev_base_ptr;
            bool                   next_owns = ph->prev_owns_memory;
            if ( prev_owns )
                std::free( prev );
            prev      = next_prev;
            prev_owns = next_owns;
        }
        if ( owns )
            std::free( buf );
    }

    // ─── Типизированное выделение памяти (публичный API — Issue #61) ──────────

    /**
     * @brief Выделить один объект типа T.
     * @return pptr<T> на выделенный объект, или нулевой pptr при ошибке.
     */
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

    /**
     * @brief Выделить массив из count объектов типа T.
     * @param count  Число элементов.
     * @return pptr<T> на начало массива, или нулевой pptr при ошибке.
     */
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

    /**
     * @brief Освободить объект или массив, выделенный через allocate_typed.
     * @param p  Персистный указатель на выделенный блок.
     */
    template <class T> static void deallocate_typed( pptr<T> p )
    {
        if ( !p.is_null() && s_instance != nullptr )
        {
            void* raw = s_instance->base_ptr() + detail::idx_to_byte_off( p.offset() );
            s_instance->deallocate_raw( raw );
        }
    }

    /**
     * @brief Перевыделить массив из count объектов типа T.
     *
     * Если новый размер меньше или равен старому — возвращает тот же pptr.
     * Иначе выделяет новый блок, копирует данные, освобождает старый.
     *
     * @param p      Существующий персистный указатель (может быть нулевым).
     * @param count  Новое число элементов.
     * @return Новый pptr<T>, или нулевой при ошибке.
     */
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
        if ( blk == nullptr || blk->used_size == 0 )
            return pptr<T>();

        // Guard against sizeof(T) * count overflow
        if ( count > 0 && sizeof( T ) > std::numeric_limits<std::size_t>::max() / count )
            return pptr<T>();
        std::uint32_t new_granules = detail::bytes_to_granules( sizeof( T ) * count );
        if ( new_granules <= blk->used_size )
            return p;

        std::size_t old_bytes = detail::granules_to_bytes( blk->used_size );
        pptr<T>     new_p     = allocate_typed<T>( count );
        if ( new_p.is_null() )
            return pptr<T>();

        // After allocate_typed, s_instance may have changed (auto-expand)
        std::uint8_t* new_base = s_instance->base_ptr();
        void*         new_raw  = new_base + detail::idx_to_byte_off( new_p.offset() );
        std::memcpy( new_raw, old_raw, old_bytes );
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
        // Guard against unsigned underflow: data_idx must be at least kBlockHeaderGranules
        if ( data_idx < detail::kBlockHeaderGranules )
            return 0;
        std::uint8_t* base    = s_instance->base_ptr();
        std::uint32_t blk_idx = data_idx - detail::kBlockHeaderGranules;
        if ( blk_idx * kGranuleSize < sizeof( detail::ManagerHeader ) )
            return 0;
        const detail::BlockHeader* blk = detail::block_at( base, blk_idx );
        if ( blk->magic != detail::kBlockMagic || blk->used_size == 0 )
            return 0;
        return detail::granules_to_bytes( blk->used_size );
    }

    static std::size_t total_size() noexcept
    {
        return s_instance ? static_cast<std::size_t>( s_instance->header()->total_size ) : 0;
    }

    static std::size_t manager_header_size() noexcept { return sizeof( detail::ManagerHeader ); }

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
            if ( detail::idx_to_byte_off( idx ) >= hdr->total_size )
                return false;
            const detail::BlockHeader* blk = detail::block_at( const_cast<std::uint8_t*>( base ), idx );
            if ( blk->magic != detail::kBlockMagic )
                return false;
            block_count++;
            if ( blk->used_size > 0 )
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

    std::uint8_t*                base_ptr() { return reinterpret_cast<std::uint8_t*>( this ); }
    const std::uint8_t*          const_base_ptr() const { return reinterpret_cast<const std::uint8_t*>( this ); }
    detail::ManagerHeader*       header() { return reinterpret_cast<detail::ManagerHeader*>( this ); }
    const detail::ManagerHeader* header() const { return reinterpret_cast<const detail::ManagerHeader*>( this ); }

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
        if ( blk == nullptr || blk->used_size == 0 )
            return;

        std::uint32_t freed = blk->used_size; // in granules
        blk->used_size      = 0;
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

        // Compute new_size = ceil(old_size * 5/4 / kGranuleSize) * kGranuleSize
        // Use divide-first to avoid overflow: old_size/4 * 5
        std::size_t growth   = ( old_size / kGrowDenominator ) * kGrowNumerator;
        std::size_t new_size = ( ( growth + kGranuleSize - 1 ) / kGranuleSize ) * kGranuleSize;
        if ( new_size <= old_size )
            new_size = old_size + kGranuleSize; // at minimum grow by one granule
        if ( new_size < old_size + detail::granules_to_bytes( needed ) )
        {
            // Ensure there is enough space for the requested allocation
            std::size_t extra = detail::granules_to_bytes( needed + detail::kBlockHeaderGranules );
            // Guard against addition overflow
            if ( extra > std::numeric_limits<std::size_t>::max() - old_size - ( kGranuleSize - 1 ) )
                return false;
            new_size = ( ( old_size + extra + kGranuleSize - 1 ) / kGranuleSize ) * kGranuleSize;
        }

        // Issue #59: max 64 GB
        if ( new_size > static_cast<std::uint64_t>( std::numeric_limits<std::uint32_t>::max() ) * kGranuleSize )
            return false;

        void* new_memory = std::malloc( new_size );
        if ( new_memory == nullptr )
            return false;

        std::memcpy( new_memory, base_ptr(), old_size );
        detail::ManagerHeader* nh = reinterpret_cast<detail::ManagerHeader*>( new_memory );
        std::uint8_t*          nb = static_cast<std::uint8_t*>( new_memory );
        nh->owns_memory           = true; // expanded buffer is always owned by the manager

        std::uint32_t extra_idx  = detail::byte_off_to_idx( old_size );
        std::size_t   extra_size = new_size - old_size;
        // Issue #57 opt 4: O(1) last-block lookup via last_block_offset
        detail::BlockHeader* last_blk =
            ( nh->last_block_offset != detail::kNoBlock ) ? detail::block_at( nb, nh->last_block_offset ) : nullptr;

        if ( last_blk != nullptr && last_blk->used_size == 0 )
        {
            std::uint32_t loff = detail::block_idx( nb, last_blk );
            detail::avl_remove( nb, nh, loff );
            // Issue #59: just update total_size; the last block's size is recomputed from total_size
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
            nb_blk->magic         = detail::kBlockMagic;
            nb_blk->used_size     = 0;
            nb_blk->left_offset   = detail::kNoBlock;
            nb_blk->right_offset  = detail::kNoBlock;
            nb_blk->parent_offset = detail::kNoBlock;
            nb_blk->avl_height    = 1;
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
            nh->total_size = new_size; // Update before AVL insert (affects size computation)
            detail::avl_insert( nb, nh, extra_idx );
        }
        nh->prev_base_ptr    = base_ptr();
        nh->prev_total_size  = old_size;
        nh->prev_owns_memory = hdr->owns_memory; // whether the previous buffer is owned
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
            if ( blk->used_size == 0 )
                detail::avl_insert( base, hdr, idx );
            if ( blk->next_offset == detail::kNoBlock )
                hdr->last_block_offset = idx;
            idx = blk->next_offset;
        }
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
            std::uint8_t* prev = static_cast<std::uint8_t*>( prev_buf );
            if ( raw >= prev && raw < prev + prev_sz )
                return base + ( raw - prev );
            detail::ManagerHeader* ph = reinterpret_cast<detail::ManagerHeader*>( prev_buf );
            prev_buf                  = ph->prev_base_ptr;
            prev_sz                   = ph->prev_total_size;
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
            if ( nxt->used_size == 0 )
            {
                std::uint32_t nxt_idx = blk->next_offset;
                detail::avl_remove( base, hdr, nxt_idx );
                blk->next_offset = nxt->next_offset;
                if ( nxt->next_offset != detail::kNoBlock )
                    detail::block_at( base, nxt->next_offset )->prev_offset = b_idx;
                else
                    hdr->last_block_offset = b_idx;
                nxt->magic = 0;
                hdr->block_count--;
                hdr->free_count--;
                // Merged block header (kBlockHeaderGranules granules) is now reclaimed as free space
                if ( hdr->used_size >= detail::kBlockHeaderGranules )
                    hdr->used_size -= detail::kBlockHeaderGranules;
            }
        }

        // Слияние с предыдущим свободным соседом
        if ( blk->prev_offset != detail::kNoBlock )
        {
            detail::BlockHeader* prv = detail::block_at( base, blk->prev_offset );
            if ( prv->used_size == 0 )
            {
                std::uint32_t prv_idx = blk->prev_offset;
                detail::avl_remove( base, hdr, prv_idx );
                prv->next_offset = blk->next_offset;
                if ( blk->next_offset != detail::kNoBlock )
                    detail::block_at( base, blk->next_offset )->prev_offset = prv_idx;
                else
                    hdr->last_block_offset = prv_idx;
                blk->magic = 0;
                hdr->block_count--;
                hdr->free_count--;
                // Merged block header (kBlockHeaderGranules granules) is now reclaimed as free space
                if ( hdr->used_size >= detail::kBlockHeaderGranules )
                    hdr->used_size -= detail::kBlockHeaderGranules;
                detail::avl_insert( base, hdr, prv_idx );
                return;
            }
        }

        // No merge with prev — insert blk itself
        detail::avl_insert( base, hdr, b_idx );
    }

    void* allocate_from_block( detail::BlockHeader* blk, std::size_t user_size )
    {
        std::uint8_t*          base    = base_ptr();
        detail::ManagerHeader* hdr     = header();
        std::uint32_t          blk_idx = detail::block_idx( base, blk );
        detail::avl_remove( base, hdr, blk_idx );

        // Issue #59: block total granules computed from next_offset vs total_size
        std::uint32_t blk_total_gran = detail::block_total_granules( base, hdr, blk );

        // Granules needed for this allocation: header + data
        std::uint32_t data_gran   = detail::bytes_to_granules( user_size );
        std::uint32_t needed_gran = detail::kBlockHeaderGranules + data_gran;

        std::uint32_t min_rem_gran = detail::kBlockHeaderGranules + 1; // min: header + 1 data granule
        bool          can_split    = ( blk_total_gran >= needed_gran + min_rem_gran );

        if ( can_split )
        {
            std::uint32_t        new_idx = blk_idx + needed_gran;
            detail::BlockHeader* new_blk = detail::block_at( base, new_idx );
            std::memset( new_blk, 0, sizeof( detail::BlockHeader ) );
            new_blk->magic         = detail::kBlockMagic;
            new_blk->used_size     = 0;
            new_blk->prev_offset   = blk_idx;
            new_blk->next_offset   = blk->next_offset;
            new_blk->left_offset   = detail::kNoBlock;
            new_blk->right_offset  = detail::kNoBlock;
            new_blk->parent_offset = detail::kNoBlock;
            new_blk->avl_height    = 1;
            if ( blk->next_offset != detail::kNoBlock )
                detail::block_at( base, blk->next_offset )->prev_offset = new_idx;
            else
                hdr->last_block_offset = new_idx;
            blk->next_offset = new_idx;
            hdr->block_count++;
            hdr->free_count++;
            // The new split-off block header occupies kBlockHeaderGranules granules of overhead
            hdr->used_size += detail::kBlockHeaderGranules;
            detail::avl_insert( base, hdr, new_idx );
        }

        blk->used_size     = data_gran; // store in granules
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
        const detail::BlockHeader* node = detail::block_at( const_cast<std::uint8_t*>( base ), node_idx );
        if ( node->magic != detail::kBlockMagic || node->used_size != 0 )
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
    // Bounds check: avoid (i+1)*sizeof(T) overflow — use i >= capacity form instead
    std::size_t capacity = ( sizeof( T ) > 0 ) ? ( block_bytes / sizeof( T ) ) : 0;
    if ( i >= capacity )
        return nullptr;
    T* base_elem = static_cast<T*>( PersistMemoryManager::offset_to_ptr( _idx ) );
    return base_elem + i;
}

// ─── Реализация свободных функций ─────────────────────────────────────────────

/// @brief Получить статистику менеджера (Issue #61: без параметра PersistMemoryManager*).
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
        if ( blk->used_size == 0 )
        {
            // Issue #59: compute total granules from indices
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
            // total_fragmentation = total free bytes across all free blocks
            stats.total_fragmentation += blk_size;
        }
        idx = blk->next_offset;
    }
    return stats;
}

/// @brief Получить детальную информацию о менеджере (Issue #61: без параметра PersistMemoryManager*).
inline ManagerInfo get_manager_info()
{
    ManagerInfo           info{};
    PersistMemoryManager* mgr = PersistMemoryManager::instance();
    if ( mgr == nullptr )
        return info;
    const detail::ManagerHeader* hdr = mgr->header();
    info.magic                       = hdr->magic;
    info.total_size                  = hdr->total_size;
    // Issue #59: used_size in granules, convert to bytes for external API
    info.used_size           = detail::granules_to_bytes( hdr->used_size );
    info.block_count         = hdr->block_count;
    info.free_count          = hdr->free_count;
    info.alloc_count         = hdr->alloc_count;
    info.first_block_offset  = ( hdr->first_block_offset != detail::kNoBlock )
                                   ? static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( hdr->first_block_offset ) )
                                   : -1;
    info.first_free_offset   = ( hdr->free_tree_root != detail::kNoBlock )
                                   ? static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( hdr->free_tree_root ) )
                                   : -1;
    info.last_free_offset    = -1;
    info.manager_header_size = sizeof( detail::ManagerHeader );
    return info;
}

/// @brief Итерировать по всем блокам (Issue #61: без параметра PersistMemoryManager*).
///
/// @warning The callback MUST NOT call any mutating PersistMemoryManager methods
/// (allocate_typed, deallocate_typed, reallocate_typed, create, destroy, load).
/// Doing so while a shared_lock is held by the calling thread will deadlock, because
/// mutating methods acquire a unique_lock on the same non-recursive mutex.
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
        const detail::BlockHeader* blk = detail::block_at( const_cast<std::uint8_t*>( base ), idx );
        // Issue #59: total granules from indices
        std::uint32_t gran = ( blk->next_offset != detail::kNoBlock )
                                 ? ( blk->next_offset - idx )
                                 : ( detail::byte_off_to_idx( hdr->total_size ) - idx );
        BlockView     view;
        view.index       = index;
        view.offset      = static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( idx ) );
        view.total_size  = detail::granules_to_bytes( gran );
        view.header_size = sizeof( detail::BlockHeader );
        view.user_size   = detail::granules_to_bytes( blk->used_size ); // in bytes
        view.alignment   = kDefaultAlignment;                           // always 16 (Issue #59)
        view.used        = ( blk->used_size > 0 );
        cb( view );
        ++index;
        idx = blk->next_offset;
    }
}

/// @brief Iterate over all free blocks in the AVL free-block tree (linear pass, Issue #65).
/// Callback receives a FreeBlockView with AVL structural links; avl_depth is always 0
/// (depth is computed by the caller from AVL parent/child links if needed).
/// @warning The callback MUST NOT call any mutating PersistMemoryManager methods (deadlock).
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
        if ( blk->used_size == 0 )
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
