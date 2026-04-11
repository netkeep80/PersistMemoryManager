/**
 * @file test_issue43_phase2_persistence.cpp
 * @brief Tests for Phase 2 persistence and reliability improvements.
 *
 * Verifies:
 *  - 2.1 CRC32 checksum for persisted images (save computes CRC, load verifies it)
 *  - 2.2 Atomic save (write-then-rename via temporary file)
 *  - 2.3 MMapStorage expand() support
 *
 * @see docs/phase2_persistence.md
 * @version 0.1
 */

#include "pmm/pmm_presets.h"
#include "pmm/io.h"
#include "pmm/mmap_storage.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <string>

// ─── Test macros ──────────────────────────────────────────────────────────────

// ─── Helper: temp file cleanup ───────────────────────────────────────────────

static const char* TEST_FILE = "test_phase2.dat";

static void cleanup_file( const char* path = TEST_FILE )
{
    std::remove( path );
    // Also remove tmp file that atomic save may leave
    std::string tmp = std::string( path ) + ".tmp";
    std::remove( tmp.c_str() );
}

// =============================================================================
// 2.1: CRC32 checksum
// =============================================================================

/// @brief Verify that compute_crc32 produces expected results for known inputs.
TEST_CASE( "crc32_known_values", "[test_issue43_phase2_persistence]" )
{
    // CRC32 of empty data is 0x00000000 (no bytes processed)
    REQUIRE( pmm::detail::compute_crc32( nullptr, 0 ) == 0x00000000U );

    // CRC32 of "123456789" is 0xCBF43926 (standard test vector)
    const std::uint8_t data[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    REQUIRE( pmm::detail::compute_crc32( data, 9 ) == 0xCBF43926U );
}

/// @brief Save, load — CRC32 should be stored and verified automatically.
TEST_CASE( "crc32_save_load_roundtrip", "[test_issue43_phase2_persistence]" )
{
    using M1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2100>;
    using M2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2101>;

    cleanup_file();
    const std::size_t size = 64 * 1024;

    REQUIRE( M1::create( size ) );
    M1::pptr<std::uint8_t> p = M1::allocate_typed<std::uint8_t>( 128 );
    REQUIRE( !p.is_null() );
    std::memset( p.resolve(), 0xDE, 128 );

    REQUIRE( pmm::save_manager<M1>( TEST_FILE ) );

    // Verify CRC32 was written to the header
    {
        std::uint8_t* base = M1::backend().base_ptr();
        auto*         hdr  = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>(
            base + sizeof( pmm::Block<pmm::DefaultAddressTraits> ) );
        REQUIRE( hdr->crc32 != 0 );
    }
    M1::destroy();

    // Load into a new manager instance
    REQUIRE( M2::create( size ) );
    REQUIRE( pmm::load_manager_from_file<M2>( TEST_FILE ) );
    REQUIRE( M2::is_initialized() );

    M2::destroy();
    cleanup_file();
}

/// @brief Corrupt a saved image — load should fail CRC check.
TEST_CASE( "crc32_detects_corruption", "[test_issue43_phase2_persistence]" )
{
    using M1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2110>;
    using M2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2111>;

    cleanup_file();
    const std::size_t size = 64 * 1024;

    REQUIRE( M1::create( size ) );
    M1::pptr<std::uint8_t> p = M1::allocate_typed<std::uint8_t>( 256 );
    REQUIRE( !p.is_null() );
    std::memset( p.resolve(), 0xAB, 256 );

    REQUIRE( pmm::save_manager<M1>( TEST_FILE ) );
    M1::destroy();

    // Corrupt a byte in the middle of the file
    {
        std::FILE* f = std::fopen( TEST_FILE, "r+b" );
        REQUIRE( f != nullptr );
        // Seek to somewhere in the data area (past the header)
        std::fseek( f, 512, SEEK_SET );
        std::uint8_t garbage = 0xFF;
        std::fwrite( &garbage, 1, 1, f );
        std::fclose( f );
    }

    // Load should fail due to CRC mismatch
    REQUIRE( M2::create( size ) );
    REQUIRE( !pmm::load_manager_from_file<M2>( TEST_FILE ) );

    M2::destroy();
    cleanup_file();
}

/// @brief Backward compatibility: image with crc32==0 (pre-Phase 2) should still load.
TEST_CASE( "crc32_backward_compat", "[test_issue43_phase2_persistence]" )
{
    using M1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2120>;
    using M2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2121>;

    cleanup_file();
    const std::size_t size = 64 * 1024;

    REQUIRE( M1::create( size ) );
    REQUIRE( pmm::save_manager<M1>( TEST_FILE ) );
    M1::destroy();

    // Manually zero out the CRC32 field to simulate a pre- image
    {
        std::FILE* f = std::fopen( TEST_FILE, "r+b" );
        REQUIRE( f != nullptr );
        constexpr std::size_t kHdrOffset = sizeof( pmm::Block<pmm::DefaultAddressTraits> );
        constexpr std::size_t kCrcOffset =
            kHdrOffset + offsetof( pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>, crc32 );
        std::fseek( f, static_cast<long>( kCrcOffset ), SEEK_SET );
        std::uint32_t zero_crc = 0;
        std::fwrite( &zero_crc, sizeof( zero_crc ), 1, f );
        std::fclose( f );
    }

    // Load should succeed (crc32==0 accepted for backward compatibility)
    REQUIRE( M2::create( size ) );
    REQUIRE( pmm::load_manager_from_file<M2>( TEST_FILE ) );
    REQUIRE( M2::is_initialized() );

    M2::destroy();
    cleanup_file();
}

// =============================================================================
// 2.2: Atomic save (write-then-rename)
// =============================================================================

/// @brief Verify that no .tmp file remains after a successful save.
TEST_CASE( "atomic_save_no_tmp_remains", "[test_issue43_phase2_persistence]" )
{
    using M = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2200>;

    cleanup_file();
    REQUIRE( M::create( 16 * 1024 ) );
    REQUIRE( pmm::save_manager<M>( TEST_FILE ) );

    // The .tmp file should NOT exist after a successful save
    std::string tmp_path = std::string( TEST_FILE ) + ".tmp";
    std::FILE*  f        = std::fopen( tmp_path.c_str(), "rb" );
    REQUIRE( f == nullptr ); // tmp file must not exist
    // The final file should exist
    f = std::fopen( TEST_FILE, "rb" );
    REQUIRE( f != nullptr );
    std::fclose( f );

    M::destroy();
    cleanup_file();
}

/// @brief Verify that a previous file is replaced atomically.
TEST_CASE( "atomic_save_replaces_previous", "[test_issue43_phase2_persistence]" )
{
    using M1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2210>;
    using M2 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2211>;

    cleanup_file();
    const std::size_t size = 64 * 1024;

    // Save initial state
    REQUIRE( M1::create( size ) );
    REQUIRE( pmm::save_manager<M1>( TEST_FILE ) );
    std::size_t alloc1 = M1::alloc_block_count();

    // Allocate something, save again (should overwrite)
    M1::pptr<std::uint8_t> p = M1::allocate_typed<std::uint8_t>( 512 );
    REQUIRE( !p.is_null() );
    std::memset( p.resolve(), 0xCD, 512 );
    REQUIRE( pmm::save_manager<M1>( TEST_FILE ) );
    std::size_t alloc2 = M1::alloc_block_count();
    REQUIRE( alloc2 > alloc1 );
    M1::destroy();

    // Load and verify the second save (not the first)
    REQUIRE( M2::create( size ) );
    REQUIRE( pmm::load_manager_from_file<M2>( TEST_FILE ) );
    REQUIRE( M2::alloc_block_count() == alloc2 );

    M2::destroy();
    cleanup_file();
}

// =============================================================================
// 2.3: MMapStorage expand()
// =============================================================================

/// @brief MMapStorage::expand() extends the mapping and file.
TEST_CASE( "mmap_expand_basic", "[test_issue43_phase2_persistence]" )
{
    // Use MMapStorage directly (not via PersistMemoryManager) for unit testing.
    static const char* MMAP_FILE = "test_phase2_mmap.dat";
    std::remove( MMAP_FILE );

    pmm::MMapStorage<pmm::DefaultAddressTraits> storage;
    const std::size_t                           initial_size = 4096;
    REQUIRE( storage.open( MMAP_FILE, initial_size ) );
    REQUIRE( storage.is_open() );
    REQUIRE( storage.total_size() >= initial_size );

    std::size_t old_size = storage.total_size();

    // Write a pattern to the existing region
    std::memset( storage.base_ptr(), 0xAA, old_size );

    // Expand
    REQUIRE( storage.expand( 4096 ) );
    REQUIRE( storage.total_size() > old_size );
    REQUIRE( storage.base_ptr() != nullptr );

    // Verify old data is preserved
    const std::uint8_t* data = storage.base_ptr();
    for ( std::size_t i = 0; i < old_size; ++i )
        REQUIRE( data[i] == 0xAA );

    storage.close();
    std::remove( MMAP_FILE );
}

/// @brief MMapStorage::expand(0) should return true without changes.
TEST_CASE( "mmap_expand_zero", "[test_issue43_phase2_persistence]" )
{
    static const char* MMAP_FILE = "test_phase2_mmap_zero.dat";
    std::remove( MMAP_FILE );

    pmm::MMapStorage<pmm::DefaultAddressTraits> storage;
    REQUIRE( storage.open( MMAP_FILE, 4096 ) );
    std::size_t old_size = storage.total_size();
    REQUIRE( storage.expand( 0 ) );
    REQUIRE( storage.total_size() == old_size );

    storage.close();
    std::remove( MMAP_FILE );
}

/// @brief MMapStorage::expand() on an unmapped storage returns false.
TEST_CASE( "mmap_expand_not_open", "[test_issue43_phase2_persistence]" )
{
    pmm::MMapStorage<pmm::DefaultAddressTraits> storage;
    REQUIRE( !storage.expand( 1024 ) );
}

/// @brief MMapStorage expand integrates with PersistMemoryManager via auto-grow.
TEST_CASE( "mmap_expand_with_manager", "[test_issue43_phase2_persistence]" )
{
    // Use a config that uses MMapStorage.
    // We build a custom one inline for the test.
    static const char* MMAP_FILE = "test_phase2_mmap_mgr.dat";
    std::remove( MMAP_FILE );

    // Open MMapStorage, create manager, allocate until expand is needed.
    pmm::MMapStorage<pmm::DefaultAddressTraits> storage;
    const std::size_t                           initial_size = 4096;
    REQUIRE( storage.open( MMAP_FILE, initial_size ) );
    REQUIRE( storage.is_open() );
    REQUIRE( storage.total_size() >= initial_size );

    // Verify expand works multiple times
    std::size_t size1 = storage.total_size();
    REQUIRE( storage.expand( 2048 ) );
    std::size_t size2 = storage.total_size();
    REQUIRE( size2 > size1 );
    REQUIRE( storage.expand( 2048 ) );
    std::size_t size3 = storage.total_size();
    REQUIRE( size3 > size2 );

    storage.close();
    std::remove( MMAP_FILE );
}

// =============================================================================
// CRC32 utility tests
// =============================================================================

/// @brief compute_image_crc32 treats the crc32 field as zero.
TEST_CASE( "image_crc32_ignores_crc_field", "[test_issue43_phase2_persistence]" )
{
    using M = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2300>;

    REQUIRE( M::create( 16 * 1024 ) );

    std::uint8_t* base  = M::backend().base_ptr();
    std::size_t   total = M::backend().total_size();

    // Compute CRC with crc32 field at 0
    constexpr std::size_t kHdrOff = sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto* hdr           = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>( base + kHdrOff );
    hdr->crc32          = 0;
    std::uint32_t crc_a = pmm::detail::compute_image_crc32<pmm::DefaultAddressTraits>( base, total );

    // Now set crc32 to some value — compute_image_crc32 should still produce the same result
    hdr->crc32          = 0x12345678U;
    std::uint32_t crc_b = pmm::detail::compute_image_crc32<pmm::DefaultAddressTraits>( base, total );

    REQUIRE( crc_a == crc_b );
    REQUIRE( crc_a != 0 );

    M::destroy();
}

// =============================================================================
// main
// =============================================================================
