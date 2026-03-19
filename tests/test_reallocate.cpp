/**
 * @file test_reallocate.cpp
 * @brief Tests for manual reallocation pattern (Issue #67, updated #102 — new API)
 *
 * Issue #102: reallocate_typed() was removed from the new API.
 *   The new pattern is: allocate_new → copy_old_data → deallocate_old.
 *   These tests verify the correctness of the manual realloc pattern
 *   using AbstractPersistMemoryManager via pmm_presets.h.
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>

#include <vector>

using Mgr = pmm::presets::SingleThreadedHeap;

// Manual realloc helper: allocate new block, copy old data, free old block.
// Returns the new pptr (or null if allocation fails).
template <typename T>
static Mgr::pptr<T> manual_realloc( Mgr& mgr, Mgr::pptr<T> old_p, std::size_t old_count, std::size_t new_count )
{
    Mgr::pptr<T> new_p = mgr.allocate_typed<T>( new_count );
    if ( new_p.is_null() )
        return new_p; // failure
    std::size_t copy_count = old_count < new_count ? old_count : new_count;
    std::memcpy( new_p.resolve(), old_p.resolve(), copy_count * sizeof( T ) );
    mgr.deallocate_typed( old_p );
    return new_p;
}

// ─── Basic allocation/deallocation ────────────────────────────────────────────

/// Allocate and immediately deallocate — basic smoke test.
TEST_CASE( "alloc_dealloc_basic", "[test_reallocate]" )
{
    Mgr pmm;
    REQUIRE( pmm.create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 128 );
    REQUIRE( !p.is_null() );
    REQUIRE( pmm.is_initialized() );

    pmm.deallocate_typed( p );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

/// Allocate zero-count is undefined; allocate count=1 succeeds.
TEST_CASE( "alloc_count_one", "[test_reallocate]" )
{
    Mgr pmm;
    REQUIRE( pmm.create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 1 );
    REQUIRE( !p.is_null() );

    pmm.deallocate_typed( p );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

// ─── Manual grow: data preservation ────────────────────────────────────────────

/// Growing a block manually preserves existing bytes.
TEST_CASE( "manual_grow_preserves_data", "[test_reallocate]" )
{
    const std::size_t size = 256 * 1024;
    Mgr               pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 128 );
    REQUIRE( !p.is_null() );
    for ( std::size_t i = 0; i < 128; ++i )
        p.resolve()[i] = static_cast<std::uint8_t>( i & 0xFF );

    Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, p, 128, 512 );
    REQUIRE( !p2.is_null() );
    REQUIRE( pmm.is_initialized() );

    for ( std::size_t i = 0; i < 128; ++i )
        REQUIRE( p2.resolve()[i] == static_cast<std::uint8_t>( i & 0xFF ) );

    pmm.deallocate_typed( p2 );
    pmm.destroy();
}

/// Multiple grow steps, each preserving previously written data.
TEST_CASE( "manual_repeated_grow", "[test_reallocate]" )
{
    const std::size_t size = 512 * 1024;
    Mgr               pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 64 );
    REQUIRE( !p.is_null() );
    std::memset( p.resolve(), 0xAB, 64 );

    std::size_t prev_count = 64;
    std::size_t counts[]   = { 128, 256, 512, 1024 };
    for ( std::size_t new_count : counts )
    {
        Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, p, prev_count, new_count );
        REQUIRE( !p2.is_null() );
        // First 64 bytes must still be 0xAB
        for ( std::size_t i = 0; i < 64; ++i )
            REQUIRE( p2.resolve()[i] == 0xAB );
        REQUIRE( pmm.is_initialized() );
        p          = p2;
        prev_count = new_count;
    }

    pmm.deallocate_typed( p );
    pmm.destroy();
}

// ─── Memory management correctness ───────────────────────────────────────────

/// Old block is freed after manual reallocation — free size recovers.
TEST_CASE( "manual_grow_frees_old_block", "[test_reallocate]" )
{
    const std::size_t size = 256 * 1024;
    Mgr               pmm;
    REQUIRE( pmm.create( size ) );

    std::size_t free_before = pmm.free_size();

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 256 );
    REQUIRE( !p.is_null() );

    Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, p, 256, 512 );
    REQUIRE( !p2.is_null() );
    REQUIRE( pmm.is_initialized() );

    pmm.deallocate_typed( p2 );
    // After freeing the new block, free size should approximately match initial
    REQUIRE( pmm.free_size() >= free_before - pmm::config::kDefaultGrowDenominator * 4 );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

/// Pointers returned by manual realloc are distinct from old pointer when grown.
TEST_CASE( "manual_grow_new_ptr_distinct", "[test_reallocate]" )
{
    const std::size_t size = 256 * 1024;
    Mgr               pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 64 );
    REQUIRE( !p.is_null() );
    std::uint32_t old_offset = p.offset();

    Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, p, 64, 4096 );
    REQUIRE( !p2.is_null() );
    // When size grows significantly, the offset typically changes
    // (the old block is freed and a new one allocated)
    REQUIRE( p2.offset() != old_offset );
    REQUIRE( pmm.is_initialized() );

    pmm.deallocate_typed( p2 );
    pmm.destroy();
}

// ─── Auto-expand path ─────────────────────────────────────────────────────────

/**
 * @brief Trigger auto-expand via many allocations and verify data integrity after manual_realloc.
 *
 * Strategy:
 *   1. Create a small PMM (8 KB initial buffer).
 *   2. Allocate a block and fill it with a known pattern.
 *   3. Allocate many blocks to exercise the auto-expand path.
 *   4. Manual realloc the original block to a larger size.
 *   5. Verify the original data pattern is intact in the new block.
 *   6. Verify manager is still valid (total_size >= initial_size).
 *
 * Note: HeapStorage auto-expands transparently during allocation.
 * The test verifies that manual_realloc works correctly even after expansion,
 * and that the expanded total_size >= initial_size.
 */
TEST_CASE( "manual_grow_triggers_expand_preserves_data", "[test_reallocate]" )
{
    const std::size_t initial_size = 8 * 1024;
    Mgr               pmm;
    REQUIRE( pmm.create( initial_size ) );

    // Allocate the "to-be-reallocated" block with a distinctive pattern.
    const std::size_t       orig_count = 64;
    Mgr::pptr<std::uint8_t> p          = pmm.allocate_typed<std::uint8_t>( orig_count );
    REQUIRE( !p.is_null() );
    for ( std::size_t i = 0; i < orig_count; ++i )
        p.resolve()[i] = static_cast<std::uint8_t>( 0xAA );

    // Allocate many blocks to fill memory and exercise auto-expand path.
    // Keep allocating until auto-expand triggers (total_size grows).
    std::vector<Mgr::pptr<std::uint8_t>> fillers;
    const std::size_t                    fill_sz   = 256;
    const int                            MAX_FILLS = 200; // safety limit
    for ( int k = 0; k < MAX_FILLS; ++k )
    {
        Mgr::pptr<std::uint8_t> f = pmm.allocate_typed<std::uint8_t>( fill_sz );
        if ( f.is_null() )
            break;
        fillers.push_back( f );
        if ( pmm.total_size() > initial_size )
            break; // auto-expand happened, stop filling
    }

    std::size_t total_before = pmm.total_size();
    REQUIRE( total_before >= initial_size ); // may have expanded

    // Manual realloc to a large size — auto-expand handles any needed growth
    const std::size_t       new_count = 2 * 1024;
    Mgr::pptr<std::uint8_t> p2        = manual_realloc( pmm, p, orig_count, new_count );
    REQUIRE( !p2.is_null() );
    REQUIRE( pmm.is_initialized() );
    REQUIRE( pmm.total_size() >= total_before ); // size can only grow or stay same

    // The original 64 bytes of pattern 0xAA must be intact.
    for ( std::size_t i = 0; i < orig_count; ++i )
        REQUIRE( p2.resolve()[i] == 0xAA );

    pmm.deallocate_typed( p2 );
    for ( auto& f : fillers )
        pmm.deallocate_typed( f );
    REQUIRE( pmm.is_initialized() );

    pmm.destroy();
}

/**
 * @brief Verify total_size does NOT grow when manual realloc fits within free space.
 */
TEST_CASE( "manual_grow_no_expand_when_space_available", "[test_reallocate]" )
{
    const std::size_t size = 256 * 1024;
    Mgr               pmm;
    REQUIRE( pmm.create( size ) );

    std::size_t total_before = pmm.total_size();

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 128 );
    REQUIRE( !p.is_null() );
    std::memset( p.resolve(), 0x55, 128 );

    // Grow to 1 KB — still within the 256 KB buffer.
    Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, p, 128, 1024 );
    REQUIRE( !p2.is_null() );
    REQUIRE( pmm.total_size() == total_before );
    REQUIRE( pmm.is_initialized() );

    // Data preserved.
    for ( std::size_t i = 0; i < 128; ++i )
        REQUIRE( p2.resolve()[i] == 0x55 );

    pmm.deallocate_typed( p2 );
    pmm.destroy();
}

// ─── Mixed type ───────────────────────────────────────────────────────────────

/// Manual realloc works for non-byte types (e.g. uint32_t).
TEST_CASE( "manual_grow_typed_uint32", "[test_reallocate]" )
{
    const std::size_t size = 128 * 1024;
    Mgr               pmm;
    REQUIRE( pmm.create( size ) );

    Mgr::pptr<std::uint32_t> p = pmm.allocate_typed<std::uint32_t>( 4 );
    REQUIRE( !p.is_null() );
    p.resolve()[0] = 0xDEADBEEFU;
    p.resolve()[1] = 0xCAFEBABEU;
    p.resolve()[2] = 0x12345678U;
    p.resolve()[3] = 0xABCDEF01U;

    Mgr::pptr<std::uint32_t> p2 = manual_realloc( pmm, p, 4, 16 );
    REQUIRE( !p2.is_null() );
    REQUIRE( pmm.is_initialized() );

    REQUIRE( p2.resolve()[0] == 0xDEADBEEFU );
    REQUIRE( p2.resolve()[1] == 0xCAFEBABEU );
    REQUIRE( p2.resolve()[2] == 0x12345678U );
    REQUIRE( p2.resolve()[3] == 0xABCDEF01U );

    pmm.deallocate_typed( p2 );
    pmm.destroy();
}

// ─── Multiple blocks ──────────────────────────────────────────────────────────

/// Manually growing multiple distinct blocks preserves all data independently.
TEST_CASE( "manual_grow_multiple_blocks_independent", "[test_reallocate]" )
{
    const std::size_t size = 512 * 1024;
    Mgr               pmm;
    REQUIRE( pmm.create( size ) );

    const int               N = 8;
    Mgr::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( 64 );
        REQUIRE( !ptrs[i].is_null() );
        std::memset( ptrs[i].resolve(), static_cast<int>( 0x10 + i ), 64 );
    }

    // Manual realloc each to 256 bytes and verify its own pattern.
    for ( int i = 0; i < N; ++i )
    {
        Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, ptrs[i], 64, 256 );
        REQUIRE( !p2.is_null() );
        for ( std::size_t j = 0; j < 64; ++j )
            REQUIRE( p2.resolve()[j] == static_cast<std::uint8_t>( 0x10 + i ) );
        ptrs[i] = p2;
    }

    REQUIRE( pmm.is_initialized() );

    for ( int i = 0; i < N; ++i )
        pmm.deallocate_typed( ptrs[i] );

    REQUIRE( pmm.alloc_block_count() == 1 ); // Issue #75: BlockHeader_0 always allocated
    pmm.destroy();
}
