/**
 * @file pmm/types.h
 * @brief Core types and constants for PersistMemoryManager (Issue #95 refactoring).
 *
 * Contains: BlockHeader, ManagerHeader, MemoryStats, ManagerInfo,
 * BlockView, FreeBlockView and utility functions for byte/granule conversion.
 *
 * Sizes of structures are protected by static_assert (Issue #59, #73 FR-03):
 *   BlockHeader   == 32 bytes
 *   ManagerHeader == 64 bytes
 *
 * Issue #95: Moved from persist_memory_types.h to pmm/types.h as part of
 * refactoring to consolidate all PMM code under include/pmm/.
 *
 * @version 2.0 (Issue #95 refactoring)
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/block.h"
#include "pmm/linked_list_node.h"
#include "pmm/tree_node.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>

namespace pmm
{

// ─── Constants ────────────────────────────────────────────────────────────────

/// @brief Granule size in bytes (Issue #59, #83). All alignment/granularity expressed via this constant.
/// Issue #87 Phase 1: matches DefaultAddressTraits::granule_size.
inline constexpr std::size_t kGranuleSize = 16;
static_assert( ( kGranuleSize & ( kGranuleSize - 1 ) ) == 0, "kGranuleSize must be a power of 2 (Issue #83)" );
static_assert( kGranuleSize == pmm::DefaultAddressTraits::granule_size,
               "kGranuleSize must match DefaultAddressTraits::granule_size (Issue #87)" );

inline constexpr std::uint64_t kMagic = 0x504D4D5F56303833ULL; ///< "PMM_V083" (Issue #83: granule_size in header)

// ─── Public data structures ────────────────────────────────────────────────────

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
    std::ptrdiff_t first_free_offset; ///< Root of AVL tree of free blocks
    std::size_t    manager_header_size;
};

struct BlockView
{
    std::size_t    index;
    std::ptrdiff_t offset;      ///< Byte offset in PAS
    std::size_t    total_size;  ///< Total block size in bytes
    std::size_t    header_size; ///< BlockHeader size in bytes
    std::size_t    user_size;   ///< User data size in bytes
    std::size_t    alignment;   ///< Always kGranuleSize (Issue #59, #83)
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

// ─── Internal types (detail) ─────────────────────────────────────────────────

namespace detail
{

/// @brief Block header (32 bytes = 2 granules, Issue #59). size=0: free; size>0: data granules.
/// root_offset=0: free-blocks tree; root_offset=own_idx: allocated block root (Issue #75).
/// Issue #69: magic removed; validity via is_valid_block(). total_size = next_offset - idx.
struct BlockHeader
{
    std::uint32_t size;          ///< [1] Data size in granules (0 = free block, Issue #75)
    std::uint32_t prev_offset;   ///< [2] Previous block (granule index)
    std::uint32_t next_offset;   ///< [3] Next block (granule index)
    std::uint32_t left_offset;   ///< [4] Left child of AVL tree (granule index)
    std::uint32_t right_offset;  ///< [5] Right child of AVL tree (granule index)
    std::uint32_t parent_offset; ///< [6] Parent node of AVL tree (granule index)
    std::int16_t  avl_height;    ///< AVL subtree height (0 = not in tree)
    std::uint16_t _pad;          ///< Reserved (Issue #69: previously held magic[3:2])
    std::uint32_t root_offset;   ///< 0=free block (free tree); own_idx=allocated (Issue #75)
};

static_assert( sizeof( BlockHeader ) == 32, "BlockHeader must be exactly 32 bytes (Issue #59, #73 FR-03)" );
static_assert( sizeof( BlockHeader ) % kGranuleSize == 0,
               "BlockHeader must be granule-aligned (Issue #59, #73 FR-03)" );

// Issue #87 Phase 2: verify binary compatibility of LinkedListNode/TreeNode with BlockHeader.
// LinkedListNode<DefaultAddressTraits> maps to prev_offset/next_offset (8 bytes).
static_assert( sizeof( pmm::LinkedListNode<pmm::DefaultAddressTraits> ) == 2 * sizeof( std::uint32_t ),
               "LinkedListNode<DefaultAddressTraits> must be 8 bytes (Issue #87)" );
static_assert( offsetof( pmm::LinkedListNode<pmm::DefaultAddressTraits>, prev_offset ) == 0,
               "LinkedListNode::prev_offset must be at offset 0 (Issue #87)" );
static_assert( offsetof( pmm::LinkedListNode<pmm::DefaultAddressTraits>, next_offset ) == sizeof( std::uint32_t ),
               "LinkedListNode::next_offset must be at offset 4 (Issue #87)" );
// TreeNode<DefaultAddressTraits> maps to left/right/parent + avl_height/_pad + weight + root_offset (24 bytes).
// Phase 2 v0.2: weight and root_offset are now part of TreeNode (moved from Block own fields).
static_assert( sizeof( pmm::TreeNode<pmm::DefaultAddressTraits> ) ==
                   3 * sizeof( std::uint32_t ) + 4 + 2 * sizeof( std::uint32_t ),
               "TreeNode<DefaultAddressTraits> must be 24 bytes (Issue #87)" );
static_assert( offsetof( pmm::TreeNode<pmm::DefaultAddressTraits>, left_offset ) == 0,
               "TreeNode::left_offset must be at offset 0 within TreeNode (Issue #87)" );
static_assert( offsetof( pmm::TreeNode<pmm::DefaultAddressTraits>, right_offset ) == sizeof( std::uint32_t ),
               "TreeNode::right_offset must be at offset 4 within TreeNode (Issue #87)" );
static_assert( offsetof( pmm::TreeNode<pmm::DefaultAddressTraits>, parent_offset ) == 2 * sizeof( std::uint32_t ),
               "TreeNode::parent_offset must be at offset 8 within TreeNode (Issue #87)" );
static_assert( offsetof( pmm::TreeNode<pmm::DefaultAddressTraits>, avl_height ) == 3 * sizeof( std::uint32_t ),
               "TreeNode::avl_height must be at offset 12 within TreeNode (Issue #87)" );
static_assert( offsetof( pmm::TreeNode<pmm::DefaultAddressTraits>, weight ) == 3 * sizeof( std::uint32_t ) + 4,
               "TreeNode::weight must be at offset 16 within TreeNode (Issue #87)" );
static_assert( offsetof( pmm::TreeNode<pmm::DefaultAddressTraits>, root_offset ) == 4 * sizeof( std::uint32_t ) + 4,
               "TreeNode::root_offset must be at offset 20 within TreeNode (Issue #87)" );
// Issue #87 Phase 3: verify sizeof(Block<DefaultAddressTraits>) == sizeof(BlockHeader).
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32,
               "Block<DefaultAddressTraits> must be 32 bytes (Issue #87)" );

/// @brief Number of granules in BlockHeader (2 granules = 32 bytes)
inline constexpr std::uint32_t kBlockHeaderGranules = sizeof( BlockHeader ) / kGranuleSize;

// kBlockMagic removed (Issue #69): block validity now uses is_valid_block() structural invariants.
/// Issue #87 Phase 1: matches DefaultAddressTraits::no_block.
inline constexpr std::uint32_t kNoBlock = 0xFFFFFFFFU; ///< Sentinel: no block (granule index)
static_assert( kNoBlock == pmm::DefaultAddressTraits::no_block,
               "kNoBlock must match DefaultAddressTraits::no_block (Issue #87)" );

/// @brief Manager header (Issue #59: 64 bytes). All _offset fields are granule indices.
/// prev_base_ptr / prev_total_size are runtime-only (nulled by load() — not persisted).
struct ManagerHeader
{
    std::uint64_t magic;              ///< Manager magic number
    std::uint64_t total_size;         ///< Total size of managed area in bytes
    std::uint32_t used_size;          ///< Used size in granules (Issue #59)
    std::uint32_t block_count;        ///< Total number of blocks
    std::uint32_t free_count;         ///< Number of free blocks
    std::uint32_t alloc_count;        ///< Number of allocated blocks
    std::uint32_t first_block_offset; ///< First block (granule index)
    std::uint32_t last_block_offset;  ///< [Issue #57 opt 4] Last block (granule index)
    std::uint32_t free_tree_root;     ///< Root of AVL tree of free blocks (granule index)
    bool          owns_memory;        ///< Manager owns buffer (runtime-only)
    bool          prev_owns_memory;   ///< prev_base_ptr was allocated by manager (runtime-only)
    std::uint16_t granule_size;       ///< Issue #83: kGranuleSize at creation time; validated on load
    std::uint64_t prev_total_size;    ///< Previous buffer size in bytes (runtime-only)
    void*         prev_base_ptr;      ///< Pointer to previous buffer (runtime-only; nulled on load)
};

static_assert( sizeof( ManagerHeader ) == 64, "ManagerHeader must be exactly 64 bytes (Issue #59, #73 FR-03)" );
static_assert( sizeof( ManagerHeader ) % kGranuleSize == 0,
               "ManagerHeader must be granule-aligned (Issue #59, #73 FR-03)" );

/// @brief Number of granules in ManagerHeader
inline constexpr std::uint32_t kManagerHeaderGranules = sizeof( ManagerHeader ) / kGranuleSize;

/// @brief Issue #83: Minimum block size = BlockHeader + 1 data granule (computed, not hardcoded).
inline constexpr std::size_t kMinBlockSize = sizeof( BlockHeader ) + kGranuleSize;

/// @brief Issue #83: Minimum memory size = BlockHeader_0 + ManagerHeader + BlockHeader_1 + kMinBlockSize (computed).
inline constexpr std::size_t kMinMemorySize =
    sizeof( BlockHeader ) + sizeof( ManagerHeader ) + sizeof( BlockHeader ) + kMinBlockSize;

// ─── Byte ↔ granule conversion ──────────────────────────────────────────────

/// @brief Convert bytes to granules (ceiling). Returns 0 on overflow.
inline std::uint32_t bytes_to_granules( std::size_t bytes )
{
    if ( bytes > std::numeric_limits<std::size_t>::max() - ( kGranuleSize - 1 ) )
        return 0;
    std::size_t granules = ( bytes + kGranuleSize - 1 ) / kGranuleSize;
    if ( granules > std::numeric_limits<std::uint32_t>::max() )
        return 0;
    return static_cast<std::uint32_t>( granules );
}

/// @brief Convert granules to bytes.
inline std::size_t granules_to_bytes( std::uint32_t granules )
{
    return static_cast<std::size_t>( granules ) * kGranuleSize;
}

/// @brief Get byte offset from granule index.
inline std::size_t idx_to_byte_off( std::uint32_t idx )
{
    return static_cast<std::size_t>( idx ) * kGranuleSize;
}

/// @brief Get granule index from byte offset (must be multiple of kGranuleSize).
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

/// @brief Get pointer to BlockHeader by granule index.
inline BlockHeader* block_at( std::uint8_t* base, std::uint32_t idx )
{
    assert( idx != kNoBlock );
    return reinterpret_cast<BlockHeader*>( base + idx_to_byte_off( idx ) );
}

/// @brief Get const pointer to BlockHeader by granule index (read-only).
inline const BlockHeader* block_at( const std::uint8_t* base, std::uint32_t idx )
{
    assert( idx != kNoBlock );
    return reinterpret_cast<const BlockHeader*>( base + idx_to_byte_off( idx ) );
}

/// @brief Get granule index of BlockHeader.
inline std::uint32_t block_idx( const std::uint8_t* base, const BlockHeader* block )
{
    std::size_t byte_off = reinterpret_cast<const std::uint8_t*>( block ) - base;
    assert( byte_off % kGranuleSize == 0 );
    return static_cast<std::uint32_t>( byte_off / kGranuleSize );
}

/// @brief Compute total_size of block in granules.
/// Issue #59: total_size is no longer stored — computed via next_offset.
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

/// @brief Compute user data address for block (block + sizeof(BlockHeader)).
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
    std::uint32_t cand_idx = static_cast<std::uint32_t>( ( cand_addr - base ) / kGranuleSize );
    // Issue #83: ManagerHeader is at base + sizeof(BlockHeader), not base.
    const ManagerHeader* hdr_const = reinterpret_cast<const ManagerHeader*>( base + sizeof( BlockHeader ) );
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
