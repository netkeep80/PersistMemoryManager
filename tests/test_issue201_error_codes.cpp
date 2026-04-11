/**
 * @file test_issue201_error_codes.cpp
 * @brief Tests for PmmError error codes API.
 *
 * Verifies the key requirements from this feature:
 *  1. PmmError enum values exist and are distinct.
 *  2. last_error() returns Ok after successful create().
 *  3. last_error() returns InvalidSize when create() gets too small size.
 *  4. last_error() returns Overflow on huge sizes.
 *  5. last_error() returns NotInitialized on allocate() before create().
 *  6. last_error() returns InvalidSize on allocate(0).
 *  7. last_error() returns OutOfMemory when heap is exhausted.
 *  8. last_error() returns InvalidMagic on load() with bad magic.
 *  9. last_error() returns SizeMismatch on load() with wrong total_size.
 * 10. last_error() returns GranuleMismatch on load() with wrong granule_size.
 * 11. clear_error() resets to Ok.
 * 12. set_last_error() works (used by io.h).
 * 13. last_error() returns CrcMismatch when loading corrupted file.
 * 14. Error codes work with SmallAddressTraits.
 * 15. Error codes work with LargeAddressTraits.
 *
 * @see include/pmm/types.h — PmmError enum
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @version 0.1
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/io.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <limits>

// --- Manager aliases ---------------------------------------------------------

using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 201>;

// Separate instance IDs for each test that needs isolated state.
using MgrLoad      = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 202>;
using MgrSmall     = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 203>;
using MgrLarge     = pmm::PersistMemoryManager<pmm::LargeDBConfig, 204>;
using MgrCrc       = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 205>;
using MgrAllocFail = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 206>;

// --- Tests -------------------------------------------------------------------

/// 1. PmmError enum values exist and are distinct.
TEST_CASE( "enum_values", "[test_issue201_error_codes]" )
{
    REQUIRE( static_cast<int>( pmm::PmmError::Ok ) == 0 );
    REQUIRE( static_cast<int>( pmm::PmmError::NotInitialized ) == 1 );
    REQUIRE( static_cast<int>( pmm::PmmError::InvalidSize ) == 2 );
    REQUIRE( static_cast<int>( pmm::PmmError::Overflow ) == 3 );
    REQUIRE( static_cast<int>( pmm::PmmError::OutOfMemory ) == 4 );
    REQUIRE( static_cast<int>( pmm::PmmError::ExpandFailed ) == 5 );
    REQUIRE( static_cast<int>( pmm::PmmError::InvalidMagic ) == 6 );
    REQUIRE( static_cast<int>( pmm::PmmError::CrcMismatch ) == 7 );
    REQUIRE( static_cast<int>( pmm::PmmError::SizeMismatch ) == 8 );
    REQUIRE( static_cast<int>( pmm::PmmError::GranuleMismatch ) == 9 );
    REQUIRE( static_cast<int>( pmm::PmmError::BackendError ) == 10 );
    REQUIRE( static_cast<int>( pmm::PmmError::InvalidPointer ) == 11 );
    REQUIRE( static_cast<int>( pmm::PmmError::BlockLocked ) == 12 );
}

/// 2. last_error() returns Ok after successful create().
TEST_CASE( "create_success", "[test_issue201_error_codes]" )
{
    Mgr::clear_error();
    bool ok = Mgr::create( 64 * 1024 );
    REQUIRE( ok );
    REQUIRE( Mgr::last_error() == pmm::PmmError::Ok );
    Mgr::destroy();
}

/// 3. last_error() returns InvalidSize when create() gets too small size.
TEST_CASE( "create_invalid_size", "[test_issue201_error_codes]" )
{
    Mgr::clear_error();
    bool ok = Mgr::create( 1 ); // way too small
    REQUIRE( !ok );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidSize );
}

/// 4. last_error() returns Overflow on huge sizes.
TEST_CASE( "create_overflow", "[test_issue201_error_codes]" )
{
    Mgr::clear_error();
    bool ok = Mgr::create( std::numeric_limits<std::size_t>::max() );
    REQUIRE( !ok );
    REQUIRE( Mgr::last_error() == pmm::PmmError::Overflow );
}

/// 5. last_error() returns NotInitialized on allocate() before create().
TEST_CASE( "allocate_not_initialized", "[test_issue201_error_codes]" )
{
    // Use a fresh manager instance that has never been initialized.
    using MgrFresh = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 210>;
    MgrFresh::clear_error();
    void* p = MgrFresh::allocate( 16 );
    REQUIRE( p == nullptr );
    REQUIRE( MgrFresh::last_error() == pmm::PmmError::NotInitialized );
}

/// 6. last_error() returns InvalidSize on allocate(0).
TEST_CASE( "allocate_zero_size", "[test_issue201_error_codes]" )
{
    Mgr::create( 64 * 1024 );
    Mgr::clear_error();
    void* p = Mgr::allocate( 0 );
    REQUIRE( p == nullptr );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidSize );
    Mgr::destroy();
}

/// 7. last_error() returns OutOfMemory when heap is exhausted.
TEST_CASE( "allocate_out_of_memory", "[test_issue201_error_codes]" )
{
    // Use a fresh CacheManagerConfig instance but exhaust its memory.
    using MgrOOM = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<4096>, 220>;
    MgrOOM::create( 4096 );
    // Exhaust all free space by allocating in a loop
    int alloc_count = 0;
    while ( true )
    {
        void* p = MgrOOM::allocate( 16 );
        if ( p == nullptr )
            break;
        ++alloc_count;
        if ( alloc_count > 1000 )
            break; // safety limit
    }
    // The last allocate() should have failed with OutOfMemory
    REQUIRE( MgrOOM::last_error() == pmm::PmmError::OutOfMemory );
    MgrOOM::destroy();
}

/// 8. last_error() returns InvalidMagic on load() with bad magic.
TEST_CASE( "load_invalid_magic", "[test_issue201_error_codes]" )
{
    MgrLoad::create( 64 * 1024 );
    MgrLoad::destroy();
    // After destroy, magic is zeroed. Set a deliberately wrong magic.
    auto& be  = MgrLoad::backend();
    auto* hdr = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>(
        be.base_ptr() + sizeof( pmm::Block<pmm::DefaultAddressTraits> ) );
    hdr->magic = 0xDEADBEEF;
    MgrLoad::clear_error();
    bool ok = MgrLoad::load();
    REQUIRE( !ok );
    REQUIRE( MgrLoad::last_error() == pmm::PmmError::InvalidMagic );
}

/// 9. last_error() returns SizeMismatch on load() with wrong total_size.
TEST_CASE( "load_size_mismatch", "[test_issue201_error_codes]" )
{
    using MgrSz = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 211>;
    MgrSz::create( 64 * 1024 );
    MgrSz::destroy();
    // After destroy, magic is zeroed. Restore magic but set wrong total_size.
    auto& be  = MgrSz::backend();
    auto* hdr = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>(
        be.base_ptr() + sizeof( pmm::Block<pmm::DefaultAddressTraits> ) );
    hdr->magic      = pmm::kMagic;
    hdr->total_size = 12345; // wrong — doesn't match backend
    MgrSz::clear_error();
    bool ok = MgrSz::load();
    REQUIRE( !ok );
    REQUIRE( MgrSz::last_error() == pmm::PmmError::SizeMismatch );
}

/// 10. last_error() returns GranuleMismatch on load() with wrong granule_size.
TEST_CASE( "load_granule_mismatch", "[test_issue201_error_codes]" )
{
    using MgrGr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 212>;
    MgrGr::create( 64 * 1024 );
    MgrGr::destroy();
    // After destroy, magic is zeroed. Restore magic and total_size, but set wrong granule_size.
    auto& be  = MgrGr::backend();
    auto* hdr = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>(
        be.base_ptr() + sizeof( pmm::Block<pmm::DefaultAddressTraits> ) );
    hdr->magic        = pmm::kMagic;
    hdr->total_size   = be.total_size(); // correct
    hdr->granule_size = 99;              // wrong
    MgrGr::clear_error();
    bool ok = MgrGr::load();
    REQUIRE( !ok );
    REQUIRE( MgrGr::last_error() == pmm::PmmError::GranuleMismatch );
}

/// 11. clear_error() resets to Ok.
TEST_CASE( "clear_error", "[test_issue201_error_codes]" )
{
    Mgr::clear_error();
    Mgr::create( 1 ); // will fail — InvalidSize
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidSize );
    Mgr::clear_error();
    REQUIRE( Mgr::last_error() == pmm::PmmError::Ok );
}

/// 12. set_last_error() works (used by io.h).
TEST_CASE( "set_last_error", "[test_issue201_error_codes]" )
{
    Mgr::clear_error();
    Mgr::set_last_error( pmm::PmmError::CrcMismatch );
    REQUIRE( Mgr::last_error() == pmm::PmmError::CrcMismatch );
    Mgr::clear_error();
    REQUIRE( Mgr::last_error() == pmm::PmmError::Ok );
}

/// 13. last_error() returns CrcMismatch when loading corrupted file via io.h.
TEST_CASE( "crc_mismatch", "[test_issue201_error_codes]" )
{
    MgrCrc::create( 64 * 1024 );
    // Allocate something so the image is non-trivial
    auto p = MgrCrc::allocate_typed<int>();
    *p     = 42;

    const char* filename = "test_issue201_crc.dat";
    bool        saved    = pmm::save_manager<MgrCrc>( filename );
    REQUIRE( saved );
    MgrCrc::destroy();

    // Corrupt a data byte in the saved file
    std::FILE* f = std::fopen( filename, "r+b" );
    REQUIRE( f != nullptr );
    // Seek past headers and corrupt some data byte
    std::fseek( f, 200, SEEK_SET );
    std::uint8_t bad = 0xFF;
    std::fwrite( &bad, 1, 1, f );
    std::fclose( f );

    // Try to load the corrupted file
    MgrCrc::create( 64 * 1024 );
    MgrCrc::clear_error();
    bool loaded = pmm::load_manager_from_file<MgrCrc>( filename );
    REQUIRE( !loaded );
    REQUIRE( MgrCrc::last_error() == pmm::PmmError::CrcMismatch );

    MgrCrc::destroy();
    std::remove( filename );
}

/// 14. Error codes work with SmallAddressTraits (uint16_t).
TEST_CASE( "small_address_traits", "[test_issue201_error_codes]" )
{
    MgrSmall::clear_error();
    bool ok = MgrSmall::create( 1 ); // too small
    REQUIRE( !ok );
    REQUIRE( MgrSmall::last_error() == pmm::PmmError::InvalidSize );

    ok = MgrSmall::create( 4096 );
    REQUIRE( ok );
    REQUIRE( MgrSmall::last_error() == pmm::PmmError::Ok );
    MgrSmall::destroy();
}

/// 15. Error codes work with LargeAddressTraits (uint64_t).
TEST_CASE( "large_address_traits", "[test_issue201_error_codes]" )
{
    MgrLarge::clear_error();
    bool ok = MgrLarge::create( 1 ); // too small
    REQUIRE( !ok );
    REQUIRE( MgrLarge::last_error() == pmm::PmmError::InvalidSize );

    ok = MgrLarge::create( 64 * 1024 );
    REQUIRE( ok );
    REQUIRE( MgrLarge::last_error() == pmm::PmmError::Ok );
    MgrLarge::destroy();
}

/// 16. Successful allocate sets Ok.
TEST_CASE( "allocate_success_clears_error", "[test_issue201_error_codes]" )
{
    Mgr::create( 64 * 1024 );
    Mgr::set_last_error( pmm::PmmError::OutOfMemory ); // simulate previous failure
    void* p = Mgr::allocate( 16 );
    REQUIRE( p != nullptr );
    REQUIRE( Mgr::last_error() == pmm::PmmError::Ok );
    Mgr::deallocate( p );
    Mgr::destroy();
}

/// 17. Overflow in allocate_typed with huge count.
TEST_CASE( "allocate_typed_overflow", "[test_issue201_error_codes]" )
{
    Mgr::create( 64 * 1024 );
    Mgr::clear_error();
    // Overflow: count * sizeof(int) > size_t max
    auto p = Mgr::allocate_typed<int>( std::numeric_limits<std::size_t>::max() );
    REQUIRE( p.is_null() );
    // allocate_typed returns early before calling allocate() — no error set by allocate
    // but the pptr is null indicating failure
    Mgr::destroy();
}

/// 18. create() no-arg variant with null backend sets BackendError.
TEST_CASE( "create_no_arg_backend_error", "[test_issue201_error_codes]" )
{
    // A fresh manager instance with default (empty) HeapStorage
    using MgrBE = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 213>;
    MgrBE::clear_error();
    bool ok = MgrBE::create(); // backend has no buffer yet
    REQUIRE( !ok );
    REQUIRE( MgrBE::last_error() == pmm::PmmError::BackendError );
}
