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
 * @version 2.2.0
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

static constexpr std::size_t   kGranuleSize      = 16; ///< Issue #59: granule size in bytes
static constexpr std::size_t   kDefaultAlignment = 16;
static constexpr std::size_t   kMinAlignment     = 16;
static constexpr std::size_t   kMaxAlignment     = 16; ///< Issue #59: only 16-byte alignment supported
static constexpr std::size_t   kMinMemorySize    = 4096;
static constexpr std::size_t   kMinBlockSize     = 32; ///< Minimum block size = 2 granules = BlockHeader + user area
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
    std::ptrdiff_t offset;      ///< Byte offset in PAS
    std::size_t    total_size;  ///< Total block size in bytes
    std::size_t    header_size; ///< BlockHeader size in bytes
    std::size_t    user_size;   ///< User data size in bytes
    std::size_t    alignment;   ///< Always kDefaultAlignment (Issue #59)
    bool           used;
};

namespace detail
{

/**
 * @brief Заголовок блока памяти (Issue #59: 32 байта = 2 гранулы по 16 байт).
 *
 * Issue #59: Адресация в гранулах по 16 байт (kGranuleSize).
 *   - Все _offset поля — гранульные индексы (uint32_t), а не байтовые смещения.
 *   - Индекс i → байтовый адрес: base + i * kGranuleSize.
 *   - kNoBlock = 0xFFFFFFFF — отсутствие блока.
 *   - Поддерживает до 64 ГБ на один менеджер (2^32 * 16 = 64 ГБ).
 *
 * Структура (32 байта):
 *   - magic (4B) + used_size (4B) = 8 байт
 *   - prev_offset (4B) + next_offset (4B) = 8 байт
 *   - left_offset (4B) + right_offset (4B) = 8 байт
 *   - parent_offset (4B) + avl_height (2B) + _pad (2B) = 8 байт
 *   Итого: 32 байта = 2 гранулы
 *
 * Удалено: total_size (вычисляется через next_offset), alignment (не нужен).
 * used_size хранится в гранулах (потолок байт / kGranuleSize).
 * Пользовательские данные всегда начинаются через 32 байта после начала блока.
 *
 * Поля: used_size (1), prev_offset (2), next_offset (3),
 *       left_offset (4), right_offset (5), parent_offset (6).
 */
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
static constexpr std::uint32_t kBlockHeaderGranules = sizeof( BlockHeader ) / kGranuleSize;

static constexpr std::uint32_t kBlockMagic = 0x424C4B32U; // "BLK2" (Issue #59 version)
static constexpr std::uint32_t kNoBlock = 0xFFFFFFFFU; ///< Sentinel: нет блока (гранульный индекс)

/**
 * @brief Заголовок менеджера памяти (Issue #59: 64 байта, 16-байтное выравнивание).
 *
 * Оптимизированная структура с 32-битными гранульными индексами:
 *   - magic (8B) + total_size (8B) = 16 байт      [total_size в байтах]
 *   - used_size (4B) + block_count (4B) + free_count (4B) + alloc_count (4B) = 16 байт
 *     [used_size в гранулах]
 *   - first_block_offset (4B) + last_block_offset (4B) + free_tree_root (4B) + owns_memory(1B)+pad(3B) = 16 байт
 *     [смещения — гранульные индексы]
 *   - prev_total_size (8B) + prev_base (8B) = 16 байт
 *   Итого: 64 байта
 */
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
    bool         owns_memory; ///< Менеджер владеет буфером
    std::uint8_t _pad[3];     ///< Выравнивание
    std::uint64_t prev_total_size; ///< Размер предыдущего буфера в байтах (при расширении)
    void* prev_base;               ///< Указатель на предыдущий буфер
};

static_assert( sizeof( ManagerHeader ) == 64, "ManagerHeader must be exactly 64 bytes (Issue #59)" );
static_assert( sizeof( ManagerHeader ) % kGranuleSize == 0, "ManagerHeader must be granule-aligned (Issue #59)" );

/// @brief Число гранул в ManagerHeader
static constexpr std::uint32_t kManagerHeaderGranules = sizeof( ManagerHeader ) / kGranuleSize;

// ─── Конвертация байты ↔ гранулы ──────────────────────────────────────────────

/// @brief Перевести байты в гранулы (потолок: ceiling(bytes / kGranuleSize)).
inline std::uint32_t bytes_to_granules( std::size_t bytes )
{
    return static_cast<std::uint32_t>( ( bytes + kGranuleSize - 1 ) / kGranuleSize );
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
/// Байтовое смещение должно быть кратно kGranuleSize.
inline std::uint32_t byte_off_to_idx( std::size_t byte_off )
{
    assert( byte_off % kGranuleSize == 0 );
    return static_cast<std::uint32_t>( byte_off / kGranuleSize );
}

inline bool is_valid_alignment( std::size_t align )
{
    // Issue #59: only 16-byte alignment is supported (everything is granule-aligned)
    return align != 0 && ( align & ( align - 1 ) ) == 0 && align >= 1;
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
inline BlockHeader* header_from_ptr( std::uint8_t* base, void* ptr )
{
    if ( ptr == nullptr )
        return nullptr;
    std::uint8_t* raw_ptr  = reinterpret_cast<std::uint8_t*>( ptr );
    std::uint8_t* min_addr = base + sizeof( ManagerHeader ) + sizeof( BlockHeader );
    if ( raw_ptr < min_addr )
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

inline BlockHeader* find_block_by_ptr( std::uint8_t* base, const ManagerHeader* mhdr, void* ptr )
{
    std::uint32_t idx = mhdr->first_block_offset;
    while ( idx != kNoBlock )
    {
        BlockHeader* blk = block_at( base, idx );
        if ( blk->used_size > 0 && user_ptr( blk ) == ptr )
            return blk;
        idx = blk->next_offset;
    }
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
        bool          smaller = ( blk_gran < n_gran ) || ( blk_gran == n_gran && blk_idx <= cur );
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

/**
 * @brief Персистный типизированный указатель (Issue #59: 4 байта, гранульный индекс).
 *
 * Issue #59: хранит гранульный индекс (uint32_t).
 *   - Индекс i → байтовый адрес: base + i * kGranuleSize.
 *   - 0 означает nullptr (нулевой индекс зарезервирован для ManagerHeader).
 *   - Размер: 4 байта (уменьшен с 8 до 4 байт).
 *   - Максимальный размер PAS: 2^32 * 16 = 64 ГБ.
 *
 * Операции pptr<T>++ и pptr<T>-- запрещены.
 * pptr<T>[i] адресует i-й элемент типа T с проверкой размера блока.
 */
template <class T> class pptr
{
    std::uint32_t _idx; ///< Гранульный индекс пользовательских данных

  public:
    inline pptr() noexcept : _idx( 0 ) {}
    inline explicit pptr( std::uint32_t idx ) noexcept : _idx( idx ) {}
    inline pptr( const pptr<T>& ) noexcept               = default;
    inline pptr<T>& operator=( const pptr<T>& ) noexcept = default;
    inline ~pptr() noexcept                              = default;

    // Issue #59: ++ and -- are forbidden
    pptr<T>& operator++()      = delete;
    pptr<T>  operator++( int ) = delete;
    pptr<T>& operator--()      = delete;
    pptr<T>  operator--( int ) = delete;

    inline bool          is_null() const noexcept { return _idx == 0; }
    inline explicit      operator bool() const noexcept { return _idx != 0; }
    inline std::uint32_t offset() const noexcept { return _idx; }

    inline T* get() const noexcept;
    inline T& operator*() const noexcept { return *get(); }
    inline T* operator->() const noexcept { return get(); }

    /// @brief Доступ к i-му элементу типа T с проверкой размера блока (Issue #59).
    /// Проверяет, что i * sizeof(T) не выходит за размер блока.
    /// @return Указатель на элемент или nullptr при выходе за границы.
    inline T* operator[]( std::size_t i ) const noexcept;

    /// @brief Доступ к i-му элементу без проверки границ (внутреннее использование).
    inline T* get_at( std::size_t index ) const noexcept;

    inline T*       resolve( PersistMemoryManager* mgr ) const noexcept;
    inline const T* resolve( const PersistMemoryManager* mgr ) const noexcept;
    inline T*       resolve_at( PersistMemoryManager* mgr, std::size_t index ) const noexcept;

    inline bool operator==( const pptr<T>& other ) const noexcept { return _idx == other._idx; }
    inline bool operator!=( const pptr<T>& other ) const noexcept { return _idx != other._idx; }
};

// Issue #59: pptr<T> теперь 4 байта (uint32_t гранульный индекс)
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
        // Issue #59: max 64 GB (2^32 * kGranuleSize)
        if ( size > static_cast<std::uint64_t>( 0xFFFFFFFFULL ) * kGranuleSize )
            return nullptr;
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
        hdr->owns_memory        = true;

        // First block starts right after ManagerHeader (granule-aligned)
        std::uint32_t blk_idx = detail::kManagerHeaderGranules;

        if ( detail::idx_to_byte_off( blk_idx ) + sizeof( detail::BlockHeader ) + kMinBlockSize > size )
            return nullptr;

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

    /// @brief Выделить память.
    /// Issue #59: параметр alignment принимается для обратной совместимости,
    /// но игнорируется — всё выравнивается по kGranuleSize = 16 байт.
    void* allocate( std::size_t user_size, std::size_t alignment = kDefaultAlignment )
    {
        std::unique_lock<std::shared_mutex> lock( s_mutex );
        if ( user_size == 0 )
            return nullptr;
        if ( !detail::is_valid_alignment( alignment ) )
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

        std::uint32_t freed = blk->used_size; // in granules
        blk->used_size      = 0;
        hdr->alloc_count--;
        hdr->free_count++;
        if ( hdr->used_size >= freed )
            hdr->used_size -= freed;
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
        // Issue #59: used_size in granules; compare with granules needed for new_size
        std::uint32_t new_granules = detail::bytes_to_granules( new_size );
        if ( new_granules <= blk->used_size )
            return ptr;
        std::size_t old_bytes = detail::granules_to_bytes( blk->used_size );
        lock.unlock();
        void* new_ptr = allocate( new_size );
        if ( new_ptr == nullptr )
            return nullptr;
        std::memcpy( new_ptr, ptr, old_bytes );
        deallocate( ptr );
        return new_ptr;
    }

    template <class T> pptr<T> allocate_typed()
    {
        void* raw = allocate( sizeof( T ) );
        if ( raw == nullptr )
            return pptr<T>();
        // Issue #59: pptr stores granule index of user data
        std::size_t byte_off = static_cast<std::uint8_t*>( raw ) - base_ptr();
        assert( byte_off % kGranuleSize == 0 );
        return pptr<T>( static_cast<std::uint32_t>( byte_off / kGranuleSize ) );
    }

    template <class T> pptr<T> allocate_typed( std::size_t count )
    {
        if ( count == 0 )
            return pptr<T>();
        void* raw = allocate( sizeof( T ) * count );
        if ( raw == nullptr )
            return pptr<T>();
        std::size_t byte_off = static_cast<std::uint8_t*>( raw ) - base_ptr();
        assert( byte_off % kGranuleSize == 0 );
        return pptr<T>( static_cast<std::uint32_t>( byte_off / kGranuleSize ) );
    }

    template <class T> void deallocate_typed( pptr<T> p )
    {
        if ( !p.is_null() )
            deallocate( base_ptr() + detail::idx_to_byte_off( p.offset() ) );
    }

    /// @brief Конвертировать гранульный индекс в указатель.
    void* offset_to_ptr( std::uint32_t idx ) noexcept
    {
        return ( idx == 0 ) ? nullptr : base_ptr() + detail::idx_to_byte_off( idx );
    }

    const void* offset_to_ptr( std::uint32_t idx ) const noexcept
    {
        return ( idx == 0 ) ? nullptr : const_base_ptr() + detail::idx_to_byte_off( idx );
    }

    /// @brief Получить размер блока в байтах по гранульному индексу данных.
    /// Используется для bounds checking в pptr<T>::operator[].
    std::size_t block_data_size_bytes( std::uint32_t data_idx ) const noexcept
    {
        if ( data_idx == 0 )
            return 0;
        std::uint8_t* base    = const_cast<PersistMemoryManager*>( this )->base_ptr();
        std::uint32_t blk_idx = data_idx - detail::kBlockHeaderGranules;
        if ( blk_idx * kGranuleSize < sizeof( detail::ManagerHeader ) )
            return 0;
        const detail::BlockHeader* blk = detail::block_at( base, blk_idx );
        if ( blk->magic != detail::kBlockMagic || blk->used_size == 0 )
            return 0;
        return detail::granules_to_bytes( blk->used_size );
    }

    std::size_t        total_size() const { return header()->total_size; }
    static std::size_t manager_header_size() noexcept { return sizeof( detail::ManagerHeader ); }
    /// @brief Использованный размер в байтах (Issue #59: внутри хранится в гранулах).
    std::size_t used_size() const { return detail::granules_to_bytes( header()->used_size ); }
    std::size_t free_size() const
    {
        const detail::ManagerHeader* hdr        = header();
        std::size_t                  used_bytes = detail::granules_to_bytes( hdr->used_size );
        return ( hdr->total_size > used_bytes ) ? ( hdr->total_size - used_bytes ) : 0;
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
                  << "  used_size   : " << used_size() << " bytes\n"
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

    bool expand( std::size_t user_size )
    {
        detail::ManagerHeader* hdr      = header();
        std::size_t            old_size = hdr->total_size;
        std::uint32_t          needed   = detail::required_block_granules( user_size );
        // Round UP to granule boundary: ceil(old * 5/4 / kGranuleSize) * kGranuleSize
        std::size_t new_size =
            ( ( old_size * kGrowNumerator / kGrowDenominator + kGranuleSize - 1 ) / kGranuleSize ) * kGranuleSize;
        if ( new_size < old_size + detail::granules_to_bytes( needed ) )
        {
            new_size =
                ( ( old_size + detail::granules_to_bytes( needed + detail::kBlockHeaderGranules ) + kGranuleSize - 1 ) /
                  kGranuleSize ) *
                kGranuleSize;
        }

        // Issue #59: max 64 GB
        if ( new_size > static_cast<std::uint64_t>( 0xFFFFFFFFULL ) * kGranuleSize )
            return false;

        void* new_memory = std::malloc( new_size );
        if ( new_memory == nullptr )
            return false;

        bool old_owns = hdr->owns_memory;
        std::memcpy( new_memory, base_ptr(), old_size );
        detail::ManagerHeader* nh = reinterpret_cast<detail::ManagerHeader*>( new_memory );
        std::uint8_t*          nb = static_cast<std::uint8_t*>( new_memory );
        nh->owns_memory           = true;

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
        nh->prev_base       = base_ptr();
        nh->prev_total_size = old_size;
        s_instance          = reinterpret_cast<PersistMemoryManager*>( new_memory );
        (void)old_owns;
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

template <class T> inline T* pptr<T>::get() const noexcept
{
    PersistMemoryManager* mgr = PersistMemoryManager::instance();
    if ( mgr == nullptr || _idx == 0 )
        return nullptr;
    return static_cast<T*>( mgr->offset_to_ptr( _idx ) );
}

template <class T> inline T* pptr<T>::get_at( std::size_t index ) const noexcept
{
    T* base_elem = get();
    return ( base_elem == nullptr ) ? nullptr : base_elem + index;
}

template <class T> inline T* pptr<T>::operator[]( std::size_t i ) const noexcept
{
    PersistMemoryManager* mgr = PersistMemoryManager::instance();
    if ( mgr == nullptr || _idx == 0 )
        return nullptr;
    std::size_t block_bytes = mgr->block_data_size_bytes( _idx );
    // Bounds check: element must be within the allocated block
    if ( ( i + 1 ) * sizeof( T ) > block_bytes )
        return nullptr;
    T* base_elem = static_cast<T*>( mgr->offset_to_ptr( _idx ) );
    return base_elem + i;
}

template <class T> inline T* pptr<T>::resolve( PersistMemoryManager* mgr ) const noexcept
{
    if ( mgr == nullptr || _idx == 0 )
        return nullptr;
    return static_cast<T*>( mgr->offset_to_ptr( _idx ) );
}

template <class T> inline const T* pptr<T>::resolve( const PersistMemoryManager* mgr ) const noexcept
{
    if ( mgr == nullptr || _idx == 0 )
        return nullptr;
    return static_cast<const T*>( mgr->offset_to_ptr( _idx ) );
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
                stats.total_fragmentation += blk_size;
            }
        }
        idx = blk->next_offset;
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
        // Issue #59: used_size in granules → convert to bytes
        info.size      = detail::granules_to_bytes( blk->used_size );
        info.alignment = kDefaultAlignment; // Issue #59: always 16-byte aligned
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

template <typename Callback> inline void for_each_block( const PersistMemoryManager* mgr, Callback&& cb )
{
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

} // namespace pmm
