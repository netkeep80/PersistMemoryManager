/**
 * @file test_issue329_image_version.cpp
 * @brief Regression tests for explicit ManagerHeader image version validation.
 */

#include "pmm/persist_memory_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

namespace
{

using VersionMgr       = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32901>;
using VersionLoadMgr   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32902>;
using VersionVerifyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32903>;
using VersionLegacyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32904>;

} // namespace

TEST_CASE( "issue329: ManagerHeader exposes a persisted image version", "[issue329][layout]" )
{
    using Header = pmm::detail::ManagerHeader<>;

    static_assert( std::is_same_v<decltype( Header::image_version ), std::uint8_t>,
                   "ManagerHeader::image_version must be a persisted version byte" );
    static_assert( pmm::detail::kCurrentImageVersion == 1,
                   "The current persisted image layout version must start at 1" );
    static_assert( sizeof( Header ) == 64,
                   "Adding an explicit image version must preserve the default ManagerHeader size" );
}

TEST_CASE( "issue329: create stores the current image version", "[issue329][create]" )
{
    VersionMgr::destroy();
    REQUIRE( VersionMgr::create( 64 * 1024 ) );

    const auto* hdr = pmm::detail::manager_header_at<pmm::DefaultAddressTraits>( VersionMgr::backend().base_ptr() );
    REQUIRE( hdr->image_version == pmm::detail::kCurrentImageVersion );

    VersionMgr::destroy();
}

TEST_CASE( "issue329: load rejects an unsupported image version", "[issue329][load]" )
{
    VersionLoadMgr::destroy();
    REQUIRE( VersionLoadMgr::create( 64 * 1024 ) );
    VersionLoadMgr::destroy();

    auto* hdr = pmm::detail::manager_header_at<pmm::DefaultAddressTraits>( VersionLoadMgr::backend().base_ptr() );
    hdr->image_version = static_cast<std::uint8_t>( pmm::detail::kCurrentImageVersion + 1 );

    VersionLoadMgr::clear_error();
    pmm::VerifyResult result;
    REQUIRE_FALSE( VersionLoadMgr::load( result ) );

    REQUIRE( VersionLoadMgr::last_error() == pmm::PmmError::UnsupportedImageVersion );
    REQUIRE( result.entry_count == 1 );
    REQUIRE( result.entries[0].type == pmm::ViolationType::HeaderCorruption );
    REQUIRE( result.entries[0].action == pmm::DiagnosticAction::Aborted );
    REQUIRE( result.entries[0].expected == pmm::detail::kCurrentImageVersion );
    REQUIRE( result.entries[0].actual == static_cast<std::uint8_t>( pmm::detail::kCurrentImageVersion + 1 ) );
}

TEST_CASE( "issue329: load migrates legacy unversioned images to the current version", "[issue329][load]" )
{
    VersionLegacyMgr::destroy();
    REQUIRE( VersionLegacyMgr::create( 64 * 1024 ) );
    VersionLegacyMgr::destroy();

    auto* hdr = pmm::detail::manager_header_at<pmm::DefaultAddressTraits>( VersionLegacyMgr::backend().base_ptr() );
    hdr->image_version = pmm::detail::kLegacyUnversionedImageVersion;

    VersionLegacyMgr::clear_error();
    pmm::VerifyResult result;
    REQUIRE( VersionLegacyMgr::load( result ) );

    REQUIRE( VersionLegacyMgr::last_error() == pmm::PmmError::Ok );
    REQUIRE( hdr->image_version == pmm::detail::kCurrentImageVersion );

    VersionLegacyMgr::destroy();
}

TEST_CASE( "issue329: verify reports an unsupported image version as header corruption", "[issue329][verify]" )
{
    VersionVerifyMgr::destroy();
    REQUIRE( VersionVerifyMgr::create( 64 * 1024 ) );

    auto* hdr = pmm::detail::manager_header_at<pmm::DefaultAddressTraits>( VersionVerifyMgr::backend().base_ptr() );
    hdr->image_version = static_cast<std::uint8_t>( pmm::detail::kCurrentImageVersion + 1 );

    pmm::VerifyResult result = VersionVerifyMgr::verify();

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.entry_count == 1 );
    REQUIRE( result.entries[0].type == pmm::ViolationType::HeaderCorruption );
    REQUIRE( result.entries[0].action == pmm::DiagnosticAction::Aborted );
    REQUIRE( result.entries[0].expected == pmm::detail::kCurrentImageVersion );
    REQUIRE( result.entries[0].actual == static_cast<std::uint8_t>( pmm::detail::kCurrentImageVersion + 1 ) );

    hdr->image_version = pmm::detail::kCurrentImageVersion;
    VersionVerifyMgr::destroy();
}
