/**
 * @file pmm/types.h
 * @brief Core types and constants for PersistMemoryManager.
 *
 * Contains: ManagerHeader, MemoryStats, ManagerInfo,
 * BlockView, FreeBlockView and utility functions for byte/granule conversion.
 *
 * Sizes of structures are protected by static_assert:
 *   Block<DefaultAddressTraits> == 32 bytes
 *   ManagerHeader<DefaultAddressTraits> == 64 bytes
 *
 * @version 2.4
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/tree_node.h"
#include "pmm/validation.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>

namespace pmm
{

// ─── Error codes ──────────────────────────────────────

/**
 * @brief Error codes for PersistMemoryManager operations.
 *
 * Provides detailed error information instead of bare bool/nullptr.
 * Use `PersistMemoryManager::last_error()` to query the most recent error.
 */
enum class PmmError : std::uint8_t
{
    Ok                      = 0,  ///< Operation succeeded
    NotInitialized          = 1,  ///< Manager is not initialized
    InvalidSize             = 2,  ///< Invalid size argument (zero, too small, etc.)
    Overflow                = 3,  ///< Arithmetic overflow in size/granule computation
    OutOfMemory             = 4,  ///< Allocation failed — not enough free space
    ExpandFailed            = 5,  ///< Backend expand() failed
    InvalidMagic            = 6,  ///< Magic number mismatch on load()
    CrcMismatch             = 7,  ///< CRC32 mismatch on load (corrupted image)
    SizeMismatch            = 8,  ///< Stored total_size does not match backend
    GranuleMismatch         = 9,  ///< Stored granule_size does not match address_traits
    BackendError            = 10, ///< Backend returned null or invalid state
    InvalidPointer          = 11, ///< Pointer is null or out of bounds
    BlockLocked             = 12, ///< Block is permanently locked (cannot deallocate)
    UnsupportedImageVersion = 13, ///< Stored image_version is not supported by this build
};

// ─── Constants ────────────────────────────────────────────────────────────────

/// @brief Granule size in bytes. All alignment/granularity expressed via this constant.
/// Matches DefaultAddressTraits::granule_size.
inline constexpr std::size_t kGranuleSize = 16;
static_assert( ( kGranuleSize & ( kGranuleSize - 1 ) ) == 0, "kGranuleSize must be a power of 2 " );
static_assert( kGranuleSize == pmm::DefaultAddressTraits::granule_size,
               "kGranuleSize must match DefaultAddressTraits::granule_size " );

inline constexpr std::uint64_t kMagic = 0x504D4D5F56303938ULL; ///< "PMM_V098"

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
    std::size_t    alignment;   ///< Always kGranuleSize
    bool           used;
};

/// @brief View of a single free block in the AVL tree for visualisation.
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

/// @brief Legacy value found in images created before ManagerHeader had an explicit version byte.
inline constexpr std::uint8_t kLegacyUnversionedImageVersion = 0;

/// @brief Current persistent image layout version written by create().
inline constexpr std::uint8_t kCurrentImageVersion = 1;

/// @brief True when this build can read the image version directly or through an in-place migration.
inline constexpr bool is_supported_image_version( std::uint8_t image_version ) noexcept
{
    return image_version == kLegacyUnversionedImageVersion || image_version == kCurrentImageVersion;
}

/// @brief True when load() should upgrade the header after accepting the image.
inline constexpr bool image_version_requires_migration( std::uint8_t image_version ) noexcept
{
    return image_version == kLegacyUnversionedImageVersion;
}

// ─── CRC32 utility ────────────────────────────────────
//
// Software CRC32 (ISO 3309 / ITU-T V.42 polynomial 0xEDB88320).
// Header-only, no external dependencies.  Used by io.h save/load functions.

/// @brief CRC32 single byte accumulation.
/// Extracts the inner loop from compute_crc32 and compute_image_crc32.
inline std::uint32_t crc32_accumulate_byte( std::uint32_t crc, std::uint8_t byte ) noexcept
{
    crc ^= byte;
    for ( int bit = 0; bit < 8; ++bit )
        crc = ( crc >> 1 ) ^ ( 0xEDB88320U & ( ~( crc & 1U ) + 1U ) );
    return crc;
}

/// @brief Compute CRC32 over a byte range.
/// Uses the standard Ethernet/zlib polynomial (0xEDB88320, reflected).
inline std::uint32_t compute_crc32( const std::uint8_t* data, std::size_t length ) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for ( std::size_t i = 0; i < length; ++i )
        crc = crc32_accumulate_byte( crc, data[i] );
    return crc ^ 0xFFFFFFFFU;
}

// BlockHeader struct removed — Block<DefaultAddressTraits> is the sole block type.
// All block metadata is stored in Block<AddressTraitsT> (prev/next + TreeNode).

// Verify Block<DefaultAddressTraits> layout and size constraints.
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32, "Block<DefaultAddressTraits> must be 32 bytes " );
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) % kGranuleSize == 0,
               "Block<DefaultAddressTraits> must be granule-aligned " );

// Verify Block linked list fields occupy 2 index_type fields.
// LinkedListNode was merged into Block. Layout verified via block.h.
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) ==
                   sizeof( pmm::TreeNode<pmm::DefaultAddressTraits> ) + 2 * sizeof( std::uint32_t ),
               "Block<DefaultAddressTraits> must have TreeNode + 2 index_type list fields " );
// TreeNode<DefaultAddressTraits>: weight + left/right/parent + root_offset + avl_height/node_type (24 bytes).
// Weight moved to first field, avl_height/node_type (renamed from _pad) moved to end.
static_assert( sizeof( pmm::TreeNode<pmm::DefaultAddressTraits> ) == 5 * sizeof( std::uint32_t ) + 4,
               "TreeNode<DefaultAddressTraits> must be 24 bytes " );

// kBlockMagic removed: block validity now uses is_valid_block() structural invariants.
/// Matches DefaultAddressTraits::no_block.
inline constexpr std::uint32_t kNoBlock = 0xFFFFFFFFU; ///< Sentinel: no block (granule index)
static_assert( kNoBlock == pmm::DefaultAddressTraits::no_block, "kNoBlock must match DefaultAddressTraits::no_block " );

/// @brief Template alias for AddressTraitsT::no_block sentinel.
/// Use this instead of detail::kNoBlock in generic (templated) code that works with any AT.
/// For DefaultAddressTraits, kNoBlock_v<DefaultAddressTraits> == kNoBlock == 0xFFFFFFFFU.
template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kNoBlock_v = AddressTraitsT::no_block;

/// @brief Null granule index sentinel: index_type(0) means "no data" in persistent containers.
/// Use this instead of `static_cast<index_type>(0)` in persistent containers.
template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kNullIdx_v = static_cast<typename AddressTraitsT::index_type>( 0 );

/// @brief Manager header parameterized on AddressTraitsT.
/// All _offset and counter fields use index_type, so LargeDBConfig uses uint64_t indices
/// and DefaultAddressTraits keeps uint32_t with exactly 64 bytes as before.
/// prev_total_size is runtime-only (zeroed by load() — not persisted).
/// Removed prev_owns_memory and prev_base_ptr (obsolete runtime-only fields).
///
/// Layout for DefaultAddressTraits (uint32_t):
///   magic (8) + total_size (8) + 7×uint32_t + owns_memory(1) +
///   image_version(1) + granule_size(2) + prev_total_size(8) + crc32(4) + root_offset(4) = 64 bytes
/// For LargeAddressTraits (uint64_t): sizeof = 16 + 56 + 4*(+4 padding) + 16 = 96 bytes
///   → occupies ceil(96/64) = 2 granules = 128 bytes via kManagerHeaderGranules_t<AT>
template <typename AddressTraitsT = DefaultAddressTraits> struct ManagerHeader
{
    using index_type = typename AddressTraitsT::index_type;

    std::uint64_t magic;              ///< Manager magic number
    std::uint64_t total_size;         ///< Total size of managed area in bytes
    index_type    used_size;          ///< Used size in granules
    index_type    block_count;        ///< Total number of blocks
    index_type    free_count;         ///< Number of free blocks
    index_type    alloc_count;        ///< Number of allocated blocks
    index_type    first_block_offset; ///< First block (granule index)
    index_type    last_block_offset;  ///< Last block (granule index)
    index_type    free_tree_root;     ///< Root of AVL tree of free blocks (granule index)
    bool          owns_memory;        ///< Manager owns buffer (runtime-only)
    std::uint8_t  image_version;      ///< Persistent image layout version
    std::uint16_t granule_size;       ///< kGranuleSize at creation time; validated on load
    std::uint64_t prev_total_size;    ///< Previous buffer size in bytes (runtime-only)
    std::uint32_t crc32;              ///< CRC32 checksum of the persisted image
    index_type    root_offset;        ///< Forest registry root granule index (no_block before bootstrap)
};

static_assert( sizeof( ManagerHeader<DefaultAddressTraits> ) == 64,
               "ManagerHeader<DefaultAddressTraits> must be exactly 64 bytes " );
static_assert( sizeof( ManagerHeader<DefaultAddressTraits> ) % kGranuleSize == 0,
               "ManagerHeader<DefaultAddressTraits> must be granule-aligned " );

/// @brief Block header size in granules for AddressTraitsT.
/// Computes ceil(sizeof(Block<AT>) / AT::granule_size).
/// For DefaultAddressTraits: 32/16 = 2. For SmallAddressTraits: ceil(18/16) = 2. For Large: 64/64 = 1.
template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kBlockHeaderGranules_t =
    static_cast<typename AddressTraitsT::index_type>(
        ( sizeof( pmm::Block<AddressTraitsT> ) + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size );

/// @brief Canonical byte offset of ManagerHeader from the base pointer.
template <typename AddressTraitsT>
inline constexpr std::size_t manager_header_offset_bytes_v =
    static_cast<std::size_t>( kBlockHeaderGranules_t<AddressTraitsT> ) * AddressTraitsT::granule_size;

/// @brief Canonical mutable ManagerHeader pointer from the base pointer.
template <typename AddressTraitsT>
inline ManagerHeader<AddressTraitsT>* manager_header_at( std::uint8_t* base ) noexcept
{
    return reinterpret_cast<ManagerHeader<AddressTraitsT>*>( base + manager_header_offset_bytes_v<AddressTraitsT> );
}

/// @brief Canonical const ManagerHeader pointer from the base pointer.
template <typename AddressTraitsT>
inline const ManagerHeader<AddressTraitsT>* manager_header_at( const std::uint8_t* base ) noexcept
{
    return reinterpret_cast<const ManagerHeader<AddressTraitsT>*>( base +
                                                                   manager_header_offset_bytes_v<AddressTraitsT> );
}

/// @brief Compute CRC32 of the full persisted image, treating the crc32 field as zero.
/// @tparam AddressTraitsT Address traits type (determines ManagerHeader layout).
/// @param data   Pointer to the start of the managed region.
/// @param length Total size of the managed region in bytes.
/// @return CRC32 checksum.
template <typename AddressTraitsT>
inline std::uint32_t compute_image_crc32( const std::uint8_t* data, std::size_t length ) noexcept
{
    // Offset of the crc32 field within ManagerHeader, which itself is located
    // at the canonical granule-aligned offset after Block_0.
    constexpr std::size_t kHdrOffset = manager_header_offset_bytes_v<AddressTraitsT>;
    constexpr std::size_t kCrcOffset = kHdrOffset + offsetof( ManagerHeader<AddressTraitsT>, crc32 );
    constexpr std::size_t kCrcSize   = sizeof( std::uint32_t );
    constexpr std::size_t kAfterCrc  = kCrcOffset + kCrcSize;

    // CRC everything before the crc32 field
    std::uint32_t crc = 0xFFFFFFFFU;
    for ( std::size_t i = 0; i < kCrcOffset && i < length; ++i )
        crc = crc32_accumulate_byte( crc, data[i] );
    // Skip the crc32 field (treat as 4 zero bytes)
    for ( std::size_t i = 0; i < kCrcSize; ++i )
        crc = crc32_accumulate_byte( crc, 0x00U );
    // CRC everything after the crc32 field
    for ( std::size_t i = kAfterCrc; i < length; ++i )
        crc = crc32_accumulate_byte( crc, data[i] );
    return crc ^ 0xFFFFFFFFU;
}

/// @brief Number of granules in ManagerHeader<DefaultAddressTraits>
inline constexpr std::uint32_t kManagerHeaderGranules = sizeof( ManagerHeader<DefaultAddressTraits> ) / kGranuleSize;

/// @brief Minimum block size = Block header + 1 data granule (uses Block<A>).
inline constexpr std::size_t kMinBlockSize = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + kGranuleSize;

/// @brief Minimum memory size = Block_0 + ManagerHeader + Block_1 + kMinBlockSize.
/// Uses DefaultAddressTraits for the non-templated constant.
inline constexpr std::size_t kMinMemorySize = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) +
                                              sizeof( ManagerHeader<pmm::DefaultAddressTraits> ) +
                                              sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + kMinBlockSize;

// ─── Byte ↔ granule conversion ──────────────────────────────────────────────
//
// Templated helpers using AddressTraitsT::granule_size.
// For non-default address traits (SmallAddressTraits with 16B, LargeAddressTraits with 64B).

/// @brief Convert bytes to granules (ceiling) using AddressTraitsT::granule_size.
/// Returns AddressTraitsT::index_type.
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
/// Parameter changed from uint32_t to index_type for 64-bit support.
template <typename AddressTraitsT> inline std::size_t idx_to_byte_off_t( typename AddressTraitsT::index_type idx )
{
    return static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size;
}

/// @brief Get granule index from byte offset using AddressTraitsT::granule_size.
/// Return type changed from uint32_t to index_type for 64-bit support.
template <typename AddressTraitsT> inline typename AddressTraitsT::index_type byte_off_to_idx_t( std::size_t byte_off )
{
    using IndexT = typename AddressTraitsT::index_type;
    assert( byte_off % AddressTraitsT::granule_size == 0 );
    assert( byte_off / AddressTraitsT::granule_size <= static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) );
    return static_cast<IndexT>( byte_off / AddressTraitsT::granule_size );
}

/// @brief Returns true only for kGranuleSize (16-byte) alignment.
inline bool is_valid_alignment( std::size_t align )
{
    return align == kGranuleSize;
}

/// @brief Get pointer to Block<AddressTraitsT> by granule index.
/// Single canonical implementation replacing per-file blk_at() helpers in
/// allocator_policy.h and free_block_tree.h.
/// Uses AddressTraitsT::granule_size for byte-offset computation.
/// Idx parameter is now index_type (was uint32_t) for 64-bit support.
template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline pmm::Block<AddressTraitsT>* block_at( std::uint8_t* base, typename AddressTraitsT::index_type idx )
{
    assert( idx != kNoBlock_v<AddressTraitsT> );
    return reinterpret_cast<pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                     AddressTraitsT::granule_size );
}

/// @brief Get const pointer to Block<AddressTraitsT> by granule index (read-only).
/// Idx parameter is now index_type (was uint32_t) for 64-bit support.
template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline const pmm::Block<AddressTraitsT>* block_at( const std::uint8_t* base, typename AddressTraitsT::index_type idx )
{
    assert( idx != kNoBlock_v<AddressTraitsT> );
    return reinterpret_cast<const pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                           AddressTraitsT::granule_size );
}

/// @brief Validated block_at: returns nullptr if idx is out of range.
/// Use in verify/diagnostic paths where invalid indices must be handled gracefully.
template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline pmm::Block<AddressTraitsT>* block_at_checked( std::uint8_t* base, std::size_t total_size,
                                                     typename AddressTraitsT::index_type idx ) noexcept
{
    if ( !validate_block_index<AddressTraitsT>( total_size, idx ) )
        return nullptr;
    return reinterpret_cast<pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                     AddressTraitsT::granule_size );
}

/// @brief Validated block_at (const): returns nullptr if idx is out of range.
template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline const pmm::Block<AddressTraitsT>* block_at_checked( const std::uint8_t* base, std::size_t total_size,
                                                           typename AddressTraitsT::index_type idx ) noexcept
{
    if ( !validate_block_index<AddressTraitsT>( total_size, idx ) )
        return nullptr;
    return reinterpret_cast<const pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                           AddressTraitsT::granule_size );
}

/// @brief Get granule index of Block<AddressTraitsT>.
template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type block_idx_t( const std::uint8_t*               base,
                                                        const pmm::Block<AddressTraitsT>* block )
{
    std::size_t byte_off = reinterpret_cast<const std::uint8_t*>( block ) - base;
    assert( byte_off % AddressTraitsT::granule_size == 0 );
    return static_cast<typename AddressTraitsT::index_type>( byte_off / AddressTraitsT::granule_size );
}

/// @brief Manager header size in granules for AddressTraitsT.
/// Uses ceiling division: ceil(sizeof(ManagerHeader<AT>) / AT::granule_size).
/// For DefaultAddressTraits (16B granule): 64/16 = 4.
/// For LargeAddressTraits (64B granule): ceil(96/64) = 2.
template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kManagerHeaderGranules_t =
    static_cast<typename AddressTraitsT::index_type>(
        ( sizeof( ManagerHeader<AddressTraitsT> ) + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size );

/// @brief Compute total granules of block for AddressTraitsT (Block<A> layout).
/// Total_size is no longer stored — computed via next_offset.
/// Single templated implementation; non-templated overload delegates here.
/// ManagerHeader parameter is now ManagerHeader<AddressTraitsT>*.
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

/// @brief Resolve a granule index to a raw pointer.
/// Eliminates repeated `base + idx * granule_size` patterns across persistent containers.
/// @return nullptr if idx is zero (null sentinel).
template <typename AddressTraitsT>
inline void* resolve_granule_ptr( std::uint8_t* base, typename AddressTraitsT::index_type idx ) noexcept
{
    if ( idx == static_cast<typename AddressTraitsT::index_type>( 0 ) )
        return nullptr;
    return base + static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size;
}

/// @brief Validated resolve_granule_ptr: returns nullptr if idx is zero or out of bounds.
/// Use in paths where external/untrusted indices must be validated before dereferencing.
template <typename AddressTraitsT>
inline void* resolve_granule_ptr_checked( std::uint8_t* base, std::size_t total_size,
                                          typename AddressTraitsT::index_type idx ) noexcept
{
    if ( idx == static_cast<typename AddressTraitsT::index_type>( 0 ) )
        return nullptr;
    std::size_t byte_off = static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size;
    if ( byte_off >= total_size )
        return nullptr;
    return base + byte_off;
}

/// @brief Convert a raw pointer to a granule index.
/// Eliminates repeated `(ptr - base) / granule_size` patterns across persistent containers.
template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type ptr_to_granule_idx( const std::uint8_t* base, const void* ptr ) noexcept
{
    using IndexT         = typename AddressTraitsT::index_type;
    std::size_t byte_off = static_cast<const std::uint8_t*>( ptr ) - base;
    return static_cast<IndexT>( byte_off / AddressTraitsT::granule_size );
}

/// @brief Validated ptr_to_granule_idx: returns no_block on invalid input.
/// Checks null, bounds, and granule alignment before converting.
template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type ptr_to_granule_idx_checked( const std::uint8_t* base, std::size_t total_size,
                                                                       const void* ptr ) noexcept
{
    using IndexT = typename AddressTraitsT::index_type;
    if ( ptr == nullptr || base == nullptr )
        return AddressTraitsT::no_block;
    const auto* raw = static_cast<const std::uint8_t*>( ptr );
    if ( raw < base || raw >= base + total_size )
        return AddressTraitsT::no_block;
    std::size_t byte_off = static_cast<std::size_t>( raw - base );
    if ( byte_off % AddressTraitsT::granule_size != 0 )
        return AddressTraitsT::no_block;
    std::size_t idx = byte_off / AddressTraitsT::granule_size;
    if ( idx > static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) )
        return AddressTraitsT::no_block;
    return static_cast<IndexT>( idx );
}

/// @brief Compute user data address for block (block + sizeof(Block<A>)).
/// Single canonical implementation — use this instead of duplicating the cast in each call site.
template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline void* user_ptr( pmm::Block<AddressTraitsT>* block )
{
    return reinterpret_cast<std::uint8_t*>( block ) + sizeof( pmm::Block<AddressTraitsT> );
}

// Canonical public-pointer reconstruction is a manager-structure check, not a
// payload-byte self-consistency check. This reads first/last anchors plus
// prev/next neighbor back-links without internal locking; callers must hold the
// manager lock or otherwise guarantee that block links are stable while this runs.
template <typename AddressTraitsT>
inline bool is_block_header_linked_in_canonical_chain( const std::uint8_t*                  base,
                                                       const ManagerHeader<AddressTraitsT>* hdr, std::size_t total_size,
                                                       typename AddressTraitsT::index_type cand_idx ) noexcept
{
    using BlockState = pmm::BlockStateBase<AddressTraitsT>;
    using IndexT     = typename AddressTraitsT::index_type;

    if ( base == nullptr || hdr == nullptr )
        return false;
    if ( hdr->block_count == 0 || hdr->first_block_offset == AddressTraitsT::no_block )
        return false;

    if ( !validate_block_index<AddressTraitsT>( total_size, hdr->first_block_offset ) ||
         !validate_block_index<AddressTraitsT>( total_size, hdr->last_block_offset ) )
        return false;

    const void*  cand = base + static_cast<std::size_t>( cand_idx ) * AddressTraitsT::granule_size;
    const IndexT prev = BlockState::get_prev_offset( cand );
    const IndexT next = BlockState::get_next_offset( cand );

    if ( prev == AddressTraitsT::no_block )
    {
        if ( cand_idx != hdr->first_block_offset )
            return false;
    }
    else
    {
        if ( !validate_block_index<AddressTraitsT>( total_size, prev ) || prev >= cand_idx )
            return false;
        const void* prev_block = base + static_cast<std::size_t>( prev ) * AddressTraitsT::granule_size;
        if ( BlockState::get_next_offset( prev_block ) != cand_idx )
            return false;
    }

    if ( next == AddressTraitsT::no_block )
    {
        if ( cand_idx != hdr->last_block_offset )
            return false;
    }
    else
    {
        if ( !validate_block_index<AddressTraitsT>( total_size, next ) || next <= cand_idx )
            return false;
        const void* next_block = base + static_cast<std::size_t>( next ) * AddressTraitsT::granule_size;
        if ( BlockState::get_prev_offset( next_block ) != cand_idx )
            return false;
    }

    return true;
}

template <typename AddressTraitsT>
inline bool is_canonical_allocated_block_header( const std::uint8_t* base, std::size_t total_size,
                                                 const std::uint8_t* cand_addr ) noexcept
{
    using BlockState = pmm::BlockStateBase<AddressTraitsT>;
    using IndexT     = typename AddressTraitsT::index_type;

    if ( base == nullptr || cand_addr == nullptr )
        return false;

    const std::uintptr_t base_addr = reinterpret_cast<std::uintptr_t>( base );
    const std::uintptr_t cand_raw  = reinterpret_cast<std::uintptr_t>( cand_addr );
    if ( cand_raw < base_addr )
        return false;

    const std::size_t cand_off = static_cast<std::size_t>( cand_raw - base_addr );
    if ( cand_off % AddressTraitsT::granule_size != 0 )
        return false;
    if ( cand_off / AddressTraitsT::granule_size > static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) )
        return false;

    const IndexT cand_idx = static_cast<IndexT>( cand_off / AddressTraitsT::granule_size );
    if ( !validate_block_index<AddressTraitsT>( total_size, cand_idx ) )
        return false;

    const IndexT weight = BlockState::get_weight( cand_addr );
    if ( weight == 0 || BlockState::get_root_offset( cand_addr ) != cand_idx )
        return false;

    const std::uint16_t node_type = BlockState::get_node_type( cand_addr );
    if ( node_type != pmm::kNodeReadWrite && node_type != pmm::kNodeReadOnly )
        return false;

    if ( total_size < manager_header_offset_bytes_v<AddressTraitsT> + sizeof( ManagerHeader<AddressTraitsT> ) )
        return false;

    const auto* hdr = manager_header_at<AddressTraitsT>( base );
    if ( hdr->total_size != total_size )
        return false;
    if ( !is_block_header_linked_in_canonical_chain<AddressTraitsT>( base, hdr, total_size, cand_idx ) )
        return false;

    constexpr std::size_t kBlockSize = sizeof( pmm::Block<AddressTraitsT> );
    if ( cand_off > total_size - kBlockSize )
        return false;
    const std::size_t data_start = cand_off + kBlockSize;
    if ( static_cast<std::size_t>( weight ) >
         ( std::numeric_limits<std::size_t>::max )() / AddressTraitsT::granule_size )
        return false;
    const std::size_t data_bytes = static_cast<std::size_t>( weight ) * AddressTraitsT::granule_size;
    if ( data_bytes > total_size - data_start )
        return false;

    return true;
}

template <typename AddressTraitsT>
inline bool is_canonical_user_ptr( const std::uint8_t* base, std::size_t total_size, const void* ptr ) noexcept
{
    constexpr std::size_t kBlockSize      = sizeof( pmm::Block<AddressTraitsT> );
    const std::size_t     min_user_offset = kBlockSize + sizeof( ManagerHeader<AddressTraitsT> ) + kBlockSize;
    if ( !validate_user_ptr<AddressTraitsT>( base, total_size, ptr, min_user_offset ) )
        return false;

    const auto* raw_ptr   = static_cast<const std::uint8_t*>( ptr );
    const auto* cand_addr = raw_ptr - kBlockSize;
    if ( cand_addr + kBlockSize != raw_ptr )
        return false;

    return is_canonical_allocated_block_header<AddressTraitsT>( base, total_size, cand_addr );
}

/// @brief O(1) get Block<AddressTraitsT> from canonical user_ptr (ptr - sizeof(Block<AddressTraitsT>)).
/// @pre Caller holds the manager lock, or block links are otherwise stable for the canonical-chain proof.
/// Block<AddressTraitsT> is the sole block type.
/// Unified templated helper replaces the former DefaultAddressTraits-specific overload.
template <typename AddressTraitsT>
inline pmm::Block<AddressTraitsT>* header_from_ptr_t( std::uint8_t* base, void* ptr, std::size_t total_size )
{
    static constexpr std::size_t kBlockSize = sizeof( pmm::Block<AddressTraitsT> );

    if ( !is_canonical_user_ptr<AddressTraitsT>( base, total_size, ptr ) )
        return nullptr;
    std::uint8_t* cand_addr = static_cast<std::uint8_t*>( ptr ) - kBlockSize;
    return reinterpret_cast<pmm::Block<AddressTraitsT>*>( cand_addr );
}

/// @brief Required block granules for user_bytes = header_granules + max(1, ceil(user_bytes / granule_size)).
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
