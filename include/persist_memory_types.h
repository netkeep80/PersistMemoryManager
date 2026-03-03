/**
 * @file persist_memory_types.h
 * @brief Базовые типы данных и константы для PersistMemoryManager (Issue #73)
 *
 * Содержит: BlockHeader, ManagerHeader, pptr<T>, MemoryStats, ManagerInfo,
 * BlockView, FreeBlockView и вспомогательные функции конвертации.
 *
 * Размеры структур защищены static_assert (Issue #59, #73 FR-03):
 *   BlockHeader   == 32 bytes
 *   ManagerHeader == 64 bytes
 *
 * @version 1.0 (Issue #73 refactoring)
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>

namespace pmm
{

// ─── Константы ────────────────────────────────────────────────────────────────

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

// ─── Публичные структуры данных ────────────────────────────────────────────────

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

// ─── Внутренние типы (detail) ─────────────────────────────────────────────────

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

static_assert( sizeof( BlockHeader ) == 32, "BlockHeader must be exactly 32 bytes (Issue #59, #73 FR-03)" );
static_assert( sizeof( BlockHeader ) % kGranuleSize == 0,
               "BlockHeader must be granule-aligned (Issue #59, #73 FR-03)" );

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

static_assert( sizeof( ManagerHeader ) == 64, "ManagerHeader must be exactly 64 bytes (Issue #59, #73 FR-03)" );
static_assert( sizeof( ManagerHeader ) % kGranuleSize == 0,
               "ManagerHeader must be granule-aligned (Issue #59, #73 FR-03)" );

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

} // namespace detail

} // namespace pmm
