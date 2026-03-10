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

inline constexpr std::uint64_t kMagic =
    0x504D4D5F56303938ULL; ///< "PMM_V098" (Issue #138: block layout changed — prev/next now after TreeNode fields)

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
// All block metadata is stored in Block<AddressTraitsT> (prev/next + TreeNode, Issue #138).

// Issue #87: Verify Block<DefaultAddressTraits> layout and size constraints.
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32,
               "Block<DefaultAddressTraits> must be 32 bytes (Issue #87, #112)" );
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) % kGranuleSize == 0,
               "Block<DefaultAddressTraits> must be granule-aligned (Issue #59, #73 FR-03)" );

// Issue #87 Phase 2, #138: verify Block linked list fields occupy 2 index_type fields.
// LinkedListNode was merged into Block (Issue #138). Layout verified via block.h.
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) ==
                   sizeof( pmm::TreeNode<pmm::DefaultAddressTraits> ) + 2 * sizeof( std::uint32_t ),
               "Block<DefaultAddressTraits> must have TreeNode + 2 index_type list fields (Issue #87, #138)" );
// TreeNode<DefaultAddressTraits>: weight + left/right/parent + root_offset + avl_height/node_type (24 bytes).
// Issue #126: weight moved to first field, avl_height/node_type (renamed from _pad) moved to end.
static_assert( sizeof( pmm::TreeNode<pmm::DefaultAddressTraits> ) == 5 * sizeof( std::uint32_t ) + 4,
               "TreeNode<DefaultAddressTraits> must be 24 bytes (Issue #87, #126)" );

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
//
// Note (Issue #141): AddressTraits<IndexT, GranuleSz> in address_traits.h also provides
// bytes_to_granules / granules_to_bytes / idx_to_byte_off / byte_off_to_idx methods.
//
// Note (Issue #160): The non-templated detail:: functions below are kept for backward
// compatibility. They now delegate to the templated _t variants using DefaultAddressTraits,
// eliminating code duplication. New code should use the _t variants directly or use
// AddressTraits<>::bytes_to_granules() etc.

// ─── Address-traits-aware byte/granule conversion helpers (Issue #146) ────────
// These variants use AddressTraitsT::granule_size instead of the fixed kGranuleSize.
// Required for non-default address traits (SmallAddressTraits with 16B, LargeAddressTraits with 64B).

/// @brief Convert bytes to granules (ceiling) using AddressTraitsT::granule_size.
template <typename AddressTraitsT> inline std::uint32_t bytes_to_granules_t( std::size_t bytes )
{
    static constexpr std::size_t kGranSz = AddressTraitsT::granule_size;
    if ( bytes > std::numeric_limits<std::size_t>::max() - ( kGranSz - 1 ) )
        return 0;
    std::size_t granules = ( bytes + kGranSz - 1 ) / kGranSz;
    if ( granules > std::numeric_limits<std::uint32_t>::max() )
        return 0;
    return static_cast<std::uint32_t>( granules );
}

/// @brief Convert bytes to index_type granules (ceiling) using AddressTraitsT::granule_size.
template <typename AddressTraitsT> inline typename AddressTraitsT::index_type bytes_to_idx_t( std::size_t bytes )
{
    static constexpr std::size_t kGranSz = AddressTraitsT::granule_size;
    using IndexT                         = typename AddressTraitsT::index_type;
    if ( bytes == 0 )
        return static_cast<IndexT>( 0 );
    if ( bytes > std::numeric_limits<std::size_t>::max() - ( kGranSz - 1 ) )
        return AddressTraitsT::no_block; // overflow
    std::size_t granules = ( bytes + kGranSz - 1 ) / kGranSz;
    if ( granules > static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) )
        return AddressTraitsT::no_block; // overflow for IndexT
    return static_cast<IndexT>( granules );
}

/// @brief Get byte offset from granule index using AddressTraitsT::granule_size.
template <typename AddressTraitsT> inline std::size_t idx_to_byte_off_t( std::uint32_t idx )
{
    return static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size;
}

/// @brief Get granule index from byte offset using AddressTraitsT::granule_size.
template <typename AddressTraitsT> inline std::uint32_t byte_off_to_idx_t( std::size_t byte_off )
{
    assert( byte_off % AddressTraitsT::granule_size == 0 );
    assert( byte_off / AddressTraitsT::granule_size <= std::numeric_limits<std::uint32_t>::max() );
    return static_cast<std::uint32_t>( byte_off / AddressTraitsT::granule_size );
}

/// @brief Convert bytes to granules (ceiling). Returns 0 on overflow.
/// @deprecated Use bytes_to_granules_t<DefaultAddressTraits>() or DefaultAddressTraits::bytes_to_granules().
inline std::uint32_t bytes_to_granules( std::size_t bytes )
{
    return bytes_to_granules_t<pmm::DefaultAddressTraits>( bytes );
}

/// @brief Convert granules to bytes.
/// @deprecated Use DefaultAddressTraits::granules_to_bytes() for new code.
inline std::size_t granules_to_bytes( std::uint32_t granules )
{
    return pmm::DefaultAddressTraits::granules_to_bytes( granules );
}

/// @brief Get byte offset from granule index.
/// @deprecated Use DefaultAddressTraits::idx_to_byte_off() for new code.
inline std::size_t idx_to_byte_off( std::uint32_t idx )
{
    return pmm::DefaultAddressTraits::idx_to_byte_off( idx );
}

/// @brief Get granule index from byte offset (must be multiple of kGranuleSize).
/// @deprecated Use byte_off_to_idx_t<DefaultAddressTraits>() for new code.
inline std::uint32_t byte_off_to_idx( std::size_t byte_off )
{
    return byte_off_to_idx_t<pmm::DefaultAddressTraits>( byte_off );
}

/// @brief Returns true only for kGranuleSize (16-byte) alignment.
inline bool is_valid_alignment( std::size_t align )
{
    return align == kGranuleSize;
}

/// @brief Get pointer to Block<AddressTraitsT> by granule index.
/// Single canonical implementation replacing per-file blk_at() helpers in
/// allocator_policy.h and free_block_tree.h (Issue #141).
/// Uses AddressTraitsT::granule_size for byte-offset computation (Issue #146).
template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline pmm::Block<AddressTraitsT>* block_at( std::uint8_t* base, std::uint32_t idx )
{
    assert( idx != kNoBlock );
    return reinterpret_cast<pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                     AddressTraitsT::granule_size );
}

/// @brief Get const pointer to Block<AddressTraitsT> by granule index (read-only).
template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline const pmm::Block<AddressTraitsT>* block_at( const std::uint8_t* base, std::uint32_t idx )
{
    assert( idx != kNoBlock );
    return reinterpret_cast<const pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                           AddressTraitsT::granule_size );
}

/// @brief Get granule index of Block<DefaultAddressTraits>.
inline std::uint32_t block_idx( const std::uint8_t* base, const pmm::Block<pmm::DefaultAddressTraits>* block )
{
    std::size_t byte_off = reinterpret_cast<const std::uint8_t*>( block ) - base;
    assert( byte_off % kGranuleSize == 0 );
    return static_cast<std::uint32_t>( byte_off / kGranuleSize );
}

/// @brief Get granule index of Block<AddressTraitsT> — templated variant for non-default address traits.
template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type block_idx_t( const std::uint8_t*               base,
                                                        const pmm::Block<AddressTraitsT>* block )
{
    std::size_t byte_off = reinterpret_cast<const std::uint8_t*>( block ) - base;
    assert( byte_off % AddressTraitsT::granule_size == 0 );
    return static_cast<typename AddressTraitsT::index_type>( byte_off / AddressTraitsT::granule_size );
}

/// @brief Block header size in granules for AddressTraitsT (Issue #146).
/// Computes ceil(sizeof(Block<AT>) / AT::granule_size).
/// For DefaultAddressTraits: 32/16 = 2. For SmallAddressTraits: ceil(18/16) = 2. For Large: 64/64 = 1.
template <typename AddressTraitsT>
inline constexpr std::uint32_t kBlockHeaderGranules_t = static_cast<std::uint32_t>(
    ( sizeof( pmm::Block<AddressTraitsT> ) + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size );

/// @brief Manager header size in granules for AddressTraitsT (Issue #146).
/// For 16B granule: 64/16 = 4. For 64B granule: 64/64 = 1.
template <typename AddressTraitsT>
inline constexpr std::uint32_t kManagerHeaderGranules_t =
    static_cast<std::uint32_t>( sizeof( ManagerHeader ) / AddressTraitsT::granule_size );

/// @brief Translate an index_type sentinel (AddressTraitsT::no_block) to uint32_t kNoBlock.
/// Used when storing index_type values in uint32_t ManagerHeader fields.
template <typename AddressTraitsT> inline std::uint32_t to_u32_idx( typename AddressTraitsT::index_type v )
{
    return ( v == AddressTraitsT::no_block ) ? kNoBlock : static_cast<std::uint32_t>( v );
}

/// @brief Translate uint32_t ManagerHeader index to index_type, mapping kNoBlock → no_block.
template <typename AddressTraitsT> inline typename AddressTraitsT::index_type from_u32_idx( std::uint32_t v )
{
    return ( v == kNoBlock ) ? AddressTraitsT::no_block : static_cast<typename AddressTraitsT::index_type>( v );
}

/// @brief Compute total granules of block for AddressTraitsT (Issue #112: Block<A> layout).
/// Issue #59: total_size is no longer stored — computed via next_offset.
/// Issue #160: Single templated implementation; non-templated overload delegates here.
template <typename AddressTraitsT>
inline std::uint32_t block_total_granules( const std::uint8_t* base, const ManagerHeader* hdr,
                                           const pmm::Block<AddressTraitsT>* blk )
{
    using BlockState                     = pmm::BlockStateBase<AddressTraitsT>;
    static constexpr std::size_t kGranSz = AddressTraitsT::granule_size;
    using IndexT                         = typename AddressTraitsT::index_type;
    static constexpr IndexT kNoBlk       = AddressTraitsT::no_block;

    std::size_t byte_off   = reinterpret_cast<const std::uint8_t*>( blk ) - base;
    IndexT      this_idx   = static_cast<IndexT>( byte_off / kGranSz );
    IndexT      next_off   = BlockState::get_next_offset( blk );
    IndexT      total_gran = static_cast<IndexT>( hdr->total_size / kGranSz );
    if ( next_off != kNoBlk )
        return static_cast<std::uint32_t>( next_off - this_idx );
    return static_cast<std::uint32_t>( total_gran - this_idx );
}

/// @brief Issue #69/#106/#112: Structural block validity using Block<A> layout.
/// Invariants: weight<total_gran, prev<idx<next, avl_height<32, distinct AVL refs.
inline bool is_valid_block( const std::uint8_t* base, const ManagerHeader* hdr, std::uint32_t idx )
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;
    if ( idx == kNoBlock )
        return false;
    if ( idx_to_byte_off( idx ) + sizeof( pmm::Block<pmm::DefaultAddressTraits> ) > hdr->total_size )
        return false;

    const void*   blk        = base + idx_to_byte_off( idx );
    auto          next_off   = BlockState::get_next_offset( blk );
    std::uint32_t total_gran = ( next_off != kNoBlock )
                                   ? ( next_off - idx )
                                   : ( byte_off_to_idx( static_cast<std::size_t>( hdr->total_size ) ) - idx );
    if ( BlockState::get_weight( blk ) >= total_gran )
        return false;
    auto prev_off = BlockState::get_prev_offset( blk );
    if ( prev_off != kNoBlock && prev_off >= idx )
        return false;
    if ( next_off != kNoBlock && next_off <= idx )
        return false;
    if ( BlockState::get_avl_height( blk ) >= 32 )
        return false;
    auto       left_off   = BlockState::get_left_offset( blk );
    auto       right_off  = BlockState::get_right_offset( blk );
    auto       parent_off = BlockState::get_parent_offset( blk );
    const bool l          = ( left_off != kNoBlock );
    const bool r          = ( right_off != kNoBlock );
    const bool p          = ( parent_off != kNoBlock );
    if ( ( l || r || p ) && ( ( l && r && left_off == right_off ) || ( l && p && left_off == parent_off ) ||
                              ( r && p && right_off == parent_off ) ) )
        return false;
    return true;
}

/// @brief Compute user data address for block (block + sizeof(Block<A>), Issue #106, #112, #141).
/// Single canonical implementation — use this instead of duplicating the cast in each call site.
template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline void* user_ptr( pmm::Block<AddressTraitsT>* block )
{
    return reinterpret_cast<std::uint8_t*>( block ) + sizeof( pmm::Block<AddressTraitsT> );
}

/// @brief O(1) get Block<A> from user_ptr (ptr - sizeof(Block<A>)); validated via is_valid_block().
/// Issue #112: Block<A> is the sole block type.
/// @note For non-default address traits, use the templated header_from_ptr_t<AddressTraitsT>().
///       Issue #160: header_from_ptr performs additional is_valid_block() structural validation
///       which is not yet available generically; for that reason these overloads are kept separate.
inline pmm::Block<pmm::DefaultAddressTraits>* header_from_ptr( std::uint8_t* base, void* ptr, std::size_t total_size )
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;
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
    if ( BlockState::get_weight( cand_addr ) == 0 )
        return nullptr;
    return reinterpret_cast<pmm::Block<pmm::DefaultAddressTraits>*>( cand_addr );
}

/// @brief Templated variant of header_from_ptr for non-default address traits.
/// O(1) get Block<AddressTraitsT> from user_ptr (ptr - sizeof(Block<AddressTraitsT>)).
template <typename AddressTraitsT>
inline pmm::Block<AddressTraitsT>* header_from_ptr_t( std::uint8_t* base, void* ptr, std::size_t total_size )
{
    using BlockState                        = pmm::BlockStateBase<AddressTraitsT>;
    static constexpr std::size_t kGranSz    = AddressTraitsT::granule_size;
    static constexpr std::size_t kBlockSize = sizeof( pmm::Block<AddressTraitsT> );
    using IndexT                            = typename AddressTraitsT::index_type;
    static constexpr IndexT kNoBlk          = AddressTraitsT::no_block;

    if ( ptr == nullptr )
        return nullptr;
    std::uint8_t* raw_ptr = reinterpret_cast<std::uint8_t*>( ptr );
    // First user data starts after Block_0 + ManagerHeader + Block_1
    std::uint8_t* min_addr = base + kBlockSize + sizeof( ManagerHeader ) + kBlockSize;
    if ( raw_ptr < min_addr )
        return nullptr;
    if ( raw_ptr > base + total_size )
        return nullptr;
    std::uint8_t* cand_addr = raw_ptr - kBlockSize;
    if ( ( reinterpret_cast<std::size_t>( cand_addr ) - reinterpret_cast<std::size_t>( base ) ) % kGranSz != 0 )
        return nullptr;
    // Validate via BlockState (uses AddressTraitsT::no_block for sentinel checks)
    if ( BlockState::get_weight( cand_addr ) == 0 )
        return nullptr;
    // Basic sanity: candidate address is within bounds
    if ( cand_addr < base || cand_addr + kBlockSize > base + total_size )
        return nullptr;
    return reinterpret_cast<pmm::Block<AddressTraitsT>*>( cand_addr );
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
