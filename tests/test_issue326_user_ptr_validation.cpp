/*
## test-issue326-user-ptr-validation
req: ac-010
*/

/**
 * @file test_issue326_user_ptr_validation.cpp
 * @brief Regression tests for forged user pointer rejection.
 */

#include "pmm/block_state.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"
#include "pmm/types.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

namespace
{
using Mgr        = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 326>;
using AT         = typename Mgr::address_traits;
using BlockState = pmm::BlockStateBase<AT>;

void reset_manager()
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );
}

void write_fake_allocated_header( void* raw )
{
    std::memset( raw, 0, sizeof( pmm::Block<AT> ) );
    BlockState::set_weight_of( raw, 1 );
    BlockState::set_prev_offset_of( raw, AT::no_block );
    BlockState::set_next_offset_of( raw, AT::no_block );
    BlockState::set_node_type_of( raw, pmm::NodeType::Generic );
}

void write_self_consistent_fake_allocated_header( std::uint8_t* base, void* raw )
{
    write_fake_allocated_header( raw );
    BlockState::set_root_offset_of( raw, pmm::detail::ptr_to_granule_idx<AT>( base, raw ) );
}
} // namespace

TEST_CASE( "I326: header_from_ptr_t rejects forged aligned payload pointer", "[issue326][validation]" )
{
    reset_manager();

    auto p = Mgr::allocate_typed<std::uint32_t>( 32 );
    REQUIRE( !p.is_null() );

    std::uint8_t* base       = Mgr::backend().base_ptr();
    std::size_t   total_size = Mgr::backend().total_size();
    auto*         payload    = reinterpret_cast<std::uint8_t*>( Mgr::resolve( p ) );
    REQUIRE( payload != nullptr );

    auto* real_block = pmm::detail::header_from_ptr_t<AT>( base, payload, total_size );
    REQUIRE( real_block != nullptr );

    write_fake_allocated_header( payload );
    void* forged = payload + sizeof( pmm::Block<AT> );

    REQUIRE( pmm::detail::header_from_ptr_t<AT>( base, forged, total_size ) == nullptr );
    REQUIRE( pmm::detail::header_from_ptr_t<AT>( base, payload, total_size ) == real_block );

    Mgr::destroy();
}

TEST_CASE( "I326: header_from_ptr_t rejects self-consistent forged payload header", "[issue326][validation]" )
{
    reset_manager();

    auto p = Mgr::allocate_typed<std::uint32_t>( 32 );
    REQUIRE( !p.is_null() );

    std::uint8_t* base       = Mgr::backend().base_ptr();
    std::size_t   total_size = Mgr::backend().total_size();
    auto*         payload    = reinterpret_cast<std::uint8_t*>( Mgr::resolve( p ) );
    REQUIRE( payload != nullptr );

    auto* real_block = pmm::detail::header_from_ptr_t<AT>( base, payload, total_size );
    REQUIRE( real_block != nullptr );

    write_self_consistent_fake_allocated_header( base, payload );
    void* forged = payload + sizeof( pmm::Block<AT> );

    REQUIRE( pmm::detail::header_from_ptr_t<AT>( base, forged, total_size ) == nullptr );
    REQUIRE( pmm::detail::header_from_ptr_t<AT>( base, payload, total_size ) == real_block );

    Mgr::destroy();
}

TEST_CASE( "I326: deallocate ignores forged aligned payload pointer", "[issue326][validation]" )
{
    reset_manager();

    auto p = Mgr::allocate_typed<std::uint32_t>( 32 );
    REQUIRE( !p.is_null() );

    std::uint8_t* base       = Mgr::backend().base_ptr();
    std::size_t   total_size = Mgr::backend().total_size();
    auto*         payload    = reinterpret_cast<std::uint8_t*>( Mgr::resolve( p ) );
    REQUIRE( payload != nullptr );

    auto* real_block      = pmm::detail::header_from_ptr_t<AT>( base, payload, total_size );
    auto  original_weight = BlockState::get_weight( real_block );
    REQUIRE( original_weight > 0 );

    write_fake_allocated_header( payload );
    void* forged = payload + sizeof( pmm::Block<AT> );

    Mgr::deallocate( forged );

    REQUIRE( BlockState::get_weight( real_block ) == original_weight );
    REQUIRE( pmm::detail::header_from_ptr_t<AT>( base, payload, total_size ) == real_block );
    REQUIRE( Mgr::verify().ok );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}

TEST_CASE( "I326: deallocate ignores self-consistent forged payload header", "[issue326][validation]" )
{
    reset_manager();

    auto p = Mgr::allocate_typed<std::uint32_t>( 32 );
    REQUIRE( !p.is_null() );

    std::uint8_t* base       = Mgr::backend().base_ptr();
    std::size_t   total_size = Mgr::backend().total_size();
    auto*         payload    = reinterpret_cast<std::uint8_t*>( Mgr::resolve( p ) );
    REQUIRE( payload != nullptr );

    auto* real_block      = pmm::detail::header_from_ptr_t<AT>( base, payload, total_size );
    auto  original_weight = BlockState::get_weight( real_block );
    REQUIRE( original_weight > 0 );

    write_self_consistent_fake_allocated_header( base, payload );
    void* forged = payload + sizeof( pmm::Block<AT> );

    Mgr::deallocate( forged );

    REQUIRE( BlockState::get_weight( real_block ) == original_weight );
    REQUIRE( pmm::detail::header_from_ptr_t<AT>( base, payload, total_size ) == real_block );
    REQUIRE( Mgr::verify().ok );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}
