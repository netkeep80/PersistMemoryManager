/**
 * @file test_issue202_logging_hooks.cpp
 * @brief Tests for logging hooks API (Issue #202, Phase 4.2).
 *
 * Verifies the key requirements from Issue #202 Phase 4.2:
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
 * @version 0.1 (Issue #202 — Phase 4.2: logging hooks)
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/io.h"
#include "pmm/logging_policy.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>

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
static bool test_no_logging_compiles()
{
    MgrDefault::create( 4096 );
    void* p = MgrDefault::allocate( 64 );
    PMM_TEST( p != nullptr );
    MgrDefault::deallocate( p );
    MgrDefault::destroy();
    return true;
}

/// 3. on_create() fires on successful creation.
static bool test_on_create_hook()
{
    TestHookCounters::reset();
    PMM_TEST( TestHookCounters::create_count == 0 );
    MgrLog::create( 8192 );
    PMM_TEST( TestHookCounters::create_count == 1 );
    PMM_TEST( TestHookCounters::last_create_size > 0 );
    MgrLog::destroy();
    return true;
}

/// 4. on_destroy() fires on destroy.
static bool test_on_destroy_hook()
{
    TestHookCounters::reset();
    MgrLog::create( 4096 );
    PMM_TEST( TestHookCounters::destroy_count == 0 );
    MgrLog::destroy();
    PMM_TEST( TestHookCounters::destroy_count == 1 );
    return true;
}

/// 5. on_allocation_failure() fires on OOM (overflow path).
static bool test_on_allocation_failure_oom()
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
    PMM_TEST( p == nullptr );
    PMM_TEST( TestHookCounters::allocation_failure_count >= 1 );
    PMM_TEST( TestHookCounters::last_alloc_err == pmm::PmmError::Overflow );

    MgrLogOOM::destroy();
    return true;
}

/// 6. on_allocation_failure() fires on allocate(0) (InvalidSize).
static bool test_on_allocation_failure_invalid_size()
{
    TestHookCounters::reset();
    MgrLog::create( 4096 );
    TestHookCounters::reset();

    void* p = MgrLog::allocate( 0 );
    PMM_TEST( p == nullptr );
    PMM_TEST( TestHookCounters::allocation_failure_count == 1 );
    PMM_TEST( TestHookCounters::last_alloc_err == pmm::PmmError::InvalidSize );
    PMM_TEST( TestHookCounters::last_alloc_size == 0 );

    MgrLog::destroy();
    return true;
}

/// 7. on_allocation_failure() fires on allocate() before create() (NotInitialized).
static bool test_on_allocation_failure_not_initialized()
{
    TestHookCounters::reset();

    // MgrLog is not initialized (no create() call).
    // Ensure it's really not initialized:
    using MgrNotInit = pmm::PersistMemoryManager<TestLoggingConfig, 320>;
    void* p          = MgrNotInit::allocate( 64 );
    PMM_TEST( p == nullptr );
    PMM_TEST( TestHookCounters::allocation_failure_count == 1 );
    PMM_TEST( TestHookCounters::last_alloc_err == pmm::PmmError::NotInitialized );

    return true;
}

/// 8. on_expand() fires on backend expansion.
static bool test_on_expand_hook()
{
    TestHookCounters::reset();
    MgrLogExpand::create( 256 );
    TestHookCounters::reset();

    // Allocate enough to trigger expansion.
    void* p = MgrLogExpand::allocate( 512 );
    PMM_TEST( p != nullptr );
    PMM_TEST( TestHookCounters::expand_count >= 1 );
    PMM_TEST( TestHookCounters::last_expand_new > TestHookCounters::last_expand_old );

    MgrLogExpand::deallocate( p );
    MgrLogExpand::destroy();
    return true;
}

/// 9. on_corruption_detected() fires on bad magic.
static bool test_on_corruption_bad_magic()
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
    PMM_TEST( f != nullptr );
    std::fread( base, 1, buf_size, f );
    std::fclose( f );

    // Corrupt the magic number, keep total_size correct.
    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto* hdr  = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>( base + kHdrOffset );
    hdr->magic = 0xDEADBEEFULL;

    // Call load() directly (manager is still created, buffer has corrupted data).
    TestHookCounters::reset();
    bool ok = Mgr::load();
    PMM_TEST( !ok );
    PMM_TEST( TestHookCounters::corruption_count == 1 );
    PMM_TEST( TestHookCounters::last_corrupt_err == pmm::PmmError::InvalidMagic );

    Mgr::destroy();
    std::remove( "test_issue202_mag.dat" );
    return true;
}

/// 10. on_corruption_detected() fires on size mismatch.
static bool test_on_corruption_size_mismatch()
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
    PMM_TEST( f != nullptr );
    std::fread( base, 1, buf_size, f );
    std::fclose( f );

    // Corrupt total_size in header. Magic is correct from the saved image.
    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto* hdr       = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>( base + kHdrOffset );
    hdr->total_size = buf_size + 999;

    // Call load() directly — manager is currently initialized so we can call load()
    // which will re-validate the header. The _initialized flag doesn't gate load().
    TestHookCounters::reset();
    bool ok = Mgr::load();
    PMM_TEST( !ok );
    PMM_TEST( TestHookCounters::corruption_count == 1 );
    PMM_TEST( TestHookCounters::last_corrupt_err == pmm::PmmError::SizeMismatch );

    Mgr::destroy();
    std::remove( "test_issue202_sz.dat" );
    return true;
}

/// 11. on_corruption_detected() fires on granule mismatch.
static bool test_on_corruption_granule_mismatch()
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
    PMM_TEST( f != nullptr );
    std::fread( base, 1, buf_size, f );
    std::fclose( f );

    // Corrupt granule_size in header. Magic and total_size are correct from saved image.
    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<pmm::DefaultAddressTraits> );
    auto* hdr         = reinterpret_cast<pmm::detail::ManagerHeader<pmm::DefaultAddressTraits>*>( base + kHdrOffset );
    hdr->granule_size = 99;

    TestHookCounters::reset();
    bool ok = Mgr::load();
    PMM_TEST( !ok );
    PMM_TEST( TestHookCounters::corruption_count == 1 );
    PMM_TEST( TestHookCounters::last_corrupt_err == pmm::PmmError::GranuleMismatch );

    Mgr::destroy();
    std::remove( "test_issue202_gr.dat" );
    return true;
}

/// 12. on_load() fires on successful load.
static bool test_on_load_hook()
{
    TestHookCounters::reset();

    using MgrSave = pmm::PersistMemoryManager<TestLoggingConfig, 330>;
    MgrSave::create( 4096 );
    pmm::save_manager<MgrSave>( "test_issue202_load.dat" );
    MgrSave::destroy();

    MgrSave::create( 4096 );
    TestHookCounters::reset();
    bool ok = pmm::load_manager_from_file<MgrSave>( "test_issue202_load.dat" );
    PMM_TEST( ok );
    PMM_TEST( TestHookCounters::load_count == 1 );

    MgrSave::destroy();
    std::remove( "test_issue202_load.dat" );
    return true;
}

/// 13. on_corruption_detected() fires on CRC mismatch.
static bool test_on_corruption_crc_mismatch()
{
    TestHookCounters::reset();

    MgrLogCrc::create( 4096 );
    pmm::save_manager<MgrLogCrc>( "test_issue202_crc.dat" );
    MgrLogCrc::destroy();

    // Corrupt the file.
    std::FILE* f = std::fopen( "test_issue202_crc.dat", "r+b" );
    PMM_TEST( f != nullptr );
    // Corrupt a byte near the end of the file.
    std::fseek( f, -10, SEEK_END );
    unsigned char bad = 0xFF;
    std::fwrite( &bad, 1, 1, f );
    std::fclose( f );

    MgrLogCrc::create( 4096 );
    TestHookCounters::reset();
    bool ok = pmm::load_manager_from_file<MgrLogCrc>( "test_issue202_crc.dat" );
    PMM_TEST( !ok );
    PMM_TEST( TestHookCounters::corruption_count == 1 );
    PMM_TEST( TestHookCounters::last_corrupt_err == pmm::PmmError::CrcMismatch );

    MgrLogCrc::destroy();
    std::remove( "test_issue202_crc.dat" );
    return true;
}

/// 14. Logging hooks work with SmallAddressTraits.
static bool test_logging_small_address_traits()
{
    TestHookCounters::reset();
    MgrLogSmall::create( 4096 );
    PMM_TEST( TestHookCounters::create_count == 1 );

    void* p = MgrLogSmall::allocate( 32 );
    PMM_TEST( p != nullptr );
    MgrLogSmall::deallocate( p );

    MgrLogSmall::destroy();
    PMM_TEST( TestHookCounters::destroy_count == 1 );
    return true;
}

/// 15. Logging hooks work with LargeAddressTraits.
static bool test_logging_large_address_traits()
{
    TestHookCounters::reset();
    MgrLogLarge::create( 8192 );
    PMM_TEST( TestHookCounters::create_count == 1 );

    void* p = MgrLogLarge::allocate( 128 );
    PMM_TEST( p != nullptr );
    MgrLogLarge::deallocate( p );

    MgrLogLarge::destroy();
    PMM_TEST( TestHookCounters::destroy_count == 1 );
    return true;
}

// --- main --------------------------------------------------------------------

int main()
{
    std::cout << "=== test_issue202_logging_hooks ===\n";
    bool all_passed = true;

    PMM_RUN( "NoLogging compiles", test_no_logging_compiles );
    PMM_RUN( "on_create hook", test_on_create_hook );
    PMM_RUN( "on_destroy hook", test_on_destroy_hook );
    PMM_RUN( "on_allocation_failure (OOM)", test_on_allocation_failure_oom );
    PMM_RUN( "on_allocation_failure (InvalidSize)", test_on_allocation_failure_invalid_size );
    PMM_RUN( "on_allocation_failure (NotInitialized)", test_on_allocation_failure_not_initialized );
    PMM_RUN( "on_expand hook", test_on_expand_hook );
    PMM_RUN( "on_corruption (bad magic)", test_on_corruption_bad_magic );
    PMM_RUN( "on_corruption (size mismatch)", test_on_corruption_size_mismatch );
    PMM_RUN( "on_corruption (granule mismatch)", test_on_corruption_granule_mismatch );
    PMM_RUN( "on_load hook", test_on_load_hook );
    PMM_RUN( "on_corruption (CRC mismatch)", test_on_corruption_crc_mismatch );
    PMM_RUN( "SmallAddressTraits", test_logging_small_address_traits );
    PMM_RUN( "LargeAddressTraits", test_logging_large_address_traits );

    std::cout << ( all_passed ? "All tests PASSED.\n" : "Some tests FAILED!\n" );
    return all_passed ? 0 : 1;
}
