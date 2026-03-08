/**
 * @file test_reallocate.cpp
 * @brief Tests for manual reallocation pattern (Issue #67, updated #102 — new API)
 *
 * Issue #102: reallocate_typed() was removed from the new API.
 *   The new pattern is: allocate_new → copy_old_data → deallocate_old.
 *   These tests verify the correctness of the manual realloc pattern
 *   using AbstractPersistMemoryManager via pmm_presets.h.
 */

#include "pmm/pmm_presets.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define PMM_TEST( expr )                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( expr ) )                                                                                               \
        {                                                                                                              \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " << #expr << "\n";                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while ( false )

#define PMM_RUN( name, fn )                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "  " << name << " ... ";                                                                          \
        if ( fn() )                                                                                                    \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

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
static bool test_alloc_dealloc_basic()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( p );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

/// Allocate zero-count is undefined; allocate count=1 succeeds.
static bool test_alloc_count_one()
{
    Mgr pmm;
    PMM_TEST( pmm.create( 64 * 1024 ) );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 1 );
    PMM_TEST( !p.is_null() );

    pmm.deallocate_typed( p );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

// ─── Manual grow: data preservation ────────────────────────────────────────────

/// Growing a block manually preserves existing bytes.
static bool test_manual_grow_preserves_data()
{
    const std::size_t size = 256 * 1024;
    Mgr               pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );
    for ( std::size_t i = 0; i < 128; ++i )
        p.resolve()[i] = static_cast<std::uint8_t>( i & 0xFF );

    Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, p, 128, 512 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm.is_initialized() );

    for ( std::size_t i = 0; i < 128; ++i )
        PMM_TEST( p2.resolve()[i] == static_cast<std::uint8_t>( i & 0xFF ) );

    pmm.deallocate_typed( p2 );
    pmm.destroy();
    return true;
}

/// Multiple grow steps, each preserving previously written data.
static bool test_manual_repeated_grow()
{
    const std::size_t size = 512 * 1024;
    Mgr               pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );
    std::memset( p.resolve(), 0xAB, 64 );

    std::size_t prev_count = 64;
    std::size_t counts[]   = { 128, 256, 512, 1024 };
    for ( std::size_t new_count : counts )
    {
        Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, p, prev_count, new_count );
        PMM_TEST( !p2.is_null() );
        // First 64 bytes must still be 0xAB
        for ( std::size_t i = 0; i < 64; ++i )
            PMM_TEST( p2.resolve()[i] == 0xAB );
        PMM_TEST( pmm.is_initialized() );
        p          = p2;
        prev_count = new_count;
    }

    pmm.deallocate_typed( p );
    pmm.destroy();
    return true;
}

// ─── Memory management correctness ───────────────────────────────────────────

/// Old block is freed after manual reallocation — free size recovers.
static bool test_manual_grow_frees_old_block()
{
    const std::size_t size = 256 * 1024;
    Mgr               pmm;
    PMM_TEST( pmm.create( size ) );

    std::size_t free_before = pmm.free_size();

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p.is_null() );

    Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, p, 256, 512 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( p2 );
    // After freeing the new block, free size should approximately match initial
    PMM_TEST( pmm.free_size() >= free_before - pmm::config::kDefaultGrowDenominator * 4 );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

/// Pointers returned by manual realloc are distinct from old pointer when grown.
static bool test_manual_grow_new_ptr_distinct()
{
    const std::size_t size = 256 * 1024;
    Mgr               pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );
    std::uint32_t old_offset = p.offset();

    Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, p, 64, 4096 );
    PMM_TEST( !p2.is_null() );
    // When size grows significantly, the offset typically changes
    // (the old block is freed and a new one allocated)
    PMM_TEST( p2.offset() != old_offset );
    PMM_TEST( pmm.is_initialized() );

    pmm.deallocate_typed( p2 );
    pmm.destroy();
    return true;
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
static bool test_manual_grow_triggers_expand_preserves_data()
{
    const std::size_t initial_size = 8 * 1024;
    Mgr               pmm;
    PMM_TEST( pmm.create( initial_size ) );

    // Allocate the "to-be-reallocated" block with a distinctive pattern.
    const std::size_t       orig_count = 64;
    Mgr::pptr<std::uint8_t> p          = pmm.allocate_typed<std::uint8_t>( orig_count );
    PMM_TEST( !p.is_null() );
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
    PMM_TEST( total_before >= initial_size ); // may have expanded

    // Manual realloc to a large size — auto-expand handles any needed growth
    const std::size_t       new_count = 2 * 1024;
    Mgr::pptr<std::uint8_t> p2        = manual_realloc( pmm, p, orig_count, new_count );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm.is_initialized() );
    PMM_TEST( pmm.total_size() >= total_before ); // size can only grow or stay same

    // The original 64 bytes of pattern 0xAA must be intact.
    for ( std::size_t i = 0; i < orig_count; ++i )
        PMM_TEST( p2.resolve()[i] == 0xAA );

    pmm.deallocate_typed( p2 );
    for ( auto& f : fillers )
        pmm.deallocate_typed( f );
    PMM_TEST( pmm.is_initialized() );

    pmm.destroy();
    return true;
}

/**
 * @brief Verify total_size does NOT grow when manual realloc fits within free space.
 */
static bool test_manual_grow_no_expand_when_space_available()
{
    const std::size_t size = 256 * 1024;
    Mgr               pmm;
    PMM_TEST( pmm.create( size ) );

    std::size_t total_before = pmm.total_size();

    Mgr::pptr<std::uint8_t> p = pmm.allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );
    std::memset( p.resolve(), 0x55, 128 );

    // Grow to 1 KB — still within the 256 KB buffer.
    Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, p, 128, 1024 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm.total_size() == total_before );
    PMM_TEST( pmm.is_initialized() );

    // Data preserved.
    for ( std::size_t i = 0; i < 128; ++i )
        PMM_TEST( p2.resolve()[i] == 0x55 );

    pmm.deallocate_typed( p2 );
    pmm.destroy();
    return true;
}

// ─── Mixed type ───────────────────────────────────────────────────────────────

/// Manual realloc works for non-byte types (e.g. uint32_t).
static bool test_manual_grow_typed_uint32()
{
    const std::size_t size = 128 * 1024;
    Mgr               pmm;
    PMM_TEST( pmm.create( size ) );

    Mgr::pptr<std::uint32_t> p = pmm.allocate_typed<std::uint32_t>( 4 );
    PMM_TEST( !p.is_null() );
    p.resolve()[0] = 0xDEADBEEFU;
    p.resolve()[1] = 0xCAFEBABEU;
    p.resolve()[2] = 0x12345678U;
    p.resolve()[3] = 0xABCDEF01U;

    Mgr::pptr<std::uint32_t> p2 = manual_realloc( pmm, p, 4, 16 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm.is_initialized() );

    PMM_TEST( p2.resolve()[0] == 0xDEADBEEFU );
    PMM_TEST( p2.resolve()[1] == 0xCAFEBABEU );
    PMM_TEST( p2.resolve()[2] == 0x12345678U );
    PMM_TEST( p2.resolve()[3] == 0xABCDEF01U );

    pmm.deallocate_typed( p2 );
    pmm.destroy();
    return true;
}

// ─── Multiple blocks ──────────────────────────────────────────────────────────

/// Manually growing multiple distinct blocks preserves all data independently.
static bool test_manual_grow_multiple_blocks_independent()
{
    const std::size_t size = 512 * 1024;
    Mgr               pmm;
    PMM_TEST( pmm.create( size ) );

    const int               N = 8;
    Mgr::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = pmm.allocate_typed<std::uint8_t>( 64 );
        PMM_TEST( !ptrs[i].is_null() );
        std::memset( ptrs[i].resolve(), static_cast<int>( 0x10 + i ), 64 );
    }

    // Manual realloc each to 256 bytes and verify its own pattern.
    for ( int i = 0; i < N; ++i )
    {
        Mgr::pptr<std::uint8_t> p2 = manual_realloc( pmm, ptrs[i], 64, 256 );
        PMM_TEST( !p2.is_null() );
        for ( std::size_t j = 0; j < 64; ++j )
            PMM_TEST( p2.resolve()[j] == static_cast<std::uint8_t>( 0x10 + i ) );
        ptrs[i] = p2;
    }

    PMM_TEST( pmm.is_initialized() );

    for ( int i = 0; i < N; ++i )
        pmm.deallocate_typed( ptrs[i] );

    PMM_TEST( pmm.alloc_block_count() == 1 ); // Issue #75: BlockHeader_0 always allocated
    pmm.destroy();
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_reallocate (Issue #67, updated #102 — new API) ===\n";
    bool all_passed = true;

    PMM_RUN( "alloc_dealloc_basic", test_alloc_dealloc_basic );
    PMM_RUN( "alloc_count_one", test_alloc_count_one );
    PMM_RUN( "manual_grow_preserves_data", test_manual_grow_preserves_data );
    PMM_RUN( "manual_repeated_grow", test_manual_repeated_grow );
    PMM_RUN( "manual_grow_frees_old_block", test_manual_grow_frees_old_block );
    PMM_RUN( "manual_grow_new_ptr_distinct", test_manual_grow_new_ptr_distinct );
    PMM_RUN( "manual_grow_triggers_expand_preserves_data", test_manual_grow_triggers_expand_preserves_data );
    PMM_RUN( "manual_grow_no_expand_when_space_available", test_manual_grow_no_expand_when_space_available );
    PMM_RUN( "manual_grow_typed_uint32", test_manual_grow_typed_uint32 );
    PMM_RUN( "manual_grow_multiple_blocks_independent", test_manual_grow_multiple_blocks_independent );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
