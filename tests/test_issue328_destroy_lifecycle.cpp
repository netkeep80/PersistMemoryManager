/**
 * @file test_issue328_destroy_lifecycle.cpp
 * @brief Regression tests for destroy() preserving persisted images.
 */

#include "pmm/mmap_storage.h"
#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{
using HeapMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 3280>;

struct MMapLifecycleConfig
{
    using address_traits                          = pmm::DefaultAddressTraits;
    using storage_backend                         = pmm::MMapStorage<address_traits>;
    using free_block_tree                         = pmm::AvlFreeTree<address_traits>;
    using lock_policy                             = pmm::config::NoLock;
    using logging_policy                          = pmm::logging::NoLogging;
    static constexpr std::size_t granule_size     = address_traits::granule_size;
    static constexpr std::size_t max_memory_gb    = 0;
    static constexpr std::size_t grow_numerator   = pmm::config::kDefaultGrowNumerator;
    static constexpr std::size_t grow_denominator = pmm::config::kDefaultGrowDenominator;
};

using MMapMgr = pmm::PersistMemoryManager<MMapLifecycleConfig, 3281>;

template <typename Mgr> std::uint64_t current_magic()
{
    auto* base = Mgr::backend().base_ptr();
    auto* hdr  = pmm::detail::manager_header_at<typename Mgr::address_traits>( base );
    return hdr->magic;
}
} // namespace

TEST_CASE( "I328: destroy preserves heap backend image for direct load", "[issue328][lifecycle]" )
{
    constexpr std::size_t kArena = 64 * 1024;

    REQUIRE( HeapMgr::create( kArena ) );

    auto values = HeapMgr::allocate_typed<std::uint32_t>( 4 );
    REQUIRE( !values.is_null() );
    std::uint32_t* data = values.resolve();
    REQUIRE( data != nullptr );
    for ( std::uint32_t i = 0; i < 4; ++i )
        data[i] = 0x32800000U + i;

    auto saved_offset = values.offset();
    auto saved_used   = HeapMgr::used_size();

    HeapMgr::destroy();

    REQUIRE_FALSE( HeapMgr::is_initialized() );
    REQUIRE( current_magic<HeapMgr>() == pmm::kMagic );

    pmm::VerifyResult result;
    REQUIRE( HeapMgr::load( result ) );
    REQUIRE( HeapMgr::is_initialized() );
    REQUIRE( HeapMgr::used_size() == saved_used );

    HeapMgr::pptr<std::uint32_t> loaded( saved_offset );
    std::uint32_t*               loaded_data = loaded.resolve();
    REQUIRE( loaded_data != nullptr );
    for ( std::uint32_t i = 0; i < 4; ++i )
        REQUIRE( loaded_data[i] == 0x32800000U + i );

    HeapMgr::destroy();
}

TEST_CASE( "I328: destroy preserves mmap file image across reopen", "[issue328][lifecycle]" )
{
    static const char*    kFile  = "test_issue328_mmap_lifecycle.dat";
    constexpr std::size_t kArena = 64 * 1024;
    std::remove( kFile );

    REQUIRE( MMapMgr::backend().open( kFile, kArena ) );
    REQUIRE( MMapMgr::create() );

    auto value = MMapMgr::create_typed<std::uint32_t>( 0x32803280U );
    REQUIRE( !value.is_null() );
    auto saved_offset = value.offset();

    MMapMgr::destroy();
    REQUIRE_FALSE( MMapMgr::is_initialized() );
    REQUIRE( current_magic<MMapMgr>() == pmm::kMagic );
    MMapMgr::backend().close();

    REQUIRE( MMapMgr::backend().open( kFile, kArena ) );
    pmm::VerifyResult result;
    REQUIRE( MMapMgr::load( result ) );

    MMapMgr::pptr<std::uint32_t> loaded( saved_offset );
    std::uint32_t*               loaded_value = loaded.resolve();
    REQUIRE( loaded_value != nullptr );
    REQUIRE( *loaded_value == 0x32803280U );

    MMapMgr::destroy();
    MMapMgr::backend().close();
    std::remove( kFile );
}

TEST_CASE( "I328: destroy_image explicitly invalidates backend image", "[issue328][lifecycle]" )
{
    using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 3282>;

    constexpr std::size_t kArena = 64 * 1024;

    REQUIRE( Mgr::create( kArena ) );
    REQUIRE( current_magic<Mgr>() == pmm::kMagic );

    Mgr::destroy_image();

    REQUIRE_FALSE( Mgr::is_initialized() );
    REQUIRE( current_magic<Mgr>() == 0 );

    pmm::VerifyResult result;
    REQUIRE_FALSE( Mgr::load( result ) );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidMagic );
}
