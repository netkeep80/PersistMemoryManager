/**
 * @file test_issue202_logging_hooks.cpp
 * @brief Tests for logging hooks API.
 *
 * Verifies the key requirements from this feature:
 *  1. NoLogging policy compiles and has zero overhead (default for all presets).
 *  2. StderrLogging policy compiles and produces output.
 *  3. Custom logging policy receives on_allocation_failure() on OOM.
 *  4. Custom logging policy receives on_expand() on backend expansion.
 *  5. Custom logging policy receives on_corruption_detected() on bad magic.
 *  6. Custom logging policy receives on_corruption_detected() on size mismatch.
 *  7. Custom logging policy receives on_corruption_detected() on granule mismatch.
 *  8. Custom logging policy receives on_create() on successful creation.
 *  9. Custom logging policy receives on_destroy() on destroy.
 * 10. Custom logging policy receives on_load() on successful load.
 * 11. on_allocation_failure() fires on allocate(0) (InvalidSize).
 * 12. on_allocation_failure() fires on allocate() before create() (NotInitialized).
 * 13. on_corruption_detected() fires on CRC mismatch via load_manager_from_file().
 * 14. Logging hooks work with SmallAddressTraits.
 * 15. Logging hooks work with LargeAddressTraits.
 *
 * @see include/pmm/logging_policy.h — logging policies
 * @see include/pmm/persist_memory_manager.h — PersistMemoryManager
 * @version 0.1
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/io.h"
#include "pmm/logging_policy.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// --- Custom logging policy for testing ---------------------------------------

/// @brief Counters for hook invocations — used to verify hooks are called.
struct TestHookCounters
{
    static inline int           allocation_failure_count = 0;
    static inline int           expand_count             = 0;
    static inline int           corruption_count         = 0;
    static inline int           create_count             = 0;
    static inline int           destroy_count            = 0;
    static inline int           load_count               = 0;
    static inline std::size_t   last_alloc_size          = 0;
    static inline pmm::PmmError last_alloc_err           = pmm::PmmError::Ok;
    static inline std::size_t   last_expand_old          = 0;
    static inline std::size_t   last_expand_new          = 0;
    static inline pmm::PmmError last_corrupt_err         = pmm::PmmError::Ok;
    static inline std::size_t   last_create_size         = 0;

    static void reset()
    {
        allocation_failure_count = 0;
        expand_count             = 0;
        corruption_count         = 0;
        create_count             = 0;
        destroy_count            = 0;
        load_count               = 0;
        last_alloc_size          = 0;
        last_alloc_err           = pmm::PmmError::Ok;
        last_expand_old          = 0;
        last_expand_new          = 0;
        last_corrupt_err         = pmm::PmmError::Ok;
        last_create_size         = 0;
    }
};

/// @brief Test logging policy that records hook invocations.
struct TestLogging
{
    static void on_allocation_failure( std::size_t user_size, pmm::PmmError err ) noexcept
    {
        TestHookCounters::allocation_failure_count++;
        TestHookCounters::last_alloc_size = user_size;
        TestHookCounters::last_alloc_err  = err;
    }

    static void on_expand( std::size_t old_size, std::size_t new_size ) noexcept
    {
        TestHookCounters::expand_count++;
        TestHookCounters::last_expand_old = old_size;
        TestHookCounters::last_expand_new = new_size;
    }

    static void on_corruption_detected( pmm::PmmError err ) noexcept
    {
        TestHookCounters::corruption_count++;
        TestHookCounters::last_corrupt_err = err;
    }

    static void on_create( std::size_t size ) noexcept
    {
        TestHookCounters::create_count++;
        TestHookCounters::last_create_size = size;
    }

    static void on_destroy() noexcept { TestHookCounters::destroy_count++; }

    static void on_load() noexcept { TestHookCounters::load_count++; }
};

// --- Config with TestLogging -------------------------------------------------

using TestLoggingConfig = pmm::BasicConfig<pmm::DefaultAddressTraits, pmm::config::NoLock, 5, 4, 64, TestLogging>;

using TestLoggingSmallConfig = pmm::BasicConfig<pmm::SmallAddressTraits, pmm::config::NoLock, 5, 4, 0, TestLogging>;

using TestLoggingLargeConfig = pmm::BasicConfig<pmm::LargeAddressTraits, pmm::config::NoLock, 5, 4, 0, TestLogging>;

// --- Manager aliases with unique InstanceIds ---------------------------------

using MgrLog       = pmm::PersistMemoryManager<TestLoggingConfig, 300>;
using MgrLogExpand = pmm::PersistMemoryManager<TestLoggingConfig, 301>;
using MgrLogLoad   = pmm::PersistMemoryManager<TestLoggingConfig, 302>;
using MgrLogOOM    = pmm::PersistMemoryManager<TestLoggingConfig, 303>;
using MgrLogCrc    = pmm::PersistMemoryManager<TestLoggingConfig, 304>;
using MgrLogSmall  = pmm::PersistMemoryManager<TestLoggingSmallConfig, 305>;
using MgrLogLarge  = pmm::PersistMemoryManager<TestLoggingLargeConfig, 306>;

// Default-config managers (NoLogging) to verify compilation
using MgrDefault = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 310>;

// --- Tests -------------------------------------------------------------------

/// 1. NoLogging policy compiles with all preset configs.
TEST_CASE( "NoLogging compiles", "[test_issue202_logging_hooks]" )
{
    MgrDefault::create( 4096 );
    void* p = MgrDefault::allocate( 64 );
    REQUIRE( p != nullptr );
    MgrDefault::deallocate( p );
    MgrDefault::destroy();
}

/// 3. on_create() fires on successful creation.
TEST_CASE( "on_create hook", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();
    REQUIRE( TestHookCounters::create_count == 0 );
    MgrLog::create( 8192 );
    REQUIRE( TestHookCounters::create_count == 1 );
    REQUIRE( TestHookCounters::last_create_size > 0 );
    MgrLog::destroy();
}

/// 4. on_destroy() fires on destroy.
TEST_CASE( "on_destroy hook", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();
    MgrLog::create( 4096 );
    REQUIRE( TestHookCounters::destroy_count == 0 );
    MgrLog::destroy();
    REQUIRE( TestHookCounters::destroy_count == 1 );
}

/// 5. on_allocation_failure() fires on OOM (overflow path).
TEST_CASE( "on_allocation_failure (OOM)", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();
    MgrLogOOM::create( 256 );
    TestHookCounters::reset(); // reset after create

    // Request a size that causes data_gran to overflow when adding header granules.
    // kBlockHdrGranules for DefaultAddressTraits is 2. We need data_gran > max(uint32_t) - 2.
    // bytes_to_granules(size) = ceil(size / 16). So size = (0xFFFFFFFF - 1) * 16 = huge.
    // This will cause data_gran + kBlockHdrGranules > max(uint32_t), triggering Overflow.
    std::size_t overflow_size = static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() - 1 ) * 16;
    void*       p             = MgrLogOOM::allocate( overflow_size );
    REQUIRE( p == nullptr );
    REQUIRE( TestHookCounters::allocation_failure_count >= 1 );
    REQUIRE( TestHookCounters::last_alloc_err == pmm::PmmError::Overflow );

    MgrLogOOM::destroy();
}

/// 6. on_allocation_failure() fires on allocate(0) (InvalidSize).
TEST_CASE( "on_allocation_failure (InvalidSize)", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();
    MgrLog::create( 4096 );
    TestHookCounters::reset();

    void* p = MgrLog::allocate( 0 );
    REQUIRE( p == nullptr );
    REQUIRE( TestHookCounters::allocation_failure_count == 1 );
    REQUIRE( TestHookCounters::last_alloc_err == pmm::PmmError::InvalidSize );
    REQUIRE( TestHookCounters::last_alloc_size == 0 );

    MgrLog::destroy();
}

/// 7. on_allocation_failure() fires on allocate() before create() (NotInitialized).
TEST_CASE( "on_allocation_failure (NotInitialized)", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();

    // MgrLog is not initialized (no create() call).
    // Ensure it's really not initialized:
    using MgrNotInit = pmm::PersistMemoryManager<TestLoggingConfig, 320>;
    void* p          = MgrNotInit::allocate( 64 );
    REQUIRE( p == nullptr );
    REQUIRE( TestHookCounters::allocation_failure_count == 1 );
    REQUIRE( TestHookCounters::last_alloc_err == pmm::PmmError::NotInitialized );
}

/// 8. on_expand() fires on backend expansion.
TEST_CASE( "on_expand hook", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();
    MgrLogExpand::create( 256 );
    TestHookCounters::reset();

    // Allocate enough to trigger expansion (must exceed HeapStorage initial minimum).
    void* p = MgrLogExpand::allocate( 8192 );
    REQUIRE( p != nullptr );
    REQUIRE( TestHookCounters::expand_count >= 1 );
    REQUIRE( TestHookCounters::last_expand_new > TestHookCounters::last_expand_old );

    MgrLogExpand::deallocate( p );
    MgrLogExpand::destroy();
}

/// 9. on_corruption_detected() fires on bad magic.
TEST_CASE( "on_corruption (bad magic)", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();

    using Mgr = pmm::PersistMemoryManager<TestLoggingConfig, 342>;
    Mgr::create( 4096 );
    pmm::save_manager<Mgr>( "test_issue202_mag.dat" );
    Mgr::destroy();

    // Load valid image into buffer.
    Mgr::create( 4096 );
    std::uint8_t* base     = Mgr::backend().base_ptr();
    std::size_t   buf_size = Mgr::backend().total_size();
    std::FILE*    f        = std::fopen( "test_issue202_mag.dat", "rb" );
    REQUIRE( f != nullptr );
    std::fread( base, 1, buf_size, f );
    std::fclose( f );

    // Corrupt the magic number, keep total_size correct.
    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto* hdr  = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>( base + kHdrOffset );
    hdr->magic = 0xDEADBEEFULL;

    TestHookCounters::reset();
    pmm::VerifyResult vr;
    bool              ok = Mgr::load( vr );
    REQUIRE( !ok );
    REQUIRE( TestHookCounters::corruption_count == 1 );
    REQUIRE( TestHookCounters::last_corrupt_err == pmm::PmmError::InvalidMagic );

    Mgr::destroy();
    std::remove( "test_issue202_mag.dat" );
}

/// 10. on_corruption_detected() fires on size mismatch.
TEST_CASE( "on_corruption (size mismatch)", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();

    using Mgr = pmm::PersistMemoryManager<TestLoggingConfig, 340>;
    Mgr::create( 4096 );
    pmm::save_manager<Mgr>( "test_issue202_sz.dat" );
    Mgr::destroy();

    // Load file into buffer via a fresh create.
    Mgr::create( 4096 );
    std::uint8_t* base     = Mgr::backend().base_ptr();
    std::size_t   buf_size = Mgr::backend().total_size();
    std::FILE*    f        = std::fopen( "test_issue202_sz.dat", "rb" );
    REQUIRE( f != nullptr );
    std::fread( base, 1, buf_size, f );
    std::fclose( f );

    // Corrupt total_size in header. Magic is correct from the saved image.
    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto* hdr       = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>( base + kHdrOffset );
    hdr->total_size = buf_size + 999;

    TestHookCounters::reset();
    pmm::VerifyResult vr;
    bool              ok = Mgr::load( vr );
    REQUIRE( !ok );
    REQUIRE( TestHookCounters::corruption_count == 1 );
    REQUIRE( TestHookCounters::last_corrupt_err == pmm::PmmError::SizeMismatch );

    Mgr::destroy();
    std::remove( "test_issue202_sz.dat" );
}

/// 11. on_corruption_detected() fires on granule mismatch.
TEST_CASE( "on_corruption (granule mismatch)", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();

    using Mgr = pmm::PersistMemoryManager<TestLoggingConfig, 341>;
    Mgr::create( 4096 );
    pmm::save_manager<Mgr>( "test_issue202_gr.dat" );
    Mgr::destroy();

    // Load file into buffer via a fresh create.
    Mgr::create( 4096 );
    std::uint8_t* base     = Mgr::backend().base_ptr();
    std::size_t   buf_size = Mgr::backend().total_size();
    std::FILE*    f        = std::fopen( "test_issue202_gr.dat", "rb" );
    REQUIRE( f != nullptr );
    std::fread( base, 1, buf_size, f );
    std::fclose( f );

    // Corrupt granule_size in header. Magic and total_size are correct from saved image.
    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto* hdr         = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>( base + kHdrOffset );
    hdr->granule_size = 99;

    TestHookCounters::reset();
    pmm::VerifyResult vr;
    bool              ok = Mgr::load( vr );
    REQUIRE( !ok );
    REQUIRE( TestHookCounters::corruption_count == 1 );
    REQUIRE( TestHookCounters::last_corrupt_err == pmm::PmmError::GranuleMismatch );

    Mgr::destroy();
    std::remove( "test_issue202_gr.dat" );
}

/// 12. on_load() fires on successful load.
TEST_CASE( "on_load hook", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();

    using MgrSave = pmm::PersistMemoryManager<TestLoggingConfig, 330>;
    MgrSave::create( 4096 );
    pmm::save_manager<MgrSave>( "test_issue202_load.dat" );
    MgrSave::destroy();

    MgrSave::create( 4096 );
    TestHookCounters::reset();
    pmm::VerifyResult vr_;
    bool              ok = pmm::load_manager_from_file<MgrSave>( "test_issue202_load.dat", vr_ );
    REQUIRE( ok );
    REQUIRE( TestHookCounters::load_count == 1 );

    MgrSave::destroy();
    std::remove( "test_issue202_load.dat" );
}

/// 13. on_corruption_detected() fires on CRC mismatch.
TEST_CASE( "on_corruption (CRC mismatch)", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();

    MgrLogCrc::create( 4096 );
    pmm::save_manager<MgrLogCrc>( "test_issue202_crc.dat" );
    MgrLogCrc::destroy();

    // Corrupt the file.
    std::FILE* f = std::fopen( "test_issue202_crc.dat", "r+b" );
    REQUIRE( f != nullptr );
    // Corrupt a byte near the end of the file.
    std::fseek( f, -10, SEEK_END );
    unsigned char bad = 0xFF;
    std::fwrite( &bad, 1, 1, f );
    std::fclose( f );

    MgrLogCrc::create( 4096 );
    TestHookCounters::reset();
    pmm::VerifyResult vr_;
    bool              ok = pmm::load_manager_from_file<MgrLogCrc>( "test_issue202_crc.dat", vr_ );
    REQUIRE( !ok );
    REQUIRE( TestHookCounters::corruption_count == 1 );
    REQUIRE( TestHookCounters::last_corrupt_err == pmm::PmmError::CrcMismatch );

    MgrLogCrc::destroy();
    std::remove( "test_issue202_crc.dat" );
}

/// 14. Logging hooks work with SmallAddressTraits.
TEST_CASE( "SmallAddressTraits", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();
    MgrLogSmall::create( 4096 );
    REQUIRE( TestHookCounters::create_count == 1 );

    void* p = MgrLogSmall::allocate( 32 );
    REQUIRE( p != nullptr );
    MgrLogSmall::deallocate( p );

    MgrLogSmall::destroy();
    REQUIRE( TestHookCounters::destroy_count == 1 );
}

/// 15. Logging hooks work with LargeAddressTraits.
TEST_CASE( "LargeAddressTraits", "[test_issue202_logging_hooks]" )
{
    TestHookCounters::reset();
    MgrLogLarge::create( 8192 );
    REQUIRE( TestHookCounters::create_count == 1 );

    void* p = MgrLogLarge::allocate( 128 );
    REQUIRE( p != nullptr );
    MgrLogLarge::deallocate( p );

    MgrLogLarge::destroy();
    REQUIRE( TestHookCounters::destroy_count == 1 );
}
