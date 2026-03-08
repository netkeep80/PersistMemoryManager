/**
 * @file pmm/types.h
 * @brief Core types and constants for PersistMemoryManager (Issue #95 refactoring).
 *
 * Contains: ManagerHeader, MemoryStats, ManagerInfo,
 * BlockView, FreeBlockView and utility functions for byte/granule conversion.
 *
 * Sizes of structures are protected by static_assert (Issue #59, #73 FR-03):
 *   Block<DefaultAddressTraits> == 32 bytes
 *   ManagerHeader               == 64 bytes
 *
 * Issue #95: Moved from persist_memory_types.h to pmm/types.h as part of
 * refactoring to consolidate all PMM code under include/pmm/.
 *
 * Issue #106: Block<A>* utilities replace legacy BlockHeader* ones.
 * Issue #112: BlockHeader struct removed — Block<DefaultAddressTraits> is the sole block type.
 *
 * @version 2.2 (Issue #112 — BlockHeader removed)
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/block.h"
#include "pmm/block_state.h"
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

// Issue #112: BlockHeader struct removed — Block<DefaultAddressTraits> is the sole block type.
// All block metadata is stored in Block<AddressTraitsT> (LinkedListNode + TreeNode).

// Issue #87: Verify Block<DefaultAddressTraits> layout and size constraints.
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32,
               "Block<DefaultAddressTraits> must be 32 bytes (Issue #87, #112)" );
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) % kGranuleSize == 0,
               "Block<DefaultAddressTraits> must be granule-aligned (Issue #59, #73 FR-03)" );

// Issue #87 Phase 2: verify LinkedListNode<DefaultAddressTraits> layout.
static_assert( sizeof( pmm::LinkedListNode<pmm::DefaultAddressTraits> ) == 2 * sizeof( std::uint32_t ),
               "LinkedListNode<DefaultAddressTraits> must be 8 bytes (Issue #87)" );
static_assert( offsetof( pmm::LinkedListNode<pmm::DefaultAddressTraits>, prev_offset ) == 0,
               "LinkedListNode::prev_offset must be at offset 0 (Issue #87)" );
static_assert( offsetof( pmm::LinkedListNode<pmm::DefaultAddressTraits>, next_offset ) == sizeof( std::uint32_t ),
               "LinkedListNode::next_offset must be at offset 4 (Issue #87)" );
// TreeNode<DefaultAddressTraits>: left/right/parent + avl_height/_pad + weight + root_offset (24 bytes).
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

/// @brief Number of granules per block header (2 granules = 32 bytes, Issue #112)
inline constexpr std::uint32_t kBlockHeaderGranules = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) / kGranuleSize;

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

/// @brief Issue #83: Minimum block size = Block header + 1 data granule (Issue #112: uses Block<A>).
inline constexpr std::size_t kMinBlockSize = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + kGranuleSize;

/// @brief Issue #83: Minimum memory size = Block_0 + ManagerHeader + Block_1 + kMinBlockSize (Issue #112).
inline constexpr std::size_t kMinMemorySize = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) +
                                              sizeof( ManagerHeader ) +
                                              sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + kMinBlockSize;

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

/// @brief Get pointer to Block<DefaultAddressTraits> by granule index (Issue #112: sole block type).
inline pmm::Block<pmm::DefaultAddressTraits>* block_at( std::uint8_t* base, std::uint32_t idx )
{
    assert( idx != kNoBlock );
    return reinterpret_cast<pmm::Block<pmm::DefaultAddressTraits>*>( base + idx_to_byte_off( idx ) );
}

/// @brief Get const pointer to Block<DefaultAddressTraits> by granule index (read-only).
inline const pmm::Block<pmm::DefaultAddressTraits>* block_at( const std::uint8_t* base, std::uint32_t idx )
{
    assert( idx != kNoBlock );
    return reinterpret_cast<const pmm::Block<pmm::DefaultAddressTraits>*>( base + idx_to_byte_off( idx ) );
}

/// @brief Get granule index of Block<DefaultAddressTraits>.
inline std::uint32_t block_idx( const std::uint8_t* base, const pmm::Block<pmm::DefaultAddressTraits>* block )
{
    std::size_t byte_off = reinterpret_cast<const std::uint8_t*>( block ) - base;
    assert( byte_off % kGranuleSize == 0 );
    return static_cast<std::uint32_t>( byte_off / kGranuleSize );
}

/// @brief Compute total granules of block (Issue #112: Block<A> layout).
/// Issue #59: total_size is no longer stored — computed via next_offset.
inline std::uint32_t block_total_granules( const std::uint8_t* base, const ManagerHeader* hdr,
                                           const pmm::Block<pmm::DefaultAddressTraits>* blk )
{
    std::uint32_t this_idx = block_idx( base, blk );
    if ( blk->next_offset != kNoBlock )
        return blk->next_offset - this_idx;
    return byte_off_to_idx( static_cast<std::size_t>( hdr->total_size ) ) - this_idx;
}

/// @brief Issue #69/#106/#112: Structural block validity using Block<A> layout.
/// Invariants: weight<total_gran, prev<idx<next, avl_height<32, distinct AVL refs.
inline bool is_valid_block( const std::uint8_t* base, const ManagerHeader* hdr, std::uint32_t idx )
{
    if ( idx == kNoBlock )
        return false;
    if ( idx_to_byte_off( idx ) + sizeof( pmm::Block<pmm::DefaultAddressTraits> ) > hdr->total_size )
        return false;

    const auto*   blk = reinterpret_cast<const pmm::Block<pmm::DefaultAddressTraits>*>( base + idx_to_byte_off( idx ) );
    std::uint32_t total_gran = ( blk->next_offset != kNoBlock )
                                   ? ( blk->next_offset - idx )
                                   : ( byte_off_to_idx( static_cast<std::size_t>( hdr->total_size ) ) - idx );
    if ( blk->weight >= total_gran )
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

/// @brief Compute user data address for block (block + sizeof(Block<A>), Issue #106, #112).
inline void* user_ptr( pmm::Block<pmm::DefaultAddressTraits>* block )
{
    return reinterpret_cast<std::uint8_t*>( block ) + sizeof( pmm::Block<pmm::DefaultAddressTraits> );
}

/// @brief O(1) get Block<A> from user_ptr (ptr - sizeof(Block<A>)); validated via is_valid_block().
/// Issue #112: Block<A> is the sole block type.
inline pmm::Block<pmm::DefaultAddressTraits>* header_from_ptr( std::uint8_t* base, void* ptr, std::size_t total_size )
{
    if ( ptr == nullptr )
        return nullptr;
    std::uint8_t* raw_ptr = reinterpret_cast<std::uint8_t*>( ptr );
    // First user data starts after Block_0 + ManagerHeader + Block_1 (Issue #75, #112)
    static constexpr std::size_t kBlockSize = sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    std::uint8_t*                min_addr   = base + kBlockSize + sizeof( ManagerHeader ) + kBlockSize;
    if ( raw_ptr < min_addr )
        return nullptr;
    if ( raw_ptr > base + total_size )
        return nullptr;
    std::uint8_t* cand_addr = raw_ptr - kBlockSize;
    if ( ( reinterpret_cast<std::size_t>( cand_addr ) - reinterpret_cast<std::size_t>( base ) ) % kGranuleSize != 0 )
        return nullptr;
    std::uint32_t cand_idx = static_cast<std::uint32_t>( ( cand_addr - base ) / kGranuleSize );
    // Issue #83: ManagerHeader is at base + kBlockSize.
    const ManagerHeader* hdr_const = reinterpret_cast<const ManagerHeader*>( base + kBlockSize );
    if ( !is_valid_block( base, hdr_const, cand_idx ) )
        return nullptr;
    auto* cand = reinterpret_cast<pmm::Block<pmm::DefaultAddressTraits>*>( cand_addr );
    if ( cand->weight == 0 )
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
