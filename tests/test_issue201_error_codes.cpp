/**
 * @file test_issue201_error_codes.cpp
 * @brief Tests for PmmError error codes API (Issue #201, Phase 4.1).
 *
 * Verifies the key requirements from Issue #201 Phase 4.1:
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
 * @version 0.1 (Issue #201 — Phase 4.1: error codes)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/io.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>

// --- Test macros -------------------------------------------------------------

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
static bool test_enum_values()
{
    PMM_TEST( static_cast<int>( pmm::PmmError::Ok ) == 0 );
    PMM_TEST( static_cast<int>( pmm::PmmError::NotInitialized ) == 1 );
    PMM_TEST( static_cast<int>( pmm::PmmError::InvalidSize ) == 2 );
    PMM_TEST( static_cast<int>( pmm::PmmError::Overflow ) == 3 );
    PMM_TEST( static_cast<int>( pmm::PmmError::OutOfMemory ) == 4 );
    PMM_TEST( static_cast<int>( pmm::PmmError::ExpandFailed ) == 5 );
    PMM_TEST( static_cast<int>( pmm::PmmError::InvalidMagic ) == 6 );
    PMM_TEST( static_cast<int>( pmm::PmmError::CrcMismatch ) == 7 );
    PMM_TEST( static_cast<int>( pmm::PmmError::SizeMismatch ) == 8 );
    PMM_TEST( static_cast<int>( pmm::PmmError::GranuleMismatch ) == 9 );
    PMM_TEST( static_cast<int>( pmm::PmmError::BackendError ) == 10 );
    PMM_TEST( static_cast<int>( pmm::PmmError::InvalidPointer ) == 11 );
    PMM_TEST( static_cast<int>( pmm::PmmError::BlockLocked ) == 12 );
    return true;
}

/// 2. last_error() returns Ok after successful create().
static bool test_create_success()
{
    Mgr::clear_error();
    bool ok = Mgr::create( 64 * 1024 );
    PMM_TEST( ok );
    PMM_TEST( Mgr::last_error() == pmm::PmmError::Ok );
    Mgr::destroy();
    return true;
}

/// 3. last_error() returns InvalidSize when create() gets too small size.
static bool test_create_invalid_size()
{
    Mgr::clear_error();
    bool ok = Mgr::create( 1 ); // way too small
    PMM_TEST( !ok );
    PMM_TEST( Mgr::last_error() == pmm::PmmError::InvalidSize );
    return true;
}

/// 4. last_error() returns Overflow on huge sizes.
static bool test_create_overflow()
{
    Mgr::clear_error();
    bool ok = Mgr::create( std::numeric_limits<std::size_t>::max() );
    PMM_TEST( !ok );
    PMM_TEST( Mgr::last_error() == pmm::PmmError::Overflow );
    return true;
}

/// 5. last_error() returns NotInitialized on allocate() before create().
static bool test_allocate_not_initialized()
{
    // Use a fresh manager instance that has never been initialized.
    using MgrFresh = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 210>;
    MgrFresh::clear_error();
    void* p = MgrFresh::allocate( 16 );
    PMM_TEST( p == nullptr );
    PMM_TEST( MgrFresh::last_error() == pmm::PmmError::NotInitialized );
    return true;
}

/// 6. last_error() returns InvalidSize on allocate(0).
static bool test_allocate_zero_size()
{
    Mgr::create( 64 * 1024 );
    Mgr::clear_error();
    void* p = Mgr::allocate( 0 );
    PMM_TEST( p == nullptr );
    PMM_TEST( Mgr::last_error() == pmm::PmmError::InvalidSize );
    Mgr::destroy();
    return true;
}

/// 7. last_error() returns OutOfMemory when heap is exhausted.
static bool test_allocate_out_of_memory()
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
    PMM_TEST( MgrOOM::last_error() == pmm::PmmError::OutOfMemory );
    MgrOOM::destroy();
    return true;
}

/// 8. last_error() returns InvalidMagic on load() with bad magic.
static bool test_load_invalid_magic()
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
    PMM_TEST( !ok );
    PMM_TEST( MgrLoad::last_error() == pmm::PmmError::InvalidMagic );
    return true;
}

/// 9. last_error() returns SizeMismatch on load() with wrong total_size.
static bool test_load_size_mismatch()
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
    PMM_TEST( !ok );
    PMM_TEST( MgrSz::last_error() == pmm::PmmError::SizeMismatch );
    return true;
}

/// 10. last_error() returns GranuleMismatch on load() with wrong granule_size.
static bool test_load_granule_mismatch()
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
    PMM_TEST( !ok );
    PMM_TEST( MgrGr::last_error() == pmm::PmmError::GranuleMismatch );
    return true;
}

/// 11. clear_error() resets to Ok.
static bool test_clear_error()
{
    Mgr::clear_error();
    Mgr::create( 1 ); // will fail — InvalidSize
    PMM_TEST( Mgr::last_error() == pmm::PmmError::InvalidSize );
    Mgr::clear_error();
    PMM_TEST( Mgr::last_error() == pmm::PmmError::Ok );
    return true;
}

/// 12. set_last_error() works (used by io.h).
static bool test_set_last_error()
{
    Mgr::clear_error();
    Mgr::set_last_error( pmm::PmmError::CrcMismatch );
    PMM_TEST( Mgr::last_error() == pmm::PmmError::CrcMismatch );
    Mgr::clear_error();
    PMM_TEST( Mgr::last_error() == pmm::PmmError::Ok );
    return true;
}

/// 13. last_error() returns CrcMismatch when loading corrupted file via io.h.
static bool test_crc_mismatch()
{
    MgrCrc::create( 64 * 1024 );
    // Allocate something so the image is non-trivial
    auto p = MgrCrc::allocate_typed<int>();
    *p     = 42;

    const char* filename = "test_issue201_crc.dat";
    bool        saved    = pmm::save_manager<MgrCrc>( filename );
    PMM_TEST( saved );
    MgrCrc::destroy();

    // Corrupt a data byte in the saved file
    std::FILE* f = std::fopen( filename, "r+b" );
    PMM_TEST( f != nullptr );
    // Seek past headers and corrupt some data byte
    std::fseek( f, 200, SEEK_SET );
    std::uint8_t bad = 0xFF;
    std::fwrite( &bad, 1, 1, f );
    std::fclose( f );

    // Try to load the corrupted file
    MgrCrc::create( 64 * 1024 );
    MgrCrc::clear_error();
    bool loaded = pmm::load_manager_from_file<MgrCrc>( filename );
    PMM_TEST( !loaded );
    PMM_TEST( MgrCrc::last_error() == pmm::PmmError::CrcMismatch );

    MgrCrc::destroy();
    std::remove( filename );
    return true;
}

/// 14. Error codes work with SmallAddressTraits (uint16_t).
static bool test_small_address_traits()
{
    MgrSmall::clear_error();
    bool ok = MgrSmall::create( 1 ); // too small
    PMM_TEST( !ok );
    PMM_TEST( MgrSmall::last_error() == pmm::PmmError::InvalidSize );

    ok = MgrSmall::create( 4096 );
    PMM_TEST( ok );
    PMM_TEST( MgrSmall::last_error() == pmm::PmmError::Ok );
    MgrSmall::destroy();
    return true;
}

/// 15. Error codes work with LargeAddressTraits (uint64_t).
static bool test_large_address_traits()
{
    MgrLarge::clear_error();
    bool ok = MgrLarge::create( 1 ); // too small
    PMM_TEST( !ok );
    PMM_TEST( MgrLarge::last_error() == pmm::PmmError::InvalidSize );

    ok = MgrLarge::create( 64 * 1024 );
    PMM_TEST( ok );
    PMM_TEST( MgrLarge::last_error() == pmm::PmmError::Ok );
    MgrLarge::destroy();
    return true;
}

/// 16. Successful allocate sets Ok.
static bool test_allocate_success_clears_error()
{
    Mgr::create( 64 * 1024 );
    Mgr::set_last_error( pmm::PmmError::OutOfMemory ); // simulate previous failure
    void* p = Mgr::allocate( 16 );
    PMM_TEST( p != nullptr );
    PMM_TEST( Mgr::last_error() == pmm::PmmError::Ok );
    Mgr::deallocate( p );
    Mgr::destroy();
    return true;
}

/// 17. Overflow in allocate_typed with huge count.
static bool test_allocate_typed_overflow()
{
    Mgr::create( 64 * 1024 );
    Mgr::clear_error();
    // Overflow: count * sizeof(int) > size_t max
    auto p = Mgr::allocate_typed<int>( std::numeric_limits<std::size_t>::max() );
    PMM_TEST( p.is_null() );
    // allocate_typed returns early before calling allocate() — no error set by allocate
    // but the pptr is null indicating failure
    Mgr::destroy();
    return true;
}

/// 18. create() no-arg variant with null backend sets BackendError.
static bool test_create_no_arg_backend_error()
{
    // A fresh manager instance with default (empty) HeapStorage
    using MgrBE = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 213>;
    MgrBE::clear_error();
    bool ok = MgrBE::create(); // backend has no buffer yet
    PMM_TEST( !ok );
    PMM_TEST( MgrBE::last_error() == pmm::PmmError::BackendError );
    return true;
}

// --- Main --------------------------------------------------------------------

int main()
{
    bool all_passed = true;
    std::cout << "test_issue201_error_codes:\n";

    PMM_RUN( "enum_values", test_enum_values );
    PMM_RUN( "create_success", test_create_success );
    PMM_RUN( "create_invalid_size", test_create_invalid_size );
    PMM_RUN( "create_overflow", test_create_overflow );
    PMM_RUN( "allocate_not_initialized", test_allocate_not_initialized );
    PMM_RUN( "allocate_zero_size", test_allocate_zero_size );
    PMM_RUN( "allocate_out_of_memory", test_allocate_out_of_memory );
    PMM_RUN( "load_invalid_magic", test_load_invalid_magic );
    PMM_RUN( "load_size_mismatch", test_load_size_mismatch );
    PMM_RUN( "load_granule_mismatch", test_load_granule_mismatch );
    PMM_RUN( "clear_error", test_clear_error );
    PMM_RUN( "set_last_error", test_set_last_error );
    PMM_RUN( "crc_mismatch", test_crc_mismatch );
    PMM_RUN( "small_address_traits", test_small_address_traits );
    PMM_RUN( "large_address_traits", test_large_address_traits );
    PMM_RUN( "allocate_success_clears_error", test_allocate_success_clears_error );
    PMM_RUN( "allocate_typed_overflow", test_allocate_typed_overflow );
    PMM_RUN( "create_no_arg_backend_error", test_create_no_arg_backend_error );

    std::cout << ( all_passed ? "All tests PASSED.\n" : "Some tests FAILED!\n" );
    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
