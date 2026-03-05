/**
 * @file test_reallocate.cpp
 * @brief Comprehensive tests for reallocate_typed (Issue #67)
 *
 * Issue #67 requirements:
 *   1. Code review: reallocate_typed must not invalidate old_raw via PAP expand.
 *   2. After the fix: old data is re-read from the current (possibly expanded)
 *      base pointer so the memcpy always reads from the live buffer.
 *   3. Good tests for reallocate_typed covering edge cases and the expand path.
 */

#include "pmm/legacy_manager.h"

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

// ─── Basic edge cases ─────────────────────────────────────────────────────────

/// reallocate_typed(null, n) behaves like allocate_typed(n).
static bool test_reallocate_null_ptr()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> null_p;
    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::reallocate_typed( null_p, 128 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( p.get() != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/// reallocate_typed(p, 0) frees the block and returns null.
static bool test_reallocate_to_zero()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p.is_null() );

    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, 0 );
    PMM_TEST( p2.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/// reallocate_typed with count that fits in existing block returns same pptr.
static bool test_reallocate_same_size_returns_original()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p.is_null() );

    // Same count — must return the same pptr unchanged.
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, 256 );
    PMM_TEST( p2.offset() == p.offset() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/// reallocate_typed with smaller count returns same pptr (no shrink).
static bool test_reallocate_smaller_returns_original()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 512 );
    PMM_TEST( !p.is_null() );

    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, 128 );
    PMM_TEST( p2.offset() == p.offset() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── Data preservation ────────────────────────────────────────────────────────

/// Growing a block preserves existing bytes.
static bool test_reallocate_grow_preserves_data()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );
    for ( std::size_t i = 0; i < 128; ++i )
        p.get()[i] = static_cast<std::uint8_t>( i & 0xFF );

    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, 512 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( std::size_t i = 0; i < 128; ++i )
        PMM_TEST( p2.get()[i] == static_cast<std::uint8_t>( i & 0xFF ) );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/// Multiple grow steps each preserving the previously-written data.
static bool test_reallocate_repeated_grow()
{
    const std::size_t size = 512 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );
    std::memset( p.get(), 0xAB, 64 );

    std::size_t counts[] = { 128, 256, 512, 1024 };
    for ( std::size_t cnt : counts )
    {
        pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, cnt );
        PMM_TEST( !p2.is_null() );
        // First 64 bytes must still be 0xAB
        for ( std::size_t i = 0; i < 64; ++i )
            PMM_TEST( p2.get()[i] == 0xAB );
        PMM_TEST( pmm::PersistMemoryManager<>::validate() );
        p = p2;
    }

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── Memory management correctness ───────────────────────────────────────────

/// Old block is freed after successful reallocation.
static bool test_reallocate_frees_old_block()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    std::size_t free_before = pmm::PersistMemoryManager<>::free_size();

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 256 );
    PMM_TEST( !p.is_null() );

    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, 512 );
    PMM_TEST( !p2.is_null() );
    // After grow: used = old + new - old = new (coalesced), free recovers old block
    // At minimum the free size should not have grown beyond original (old block freed)
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    // After freeing the new block, free size should approximately match initial
    PMM_TEST( pmm::PersistMemoryManager<>::free_size() >= free_before - pmm::kGranuleSize * 4 );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/// Pointer returned by reallocate_typed is distinct from old pointer when grown.
static bool test_reallocate_new_ptr_distinct()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
    PMM_TEST( !p.is_null() );

    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, 4096 );
    PMM_TEST( !p2.is_null() );
    // When size grows the offset must change (new allocation)
    PMM_TEST( p2.offset() != p.offset() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── Auto-expand path (Issue #67 core fix) ───────────────────────────────────

/**
 * @brief Trigger auto-expand inside reallocate_typed and verify data integrity.
 *
 * Strategy:
 *   1. Create a small PMM (8 KB initial buffer).
 *   2. Allocate a block and fill it with a known pattern.
 *   3. Fill most of remaining memory so the next allocation will trigger expand.
 *   4. reallocate_typed to a size that forces a new allocation AND expand.
 *   5. Verify the original data pattern is intact in the new block.
 *   6. Verify PMM state is consistent (validate()).
 *
 * Before the Issue #67 fix: memcpy read from stale old_raw (old buffer).
 * After the fix: old_raw is re-derived from the current (expanded) base.
 */
static bool test_reallocate_triggers_expand_preserves_data()
{
    const std::size_t initial_size = 8 * 1024;
    void*             mem          = std::malloc( initial_size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, initial_size ) );

    // Allocate the "to-be-reallocated" block with a distinctive pattern.
    const std::size_t       orig_count = 64;
    pmm::pptr<std::uint8_t> p          = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( orig_count );
    PMM_TEST( !p.is_null() );
    for ( std::size_t i = 0; i < orig_count; ++i )
        p.get()[i] = static_cast<std::uint8_t>( 0xAA );

    // Fill the rest of the initial buffer so the next (large) allocation triggers expand.
    std::vector<pmm::pptr<std::uint8_t>> fillers;
    const std::size_t                    fill_sz = 256;
    while ( true )
    {
        if ( pmm::PersistMemoryManager<>::total_size() != initial_size )
        {
            // Already expanded; break to avoid runaway filling.
            break;
        }
        pmm::pptr<std::uint8_t> f = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( fill_sz );
        if ( f.is_null() )
            break;
        fillers.push_back( f );
    }

    std::size_t total_before = pmm::PersistMemoryManager<>::total_size();

    // Reallocate 'p' to a large count that won't fit in the current free space,
    // forcing allocate_typed to call expand() internally.
    const std::size_t       new_count = 2 * 1024; // 2 KB — definitely needs expand
    pmm::pptr<std::uint8_t> p2        = pmm::PersistMemoryManager<>::reallocate_typed( p, new_count );
    PMM_TEST( !p2.is_null() );

    // The PMM must have expanded to accommodate the new block.
    PMM_TEST( pmm::PersistMemoryManager<>::total_size() > total_before );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    // The original 64 bytes of pattern 0xAA must be intact.
    for ( std::size_t i = 0; i < orig_count; ++i )
        PMM_TEST( p2.get()[i] == 0xAA );

    // Cleanup
    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    for ( auto& f : fillers )
        pmm::PersistMemoryManager<>::deallocate_typed( f );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    // Note: after expand, destroy() frees the expanded buffer.
    return true;
}

/**
 * @brief Verify total_size does NOT grow when reallocate fits within free space.
 *
 * If there is sufficient free space, reallocate_typed must reuse existing
 * memory without calling expand().
 */
static bool test_reallocate_no_expand_when_space_available()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    std::size_t total_before = pmm::PersistMemoryManager<>::total_size();

    pmm::pptr<std::uint8_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );
    std::memset( p.get(), 0x55, 128 );

    // Grow to 1 KB — still within the 256 KB buffer.
    pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, 1024 );
    PMM_TEST( !p2.is_null() );

    // No expand should have occurred.
    PMM_TEST( pmm::PersistMemoryManager<>::total_size() == total_before );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    // Data preserved.
    for ( std::size_t i = 0; i < 128; ++i )
        PMM_TEST( p2.get()[i] == 0x55 );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── Mixed type ───────────────────────────────────────────────────────────────

/// reallocate_typed works for non-byte types (e.g. uint32_t).
static bool test_reallocate_typed_uint32()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<std::uint32_t> p = pmm::PersistMemoryManager<>::allocate_typed<std::uint32_t>( 4 );
    PMM_TEST( !p.is_null() );
    p.get()[0] = 0xDEADBEEFU;
    p.get()[1] = 0xCAFEBABEU;
    p.get()[2] = 0x12345678U;
    p.get()[3] = 0xABCDEF01U;

    pmm::pptr<std::uint32_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( p, 16 );
    PMM_TEST( !p2.is_null() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    PMM_TEST( p2.get()[0] == 0xDEADBEEFU );
    PMM_TEST( p2.get()[1] == 0xCAFEBABEU );
    PMM_TEST( p2.get()[2] == 0x12345678U );
    PMM_TEST( p2.get()[3] == 0xABCDEF01U );

    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── Multiple blocks ──────────────────────────────────────────────────────────

/// Reallocating multiple distinct blocks preserves all data independently.
static bool test_reallocate_multiple_blocks_independent()
{
    const std::size_t size = 512 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    const int               N = 8;
    pmm::pptr<std::uint8_t> ptrs[N];
    for ( int i = 0; i < N; ++i )
    {
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 64 );
        PMM_TEST( !ptrs[i].is_null() );
        std::memset( ptrs[i].get(), static_cast<int>( 0x10 + i ), 64 );
    }

    // Reallocate each to 256 bytes and verify its own pattern.
    for ( int i = 0; i < N; ++i )
    {
        pmm::pptr<std::uint8_t> p2 = pmm::PersistMemoryManager<>::reallocate_typed( ptrs[i], 256 );
        PMM_TEST( !p2.is_null() );
        for ( std::size_t j = 0; j < 64; ++j )
            PMM_TEST( p2.get()[j] == static_cast<std::uint8_t>( 0x10 + i ) );
        ptrs[i] = p2;
    }

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    for ( int i = 0; i < N; ++i )
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== test_reallocate (Issue #67) ===\n";
    bool all_passed = true;

    PMM_RUN( "null_ptr", test_reallocate_null_ptr );
    PMM_RUN( "to_zero", test_reallocate_to_zero );
    PMM_RUN( "same_size_returns_original", test_reallocate_same_size_returns_original );
    PMM_RUN( "smaller_returns_original", test_reallocate_smaller_returns_original );
    PMM_RUN( "grow_preserves_data", test_reallocate_grow_preserves_data );
    PMM_RUN( "repeated_grow", test_reallocate_repeated_grow );
    PMM_RUN( "frees_old_block", test_reallocate_frees_old_block );
    PMM_RUN( "new_ptr_distinct", test_reallocate_new_ptr_distinct );
    PMM_RUN( "triggers_expand_preserves_data", test_reallocate_triggers_expand_preserves_data );
    PMM_RUN( "no_expand_when_space_available", test_reallocate_no_expand_when_space_available );
    PMM_RUN( "typed_uint32", test_reallocate_typed_uint32 );
    PMM_RUN( "multiple_blocks_independent", test_reallocate_multiple_blocks_independent );

    std::cout << ( all_passed ? "\nAll tests PASSED\n" : "\nSome tests FAILED\n" );
    return all_passed ? 0 : 1;
}
