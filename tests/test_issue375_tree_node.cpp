/**
 * @file test_issue375_tree_node.cpp
 * @brief Tests proving sentinel removal from ManagerT::tree_node (issue #375).
 *
 * The old ref-returning tree_node API silently masked invalid pointers by
 * returning a thread-local sentinel BlockHeader. The new API splits checked
 * (try_tree_node → pointer/null) and unchecked (tree_node_unchecked → ref,
 * preconditioned) access. These tests verify the sentinel is gone.
 */

#include <catch2/catch_test_macros.hpp>

#include "pmm/pmm_presets.h"

namespace
{
using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 375>;

void setup_clean_image()
{
    if ( Mgr::is_initialized() )
        Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );
}
} // namespace

TEST_CASE( "I375-T: try_tree_node returns nullptr for null pptr", "[issue375][tree_node]" )
{
    setup_clean_image();

    Mgr::pptr<int> p;
    REQUIRE( p.is_null() );
    REQUIRE( Mgr::try_tree_node( p ) == nullptr );

    Mgr::destroy();
}

TEST_CASE( "I375-T: try_tree_node returns nullptr and sets InvalidPointer for OOB pptr", "[issue375][tree_node]" )
{
    setup_clean_image();

    Mgr::pptr<int> bad( 0xFFFF );

    Mgr::clear_error();
    auto* tn = Mgr::try_tree_node( bad );
    REQUIRE( tn == nullptr );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "I375-T: try_tree_node returns valid header for in-range pptr", "[issue375][tree_node]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    Mgr::clear_error();
    auto* tn = Mgr::try_tree_node( p );
    REQUIRE( tn != nullptr );
    REQUIRE( Mgr::last_error() == pmm::PmmError::Ok );

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}

TEST_CASE( "I375-T: pmap operations still work for valid nodes", "[issue375][tree_node]" )
{
    setup_clean_image();

    using Map = Mgr::pmap<int, int>;
    auto map  = Mgr::create_typed<Map>();
    REQUIRE( !map.is_null() );

    REQUIRE( !map->insert( 1, 100 ).is_null() );
    REQUIRE( !map->insert( 2, 200 ).is_null() );
    REQUIRE( !map->insert( 3, 300 ).is_null() );

    auto node = map->find( 2 );
    REQUIRE( !node.is_null() );
    auto* obj = node.resolve();
    REQUIRE( obj != nullptr );
    REQUIRE( obj->value == 200 );

    Mgr::destroy();
}

TEST_CASE( "I375-T: invalid pmap/tree access reports InvalidPointer (no silent sentinel write)",
           "[issue375][tree_node]" )
{
    setup_clean_image();

    Mgr::pptr<int> bad( 0xFFFF );

    Mgr::clear_error();
    auto* hdr = Mgr::try_tree_node( bad );
    REQUIRE( hdr == nullptr );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    // get_tree_X helpers must also surface InvalidPointer rather than scribble
    // on a fake header.
    Mgr::clear_error();
    auto h = Mgr::get_tree_height( bad );
    REQUIRE( h == 0 );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "I375-T: try_tree_node rejects stale pptr after deallocate", "[issue375][tree_node]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    Mgr::deallocate_typed( p );

    Mgr::clear_error();
    REQUIRE( Mgr::try_tree_node( p ) == nullptr );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "I375-T: get_tree_* / set_tree_* reject stale pptr after deallocate", "[issue375][tree_node]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    Mgr::deallocate_typed( p );

    Mgr::clear_error();
    auto h = Mgr::get_tree_height( p );
    REQUIRE( h == 0 );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::clear_error();
    auto left = Mgr::get_tree_left_offset( p );
    REQUIRE( left == 0 );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    // Snapshot the (now-free) header fields so we can prove the setters
    // did not write through.
    auto* blk_raw = pmm::detail::resolve_granule_ptr<Mgr::address_traits>(
        Mgr::backend().base_ptr(),
        static_cast<Mgr::index_type>(
            p.offset() - ( sizeof( pmm::Block<Mgr::address_traits> ) + Mgr::address_traits::granule_size - 1 ) /
                             Mgr::address_traits::granule_size ) );
    REQUIRE( blk_raw != nullptr );
    using BlockState              = pmm::BlockStateBase<Mgr::address_traits>;
    const auto saved_height       = BlockState::get_avl_height( blk_raw );
    const auto saved_left_offset  = BlockState::get_left_offset( blk_raw );
    const auto saved_right_offset = BlockState::get_right_offset( blk_raw );
    const auto saved_node_type    = BlockState::get_node_type( blk_raw );

    Mgr::clear_error();
    Mgr::set_tree_height( p, static_cast<std::uint8_t>( 99 ) );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );
    REQUIRE( BlockState::get_avl_height( blk_raw ) == saved_height );

    Mgr::clear_error();
    Mgr::set_tree_left_offset( p, static_cast<Mgr::index_type>( 0xABC ) );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );
    REQUIRE( BlockState::get_left_offset( blk_raw ) == saved_left_offset );

    Mgr::clear_error();
    Mgr::set_tree_right_offset( p, static_cast<Mgr::index_type>( 0xDEF ) );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );
    REQUIRE( BlockState::get_right_offset( blk_raw ) == saved_right_offset );

    REQUIRE( BlockState::get_node_type( blk_raw ) == saved_node_type );

    Mgr::destroy();
}

TEST_CASE( "I392: application NodeType remains valid across lifecycle", "[issue392][node_type]" )
{
    static_assert( pmm::kApplicationNodeTypeFirst == 32 );
    static_assert( !pmm::is_known_node_type( 31 ) );
    static_assert( pmm::is_known_node_type( 32 ) );
    static_assert( pmm::is_known_node_type( 255 ) );

    setup_clean_image();
    auto       p = Mgr::create_typed<int>( 7 );
    const auto t = static_cast<pmm::NodeType>( pmm::kApplicationNodeTypeFirst + 1 );

    REQUIRE( Mgr::set_application_node_type( p, t ) );
    REQUIRE( Mgr::get_node_type( p ) == t );
    REQUIRE( pmm::is_allocated( t ) );
    REQUIRE( pmm::is_mutable( t ) );
    REQUIRE( pmm::can_be_deleted_from_pap( t ) );
    REQUIRE( Mgr::resolve( p ) != nullptr );
    REQUIRE( Mgr::verify().ok );

    Mgr::pptr<int> null;
    REQUIRE_FALSE( Mgr::set_application_node_type( null, t ) );
    REQUIRE_FALSE( Mgr::set_application_node_type( p, pmm::NodeType::PMap ) );

    // Force relocation without forcing arena expansion: keep p's immediate
    // successor allocated, create an exact-fit destination later, pin its
    // right neighbour, then turn that destination into a free hole.
    auto blocker = Mgr::allocate_typed<std::byte>();
    REQUIRE( !blocker.is_null() );
    auto destination = Mgr::allocate_typed<int>( 128 );
    REQUIRE( !destination.is_null() );
    auto tail_guard = Mgr::allocate_typed<std::byte>();
    REQUIRE( !tail_guard.is_null() );
    Mgr::deallocate_typed( destination );

    auto grown = Mgr::reallocate_typed<int>( p, 1, 128 );
    REQUIRE( !grown.is_null() );
    REQUIRE( grown != p );
    REQUIRE( grown == destination );
    REQUIRE( Mgr::get_node_type( grown ) == t );
    REQUIRE( *grown == 7 );
    REQUIRE( Mgr::verify().ok );

    Mgr::deallocate_typed( blocker );
    Mgr::deallocate_typed( tail_guard );
    Mgr::destroy_typed( grown );
    REQUIRE_FALSE( Mgr::set_application_node_type( grown, t ) );
    Mgr::destroy();
}
