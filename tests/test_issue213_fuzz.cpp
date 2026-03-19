/**
 * @file test_issue213_fuzz.cpp
 * @brief Fuzz-style testing of the allocator (Issue #213, Phase 5.2).
 *
 * Uses deterministic pseudo-random sequences to exercise the allocator
 * with diverse operation patterns. This approach catches bugs that
 * simple unit tests miss by exploring a wider state space.
 *
 * Each test case runs thousands of random allocate/deallocate/reallocate
 * operations and verifies structural integrity after each round.
 *
 * @note This file provides deterministic reproducible fuzz tests via Catch2.
 *       For true coverage-guided fuzzing, a libFuzzer harness is provided
 *       in tests/fuzz_allocator.cpp (built separately with -fsanitize=fuzzer).
 *
 * @see docs/phase5_testing.md §5.2
 * @version 0.1 (Issue #213 — Phase 5.2: Extended test coverage)
 */

#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// ─── PRNG utility ────────────────────────────────────────────────────────────

/// Simple LCG PRNG for deterministic test sequences.
struct Lcg
{
    std::uint32_t state;
    explicit Lcg( std::uint32_t seed ) : state( seed ) {}
    std::uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    /// Return a value in [lo, hi] inclusive.
    std::uint32_t range( std::uint32_t lo, std::uint32_t hi ) { return lo + ( next() >> 16 ) % ( hi - lo + 1 ); }
};

// ─── Manager aliases ─────────────────────────────────────────────────────────

/// 32-bit, no auto-expand — stresses allocator under fixed-size constraints.
using StaticM = pmm::PersistMemoryManager<pmm::EmbeddedStaticConfig<65536>, 21310>;

/// 32-bit, heap with auto-expand.
using HeapM = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 21311>;

/// 16-bit index, small static buffer.
using SmallM = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<8192>, 21312>;

// =============================================================================
// Fuzz: random alloc/dealloc on static storage
// =============================================================================

TEST_CASE( "fuzz: random alloc/dealloc on static storage", "[test_issue213_fuzz]" )
{
    StaticM::destroy();
    REQUIRE( StaticM::create() );

    struct LiveBlock
    {
        void*        ptr;
        std::size_t  size;
        std::uint8_t pattern;
    };

    Lcg                    rng( 12345 );
    std::vector<LiveBlock> live;
    live.reserve( 256 );
    constexpr int kIterations = 5000;

    for ( int i = 0; i < kIterations; ++i )
    {
        std::uint32_t action = rng.next();

        if ( live.empty() || ( action & 3 ) != 0 )
        {
            // Allocate with random size: 1..512 bytes.
            std::size_t  sz      = rng.range( 1, 512 );
            std::uint8_t pattern = static_cast<std::uint8_t>( rng.next() & 0xFF );
            void*        p       = StaticM::allocate( sz );
            if ( p != nullptr )
            {
                std::memset( p, pattern, sz );
                live.push_back( { p, sz, pattern } );
            }
        }
        else
        {
            // Free a random block and verify its data first.
            std::size_t idx  = rng.range( 0, static_cast<std::uint32_t>( live.size() - 1 ) );
            auto&       blk  = live[idx];
            auto*       data = static_cast<std::uint8_t*>( blk.ptr );
            for ( std::size_t j = 0; j < blk.size; ++j )
            {
                if ( data[j] != blk.pattern )
                {
                    INFO( "Data corruption at block " << idx << " byte " << j );
                    REQUIRE( data[j] == blk.pattern );
                }
            }
            StaticM::deallocate( blk.ptr );
            live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
        }
    }

    // Verify integrity.
    REQUIRE( StaticM::is_initialized() );

    // Free remaining blocks.
    for ( auto& blk : live )
        StaticM::deallocate( blk.ptr );

    REQUIRE( StaticM::is_initialized() );
    StaticM::destroy();
}

// =============================================================================
// Fuzz: random alloc/dealloc on heap storage
// =============================================================================

TEST_CASE( "fuzz: random alloc/dealloc on heap storage", "[test_issue213_fuzz]" )
{
    HeapM::destroy();
    REQUIRE( HeapM::create( 256 * 1024 ) ); // 256 KB initial

    struct LiveBlock
    {
        void*        ptr;
        std::size_t  size;
        std::uint8_t pattern;
    };

    Lcg                    rng( 67890 );
    std::vector<LiveBlock> live;
    live.reserve( 256 );
    constexpr int         kIterations = 5000;
    constexpr std::size_t kMaxLive    = 200; // Cap live blocks to limit memory.

    for ( int i = 0; i < kIterations; ++i )
    {
        std::uint32_t action       = rng.next() % 10;
        bool          should_alloc = live.empty() || ( action < 5 && live.size() < kMaxLive );

        if ( should_alloc )
        {
            // Varied sizes: small (1-64), medium (65-512), large (513-2048).
            std::size_t sz;
            auto        size_class = rng.next() % 3;
            if ( size_class == 0 )
                sz = rng.range( 1, 64 );
            else if ( size_class == 1 )
                sz = rng.range( 65, 512 );
            else
                sz = rng.range( 513, 2048 );

            std::uint8_t pattern = static_cast<std::uint8_t>( rng.next() & 0xFF );
            void*        p       = HeapM::allocate( sz );
            if ( p != nullptr )
            {
                std::memset( p, pattern, sz );
                live.push_back( { p, sz, pattern } );
            }
        }
        else if ( !live.empty() )
        {
            std::size_t idx  = rng.range( 0, static_cast<std::uint32_t>( live.size() - 1 ) );
            auto&       blk  = live[idx];
            auto*       data = static_cast<std::uint8_t*>( blk.ptr );
            for ( std::size_t j = 0; j < blk.size; ++j )
            {
                if ( data[j] != blk.pattern )
                {
                    INFO( "Data corruption at iteration " << i << " block " << idx << " byte " << j );
                    REQUIRE( data[j] == blk.pattern );
                }
            }
            HeapM::deallocate( blk.ptr );
            live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
        }
    }

    REQUIRE( HeapM::is_initialized() );

    for ( auto& blk : live )
        HeapM::deallocate( blk.ptr );

    REQUIRE( HeapM::is_initialized() );
    HeapM::destroy();
}

// =============================================================================
// Fuzz: mixed alloc/dealloc/reallocate on heap storage
// =============================================================================

TEST_CASE( "fuzz: mixed alloc/dealloc/reallocate", "[test_issue213_fuzz]" )
{
    HeapM::destroy();
    REQUIRE( HeapM::create( 512 * 1024 ) );

    struct LiveBlock
    {
        HeapM::pptr<std::uint8_t> p;
        std::size_t               count;
        std::uint8_t              pattern;
    };

    Lcg                    rng( 54321 );
    std::vector<LiveBlock> live;
    live.reserve( 256 );
    constexpr int         kIterations = 3000;
    constexpr std::size_t kMaxLive    = 150;

    for ( int i = 0; i < kIterations; ++i )
    {
        std::uint32_t action = rng.next() % 10;

        if ( live.empty() || ( action < 4 && live.size() < kMaxLive ) ) // 40% allocate
        {
            std::size_t  count   = rng.range( 1, 256 );
            std::uint8_t pattern = static_cast<std::uint8_t>( rng.next() & 0xFF );
            auto         p       = HeapM::allocate_typed<std::uint8_t>( count );
            if ( !p.is_null() )
            {
                std::memset( p.resolve(), pattern, count );
                live.push_back( { p, count, pattern } );
            }
        }
        else if ( action < 7 && !live.empty() ) // 30% deallocate
        {
            std::size_t idx = rng.range( 0, static_cast<std::uint32_t>( live.size() - 1 ) );
            auto&       blk = live[idx];
            // Verify data.
            auto* data = blk.p.resolve();
            for ( std::size_t j = 0; j < blk.count; ++j )
            {
                if ( data[j] != blk.pattern )
                {
                    INFO( "Data corruption at iteration " << i << " block " << idx );
                    REQUIRE( data[j] == blk.pattern );
                }
            }
            HeapM::deallocate_typed( blk.p );
            live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
        }
        else if ( !live.empty() ) // 30% reallocate
        {
            std::size_t idx       = rng.range( 0, static_cast<std::uint32_t>( live.size() - 1 ) );
            auto&       blk       = live[idx];
            std::size_t new_count = rng.range( 1, 512 );
            auto        new_p     = HeapM::reallocate_typed<std::uint8_t>( blk.p, blk.count, new_count );
            if ( !new_p.is_null() )
            {
                // Verify old data is preserved up to min(old, new).
                std::size_t check_count = std::min( blk.count, new_count );
                auto*       data        = new_p.resolve();
                for ( std::size_t j = 0; j < check_count; ++j )
                {
                    if ( data[j] != blk.pattern )
                    {
                        INFO( "Data corruption after reallocate at iteration " << i );
                        REQUIRE( data[j] == blk.pattern );
                    }
                }
                // Fill new region with same pattern.
                if ( new_count > blk.count )
                    std::memset( data + blk.count, blk.pattern, new_count - blk.count );
                blk.p     = new_p;
                blk.count = new_count;
            }
        }
    }

    REQUIRE( HeapM::is_initialized() );

    for ( auto& blk : live )
        HeapM::deallocate_typed( blk.p );

    REQUIRE( HeapM::is_initialized() );
    HeapM::destroy();
}

// =============================================================================
// Fuzz: small index (16-bit) stress
// =============================================================================

TEST_CASE( "fuzz: 16-bit index stress", "[test_issue213_fuzz]" )
{
    SmallM::destroy();
    REQUIRE( SmallM::create() );

    struct LiveBlock
    {
        void*        ptr;
        std::size_t  size;
        std::uint8_t pattern;
    };

    Lcg                    rng( 99999 );
    std::vector<LiveBlock> live;
    live.reserve( 128 );
    constexpr int kIterations = 2000;

    for ( int i = 0; i < kIterations; ++i )
    {
        std::uint32_t action = rng.next();

        if ( live.empty() || ( action & 3 ) != 0 )
        {
            // Small allocations (1-128 bytes) to stay within 8 KB buffer.
            std::size_t  sz      = rng.range( 1, 128 );
            std::uint8_t pattern = static_cast<std::uint8_t>( rng.next() & 0xFF );
            void*        p       = SmallM::allocate( sz );
            if ( p != nullptr )
            {
                std::memset( p, pattern, sz );
                live.push_back( { p, sz, pattern } );
            }
        }
        else
        {
            std::size_t idx  = rng.range( 0, static_cast<std::uint32_t>( live.size() - 1 ) );
            auto&       blk  = live[idx];
            auto*       data = static_cast<std::uint8_t*>( blk.ptr );
            for ( std::size_t j = 0; j < blk.size; ++j )
            {
                if ( data[j] != blk.pattern )
                {
                    INFO( "Data corruption at iteration " << i << " block " << idx );
                    REQUIRE( data[j] == blk.pattern );
                }
            }
            SmallM::deallocate( blk.ptr );
            live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
        }
    }

    REQUIRE( SmallM::is_initialized() );

    for ( auto& blk : live )
        SmallM::deallocate( blk.ptr );

    REQUIRE( SmallM::is_initialized() );
    SmallM::destroy();
}

// =============================================================================
// Fuzz: fragmentation stress (allocate small, free every other, allocate large)
// =============================================================================

TEST_CASE( "fuzz: fragmentation stress pattern", "[test_issue213_fuzz]" )
{
    HeapM::destroy();
    REQUIRE( HeapM::create( 256 * 1024 ) );

    // Phase 1: Fill with small blocks.
    std::vector<void*> ptrs;
    for ( int i = 0; i < 200; ++i )
    {
        void* p = HeapM::allocate( 64 );
        if ( p == nullptr )
            break;
        std::memset( p, 0xAA, 64 );
        ptrs.push_back( p );
    }
    REQUIRE( !ptrs.empty() );

    // Phase 2: Free every other block (create fragmentation).
    for ( std::size_t i = 0; i < ptrs.size(); i += 2 )
    {
        HeapM::deallocate( ptrs[i] );
        ptrs[i] = nullptr;
    }

    REQUIRE( HeapM::is_initialized() );

    // Phase 3: Allocate medium blocks (should use coalesced free space or expand).
    std::vector<void*> medium_ptrs;
    for ( int i = 0; i < 50; ++i )
    {
        void* p = HeapM::allocate( 128 );
        if ( p != nullptr )
        {
            std::memset( p, 0xBB, 128 );
            medium_ptrs.push_back( p );
        }
    }

    REQUIRE( HeapM::is_initialized() );

    // Phase 4: Verify non-freed blocks still have correct data.
    for ( std::size_t i = 1; i < ptrs.size(); i += 2 )
    {
        auto* data = static_cast<std::uint8_t*>( ptrs[i] );
        for ( int j = 0; j < 64; ++j )
        {
            REQUIRE( data[j] == 0xAA );
        }
    }

    // Cleanup.
    for ( auto* p : ptrs )
        if ( p != nullptr )
            HeapM::deallocate( p );
    for ( auto* p : medium_ptrs )
        HeapM::deallocate( p );

    REQUIRE( HeapM::is_initialized() );
    HeapM::destroy();
}

// =============================================================================
// Fuzz: deterministic seed sweep (multiple seeds for coverage)
// =============================================================================

TEST_CASE( "fuzz: deterministic seed sweep", "[test_issue213_fuzz]" )
{
    constexpr std::uint32_t seeds[] = { 1, 42, 1337, 65536, 999999, 0xDEADBEEF };

    for ( std::uint32_t seed : seeds )
    {
        HeapM::destroy();
        REQUIRE( HeapM::create( 128 * 1024 ) );

        Lcg                rng( seed );
        std::vector<void*> live;
        live.reserve( 128 );

        for ( int i = 0; i < 1000; ++i )
        {
            if ( live.empty() || rng.next() % 3 != 0 )
            {
                std::size_t sz = rng.range( 1, 1024 );
                void*       p  = HeapM::allocate( sz );
                if ( p != nullptr )
                    live.push_back( p );
            }
            else
            {
                std::size_t idx = rng.range( 0, static_cast<std::uint32_t>( live.size() - 1 ) );
                HeapM::deallocate( live[idx] );
                live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
            }
        }

        for ( auto* p : live )
            HeapM::deallocate( p );

        INFO( "Seed " << seed << " failed validation" );
        REQUIRE( HeapM::is_initialized() );

        HeapM::destroy();
    }
}
