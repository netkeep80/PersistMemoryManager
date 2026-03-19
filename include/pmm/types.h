/**
 * @file pmm/types.h
 * @brief Core types and constants for PersistMemoryManager (Issue #95 refactoring).
 *
 * Contains: ManagerHeader, MemoryStats, ManagerInfo,
 * BlockView, FreeBlockView and utility functions for byte/granule conversion.
 *
 * Sizes of structures are protected by static_assert (Issue #59, #73 FR-03):
 *   Block<DefaultAddressTraits> == 32 bytes
 *   ManagerHeader<DefaultAddressTraits> == 64 bytes
 *
 * Issue #95: Moved from persist_memory_types.h to pmm/types.h as part of
 * refactoring to consolidate all PMM code under include/pmm/.
 *
 * Issue #106: Block<A>* utilities replace legacy BlockHeader* ones.
 * Issue #112: BlockHeader struct removed — Block<DefaultAddressTraits> is the sole block type.
 * Issue #175: ManagerHeader<AT> templated on AddressTraitsT — index fields use index_type
 *   so LargeDBConfig (uint64_t) uses 64-bit indices throughout (no 32-bit truncation).
 *
 * @version 2.4 (Issue #175 — ManagerHeader<AT> templated on AddressTraitsT for 64-bit support)
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

// ─── Error codes (Issue #201, Phase 4.1) ──────────────────────────────────────

/**
 * @brief Error codes for PersistMemoryManager operations (Issue #201, Phase 4.1).
 *
 * Provides detailed error information instead of bare bool/nullptr.
 * Use `PersistMemoryManager::last_error()` to query the most recent error.
 */
enum class PmmError : std::uint8_t
{
    Ok              = 0,  ///< Operation succeeded
    NotInitialized  = 1,  ///< Manager is not initialized
    InvalidSize     = 2,  ///< Invalid size argument (zero, too small, etc.)
    Overflow        = 3,  ///< Arithmetic overflow in size/granule computation
    OutOfMemory     = 4,  ///< Allocation failed — not enough free space
    ExpandFailed    = 5,  ///< Backend expand() failed
    InvalidMagic    = 6,  ///< Magic number mismatch on load()
    CrcMismatch     = 7,  ///< CRC32 mismatch on load (corrupted image)
    SizeMismatch    = 8,  ///< Stored total_size does not match backend
    GranuleMismatch = 9,  ///< Stored granule_size does not match address_traits
    BackendError    = 10, ///< Backend returned null or invalid state
    InvalidPointer  = 11, ///< Pointer is null or out of bounds
    BlockLocked     = 12, ///< Block is permanently locked (cannot deallocate)
};

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

// ─── CRC32 utility (Issue #43 Phase 2.1) ────────────────────────────────────
//
// Software CRC32 (ISO 3309 / ITU-T V.42 polynomial 0xEDB88320).
// Header-only, no external dependencies.  Used by io.h save/load functions.

/// @brief Compute CRC32 over a byte range.
/// Uses the standard Ethernet/zlib polynomial (0xEDB88320, reflected).
inline std::uint32_t compute_crc32( const std::uint8_t* data, std::size_t length ) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for ( std::size_t i = 0; i < length; ++i )
    {
        crc ^= data[i];
        for ( int bit = 0; bit < 8; ++bit )
            crc = ( crc >> 1 ) ^ ( 0xEDB88320U & ( ~( crc & 1U ) + 1U ) );
    }
    return crc ^ 0xFFFFFFFFU;
}

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

// kBlockMagic removed (Issue #69): block validity now uses is_valid_block() structural invariants.
/// Issue #87 Phase 1: matches DefaultAddressTraits::no_block.
inline constexpr std::uint32_t kNoBlock = 0xFFFFFFFFU; ///< Sentinel: no block (granule index)
static_assert( kNoBlock == pmm::DefaultAddressTraits::no_block,
               "kNoBlock must match DefaultAddressTraits::no_block (Issue #87)" );

/// @brief Issue #166: Template alias for AddressTraitsT::no_block sentinel.
/// Use this instead of detail::kNoBlock in generic (templated) code that works with any AT.
/// For DefaultAddressTraits, kNoBlock_v<DefaultAddressTraits> == kNoBlock == 0xFFFFFFFFU.
template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kNoBlock_v = AddressTraitsT::no_block;

/// @brief Manager header parameterized on AddressTraitsT (Issue #175: 64-bit index support).
/// All _offset and counter fields use index_type, so LargeDBConfig uses uint64_t indices
/// and DefaultAddressTraits keeps uint32_t with exactly 64 bytes as before.
/// prev_total_size is runtime-only (zeroed by load() — not persisted).
/// Issue #176: removed prev_owns_memory and prev_base_ptr (obsolete runtime-only fields).
///
/// Layout for DefaultAddressTraits (uint32_t):
///   magic (8) + total_size (8) + 7×uint32_t + owns_memory(1) + _pad(1) +
///   granule_size(2) + prev_total_size(8) + _reserved[8] = 64 bytes
/// For LargeAddressTraits (uint64_t): sizeof = 16 + 56 + 4*(+4 padding) + 16 = 96 bytes
///   → occupies ceil(96/64) = 2 granules = 128 bytes via kManagerHeaderGranules_t<AT>
template <typename AddressTraitsT = DefaultAddressTraits> struct ManagerHeader
{
    using index_type = typename AddressTraitsT::index_type;

    std::uint64_t magic;              ///< Manager magic number
    std::uint64_t total_size;         ///< Total size of managed area in bytes
    index_type    used_size;          ///< Used size in granules (Issue #59)
    index_type    block_count;        ///< Total number of blocks
    index_type    free_count;         ///< Number of free blocks
    index_type    alloc_count;        ///< Number of allocated blocks
    index_type    first_block_offset; ///< First block (granule index)
    index_type    last_block_offset;  ///< [Issue #57 opt 4] Last block (granule index)
    index_type    free_tree_root;     ///< Root of AVL tree of free blocks (granule index)
    bool          owns_memory;        ///< Manager owns buffer (runtime-only)
    std::uint8_t  _pad;               ///< Reserved padding byte (Issue #176: was prev_owns_memory)
    std::uint16_t granule_size;       ///< Issue #83: kGranuleSize at creation time; validated on load
    std::uint64_t prev_total_size;    ///< Previous buffer size in bytes (runtime-only)
    std::uint32_t crc32;              ///< Issue #43 Phase 2.1: CRC32 checksum of the persisted image
    index_type    root_offset;        ///< Issue #200 Phase 3.7: Root object granule index (no_block = no root set)
};

static_assert( sizeof( ManagerHeader<DefaultAddressTraits> ) == 64,
               "ManagerHeader<DefaultAddressTraits> must be exactly 64 bytes (Issue #59, #73 FR-03, #175)" );
static_assert( sizeof( ManagerHeader<DefaultAddressTraits> ) % kGranuleSize == 0,
               "ManagerHeader<DefaultAddressTraits> must be granule-aligned (Issue #59, #73 FR-03)" );

/// @brief Compute CRC32 of the full persisted image, treating the crc32 field as zero.
/// @tparam AddressTraitsT Address traits type (determines ManagerHeader layout).
/// @param data   Pointer to the start of the managed region.
/// @param length Total size of the managed region in bytes.
/// @return CRC32 checksum.
template <typename AddressTraitsT>
inline std::uint32_t compute_image_crc32( const std::uint8_t* data, std::size_t length ) noexcept
{
    // Offset of the crc32 field within ManagerHeader, which itself is located
    // after Block_0 (sizeof(Block<AT>) bytes from base).
    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<AddressTraitsT> );
    constexpr std::size_t kCrcOffset = kHdrOffset + offsetof( ManagerHeader<AddressTraitsT>, crc32 );
    constexpr std::size_t kCrcSize   = sizeof( std::uint32_t );
    constexpr std::size_t kAfterCrc  = kCrcOffset + kCrcSize;

    // CRC everything before the crc32 field
    std::uint32_t crc = 0xFFFFFFFFU;
    for ( std::size_t i = 0; i < kCrcOffset && i < length; ++i )
    {
        crc ^= data[i];
        for ( int bit = 0; bit < 8; ++bit )
            crc = ( crc >> 1 ) ^ ( 0xEDB88320U & ( ~( crc & 1U ) + 1U ) );
    }
    // Skip the crc32 field (treat as 4 zero bytes)
    for ( std::size_t i = 0; i < kCrcSize; ++i )
    {
        crc ^= 0x00U;
        for ( int bit = 0; bit < 8; ++bit )
            crc = ( crc >> 1 ) ^ ( 0xEDB88320U & ( ~( crc & 1U ) + 1U ) );
    }
    // CRC everything after the crc32 field
    for ( std::size_t i = kAfterCrc; i < length; ++i )
    {
        crc ^= data[i];
        for ( int bit = 0; bit < 8; ++bit )
            crc = ( crc >> 1 ) ^ ( 0xEDB88320U & ( ~( crc & 1U ) + 1U ) );
    }
    return crc ^ 0xFFFFFFFFU;
}

/// @brief Number of granules in ManagerHeader<DefaultAddressTraits>
inline constexpr std::uint32_t kManagerHeaderGranules = sizeof( ManagerHeader<DefaultAddressTraits> ) / kGranuleSize;

/// @brief Issue #83: Minimum block size = Block header + 1 data granule (Issue #112: uses Block<A>).
inline constexpr std::size_t kMinBlockSize = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + kGranuleSize;

/// @brief Issue #83: Minimum memory size = Block_0 + ManagerHeader + Block_1 + kMinBlockSize (Issue #112).
/// Uses DefaultAddressTraits for the non-templated constant (backwards compatibility).
inline constexpr std::size_t kMinMemorySize = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) +
                                              sizeof( ManagerHeader<pmm::DefaultAddressTraits> ) +
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
/// Returns AddressTraitsT::index_type (Issue #175: was uint32_t, now uses index_type).
template <typename AddressTraitsT> inline typename AddressTraitsT::index_type bytes_to_granules_t( std::size_t bytes )
{
    using IndexT                         = typename AddressTraitsT::index_type;
    static constexpr std::size_t kGranSz = AddressTraitsT::granule_size;
    if ( bytes > std::numeric_limits<std::size_t>::max() - ( kGranSz - 1 ) )
        return static_cast<IndexT>( 0 );
    std::size_t granules = ( bytes + kGranSz - 1 ) / kGranSz;
    if ( granules > static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) )
        return static_cast<IndexT>( 0 );
    return static_cast<IndexT>( granules );
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
/// Issue #175: parameter changed from uint32_t to index_type for 64-bit support.
template <typename AddressTraitsT> inline std::size_t idx_to_byte_off_t( typename AddressTraitsT::index_type idx )
{
    return static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size;
}

/// @brief Get granule index from byte offset using AddressTraitsT::granule_size.
/// Issue #175: return type changed from uint32_t to index_type for 64-bit support.
template <typename AddressTraitsT> inline typename AddressTraitsT::index_type byte_off_to_idx_t( std::size_t byte_off )
{
    using IndexT = typename AddressTraitsT::index_type;
    assert( byte_off % AddressTraitsT::granule_size == 0 );
    assert( byte_off / AddressTraitsT::granule_size <= static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) );
    return static_cast<IndexT>( byte_off / AddressTraitsT::granule_size );
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
/// Issue #175: idx parameter is now index_type (was uint32_t) for 64-bit support.
template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline pmm::Block<AddressTraitsT>* block_at( std::uint8_t* base, typename AddressTraitsT::index_type idx )
{
    assert( idx != kNoBlock_v<AddressTraitsT> );
    return reinterpret_cast<pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                     AddressTraitsT::granule_size );
}

/// @brief Get const pointer to Block<AddressTraitsT> by granule index (read-only).
/// Issue #175: idx parameter is now index_type (was uint32_t) for 64-bit support.
template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline const pmm::Block<AddressTraitsT>* block_at( const std::uint8_t* base, typename AddressTraitsT::index_type idx )
{
    assert( idx != kNoBlock_v<AddressTraitsT> );
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
inline constexpr typename AddressTraitsT::index_type kBlockHeaderGranules_t =
    static_cast<typename AddressTraitsT::index_type>(
        ( sizeof( pmm::Block<AddressTraitsT> ) + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size );

/// @brief Manager header size in granules for AddressTraitsT (Issue #146, #175).
/// Uses ceiling division: ceil(sizeof(ManagerHeader<AT>) / AT::granule_size).
/// For DefaultAddressTraits (16B granule): 64/16 = 4.
/// For LargeAddressTraits (64B granule): ceil(96/64) = 2.
template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kManagerHeaderGranules_t =
    static_cast<typename AddressTraitsT::index_type>(
        ( sizeof( ManagerHeader<AddressTraitsT> ) + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size );

/// @brief Translate an index_type sentinel (AddressTraitsT::no_block) to index_type kNoBlock_v.
/// Issue #175: ManagerHeader fields are now index_type, so this is an identity function.
/// Kept for backward compatibility — callers that used to need uint32_t→index_type translation
/// can now use no_block_v<AT> directly; this function is a no-op pass-through.
template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type to_u32_idx( typename AddressTraitsT::index_type v )
{
    return v;
}

/// @brief Translate index_type ManagerHeader index to index_type (identity, Issue #175).
/// Kept for backward compatibility — was uint32_t→index_type, now index_type→index_type.
template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type from_u32_idx( typename AddressTraitsT::index_type v )
{
    return v;
}

/// @brief Compute total granules of block for AddressTraitsT (Issue #112: Block<A> layout).
/// Issue #59: total_size is no longer stored — computed via next_offset.
/// Issue #160: Single templated implementation; non-templated overload delegates here.
/// Issue #175: ManagerHeader parameter is now ManagerHeader<AddressTraitsT>*.
template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type block_total_granules( const std::uint8_t*                  base,
                                                                 const ManagerHeader<AddressTraitsT>* hdr,
                                                                 const pmm::Block<AddressTraitsT>*    blk )
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
        return static_cast<IndexT>( next_off - this_idx );
    return static_cast<IndexT>( total_gran - this_idx );
}

/// @brief Issue #69/#106/#112: Structural block validity using Block<DefaultAddressTraits> layout.
/// Invariants: weight<total_gran, prev<idx<next, avl_height<32, distinct AVL refs.
/// Note: this non-templated overload is kept for backward compatibility (DefaultAddressTraits).
inline bool is_valid_block( const std::uint8_t* base, const ManagerHeader<pmm::DefaultAddressTraits>* hdr,
                            std::uint32_t idx )
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

/// @brief O(1) get Block<AddressTraitsT> from user_ptr (ptr - sizeof(Block<AddressTraitsT>)).
/// Issue #112: Block<AddressTraitsT> is the sole block type.
/// Issue #179: unified templated helper replaces the former DefaultAddressTraits-specific overload.
template <typename AddressTraitsT>
inline pmm::Block<AddressTraitsT>* header_from_ptr_t( std::uint8_t* base, void* ptr, std::size_t total_size )
{
    using BlockState                        = pmm::BlockStateBase<AddressTraitsT>;
    static constexpr std::size_t kGranSz    = AddressTraitsT::granule_size;
    static constexpr std::size_t kBlockSize = sizeof( pmm::Block<AddressTraitsT> );

    if ( ptr == nullptr )
        return nullptr;
    std::uint8_t* raw_ptr = reinterpret_cast<std::uint8_t*>( ptr );
    // First user data starts after Block_0 + ManagerHeader<AddressTraitsT> + Block_1
    std::uint8_t* min_addr = base + kBlockSize + sizeof( ManagerHeader<AddressTraitsT> ) + kBlockSize;
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
/// @deprecated Use required_block_granules_t<DefaultAddressTraits>() for new code.
inline std::uint32_t required_block_granules( std::size_t user_bytes )
{
    std::uint32_t data_granules = bytes_to_granules( user_bytes );
    if ( data_granules == 0 )
        data_granules = 1;
    return kBlockHeaderGranules_t<pmm::DefaultAddressTraits> + data_granules;
}

/// @brief Issue #166: Templated variant of required_block_granules for any AddressTraitsT.
/// Minimum block granules for user_bytes = header_granules + max(1, ceil(user_bytes / granule_size)).
/// Uses AddressTraitsT::granule_size and kBlockHeaderGranules_t<AT> for correct per-AT computation.
template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type required_block_granules_t( std::size_t user_bytes )
{
    using index_type         = typename AddressTraitsT::index_type;
    index_type data_granules = bytes_to_granules_t<AddressTraitsT>( user_bytes );
    if ( data_granules == 0 )
        data_granules = 1;
    return kBlockHeaderGranules_t<AddressTraitsT> + data_granules;
}

} // namespace detail

} // namespace pmm
