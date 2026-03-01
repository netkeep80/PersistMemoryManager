/**
 * @file persist_memory_manager.h
 * @brief Менеджер персистентной кучи памяти для C++17
 *
 * Single-header библиотека управления персистентной памятью.
 * Предоставляет низкоуровневый менеджер памяти, хранящий все метаданные
 * в управляемой области памяти для возможности персистентности между запусками.
 *
 * Алгоритм (Issue #55): Каждый блок содержит 6 ключевых полей:
 *   1. used_size   — занятый размер данных (0 = свободный блок)
 *   2. prev_offset — смещение предыдущего блока (kNoBlock = нет)
 *   3. next_offset — смещение следующего блока (kNoBlock = последний)
 *   4. left_offset — левый дочерний узел AVL-дерева свободных блоков
 *   5. right_offset — правый дочерний узел AVL-дерева свободных блоков
 *   6. parent_offset — родительский узел AVL-дерева
 *
 * Поиск свободного блока: best-fit через AVL-дерево (O(log n)).
 * При освобождении: слияние с соседними свободными блоками.
 *
 * @version 2.2.0
 *
 * Optimizations (Issue #57):
 *   1. O(1) back-pointer (boundary tag) in header_from_ptr — O(1) instead of up to 512 iterations.
 *   2. Restructured deallocate()+coalesce() — max 2 removes + 1 insert instead of insert+removes+insert.
 *   3. Actual-padding computation in allocate_from_block() — less internal fragmentation.
 *   4. last_block_offset in ManagerHeader — O(1) last-block lookup in expand().
 *
 * Optimizations (Issue #59):
 *   1. 16-byte alignment standard for all structures and blocks in PAS.
 *   2. 32-bit indices (uint32_t) for all block offsets — supports up to 4 GB per manager.
 *   3. BlockHeader reduced to 32 bytes (removed redundant total_size and alignment fields).
 *   4. total_size computed from next_offset - current_offset (last block uses manager total_size).
 *   5. pptr<T> reduced to 4 bytes (uint32_t offset) — halves pointer memory in JSON structures.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <shared_mutex>

namespace pmm
{

class PersistMemoryManager;

static constexpr std::size_t   kDefaultAlignment = 16;
static constexpr std::size_t   kMinAlignment     = 16;
static constexpr std::size_t   kMaxAlignment     = 4096;
static constexpr std::size_t   kMinMemorySize    = 4096;
static constexpr std::size_t   kMinBlockSize     = 32;
static constexpr std::uint64_t kMagic            = 0x504D4D5F56303232ULL; // "PMM_V022"
static constexpr std::size_t   kGrowNumerator    = 5;
static constexpr std::size_t   kGrowDenominator  = 4;

struct MemoryStats
{
    std::size_t total_blocks;
    std::size_t free_blocks;
    std::size_t allocated_blocks;
    std::size_t largest_free;
    std::size_t smallest_free;
    std::size_t total_fragmentation;
};

struct AllocationInfo
{
    void*       ptr;
    std::size_t size;
    std::size_t alignment;
    bool        is_valid;
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
    std::ptrdiff_t offset;
    std::size_t    total_size;
    std::size_t    header_size;
    std::size_t    user_size;
    std::size_t    alignment;
    bool           used;
};

namespace detail
{

/**
 * @brief Заголовок блока памяти (Issue #59: 32 байта, 16-байтное выравнивание).
 *
 * Issue #59: Оптимизированная структура:
 *   - magic (4B) + used_size (4B) = 8 байт
 *   - prev_offset (4B) + next_offset (4B) = 8 байт
 *   - left_offset (4B) + right_offset (4B) = 8 байт
 *   - parent_offset (4B) + avl_height (2B) + alignment_log2 (1B) + _pad (1B) = 8 байт
 *   Итого: 32 байта
 *
 * Удалено поле: total_size (вычисляется по next_offset).
 * alignment хранится компактно как log2: alignment = 1 << alignment_log2 (1 байт).
 * Смещения — std::uint32_t (4 байта), kNoBlock = UINT32_MAX.
 * Поддерживает до 4 ГБ на один менеджер.
 *
 * Поля: used_size (1), prev_offset (2), next_offset (3),
 *       left_offset (4), right_offset (5), parent_offset (6).
 */
struct BlockHeader
{
    std::uint32_t magic;          ///< Магическое число (4 байта)
    std::uint32_t used_size;      ///< [1] Занятый размер (0 = свободный). Max 4 ГБ.
    std::uint32_t prev_offset;    ///< [2] Предыдущий блок в адресном порядке
    std::uint32_t next_offset;    ///< [3] Следующий блок в адресном порядке
    std::uint32_t left_offset;    ///< [4] Левый дочерний узел AVL-дерева
    std::uint32_t right_offset;   ///< [5] Правый дочерний узел AVL-дерева
    std::uint32_t parent_offset;  ///< [6] Родительский узел AVL-дерева
    std::int16_t  avl_height;     ///< Высота AVL-поддерева (0 = не в дереве)
    std::uint8_t  alignment_log2; ///< log2(alignment): alignment = 1 << alignment_log2
    std::uint8_t  _pad;           ///< Выравнивание
};

static_assert( sizeof( BlockHeader ) == 32, "BlockHeader must be exactly 32 bytes (Issue #59)" );
static_assert( sizeof( BlockHeader ) % 16 == 0, "BlockHeader must be 16-byte aligned (Issue #59)" );

static constexpr std::uint32_t kBlockMagic = 0x424C4B32U; // "BLK2" (Issue #59 version)
static constexpr std::uint32_t kNoBlock    = 0xFFFFFFFFU; ///< Sentinel: нет блока

/**
 * @brief Заголовок менеджера памяти (Issue #59: 64 байта, 16-байтное выравнивание).
 *
 * Оптимизированная структура с 32-битными смещениями:
 *   - magic (8B) + total_size (8B) = 16 байт
 *   - used_size (4B) + block_count (4B) + free_count (4B) + alloc_count (4B) = 16 байт
 *   - first_block_offset (4B) + last_block_offset (4B) + free_tree_root (4B) + owns_memory(1B)+pad(3B) = 16 байт
 *   - prev_total_size (8B) + prev_base (8B) = 16 байт
 *   Итого: 64 байта
 */
struct ManagerHeader
{
    std::uint64_t  magic;             ///< Магическое число менеджера
    std::uint64_t  total_size;        ///< Полный размер управляемой области
    std::uint32_t  used_size;         ///< Занятый размер (включая заголовки)
    std::uint32_t  block_count;       ///< Общее число блоков
    std::uint32_t  free_count;        ///< Число свободных блоков
    std::uint32_t  alloc_count;       ///< Число занятых блоков
    std::uint32_t  first_block_offset;///< Первый блок в адресном порядке
    std::uint32_t  last_block_offset; ///< [Issue #57 opt 4] Последний блок — O(1) в expand()
    std::uint32_t  free_tree_root;    ///< Корень AVL-дерева свободных блоков
    bool           owns_memory;       ///< Менеджер владеет буфером
    std::uint8_t   _pad[3];           ///< Выравнивание
    std::uint64_t  prev_total_size;   ///< Размер предыдущего буфера (при расширении)
    void*          prev_base;         ///< Указатель на предыдущий буфер
};

static_assert( sizeof( ManagerHeader ) == 64, "ManagerHeader must be exactly 64 bytes (Issue #59)" );
static_assert( sizeof( ManagerHeader ) % 16 == 0, "ManagerHeader must be 16-byte aligned (Issue #59)" );

inline std::size_t align_up( std::size_t value, std::size_t align )
{
    assert( align != 0 && ( align & ( align - 1 ) ) == 0 );
    return ( value + align - 1 ) & ~( align - 1 );
}

inline bool is_valid_alignment( std::size_t align )
{
    return align >= kMinAlignment && align <= kMaxAlignment && ( align & ( align - 1 ) ) == 0;
}

/// @brief Вычислить log2 выравнивания (align должно быть степенью двойки >= 1).
inline std::uint8_t alignment_to_log2( std::size_t align )
{
    std::uint8_t log2 = 0;
    while ( align > 1 ) { align >>= 1; ++log2; }
    return log2;
}

/// @brief Восстановить выравнивание из alignment_log2.
inline std::size_t log2_to_alignment( std::uint8_t log2 )
{
    return static_cast<std::size_t>( 1 ) << log2;
}

inline BlockHeader* block_at( std::uint8_t* base, std::uint32_t offset )
{
    assert( offset != kNoBlock );
    return reinterpret_cast<BlockHeader*>( base + offset );
}

inline std::uint32_t block_offset( const std::uint8_t* base, const BlockHeader* block )
{
    return static_cast<std::uint32_t>( reinterpret_cast<const std::uint8_t*>( block ) - base );
}

/// @brief Вычислить total_size блока из цепочки: next_offset - this_offset, или manager total_size - this_offset.
/// Issue #59: total_size больше не хранится в заголовке блока.
inline std::size_t block_total_size( const std::uint8_t* base, const ManagerHeader* hdr, const BlockHeader* blk )
{
    std::uint32_t this_off = block_offset( base, blk );
    if ( blk->next_offset != kNoBlock )
        return static_cast<std::size_t>( blk->next_offset - this_off );
    return static_cast<std::size_t>( hdr->total_size ) - static_cast<std::size_t>( this_off );
}

/// @brief Вычислить адрес пользовательских данных для блока.
/// Issue #57 opt 1: пропускает sizeof(uint32_t) байт перед user_ptr для back-pointer тега.
/// Issue #59: тег теперь 4 байта (uint32_t); alignment хранится как alignment_log2.
inline void* user_ptr( BlockHeader* block )
{
    std::uint8_t* raw          = reinterpret_cast<std::uint8_t*>( block ) + sizeof( BlockHeader );
    std::size_t   addr         = reinterpret_cast<std::size_t>( raw ) + sizeof( std::uint32_t );
    std::size_t   alignment    = log2_to_alignment( block->alignment_log2 );
    std::size_t   aligned_addr = align_up( addr, alignment );
    return reinterpret_cast<void*>( aligned_addr );
}

/// @brief Записать тег обратного указателя (Issue #57 opt 1, Issue #59: тег 4 байта).
/// Хранит offset блока (uint32_t) в 4 байтах прямо перед user_ptr.
/// Вызывать при каждом выделении (allocate_from_block).
inline void write_back_ptr( std::uint8_t* base, BlockHeader* block )
{
    std::uint8_t* uptr = reinterpret_cast<std::uint8_t*>( user_ptr( block ) );
    std::uint32_t off  = static_cast<std::uint32_t>( reinterpret_cast<std::uint8_t*>( block ) - base );
    std::memcpy( uptr - sizeof( std::uint32_t ), &off, sizeof( std::uint32_t ) );
}

/// @brief O(1) восстановление заголовка блока по user_ptr через тег (Issue #57 opt 1, Issue #59: тег 4 байта).
inline BlockHeader* header_from_ptr( std::uint8_t* base, void* ptr )
{
    if ( ptr == nullptr )
        return nullptr;
    std::uint8_t* raw_ptr  = reinterpret_cast<std::uint8_t*>( ptr );
    std::uint8_t* min_addr = base + sizeof( ManagerHeader );
    // Minimum size needed before user_ptr: BlockHeader + tag(4B)
    if ( raw_ptr < min_addr + sizeof( BlockHeader ) + sizeof( std::uint32_t ) )
        return nullptr;

    // Read the back-pointer tag stored just before user_ptr
    std::uint32_t off = 0;
    std::memcpy( &off, raw_ptr - sizeof( std::uint32_t ), sizeof( std::uint32_t ) );
    if ( off == kNoBlock )
        return nullptr;
    std::uint8_t* cand_addr = base + off;
    if ( cand_addr < min_addr || cand_addr + sizeof( BlockHeader ) > raw_ptr )
        return nullptr;
    BlockHeader* cand = reinterpret_cast<BlockHeader*>( cand_addr );
    if ( cand->magic == kBlockMagic && cand->used_size > 0 && user_ptr( cand ) == ptr )
        return cand;
    return nullptr;
}

inline BlockHeader* find_block_by_ptr( std::uint8_t* base, const ManagerHeader* mhdr, void* ptr )
{
    std::uint32_t offset = mhdr->first_block_offset;
    while ( offset != kNoBlock )
    {
        BlockHeader* blk = block_at( base, offset );
        if ( blk->used_size > 0 && user_ptr( blk ) == ptr )
            return blk;
        offset = blk->next_offset;
    }
    return nullptr;
}

/// @brief Worst-case размер блока: заголовок + overhead + данные.
/// Issue #59: tag теперь 4 байта (sizeof(uint32_t)), alignment всегда kDefaultAlignment.
/// user_ptr() = align_up(raw + sizeof(uint32_t), kDefaultAlignment), raw = block + sizeof(BlockHeader).
inline std::size_t required_block_size( std::size_t user_size, std::size_t alignment )
{
    std::size_t min_total = sizeof( BlockHeader ) + alignment + user_size;
    return align_up( std::max( min_total, kMinBlockSize ), kMinAlignment );
}

// ─── AVL-дерево свободных блоков (ключ: total_size, затем offset) ────────────

inline std::int32_t avl_height( std::uint8_t* base, std::uint32_t off )
{
    return ( off == kNoBlock ) ? 0 : block_at( base, off )->avl_height;
}

inline void avl_update_height( std::uint8_t* base, BlockHeader* node )
{
    node->avl_height = 1 + std::max( avl_height( base, node->left_offset ), avl_height( base, node->right_offset ) );
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

inline std::uint32_t avl_rotate_right( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t y_off )
{
    BlockHeader*  y     = block_at( base, y_off );
    std::uint32_t x_off = y->left_offset;
    BlockHeader*  x     = block_at( base, x_off );
    std::uint32_t t2    = x->right_offset;

    x->right_offset  = y_off;
    y->left_offset   = t2;
    x->parent_offset = y->parent_offset;
    y->parent_offset = x_off;
    if ( t2 != kNoBlock )
        block_at( base, t2 )->parent_offset = y_off;
    avl_set_child( base, hdr, x->parent_offset, y_off, x_off );
    avl_update_height( base, y );
    avl_update_height( base, x );
    return x_off;
}

inline std::uint32_t avl_rotate_left( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t x_off )
{
    BlockHeader*  x     = block_at( base, x_off );
    std::uint32_t y_off = x->right_offset;
    BlockHeader*  y     = block_at( base, y_off );
    std::uint32_t t2    = y->left_offset;

    y->left_offset   = x_off;
    x->right_offset  = t2;
    y->parent_offset = x->parent_offset;
    x->parent_offset = y_off;
    if ( t2 != kNoBlock )
        block_at( base, t2 )->parent_offset = x_off;
    avl_set_child( base, hdr, y->parent_offset, x_off, y_off );
    avl_update_height( base, x );
    avl_update_height( base, y );
    return y_off;
}

inline void avl_rebalance_up( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t node_off )
{
    std::uint32_t cur = node_off;
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

inline void avl_insert( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t blk_off )
{
    BlockHeader* blk   = block_at( base, blk_off );
    blk->left_offset   = kNoBlock;
    blk->right_offset  = kNoBlock;
    blk->parent_offset = kNoBlock;
    blk->avl_height    = 1;
    if ( hdr->free_tree_root == kNoBlock )
    {
        hdr->free_tree_root = blk_off;
        return;
    }
    std::uint32_t cur = hdr->free_tree_root, parent = kNoBlock;
    bool          go_left = false;
    while ( cur != kNoBlock )
    {
        parent         = cur;
        BlockHeader* n = block_at( base, cur );
        // Issue #59: total_size computed from offsets
        std::size_t blk_size = ( blk->next_offset != kNoBlock )
                                   ? static_cast<std::size_t>( blk->next_offset - blk_off )
                                   : static_cast<std::size_t>( hdr->total_size - blk_off );
        std::size_t n_size   = ( n->next_offset != kNoBlock )
                                   ? static_cast<std::size_t>( n->next_offset - cur )
                                   : static_cast<std::size_t>( hdr->total_size - cur );
        bool smaller   = ( blk_size < n_size ) || ( blk_size == n_size && blk_off <= cur );
        go_left        = smaller;
        cur            = smaller ? n->left_offset : n->right_offset;
    }
    blk->parent_offset = parent;
    if ( go_left )
        block_at( base, parent )->left_offset = blk_off;
    else
        block_at( base, parent )->right_offset = blk_off;
    avl_rebalance_up( base, hdr, parent );
}

inline std::uint32_t avl_min_node( std::uint8_t* base, std::uint32_t node_off )
{
    while ( node_off != kNoBlock )
    {
        std::uint32_t left = block_at( base, node_off )->left_offset;
        if ( left == kNoBlock )
            break;
        node_off = left;
    }
    return node_off;
}

inline void avl_remove( std::uint8_t* base, ManagerHeader* hdr, std::uint32_t blk_off )
{
    BlockHeader*  blk    = block_at( base, blk_off );
    std::uint32_t parent = blk->parent_offset;
    std::uint32_t left   = blk->left_offset;
    std::uint32_t right  = blk->right_offset;
    std::uint32_t rebal  = kNoBlock;

    if ( left == kNoBlock && right == kNoBlock )
    {
        avl_set_child( base, hdr, parent, blk_off, kNoBlock );
        rebal = parent;
    }
    else if ( left == kNoBlock || right == kNoBlock )
    {
        std::uint32_t child                   = ( left != kNoBlock ) ? left : right;
        block_at( base, child )->parent_offset = parent;
        avl_set_child( base, hdr, parent, blk_off, child );
        rebal = parent;
    }
    else
    {
        std::uint32_t succ_off    = avl_min_node( base, right );
        BlockHeader*  succ        = block_at( base, succ_off );
        std::uint32_t succ_parent = succ->parent_offset;
        std::uint32_t succ_right  = succ->right_offset;

        if ( succ_parent != blk_off )
        {
            avl_set_child( base, hdr, succ_parent, succ_off, succ_right );
            if ( succ_right != kNoBlock )
                block_at( base, succ_right )->parent_offset = succ_parent;
            succ->right_offset                     = right;
            block_at( base, right )->parent_offset = succ_off;
            rebal                                  = succ_parent;
        }
        else
        {
            rebal = succ_off;
        }
        succ->left_offset                     = left;
        block_at( base, left )->parent_offset = succ_off;
        succ->parent_offset                   = parent;
        avl_set_child( base, hdr, parent, blk_off, succ_off );
        avl_update_height( base, succ );
    }
    blk->left_offset   = kNoBlock;
    blk->right_offset  = kNoBlock;
    blk->parent_offset = kNoBlock;
    blk->avl_height    = 0;
    avl_rebalance_up( base, hdr, rebal );
}

/// @brief Найти наименьший блок >= needed (best-fit, O(log n)).
/// Issue #59: total_size вычисляется из смещений.
inline std::uint32_t avl_find_best_fit( std::uint8_t* base, ManagerHeader* hdr, std::size_t needed )
{
    std::uint32_t cur = hdr->free_tree_root, result = kNoBlock;
    while ( cur != kNoBlock )
    {
        BlockHeader* node    = block_at( base, cur );
        std::size_t  cur_size = ( node->next_offset != kNoBlock )
                                    ? static_cast<std::size_t>( node->next_offset - cur )
                                    : static_cast<std::size_t>( hdr->total_size - cur );
        if ( cur_size >= needed )
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

/**
 * @brief Персистный типизированный указатель (Issue #59: 4 байта).
 *
 * Issue #59: использует uint32_t вместо ptrdiff_t.
 * Размер уменьшен с 8 до 4 байт.
 * Максимальное смещение: 4 ГБ - 1.
 * Нулевое значение (0) означает nullptr.
 */
template <class T> class pptr
{
    std::uint32_t _offset;

  public:
    inline pptr() noexcept : _offset( 0 ) {}
    inline explicit pptr( std::uint32_t offset ) noexcept : _offset( offset ) {}
    inline pptr( const pptr<T>& ) noexcept               = default;
    inline pptr<T>& operator=( const pptr<T>& ) noexcept = default;
    inline ~pptr() noexcept                              = default;

    inline bool          is_null() const noexcept { return _offset == 0; }
    inline explicit      operator bool() const noexcept { return _offset != 0; }
    inline std::uint32_t offset() const noexcept { return _offset; }

    inline T* get() const noexcept;
    inline T& operator*() const noexcept { return *get(); }
    inline T* operator->() const noexcept { return get(); }
    inline T* get_at( std::size_t index ) const noexcept;

    inline T*       resolve( PersistMemoryManager* mgr ) const noexcept;
    inline const T* resolve( const PersistMemoryManager* mgr ) const noexcept;
    inline T*       resolve_at( PersistMemoryManager* mgr, std::size_t index ) const noexcept;

    inline bool operator==( const pptr<T>& other ) const noexcept { return _offset == other._offset; }
    inline bool operator!=( const pptr<T>& other ) const noexcept { return _offset != other._offset; }
};

// Issue #59: pptr<T> теперь 4 байта (uint32_t), не sizeof(void*)
static_assert( sizeof( pptr<int> ) == 4, "sizeof(pptr<T>) должен быть 4 байта (Issue #59)" );
static_assert( sizeof( pptr<double> ) == 4, "sizeof(pptr<T>) должен быть 4 байта (Issue #59)" );

// ─── Основной класс ───────────────────────────────────────────────────────────

MemoryStats    get_stats( const PersistMemoryManager* mgr );
AllocationInfo get_info( const PersistMemoryManager* mgr, void* ptr );

class PersistMemoryManager
{
  public:
    static PersistMemoryManager* instance() noexcept { return s_instance; }

    static PersistMemoryManager* create( void* memory, std::size_t size )
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( memory == nullptr || size < kMinMemorySize )
            return nullptr;
        if ( size > 0xFFFFFFFFULL )
            return nullptr; // Issue #59: max 4 GB per manager

        std::uint8_t*          base = static_cast<std::uint8_t*>( memory );
        detail::ManagerHeader* hdr  = reinterpret_cast<detail::ManagerHeader*>( base );
        std::memset( hdr, 0, sizeof( detail::ManagerHeader ) );
        hdr->magic              = kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = detail::kNoBlock;
        hdr->last_block_offset  = detail::kNoBlock; // Issue #57 opt 4
        hdr->free_tree_root     = detail::kNoBlock;
        hdr->owns_memory        = true;

        std::size_t    hdr_end = detail::align_up( sizeof( detail::ManagerHeader ), kDefaultAlignment );
        std::uint32_t  blk_off = static_cast<std::uint32_t>( hdr_end );

        if ( static_cast<std::size_t>( blk_off ) + sizeof( detail::BlockHeader ) + kMinBlockSize > size )
            return nullptr;

        detail::BlockHeader* blk = detail::block_at( base, blk_off );
        std::memset( blk, 0, sizeof( detail::BlockHeader ) );
        blk->magic         = detail::kBlockMagic;
        // Issue #59: no total_size field; computed from next_offset vs manager total_size
        blk->used_size     = 0;
        blk->prev_offset   = detail::kNoBlock;
        blk->next_offset   = detail::kNoBlock;
        blk->left_offset   = detail::kNoBlock;
        blk->right_offset  = detail::kNoBlock;
        blk->parent_offset = detail::kNoBlock;
        blk->avl_height    = 1;

        hdr->first_block_offset = blk_off;
        hdr->last_block_offset  = blk_off; // Issue #57 opt 4
        hdr->free_tree_root     = blk_off;
        hdr->block_count        = 1;
        hdr->free_count         = 1;
        hdr->used_size          = static_cast<std::uint32_t>( hdr_end + sizeof( detail::BlockHeader ) );

        PersistMemoryManager* mgr = reinterpret_cast<PersistMemoryManager*>( base );
        s_instance                = mgr;
        return mgr;
    }

    static PersistMemoryManager* load( void* memory, std::size_t size )
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( memory == nullptr || size < kMinMemorySize )
            return nullptr;
        std::uint8_t*          base = static_cast<std::uint8_t*>( memory );
        detail::ManagerHeader* hdr  = reinterpret_cast<detail::ManagerHeader*>( base );
        if ( hdr->magic != kMagic || hdr->total_size != size )
            return nullptr;
        hdr->owns_memory     = true;
        hdr->prev_total_size = 0;
        hdr->prev_base       = nullptr;
        auto* mgr            = reinterpret_cast<PersistMemoryManager*>( base );
        mgr->rebuild_free_tree();
        s_instance = mgr;
        return mgr;
    }

    static void destroy()
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( s_instance == nullptr )
            return;
        detail::ManagerHeader* hdr = s_instance->header();
        hdr->magic                 = 0;
        bool  owns                 = hdr->owns_memory;
        void* buf                  = s_instance->base_ptr();
        void* prev                 = hdr->prev_base;
        bool  prev_owns            = ( prev != nullptr );
        s_instance                 = nullptr;
        while ( prev != nullptr && prev_owns )
        {
            detail::ManagerHeader* ph        = reinterpret_cast<detail::ManagerHeader*>( prev );
            void*                  next_prev = ph->prev_base;
            std::free( prev );
            prev      = next_prev;
            prev_owns = ( next_prev != nullptr );
        }
        if ( owns )
            std::free( buf );
    }

    void* allocate( std::size_t user_size, std::size_t alignment = kDefaultAlignment )
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( user_size == 0 || !detail::is_valid_alignment( alignment ) )
            return nullptr;

        std::uint8_t*          base   = base_ptr();
        detail::ManagerHeader* hdr    = header();
        std::size_t            needed = detail::required_block_size( user_size, alignment );
        std::uint32_t          off    = detail::avl_find_best_fit( base, hdr, needed );

        if ( off != detail::kNoBlock )
            return allocate_from_block( detail::block_at( base, off ), user_size, alignment );

        if ( !expand( user_size, alignment ) )
            return nullptr;

        PersistMemoryManager* new_mgr = s_instance;
        if ( new_mgr == nullptr )
            return nullptr;
        std::uint8_t*          nb  = new_mgr->base_ptr();
        detail::ManagerHeader* nh  = new_mgr->header();
        std::uint32_t          nof = detail::avl_find_best_fit( nb, nh, needed );
        if ( nof != detail::kNoBlock )
            return new_mgr->allocate_from_block( detail::block_at( nb, nof ), user_size, alignment );
        return nullptr;
    }

    void deallocate( void* ptr )
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( ptr == nullptr )
            return;
        ptr                         = translate_ptr( ptr );
        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();
        detail::BlockHeader*   blk  = detail::header_from_ptr( base, ptr );
        if ( blk == nullptr || blk->used_size == 0 )
            return;

        std::size_t freed = blk->used_size;
        blk->used_size    = 0;
        hdr->alloc_count--;
        hdr->free_count++;
        if ( hdr->used_size >= freed )
            hdr->used_size -= static_cast<std::uint32_t>( freed );
        // Issue #57 opt 2: do NOT insert into AVL yet; coalesce() will do a single insert after merging.
        coalesce( blk );
    }

    void* reallocate( void* ptr, std::size_t new_size )
    {
        if ( ptr == nullptr )
            return allocate( new_size );
        if ( new_size == 0 )
        {
            deallocate( ptr );
            return nullptr;
        }
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        ptr                       = translate_ptr( ptr );
        std::uint8_t*        base = base_ptr();
        detail::BlockHeader* blk  = detail::header_from_ptr( base, ptr );
        if ( blk == nullptr || blk->used_size == 0 )
            return nullptr;
        if ( new_size <= blk->used_size )
            return ptr;
        std::size_t old_size = blk->used_size;
        std::size_t align    = detail::log2_to_alignment( blk->alignment_log2 ); // Issue #59
        lock.unlock();
        void* new_ptr = allocate( new_size, align );
        if ( new_ptr == nullptr )
            return nullptr;
        std::memcpy( new_ptr, ptr, old_size );
        deallocate( ptr );
        return new_ptr;
    }

    template <class T> pptr<T> allocate_typed()
    {
        std::size_t align = alignof( T ) < kMinAlignment ? kMinAlignment : alignof( T );
        void*       raw   = allocate( sizeof( T ), align );
        if ( raw == nullptr )
            return pptr<T>();
        return pptr<T>( static_cast<std::uint32_t>( static_cast<std::uint8_t*>( raw ) - base_ptr() ) );
    }

    template <class T> pptr<T> allocate_typed( std::size_t count )
    {
        if ( count == 0 )
            return pptr<T>();
        std::size_t align = alignof( T ) < kMinAlignment ? kMinAlignment : alignof( T );
        void*       raw   = allocate( sizeof( T ) * count, align );
        if ( raw == nullptr )
            return pptr<T>();
        return pptr<T>( static_cast<std::uint32_t>( static_cast<std::uint8_t*>( raw ) - base_ptr() ) );
    }

    template <class T> void deallocate_typed( pptr<T> p )
    {
        if ( !p.is_null() )
            deallocate( base_ptr() + p.offset() );
    }

    void* offset_to_ptr( std::uint32_t offset ) noexcept { return ( offset == 0 ) ? nullptr : base_ptr() + offset; }

    const void* offset_to_ptr( std::uint32_t offset ) const noexcept
    {
        return ( offset == 0 ) ? nullptr : const_base_ptr() + offset;
    }

    std::size_t        total_size() const { return header()->total_size; }
    static std::size_t manager_header_size() noexcept { return sizeof( detail::ManagerHeader ); }
    std::size_t        used_size() const { return header()->used_size; }
    std::size_t        free_size() const
    {
        const detail::ManagerHeader* hdr = header();
        return ( hdr->total_size > hdr->used_size ) ? ( hdr->total_size - hdr->used_size ) : 0;
    }
    std::size_t fragmentation() const
    {
        const detail::ManagerHeader* hdr = header();
        return ( hdr->free_count > 1 ) ? ( hdr->free_count - 1 ) : 0;
    }

    bool validate() const
    {
        std::shared_lock<std::shared_mutex> lock( s_mutex );
        const std::uint8_t*                 base = const_base_ptr();
        const detail::ManagerHeader*        hdr  = header();

        if ( hdr->magic != kMagic )
            return false;

        std::size_t   block_count = 0, free_count = 0, alloc_count = 0;
        std::uint32_t offset = hdr->first_block_offset;
        while ( offset != detail::kNoBlock )
        {
            if ( static_cast<std::size_t>( offset ) >= hdr->total_size )
                return false;
            const detail::BlockHeader* blk = reinterpret_cast<const detail::BlockHeader*>( base + offset );
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
                    reinterpret_cast<const detail::BlockHeader*>( base + blk->next_offset );
                if ( nxt->prev_offset != offset )
                    return false;
            }
            offset = blk->next_offset;
        }
        std::size_t tree_free = 0;
        if ( !validate_avl( base, hdr, hdr->free_tree_root, tree_free ) )
            return false;
        if ( tree_free != free_count )
            return false;
        return ( block_count == hdr->block_count && free_count == hdr->free_count && alloc_count == hdr->alloc_count );
    }

    void dump_stats() const
    {
        std::shared_lock<std::shared_mutex> lock( s_mutex );
        const detail::ManagerHeader*        hdr = header();
        std::cout << "=== PersistMemoryManager stats ===\n"
                  << "  total_size  : " << hdr->total_size << " bytes\n"
                  << "  used_size   : " << hdr->used_size << " bytes\n"
                  << "  free_size   : " << free_size() << " bytes\n"
                  << "  blocks      : " << hdr->block_count << " (free=" << hdr->free_count
                  << ", alloc=" << hdr->alloc_count << ")\n"
                  << "  fragmentation: " << fragmentation() << " extra free segments\n"
                  << "==================================\n";
    }

    friend MemoryStats                       get_stats( const PersistMemoryManager* mgr );
    friend AllocationInfo                    get_info( const PersistMemoryManager* mgr, void* ptr );
    friend ManagerInfo                       get_manager_info( const PersistMemoryManager* mgr );
    template <typename Callback> friend void for_each_block( const PersistMemoryManager* mgr, Callback&& cb );

  private:
    static PersistMemoryManager* s_instance;
    static std::shared_mutex     s_mutex;

    std::uint8_t*                base_ptr() { return reinterpret_cast<std::uint8_t*>( this ); }
    const std::uint8_t*          const_base_ptr() const { return reinterpret_cast<const std::uint8_t*>( this ); }
    detail::ManagerHeader*       header() { return reinterpret_cast<detail::ManagerHeader*>( this ); }
    const detail::ManagerHeader* header() const { return reinterpret_cast<const detail::ManagerHeader*>( this ); }

    bool expand( std::size_t user_size, std::size_t alignment )
    {
        detail::ManagerHeader* hdr      = header();
        std::size_t            old_size = hdr->total_size;
        std::size_t            needed   = detail::required_block_size( user_size, alignment );
        std::size_t new_size = detail::align_up( old_size * kGrowNumerator / kGrowDenominator, kMinAlignment );
        if ( new_size < old_size + needed )
            new_size = detail::align_up( old_size + needed +
                                             detail::align_up( sizeof( detail::BlockHeader ), kDefaultAlignment ),
                                         kMinAlignment );

        if ( new_size > 0xFFFFFFFFULL )
            return false; // Issue #59: max 4 GB

        void* new_memory = std::malloc( new_size );
        if ( new_memory == nullptr )
            return false;

        bool old_owns = hdr->owns_memory;
        std::memcpy( new_memory, base_ptr(), old_size );
        detail::ManagerHeader* nh = reinterpret_cast<detail::ManagerHeader*>( new_memory );
        std::uint8_t*          nb = static_cast<std::uint8_t*>( new_memory );
        nh->owns_memory           = true;

        std::uint32_t extra_off  = static_cast<std::uint32_t>( old_size );
        std::size_t   extra_size = new_size - old_size;
        // Issue #57 opt 4: O(1) last-block lookup via last_block_offset
        detail::BlockHeader* last_blk =
            ( nh->last_block_offset != detail::kNoBlock ) ? detail::block_at( nb, nh->last_block_offset ) : nullptr;

        if ( last_blk != nullptr && last_blk->used_size == 0 )
        {
            std::uint32_t loff = detail::block_offset( nb, last_blk );
            detail::avl_remove( nb, nh, loff );
            // Issue #59: no total_size field — just update next_offset or manager total_size
            // Since last_blk is the last block, its size = new_size - loff (updated below via nh->total_size)
            // We need to re-insert after updating total_size
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
            detail::BlockHeader* nb_blk = detail::block_at( nb, extra_off );
            std::memset( nb_blk, 0, sizeof( detail::BlockHeader ) );
            nb_blk->magic         = detail::kBlockMagic;
            nb_blk->used_size     = 0;
            nb_blk->left_offset   = detail::kNoBlock;
            nb_blk->right_offset  = detail::kNoBlock;
            nb_blk->parent_offset = detail::kNoBlock;
            nb_blk->avl_height    = 1;
            if ( last_blk != nullptr )
            {
                std::uint32_t loff    = detail::block_offset( nb, last_blk );
                nb_blk->prev_offset   = loff;
                nb_blk->next_offset   = detail::kNoBlock;
                last_blk->next_offset = extra_off;
            }
            else
            {
                nb_blk->prev_offset    = detail::kNoBlock;
                nb_blk->next_offset    = detail::kNoBlock;
                nh->first_block_offset = extra_off;
            }
            nh->last_block_offset = extra_off; // Issue #57 opt 4
            nh->block_count++;
            nh->free_count++;
            nh->total_size = new_size; // Update total_size before AVL insert (affects size computation)
            detail::avl_insert( nb, nh, extra_off );
        }
        nh->prev_base       = base_ptr();
        nh->prev_total_size = old_size;
        s_instance          = reinterpret_cast<PersistMemoryManager*>( new_memory );
        if ( old_owns )
        {
            // Schedule old buffer to be freed; it's chained via prev_base
            // (old buffer will be freed in destroy() via prev_base chain)
        }
        return true;
    }

    void rebuild_free_tree()
    {
        std::uint8_t*          base = base_ptr();
        detail::ManagerHeader* hdr  = header();
        hdr->free_tree_root         = detail::kNoBlock;
        hdr->last_block_offset      = detail::kNoBlock; // Issue #57 opt 4: rebuild
        std::uint32_t offset        = hdr->first_block_offset;
        while ( offset != detail::kNoBlock )
        {
            detail::BlockHeader* blk = detail::block_at( base, offset );
            blk->left_offset         = detail::kNoBlock;
            blk->right_offset        = detail::kNoBlock;
            blk->parent_offset       = detail::kNoBlock;
            blk->avl_height          = 0;
            if ( blk->used_size == 0 )
                detail::avl_insert( base, hdr, offset );
            if ( blk->next_offset == detail::kNoBlock )
                hdr->last_block_offset = offset; // track last block
            offset = blk->next_offset;
        }
    }

    void* translate_ptr( void* ptr ) const noexcept
    {
        std::uint8_t*                base     = const_cast<PersistMemoryManager*>( this )->base_ptr();
        const detail::ManagerHeader* hdr      = header();
        std::uint8_t*                raw      = static_cast<std::uint8_t*>( ptr );
        void*                        prev_buf = hdr->prev_base;
        std::size_t                  prev_sz  = hdr->prev_total_size;
        while ( prev_buf != nullptr && prev_sz > 0 )
        {
            std::uint8_t* prev = static_cast<std::uint8_t*>( prev_buf );
            if ( raw >= prev && raw < prev + prev_sz )
                return base + ( raw - prev );
            detail::ManagerHeader* ph = reinterpret_cast<detail::ManagerHeader*>( prev_buf );
            prev_buf                  = ph->prev_base;
            prev_sz                   = ph->prev_total_size;
        }
        return ptr;
    }

    /// @brief Слить блок с соседними свободными блоками и вставить результат в AVL один раз.
    /// Issue #57 opt 2: max 2 avl_remove + 1 avl_insert вместо insert + remove/remove/insert.
    /// Issue #57 opt 4: поддерживает last_block_offset.
    /// Issue #59: total_size вычисляется из смещений, а не хранится в заголовке.
    void coalesce( detail::BlockHeader* blk )
    {
        std::uint8_t*          base  = base_ptr();
        detail::ManagerHeader* hdr   = header();
        std::uint32_t          b_off = detail::block_offset( base, blk );

        // Слияние со следующим свободным соседом
        if ( blk->next_offset != detail::kNoBlock )
        {
            detail::BlockHeader* nxt = detail::block_at( base, blk->next_offset );
            if ( nxt->used_size == 0 )
            {
                std::uint32_t nxt_off = blk->next_offset;
                detail::avl_remove( base, hdr, nxt_off );
                // Issue #59: merge by updating next_offset (total_size implicitly updated)
                blk->next_offset = nxt->next_offset;
                if ( nxt->next_offset != detail::kNoBlock )
                    detail::block_at( base, nxt->next_offset )->prev_offset = b_off;
                else
                    hdr->last_block_offset = b_off; // Issue #57 opt 4
                nxt->magic = 0;
                hdr->block_count--;
                hdr->free_count--;
            }
        }

        // Слияние с предыдущим свободным соседом
        if ( blk->prev_offset != detail::kNoBlock )
        {
            detail::BlockHeader* prv = detail::block_at( base, blk->prev_offset );
            if ( prv->used_size == 0 )
            {
                std::uint32_t prv_off = blk->prev_offset;
                detail::avl_remove( base, hdr, prv_off );
                // Issue #59: merge by updating next_offset of prev block
                prv->next_offset = blk->next_offset;
                if ( blk->next_offset != detail::kNoBlock )
                    detail::block_at( base, blk->next_offset )->prev_offset = prv_off;
                else
                    hdr->last_block_offset = prv_off; // Issue #57 opt 4
                blk->magic = 0;
                hdr->block_count--;
                hdr->free_count--;
                // Insert the merged prev block (which absorbed blk)
                detail::avl_insert( base, hdr, prv_off );
                return;
            }
        }

        // No merge with prev — insert blk itself
        detail::avl_insert( base, hdr, b_off );
    }

    void* allocate_from_block( detail::BlockHeader* blk, std::size_t user_size, std::size_t alignment )
    {
        std::uint8_t*          base    = base_ptr();
        detail::ManagerHeader* hdr     = header();
        std::uint32_t          blk_off = detail::block_offset( base, blk );
        detail::avl_remove( base, hdr, blk_off );

        // Issue #59: compute block total_size from offsets
        std::size_t blk_total_size = detail::block_total_size( base, hdr, blk );

        // Issue #57 opt 3: compute actual needed for this specific block address.
        // user_ptr() = align_up(raw + sizeof(uint32_t), alignment).
        std::uint8_t* raw        = reinterpret_cast<std::uint8_t*>( blk ) + sizeof( detail::BlockHeader );
        std::size_t   raw_addr   = reinterpret_cast<std::size_t>( raw );
        std::size_t   user_start = detail::align_up( raw_addr + sizeof( std::uint32_t ), alignment );
        std::size_t   needed     = ( user_start - reinterpret_cast<std::size_t>( blk ) ) + user_size;
        needed                   = detail::align_up( std::max( needed, kMinBlockSize ), kMinAlignment );

        std::size_t min_rem   = sizeof( detail::BlockHeader ) + kMinBlockSize;
        bool        can_split = ( blk_total_size >= needed + min_rem );

        if ( can_split )
        {
            std::uint32_t        new_off = blk_off + static_cast<std::uint32_t>( needed );
            detail::BlockHeader* new_blk = detail::block_at( base, new_off );
            std::memset( new_blk, 0, sizeof( detail::BlockHeader ) );
            new_blk->magic         = detail::kBlockMagic;
            new_blk->used_size     = 0;
            new_blk->prev_offset   = blk_off;
            new_blk->next_offset   = blk->next_offset;
            new_blk->left_offset   = detail::kNoBlock;
            new_blk->right_offset  = detail::kNoBlock;
            new_blk->parent_offset = detail::kNoBlock;
            new_blk->avl_height    = 1;
            if ( blk->next_offset != detail::kNoBlock )
                detail::block_at( base, blk->next_offset )->prev_offset = new_off;
            else
                hdr->last_block_offset = new_off; // Issue #57 opt 4
            blk->next_offset = new_off;
            // Issue #59: no total_size field to update; size of blk is now (new_off - blk_off)
            hdr->block_count++;
            hdr->free_count++;
            detail::avl_insert( base, hdr, new_off );
        }

        blk->used_size      = static_cast<std::uint32_t>( user_size );
        blk->alignment_log2 = detail::alignment_to_log2( alignment ); // Issue #59: compact alignment storage
        blk->left_offset    = detail::kNoBlock;
        blk->right_offset   = detail::kNoBlock;
        blk->parent_offset  = detail::kNoBlock;
        blk->avl_height     = 0;
        hdr->alloc_count++;
        hdr->free_count--;
        hdr->used_size += static_cast<std::uint32_t>( user_size );
        // Issue #57 opt 1: write back-pointer tag before user_ptr for O(1) header_from_ptr.
        void* uptr = detail::user_ptr( blk );
        detail::write_back_ptr( base, blk );
        return uptr;
    }

    bool validate_avl( const std::uint8_t* base, const detail::ManagerHeader* hdr, std::uint32_t node_off,
                       std::size_t& count ) const
    {
        if ( node_off == detail::kNoBlock )
            return true;
        if ( static_cast<std::size_t>( node_off ) >= hdr->total_size )
            return false;
        const detail::BlockHeader* node = reinterpret_cast<const detail::BlockHeader*>( base + node_off );
        if ( node->magic != detail::kBlockMagic || node->used_size != 0 )
            return false;
        count++;
        if ( !validate_avl( base, hdr, node->left_offset, count ) )
            return false;
        if ( !validate_avl( base, hdr, node->right_offset, count ) )
            return false;
        if ( node->left_offset != detail::kNoBlock )
        {
            const detail::BlockHeader* lc = reinterpret_cast<const detail::BlockHeader*>( base + node->left_offset );
            if ( lc->parent_offset != node_off )
                return false;
        }
        if ( node->right_offset != detail::kNoBlock )
        {
            const detail::BlockHeader* rc = reinterpret_cast<const detail::BlockHeader*>( base + node->right_offset );
            if ( rc->parent_offset != node_off )
                return false;
        }
        return true;
    }
};

inline PersistMemoryManager* PersistMemoryManager::s_instance = nullptr;
inline std::shared_mutex     PersistMemoryManager::s_mutex;

// ─── Реализация методов pptr<T> ───────────────────────────────────────────────

template <class T> inline T* pptr<T>::get() const noexcept
{
    PersistMemoryManager* mgr = PersistMemoryManager::instance();
    if ( mgr == nullptr || _offset == 0 )
        return nullptr;
    return static_cast<T*>( mgr->offset_to_ptr( _offset ) );
}

template <class T> inline T* pptr<T>::get_at( std::size_t index ) const noexcept
{
    T* base_elem = get();
    return ( base_elem == nullptr ) ? nullptr : base_elem + index;
}

template <class T> inline T* pptr<T>::resolve( PersistMemoryManager* mgr ) const noexcept
{
    if ( mgr == nullptr || _offset == 0 )
        return nullptr;
    return static_cast<T*>( mgr->offset_to_ptr( _offset ) );
}

template <class T> inline const T* pptr<T>::resolve( const PersistMemoryManager* mgr ) const noexcept
{
    if ( mgr == nullptr || _offset == 0 )
        return nullptr;
    return static_cast<const T*>( mgr->offset_to_ptr( _offset ) );
}

template <class T> inline T* pptr<T>::resolve_at( PersistMemoryManager* mgr, std::size_t index ) const noexcept
{
    T* base_elem = resolve( mgr );
    return ( base_elem == nullptr ) ? nullptr : base_elem + index;
}

// ─── Реализация свободных функций ─────────────────────────────────────────────

inline MemoryStats get_stats( const PersistMemoryManager* mgr )
{
    MemoryStats stats{};
    if ( mgr == nullptr )
        return stats;
    const std::uint8_t*          base = mgr->const_base_ptr();
    const detail::ManagerHeader* hdr  = mgr->header();
    stats.total_blocks                = hdr->block_count;
    stats.free_blocks                 = hdr->free_count;
    stats.allocated_blocks            = hdr->alloc_count;
    bool          first_free          = true;
    std::uint32_t offset              = hdr->first_block_offset;
    while ( offset != detail::kNoBlock )
    {
        const detail::BlockHeader* blk = reinterpret_cast<const detail::BlockHeader*>( base + offset );
        if ( blk->used_size == 0 )
        {
            // Issue #59: compute total_size from offsets
            std::size_t blk_size = ( blk->next_offset != detail::kNoBlock )
                                       ? static_cast<std::size_t>( blk->next_offset - offset )
                                       : static_cast<std::size_t>( hdr->total_size - offset );
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
                stats.total_fragmentation += blk_size;
            }
        }
        offset = blk->next_offset;
    }
    return stats;
}

inline AllocationInfo get_info( const PersistMemoryManager* mgr, void* ptr )
{
    AllocationInfo info{};
    info.ptr      = ptr;
    info.is_valid = false;
    if ( mgr == nullptr || ptr == nullptr )
        return info;
    std::uint8_t*          base = const_cast<PersistMemoryManager*>( mgr )->base_ptr();
    detail::ManagerHeader* mhdr = const_cast<PersistMemoryManager*>( mgr )->header();
    detail::BlockHeader*   blk  = detail::find_block_by_ptr( base, mhdr, ptr );
    if ( blk != nullptr && blk->used_size > 0 )
    {
        info.size      = blk->used_size;
        info.alignment = detail::log2_to_alignment( blk->alignment_log2 ); // Issue #59
        info.is_valid  = true;
    }
    return info;
}

inline ManagerInfo get_manager_info( const PersistMemoryManager* mgr )
{
    ManagerInfo info{};
    if ( mgr == nullptr )
        return info;
    const detail::ManagerHeader* hdr = mgr->header();
    info.magic                       = hdr->magic;
    info.total_size                  = hdr->total_size;
    info.used_size                   = hdr->used_size;
    info.block_count                 = hdr->block_count;
    info.free_count                  = hdr->free_count;
    info.alloc_count                 = hdr->alloc_count;
    info.first_block_offset          = ( hdr->first_block_offset != detail::kNoBlock )
                                           ? static_cast<std::ptrdiff_t>( hdr->first_block_offset )
                                           : -1;
    info.first_free_offset           = ( hdr->free_tree_root != detail::kNoBlock )
                                           ? static_cast<std::ptrdiff_t>( hdr->free_tree_root )
                                           : -1;
    info.last_free_offset            = -1;
    info.manager_header_size         = sizeof( detail::ManagerHeader );
    return info;
}

template <typename Callback> inline void for_each_block( const PersistMemoryManager* mgr, Callback&& cb )
{
    if ( mgr == nullptr )
        return;
    std::shared_lock<std::shared_mutex> lock( PersistMemoryManager::s_mutex );
    const std::uint8_t*                 base   = mgr->const_base_ptr();
    const detail::ManagerHeader*        hdr    = mgr->header();
    std::uint32_t                       offset = hdr->first_block_offset;
    std::size_t                         index  = 0;
    while ( offset != detail::kNoBlock )
    {
        if ( static_cast<std::size_t>( offset ) >= hdr->total_size )
            break;
        const detail::BlockHeader* blk = reinterpret_cast<const detail::BlockHeader*>( base + offset );
        // Issue #59: compute total_size from offsets
        std::size_t blk_total = ( blk->next_offset != detail::kNoBlock )
                                    ? static_cast<std::size_t>( blk->next_offset - offset )
                                    : static_cast<std::size_t>( hdr->total_size - offset );
        BlockView view;
        view.index       = index;
        view.offset      = static_cast<std::ptrdiff_t>( offset );
        view.total_size  = blk_total;
        view.header_size = sizeof( detail::BlockHeader );
        view.user_size   = blk->used_size;
        view.alignment   = detail::log2_to_alignment( blk->alignment_log2 ); // Issue #59
        view.used        = ( blk->used_size > 0 );
        cb( view );
        ++index;
        offset = blk->next_offset;
    }
}

} // namespace pmm
