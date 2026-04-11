/**
 * @file pmm/validation.h
 * @brief Unified pointer and block validation layer for PersistMemoryManager.
 *
 * Centralizes all low-level validation checks that guard raw pointer → block
 * transitions. Provides two levels:
 *   - cheap (fast-path): O(1) checks for normal API operations
 *   - full (verify-level): structural consistency checks for verify/repair
 *
 * All conversion paths (granule index → Block*, user pointer → Block*,
 * pptr → T*, etc.) should route through these helpers.
 *
 * @see docs/validation_model.md — specification
 * @see types.h — block_at, header_from_ptr_t (callers of validation)
 * @version 1.0
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/diagnostics.h"
#include "pmm/tree_node.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace pmm
{
namespace detail
{

// ─── Cheap (fast-path) validation ────────────────────────────────────────────

/**
 * @brief Validate that a granule index refers to a location within the managed image
 *        where a complete Block header fits.
 *
 * @tparam AT Address traits type.
 * @param total_size Total size of the managed area in bytes.
 * @param idx        Granule index to validate.
 * @return true if idx is valid (not no_block, and block header fits within image).
 */
template <typename AT> inline bool validate_block_index( std::size_t total_size, typename AT::index_type idx ) noexcept
{
    if ( idx == AT::no_block )
        return false;
    std::size_t byte_off = static_cast<std::size_t>( idx ) * AT::granule_size;
    // Overflow check: if idx * granule_size wrapped around, byte_off < idx for large idx.
    if ( idx != 0 && byte_off / AT::granule_size != static_cast<std::size_t>( idx ) )
        return false;
    if ( byte_off + sizeof( pmm::Block<AT> ) > total_size )
        return false;
    return true;
}

/**
 * @brief Validate that a user-data pointer is within the managed image,
 *        granule-aligned, and past the minimum address for user data.
 *
 * Does NOT check whether the block is actually allocated (no weight check).
 * Use header_from_ptr_t() for the full user-ptr → Block* path.
 *
 * @tparam AT Address traits type.
 * @param base            Base pointer of the managed area.
 * @param total_size      Total size of the managed area in bytes.
 * @param ptr             User-data pointer to validate.
 * @param min_user_offset Minimum byte offset from base for the first valid user-data address.
 *                        Typically: sizeof(Block<AT>) + sizeof(ManagerHeader<AT>) + sizeof(Block<AT>).
 * @return true if ptr passes all cheap checks.
 */
template <typename AT>
inline bool validate_user_ptr( const std::uint8_t* base, std::size_t total_size, const void* ptr,
                               std::size_t min_user_offset ) noexcept
{
    if ( ptr == nullptr || base == nullptr )
        return false;
    const auto* raw_ptr = static_cast<const std::uint8_t*>( ptr );
    // Must be within managed area.
    if ( raw_ptr < base || raw_ptr >= base + total_size )
        return false;
    // Must be past minimum user-data address.
    if ( static_cast<std::size_t>( raw_ptr - base ) < min_user_offset )
        return false;
    // The block header candidate must be granule-aligned relative to base.
    static constexpr std::size_t kBlockSize = sizeof( pmm::Block<AT> );
    std::size_t                  cand_off   = static_cast<std::size_t>( raw_ptr - base ) - kBlockSize;
    if ( cand_off % AT::granule_size != 0 )
        return false;
    return true;
}

/**
 * @brief Validate a granule index used in linked-list or AVL-tree traversal.
 *
 * Checks the index is either no_block (valid sentinel) or a valid in-range index.
 * Useful for validating next_offset, prev_offset, left_offset, right_offset, etc.
 *
 * @tparam AT Address traits type.
 * @param total_size Total size of the managed area in bytes.
 * @param idx        Granule index to validate (may be no_block).
 * @return true if idx is no_block or a valid in-range index.
 */
template <typename AT> inline bool validate_link_index( std::size_t total_size, typename AT::index_type idx ) noexcept
{
    if ( idx == AT::no_block )
        return true; // Sentinel is always valid.
    return validate_block_index<AT>( total_size, idx );
}

// ─── Full (verify-level) validation ──────────────────────────────────────────

/**
 * @brief Full verify-level validation of a block header's structural integrity.
 *
 * Checks:
 *   - weight/root_offset consistency (free vs allocated invariant)
 *   - prev_offset and next_offset are valid link indices
 *   - node_type is a known value (kNodeReadWrite or kNodeReadOnly)
 *   - Block data range does not exceed image boundary
 *
 * Reports violations into the VerifyResult. Does not modify the image.
 *
 * @tparam AT Address traits type.
 * @param base       Base pointer of the managed area.
 * @param total_size Total size of the managed area in bytes.
 * @param idx        Granule index of the block being validated.
 * @param result     Diagnostic result to append violations to.
 */
template <typename AT>
inline void validate_block_header_full( const std::uint8_t* base, std::size_t total_size, typename AT::index_type idx,
                                        VerifyResult& result ) noexcept
{
    using BlockState = pmm::BlockStateBase<AT>;
    using index_type = typename AT::index_type;

    if ( !validate_block_index<AT>( total_size, idx ) )
    {
        result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                    static_cast<std::uint64_t>( idx ), 0, 0 );
        return;
    }

    const void* blk_raw = base + static_cast<std::size_t>( idx ) * AT::granule_size;

    // 1. Weight / root_offset consistency (delegates to BlockState::verify_state).
    BlockState::verify_state( blk_raw, idx, result );

    // 2. next_offset within bounds.
    index_type next = BlockState::get_next_offset( blk_raw );
    if ( next != AT::no_block && !validate_block_index<AT>( total_size, next ) )
    {
        result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                    static_cast<std::uint64_t>( idx ), static_cast<std::uint64_t>( AT::no_block ),
                    static_cast<std::uint64_t>( next ) );
    }

    // 3. prev_offset within bounds.
    index_type prev = BlockState::get_prev_offset( blk_raw );
    if ( prev != AT::no_block && !validate_block_index<AT>( total_size, prev ) )
    {
        result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                    static_cast<std::uint64_t>( idx ), static_cast<std::uint64_t>( AT::no_block ),
                    static_cast<std::uint64_t>( prev ) );
    }

    // 4. node_type is a known value.
    std::uint16_t nt = BlockState::get_node_type( blk_raw );
    if ( nt != pmm::kNodeReadWrite && nt != pmm::kNodeReadOnly )
    {
        result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                    static_cast<std::uint64_t>( idx ), 0, static_cast<std::uint64_t>( nt ) );
    }

    // 5. If allocated, verify block data range fits in image.
    index_type w = BlockState::get_weight( blk_raw );
    if ( w > 0 )
    {
        static constexpr std::size_t kBlkHdrBytes = sizeof( pmm::Block<AT> );
        std::size_t                  blk_byte_off = static_cast<std::size_t>( idx ) * AT::granule_size;
        std::size_t                  data_bytes   = static_cast<std::size_t>( w ) * AT::granule_size;
        if ( blk_byte_off + kBlkHdrBytes + data_bytes > total_size )
        {
            result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( idx ), total_size,
                        static_cast<std::uint64_t>( blk_byte_off + kBlkHdrBytes + data_bytes ) );
        }
    }
}

} // namespace detail
} // namespace pmm
