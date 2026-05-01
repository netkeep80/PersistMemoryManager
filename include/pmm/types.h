#pragma once
#include "pmm/address_traits.h"
#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/validation.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
namespace pmm
{
/*
## pmm-pmmerror
req: feat-004, fr-014, fr-024, qa-rec-001, qa-compat-001, dr-020, feat-010
*/
enum class PmmError : uint8_t
{
    Ok                      = 0,
    NotInitialized          = 1,
    InvalidSize             = 2,
    Overflow                = 3,
    OutOfMemory             = 4,
    ExpandFailed            = 5,
    InvalidMagic            = 6,
    CrcMismatch             = 7,
    SizeMismatch            = 8,
    GranuleMismatch         = 9,
    BackendError            = 10,
    InvalidPointer          = 11,
    BlockLocked             = 12,
    UnsupportedImageVersion = 13,
};
inline constexpr size_t kGranuleSize = 16;
static_assert( ( kGranuleSize & ( kGranuleSize - 1 ) ) == 0, "" );
static_assert( kGranuleSize == pmm::DefaultAddressTraits::granule_size, "" );
inline constexpr uint64_t kMagic = 0x504D4D5F56303938ULL;
/*
## pmm-memorystats
req: feat-005, fr-019, fr-031, ur-004
*/
struct MemoryStats
{
    size_t total_blocks;
    size_t free_blocks;
    size_t allocated_blocks;
    size_t largest_free;
    size_t smallest_free;
    size_t total_fragmentation;
};
struct ManagerInfo
{
    uint64_t       magic;
    size_t         total_size;
    size_t         used_size;
    size_t         block_count;
    size_t         free_count;
    size_t         alloc_count;
    std::ptrdiff_t first_block_offset;
    std::ptrdiff_t first_free_offset;
    size_t         manager_header_size;
};
/*
## pmm-blockview
req: feat-005, fr-019, fr-031, ur-004
*/
struct BlockView
{
    size_t         index;
    std::ptrdiff_t offset;
    size_t         total_size;
    size_t         header_size;
    size_t         user_size;
    size_t         alignment;
    bool           used;
};
/*
## pmm-freeblockview
req: feat-005, fr-019, fr-031, ur-004
*/
struct FreeBlockView
{
    std::ptrdiff_t offset;
    size_t         total_size;
    size_t         free_size;
    std::ptrdiff_t left_offset;
    std::ptrdiff_t right_offset;
    std::ptrdiff_t parent_offset;
    int            avl_height;
    int            avl_depth;
};
namespace detail
{
inline constexpr uint8_t kLegacyUnversionedImageVersion = 0;
inline constexpr uint8_t kCurrentImageVersion           = 2;
inline constexpr bool    is_supported_image_version( uint8_t image_version ) noexcept
{
    return image_version == kCurrentImageVersion;
}
inline constexpr bool image_version_requires_migration( [[maybe_unused]] uint8_t image_version ) noexcept
{
    return false;
}
inline uint32_t crc32_accumulate_byte( uint32_t crc, uint8_t byte ) noexcept
{
    crc ^= byte;
    for ( int bit = 0; bit < 8; ++bit )
        crc = ( crc >> 1 ) ^ ( 0xEDB88320U & ( ~( crc & 1U ) + 1U ) );
    return crc;
}
inline uint32_t compute_crc32( const uint8_t* data, size_t length ) noexcept
{
    uint32_t crc = 0xFFFFFFFFU;
    for ( size_t i = 0; i < length; ++i )
        crc = crc32_accumulate_byte( crc, data[i] );
    return crc ^ 0xFFFFFFFFU;
}
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32, "" );
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) % kGranuleSize == 0, "" );
inline constexpr uint32_t kNoBlock = 0xFFFFFFFFU;
static_assert( kNoBlock == pmm::DefaultAddressTraits::no_block, "" );
template <typename AT> inline constexpr typename AT::index_type kNoBlock_v = AT::no_block;
template <typename AT> inline constexpr typename AT::index_type kNullIdx_v = static_cast<typename AT::index_type>( 0 );
/*
### pmm-detail-managerheader
*/
template <typename AT = DefaultAddressTraits> struct ManagerHeader
{
    using index_type = typename AT::index_type;
    uint64_t   magic;
    uint64_t   total_size;
    index_type used_size;
    index_type block_count;
    index_type free_count;
    index_type alloc_count;
    index_type first_block_offset;
    index_type last_block_offset;
    index_type free_tree_root;
    bool       owns_memory;
    uint8_t    image_version;
    uint16_t   granule_size;
    uint64_t   prev_total_size;
    uint32_t   crc32;
    index_type root_offset;
};
static_assert( sizeof( ManagerHeader<DefaultAddressTraits> ) == 64, "" );
static_assert( sizeof( ManagerHeader<DefaultAddressTraits> ) % kGranuleSize == 0, "" );
template <typename AT>
inline constexpr typename AT::index_type kBlockHeaderGranules_t =
    static_cast<typename AT::index_type>( ( sizeof( pmm::Block<AT> ) + AT::granule_size - 1 ) / AT::granule_size );
template <typename AT>
inline constexpr size_t manager_header_offset_bytes_v =
    static_cast<size_t>( kBlockHeaderGranules_t<AT> ) * AT::granule_size;
template <typename AT> inline ManagerHeader<AT>* manager_header_at( uint8_t* base ) noexcept
{
    return reinterpret_cast<ManagerHeader<AT>*>( base + manager_header_offset_bytes_v<AT> );
}
template <typename AT> inline const ManagerHeader<AT>* manager_header_at( const uint8_t* base ) noexcept
{
    return reinterpret_cast<const ManagerHeader<AT>*>( base + manager_header_offset_bytes_v<AT> );
}
template <typename AT> inline uint32_t compute_image_crc32( const uint8_t* data, size_t length ) noexcept
{
    constexpr size_t kHdrOffset = manager_header_offset_bytes_v<AT>;
    constexpr size_t kCrcOffset = kHdrOffset + offsetof( ManagerHeader<AT>, crc32 );
    constexpr size_t kCrcSize   = sizeof( uint32_t );
    constexpr size_t kAfterCrc  = kCrcOffset + kCrcSize;
    uint32_t         crc        = 0xFFFFFFFFU;
    for ( size_t i = 0; i < kCrcOffset && i < length; ++i )
        crc = crc32_accumulate_byte( crc, data[i] );
    for ( size_t i = 0; i < kCrcSize; ++i )
        crc = crc32_accumulate_byte( crc, 0x00U );
    for ( size_t i = kAfterCrc; i < length; ++i )
        crc = crc32_accumulate_byte( crc, data[i] );
    return crc ^ 0xFFFFFFFFU;
}
inline constexpr uint32_t kManagerHeaderGranules = sizeof( ManagerHeader<DefaultAddressTraits> ) / kGranuleSize;
inline constexpr size_t   kMinBlockSize          = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + kGranuleSize;
inline constexpr size_t   kMinMemorySize         = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) +
                                         sizeof( ManagerHeader<pmm::DefaultAddressTraits> ) +
                                         sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + kMinBlockSize;
template <typename AT> inline typename AT::index_type bytes_to_idx_t( size_t bytes )
{
    static constexpr size_t kGranSz = AT::granule_size;
    using IndexT                    = typename AT::index_type;
    if ( bytes == 0 )
        return static_cast<IndexT>( 0 );
    if ( bytes > std::numeric_limits<size_t>::max() - ( kGranSz - 1 ) )
        return AT::no_block;
    size_t granules = ( bytes + kGranSz - 1 ) / kGranSz;
    if ( granules > static_cast<size_t>( std::numeric_limits<IndexT>::max() ) )
        return AT::no_block;
    return static_cast<IndexT>( granules );
}
template <typename AT> inline size_t idx_to_byte_off_t( typename AT::index_type idx )
{
    return static_cast<size_t>( idx ) * AT::granule_size;
}
template <typename AT> inline typename AT::index_type byte_off_to_idx_t( size_t byte_off )
{
    using IndexT = typename AT::index_type;
    assert( byte_off % AT::granule_size == 0 );
    assert( byte_off / AT::granule_size <= static_cast<size_t>( std::numeric_limits<IndexT>::max() ) );
    return static_cast<IndexT>( byte_off / AT::granule_size );
}
inline bool is_valid_alignment( size_t align )
{
    return align == kGranuleSize;
}
template <typename AT = pmm::DefaultAddressTraits>
inline pmm::Block<AT>* block_at( uint8_t* base, typename AT::index_type idx )
{
    assert( idx != kNoBlock_v<AT> );
    return reinterpret_cast<pmm::Block<AT>*>( base + static_cast<size_t>( idx ) * AT::granule_size );
}
template <typename AT = pmm::DefaultAddressTraits>
inline const pmm::Block<AT>* block_at( const uint8_t* base, typename AT::index_type idx )
{
    assert( idx != kNoBlock_v<AT> );
    return reinterpret_cast<const pmm::Block<AT>*>( base + static_cast<size_t>( idx ) * AT::granule_size );
}
template <typename AT> inline typename AT::index_type block_idx_t( const uint8_t* base, const pmm::Block<AT>* block )
{
    size_t byte_off = reinterpret_cast<const uint8_t*>( block ) - base;
    assert( byte_off % AT::granule_size == 0 );
    return static_cast<typename AT::index_type>( byte_off / AT::granule_size );
}
template <typename AT>
inline constexpr typename AT::index_type kManagerHeaderGranules_t =
    static_cast<typename AT::index_type>( ( sizeof( ManagerHeader<AT> ) + AT::granule_size - 1 ) / AT::granule_size );
template <typename AT>
inline typename AT::index_type physical_block_total_granules( const uint8_t* base, const ManagerHeader<AT>* hdr,
                                                              const pmm::Block<AT>* blk )
{
    using BlockState                   = pmm::BlockStateBase<AT>;
    static constexpr size_t kGranSz    = AT::granule_size;
    using IndexT                       = typename AT::index_type;
    static constexpr IndexT kNoBlk     = AT::no_block;
    size_t                  byte_off   = reinterpret_cast<const uint8_t*>( blk ) - base;
    IndexT                  this_idx   = static_cast<IndexT>( byte_off / kGranSz );
    IndexT                  next_off   = BlockState::get_next_offset( blk );
    IndexT                  total_gran = static_cast<IndexT>( hdr->total_size / kGranSz );
    if ( next_off != kNoBlk )
        return static_cast<IndexT>( next_off - this_idx );
    return static_cast<IndexT>( total_gran - this_idx );
}
template <typename AT> inline typename AT::index_type cached_block_total_granules( const pmm::Block<AT>* blk )
{
    using BlockState = pmm::BlockStateBase<AT>;
    return BlockState::get_weight( blk );
}
template <typename AT>
inline typename AT::index_type block_total_granules( const uint8_t* base, const ManagerHeader<AT>* hdr,
                                                     const pmm::Block<AT>* blk )
{
    using BlockState = pmm::BlockStateBase<AT>;
    if ( pmm::is_free( BlockState::get_node_type( blk ) ) )
        return cached_block_total_granules<AT>( blk );
    return physical_block_total_granules<AT>( base, hdr, blk );
}
template <typename AT> inline void* resolve_granule_ptr( uint8_t* base, typename AT::index_type idx ) noexcept
{
    return ( idx == static_cast<typename AT::index_type>( 0 ) ) ? nullptr
                                                                : base + static_cast<size_t>( idx ) * AT::granule_size;
}
template <typename AT>
inline typename AT::index_type ptr_to_granule_idx( const uint8_t* base, const void* ptr ) noexcept
{
    return static_cast<typename AT::index_type>( ( static_cast<const uint8_t*>( ptr ) - base ) / AT::granule_size );
}
template <typename AT = pmm::DefaultAddressTraits> inline void* user_ptr( pmm::Block<AT>* block )
{
    return reinterpret_cast<uint8_t*>( block ) + sizeof( pmm::Block<AT> );
}
template <typename AT>
inline bool is_block_header_linked_in_canonical_chain( const uint8_t* base, const ManagerHeader<AT>* hdr,
                                                       size_t total_size, typename AT::index_type cand_idx ) noexcept
{
    using BlockState = pmm::BlockStateBase<AT>;
    using IndexT     = typename AT::index_type;
    if ( base == nullptr || hdr == nullptr )
        return false;
    if ( hdr->block_count == 0 || hdr->first_block_offset == AT::no_block )
        return false;
    if ( !validate_block_index<AT>( total_size, hdr->first_block_offset ) ||
         !validate_block_index<AT>( total_size, hdr->last_block_offset ) )
        return false;
    const void*  cand = base + static_cast<size_t>( cand_idx ) * AT::granule_size;
    const IndexT prev = BlockState::get_prev_offset( cand );
    const IndexT next = BlockState::get_next_offset( cand );
    if ( prev == AT::no_block )
    {
        if ( cand_idx != hdr->first_block_offset )
            return false;
    }
    else
    {
        if ( !validate_block_index<AT>( total_size, prev ) || prev >= cand_idx )
            return false;
        const void* prev_block = base + static_cast<size_t>( prev ) * AT::granule_size;
        if ( BlockState::get_next_offset( prev_block ) != cand_idx )
            return false;
    }
    if ( next == AT::no_block )
    {
        if ( cand_idx != hdr->last_block_offset )
            return false;
    }
    else
    {
        if ( !validate_block_index<AT>( total_size, next ) || next <= cand_idx )
            return false;
        const void* next_block = base + static_cast<size_t>( next ) * AT::granule_size;
        if ( BlockState::get_prev_offset( next_block ) != cand_idx )
            return false;
    }
    return true;
}
template <typename AT>
inline bool is_canonical_allocated_block_header( const uint8_t* base, size_t total_size,
                                                 const uint8_t* cand_addr ) noexcept
{
    using BlockState = pmm::BlockStateBase<AT>;
    using IndexT     = typename AT::index_type;
    if ( base == nullptr || cand_addr == nullptr )
        return false;
    const std::uintptr_t base_addr = reinterpret_cast<std::uintptr_t>( base );
    const std::uintptr_t cand_raw  = reinterpret_cast<std::uintptr_t>( cand_addr );
    if ( cand_raw < base_addr )
        return false;
    const size_t cand_off = static_cast<size_t>( cand_raw - base_addr );
    if ( cand_off % AT::granule_size != 0 )
        return false;
    if ( cand_off / AT::granule_size > static_cast<size_t>( std::numeric_limits<IndexT>::max() ) )
        return false;
    const IndexT cand_idx = static_cast<IndexT>( cand_off / AT::granule_size );
    if ( !validate_block_index<AT>( total_size, cand_idx ) )
        return false;
    const IndexT        weight    = BlockState::get_weight( cand_addr );
    const pmm::NodeType node_type = BlockState::get_node_type( cand_addr );
    if ( !pmm::is_allocated( node_type ) || BlockState::get_root_offset( cand_addr ) != cand_idx )
        return false;
    if ( weight == 0 )
        return false;
    if ( total_size < manager_header_offset_bytes_v<AT> + sizeof( ManagerHeader<AT> ) )
        return false;
    const auto* hdr = manager_header_at<AT>( base );
    if ( hdr->total_size != total_size )
        return false;
    if ( !is_block_header_linked_in_canonical_chain<AT>( base, hdr, total_size, cand_idx ) )
        return false;
    constexpr size_t kBlockSize = sizeof( pmm::Block<AT> );
    if ( cand_off > total_size - kBlockSize )
        return false;
    const size_t data_start = cand_off + kBlockSize;
    if ( static_cast<size_t>( weight ) > ( std::numeric_limits<size_t>::max )() / AT::granule_size )
        return false;
    const size_t data_bytes = static_cast<size_t>( weight ) * AT::granule_size;
    if ( data_bytes > total_size - data_start )
        return false;
    return true;
}
template <typename AT>
inline bool is_canonical_user_ptr( const uint8_t* base, size_t total_size, const void* ptr ) noexcept
{
    constexpr size_t kBlockSize      = sizeof( pmm::Block<AT> );
    const size_t     min_user_offset = kBlockSize + sizeof( ManagerHeader<AT> ) + kBlockSize;
    if ( !validate_user_ptr<AT>( base, total_size, ptr, min_user_offset ) )
        return false;
    const auto* raw_ptr   = static_cast<const uint8_t*>( ptr );
    const auto* cand_addr = raw_ptr - kBlockSize;
    if ( cand_addr + kBlockSize != raw_ptr )
        return false;
    return is_canonical_allocated_block_header<AT>( base, total_size, cand_addr );
}
template <typename AT> inline pmm::Block<AT>* header_from_ptr_t( uint8_t* base, void* ptr, size_t total_size )
{
    static constexpr size_t kBlockSize = sizeof( pmm::Block<AT> );
    if ( !is_canonical_user_ptr<AT>( base, total_size, ptr ) )
        return nullptr;
    uint8_t* cand_addr = static_cast<uint8_t*>( ptr ) - kBlockSize;
    return reinterpret_cast<pmm::Block<AT>*>( cand_addr );
}
}
}
