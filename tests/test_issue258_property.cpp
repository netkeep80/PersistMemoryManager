/**
 * @file test_issue258_property.cpp
 * @brief Property-based / generative tests.
 *
 * Covers test matrix group F: random alloc/dealloc sequences,
 * save/load round-trips, corruption injection, and repeated
 * verify checks.
 *
 * Uses deterministic pseudo-random sequences (fixed seeds) for
 * reproducibility.
 *
 * @see docs/test_matrix.md — F1–F6
 */

#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using AT = pmm::DefaultAddressTraits;

// ─── F1: Random allocate/deallocate, then verify ────────────────────────────

TEST_CASE( "property: random alloc/dealloc then verify", "[issue258][property]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25850>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 256 * 1024 ) );

    std::mt19937 rng( 42 );

    std::vector<typename Mgr::template pptr<std::uint8_t>> live;

    for ( int round = 0; round < 200; ++round )
    {
        std::uniform_int_distribution<int> op_dist( 0, 2 );
        int                                op = op_dist( rng );

        if ( op <= 1 || live.empty() )
        {
            // Allocate
            std::uniform_int_distribution<int> size_dist( 1, 512 );
            int                                sz = size_dist( rng );
            auto                               p  = Mgr::allocate_typed<std::uint8_t>( static_cast<std::size_t>( sz ) );
            if ( !p.is_null() )
            {
                // Write pattern to detect data corruption
                std::uint8_t* data = Mgr::template resolve<std::uint8_t>( p );
                if ( data )
                    std::memset( data, static_cast<std::uint8_t>( round & 0xFF ), static_cast<std::size_t>( sz ) );
                live.push_back( p );
            }
        }
        else
        {
            // Deallocate a random live pointer
            std::uniform_int_distribution<std::size_t> idx_dist( 0, live.size() - 1 );
            std::size_t                                idx = idx_dist( rng );
            Mgr::deallocate_typed( live[idx] );
            live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
        }
    }

    // After all operations, verify should be clean
    pmm::VerifyResult v = Mgr::verify();
    REQUIRE( v.ok );

    // Block count consistency
    REQUIRE( Mgr::block_count() == Mgr::free_block_count() + Mgr::alloc_block_count() );

    Mgr::destroy();
}

// ─── F2: Random alloc/dealloc + save/load round-trip ────────────────────────

TEST_CASE( "property: random alloc/dealloc then save/load round-trip", "[issue258][property]" )
{
    using MgrA = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25851>;
    using MgrB = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25852>;

    const char*       kFile = "test_issue258_prop_rt.dat";
    const std::size_t arena = 256 * 1024;

    REQUIRE( MgrA::create( arena ) );

    std::mt19937 rng( 123 );

    std::vector<std::pair<std::uint32_t, std::size_t>> live_offsets;

    for ( int round = 0; round < 100; ++round )
    {
        std::uniform_int_distribution<int> op_dist( 0, 2 );
        int                                op = op_dist( rng );

        if ( op <= 1 || live_offsets.empty() )
        {
            std::uniform_int_distribution<int> size_dist( 1, 256 );
            std::size_t                        sz = static_cast<std::size_t>( size_dist( rng ) );
            auto                               p  = MgrA::allocate_typed<std::uint8_t>( sz );
            if ( !p.is_null() )
            {
                std::uint8_t* data = MgrA::template resolve<std::uint8_t>( p );
                if ( data )
                    std::memset( data, 0xAB, sz );
                live_offsets.push_back( { p.offset(), sz } );
            }
        }
        else
        {
            std::uniform_int_distribution<std::size_t> idx_dist( 0, live_offsets.size() - 1 );
            std::size_t                                idx = idx_dist( rng );
            typename MgrA::template pptr<std::uint8_t> p( live_offsets[idx].first );
            MgrA::deallocate_typed( p );
            live_offsets.erase( live_offsets.begin() + static_cast<std::ptrdiff_t>( idx ) );
        }
    }

    auto saved_blocks = MgrA::block_count();
    auto saved_alloc  = MgrA::alloc_block_count();

    REQUIRE( pmm::save_manager<MgrA>( kFile ) );
    MgrA::destroy();

    REQUIRE( MgrB::create( arena ) );
    pmm::VerifyResult result;
    REQUIRE( pmm::load_manager_from_file<MgrB>( kFile, result ) );

    REQUIRE( MgrB::block_count() == saved_blocks );
    REQUIRE( MgrB::alloc_block_count() == saved_alloc );

    // Verify data survived
    for ( const auto& [off, sz] : live_offsets )
    {
        typename MgrB::template pptr<std::uint8_t> p( off );
        const std::uint8_t*                        data = MgrB::template resolve<std::uint8_t>( p );
        REQUIRE( data != nullptr );
        for ( std::size_t i = 0; i < sz; ++i )
        {
            REQUIRE( data[i] == 0xAB );
        }
    }

    pmm::VerifyResult post = MgrB::verify();
    REQUIRE( post.ok );

    MgrB::destroy();
    std::remove( kFile );
}

// ─── F3: Random corruption injection + verify ───────────────────────────────

TEST_CASE( "property: random corruption injection detected by verify", "[issue258][property]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25853>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    // Allocate several blocks
    std::vector<typename Mgr::template pptr<std::uint32_t>> ptrs;
    for ( int i = 0; i < 5; ++i )
    {
        auto p = Mgr::allocate_typed<std::uint32_t>( 8 + i * 4 );
        REQUIRE( !p.is_null() );
        ptrs.push_back( p );
    }

    // Verify clean before corruption
    pmm::VerifyResult v_clean = Mgr::verify();
    REQUIRE( v_clean.ok );

    std::mt19937 rng( 777 );

    // Inject random corruptions: corrupt root_offset of random blocks
    std::uint8_t* base = Mgr::backend().base_ptr();

    struct Saved
    {
        void*          blk_raw;
        AT::index_type orig;
    };
    std::vector<Saved> saved;

    std::uniform_int_distribution<std::size_t> idx_dist( 0, ptrs.size() - 1 );
    for ( int i = 0; i < 3; ++i )
    {
        std::size_t idx     = idx_dist( rng );
        std::size_t usr_off = static_cast<std::size_t>( ptrs[idx].offset() ) * AT::granule_size;
        void*       blk_raw = base + usr_off - sizeof( pmm::Block<AT> );

        auto                                          orig = pmm::BlockStateBase<AT>::get_root_offset( blk_raw );
        std::uniform_int_distribution<AT::index_type> val_dist( 1, 1000 );
        pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, orig + val_dist( rng ) );
        saved.push_back( { blk_raw, orig } );
    }

    // Verify should detect corruptions
    pmm::VerifyResult v_corrupt = Mgr::verify();
    REQUIRE_FALSE( v_corrupt.ok );
    REQUIRE( v_corrupt.violation_count > 0 );

    // Restore
    for ( auto& s : saved )
        pmm::BlockStateBase<AT>::set_root_offset_of( s.blk_raw, s.orig );

    Mgr::destroy();
}

// ─── F4: Repeated verify after random operations ────────────────────────────

TEST_CASE( "property: repeated verify after random operations", "[issue258][property]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25854>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 128 * 1024 ) );

    std::mt19937 rng( 999 );

    std::vector<typename Mgr::template pptr<std::uint8_t>> live;

    for ( int cycle = 0; cycle < 5; ++cycle )
    {
        // Do some operations
        for ( int op = 0; op < 20; ++op )
        {
            std::uniform_int_distribution<int> action( 0, 2 );
            if ( action( rng ) <= 1 || live.empty() )
            {
                std::uniform_int_distribution<int> sz_dist( 16, 256 );
                auto p = Mgr::allocate_typed<std::uint8_t>( static_cast<std::size_t>( sz_dist( rng ) ) );
                if ( !p.is_null() )
                    live.push_back( p );
            }
            else
            {
                std::uniform_int_distribution<std::size_t> idx_dist( 0, live.size() - 1 );
                std::size_t                                idx = idx_dist( rng );
                Mgr::deallocate_typed( live[idx] );
                live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
            }
        }

        // Verify after each cycle
        pmm::VerifyResult v = Mgr::verify();
        REQUIRE( v.ok );
    }

    Mgr::destroy();
}

// ─── F5: Alloc/dealloc mixed with pstringview operations ────────────────────

TEST_CASE( "property: mixed alloc/dealloc and pstringview ops", "[issue258][property]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25855>;

    Mgr::destroy();
    REQUIRE( Mgr::create( 128 * 1024 ) );

    std::mt19937 rng( 555 );

    std::vector<typename Mgr::template pptr<std::uint8_t>> live;

    for ( int round = 0; round < 50; ++round )
    {
        std::uniform_int_distribution<int> op_dist( 0, 3 );
        int                                op = op_dist( rng );

        if ( op == 0 )
        {
            // Allocate raw block
            std::uniform_int_distribution<int> sz_dist( 8, 128 );
            auto p = Mgr::allocate_typed<std::uint8_t>( static_cast<std::size_t>( sz_dist( rng ) ) );
            if ( !p.is_null() )
                live.push_back( p );
        }
        else if ( op == 1 && !live.empty() )
        {
            // Deallocate
            std::uniform_int_distribution<std::size_t> idx_dist( 0, live.size() - 1 );
            std::size_t                                idx = idx_dist( rng );
            Mgr::deallocate_typed( live[idx] );
            live.erase( live.begin() + static_cast<std::ptrdiff_t>( idx ) );
        }
        else
        {
            // Intern a string
            std::string                 s   = "prop_test_" + std::to_string( round );
            Mgr::pptr<Mgr::pstringview> psv = Mgr::pstringview( s.c_str() );
            // pstringview may fail if OOM, that's OK
            (void)psv;
        }
    }

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE( v.ok );

    Mgr::destroy();
}

// ─── F6: Multiple reload cycles with operations between ─────────────────────

TEST_CASE( "property: multiple reload cycles with operations between", "[issue258][property]" )
{
    using MgrC = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25856>;
    using MgrD = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25857>;

    const char*       kFile = "test_issue258_prop_multi_reload.dat";
    const std::size_t arena = 128 * 1024;

    std::mt19937 rng( 314 );

    REQUIRE( MgrC::create( arena ) );

    for ( int cycle = 0; cycle < 3; ++cycle )
    {
        // Allocate a few blocks
        for ( int i = 0; i < 5; ++i )
        {
            std::uniform_int_distribution<int> sz_dist( 16, 128 );
            auto p = MgrC::allocate_typed<std::uint8_t>( static_cast<std::size_t>( sz_dist( rng ) ) );
            // Ignore OOM
        }

        // Verify clean before save
        pmm::VerifyResult v = MgrC::verify();
        REQUIRE( v.ok );

        // Save
        REQUIRE( pmm::save_manager<MgrC>( kFile ) );
        MgrC::destroy();

        // Load into MgrD
        REQUIRE( MgrD::create( arena ) );
        pmm::VerifyResult load_result;
        REQUIRE( pmm::load_manager_from_file<MgrD>( kFile, load_result ) );

        // Verify clean after load
        pmm::VerifyResult post = MgrD::verify();
        REQUIRE( post.ok );

        // Copy state from MgrD to MgrC for next cycle
        REQUIRE( MgrC::create( arena ) );
        std::memcpy( MgrC::backend().base_ptr(), MgrD::backend().base_ptr(), arena );
        pmm::VerifyResult copy_result;
        MgrC::load( copy_result );
        MgrD::destroy();
    }

    MgrC::destroy();
    std::remove( kFile );
}
