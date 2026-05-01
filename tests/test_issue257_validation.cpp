/*
## test-issue257-validation
req: ac-007, ac-010
*/

/**
 * @file test_issue257_validation.cpp
 * @brief Negative tests for pointer and block validation layer.
 *
 * Verifies that invalid pointers, bad alignment, bad indices, broken headers,
 * and wrong domain/root are reliably rejected by the validation layer.
 *
 * @see docs/validation_model.md — validation model specification
 * @see include/pmm/validation.h — unified validation helpers
 */

#include "pmm/io.h"
#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"
#include "pmm/validation.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using Mgr = pmm::presets::SingleThreadedHeap;
using AT  = pmm::DefaultAddressTraits;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void setup_clean_image( std::size_t arena_size = 64 * 1024 )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( arena_size ) );
}

// ─── A. Pointer provenance tests ──────────────────────────────────────────────

TEST_CASE( "validation: null pointer is rejected", "[test_issue257]" )
{
    setup_clean_image();

    // deallocate(nullptr) is a no-op (safe)
    Mgr::deallocate( nullptr );

    // resolve(null pptr) returns nullptr
    Mgr::pptr<int> null_p;
    REQUIRE( Mgr::resolve( null_p ) == nullptr );

    // is_valid_ptr(null) returns false
    REQUIRE_FALSE( Mgr::is_valid_ptr( null_p ) );

    Mgr::destroy();
}

TEST_CASE( "validation: foreign pointer is rejected by deallocate", "[test_issue257]" )
{
    setup_clean_image();

    // A pointer to stack memory is foreign — should be silently rejected.
    int stack_val = 42;
    Mgr::deallocate( &stack_val );

    // Manager state should be unaffected.
    REQUIRE( Mgr::block_count() > 0 );

    Mgr::destroy();
}

TEST_CASE( "validation: pointer before managed area is rejected", "[test_issue257]" )
{
    setup_clean_image();
    std::uint8_t* base = Mgr::backend().base_ptr();

    // Pointer before base — find_block_from_user_ptr returns nullptr.
    void* before_base = base - 16;
    Mgr::deallocate( before_base );

    // Manager still works.
    auto p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    Mgr::destroy();
}

TEST_CASE( "validation: pointer past managed area is rejected", "[test_issue257]" )
{
    setup_clean_image();
    std::uint8_t* base       = Mgr::backend().base_ptr();
    std::size_t   total_size = Mgr::backend().total_size();

    // Pointer beyond end of managed area.
    void* past_end = base + total_size + 16;
    Mgr::deallocate( past_end );

    // Manager still works.
    auto p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    Mgr::destroy();
}

// ─── B. Address correctness tests ──────────────────────────────────────────────

TEST_CASE( "validation: misaligned pointer is rejected by header_from_ptr", "[test_issue257]" )
{
    setup_clean_image();
    std::uint8_t* base       = Mgr::backend().base_ptr();
    std::size_t   total_size = Mgr::backend().total_size();

    // Allocate a valid block to get a valid pointer.
    auto p = Mgr::allocate_typed<std::uint64_t>();
    REQUIRE( !p.is_null() );

    // Create a misaligned pointer (odd offset from base).
    void* misaligned = base + 33; // Not granule-aligned relative to base.
    auto* blk        = pmm::detail::header_from_ptr_t<AT>( base, misaligned, total_size );
    REQUIRE( blk == nullptr );

    Mgr::destroy();
}

TEST_CASE( "validation: validate_block_index rejects no_block sentinel", "[test_issue257]" )
{
    REQUIRE_FALSE( pmm::detail::validate_block_index<AT>( 1024, AT::no_block ) );
}

TEST_CASE( "validation: validate_block_index rejects index beyond total_size", "[test_issue257]" )
{
    // total_size = 1024 bytes, granule_size = 16 → max valid index = (1024 - 32) / 16 = 62
    // Index 65 * 16 + 32 = 1072 > 1024 → rejected.
    REQUIRE_FALSE( pmm::detail::validate_block_index<AT>( 1024, 65 ) );
}

TEST_CASE( "validation: validate_block_index accepts valid index", "[test_issue257]" )
{
    // Index 2 * 16 + 32 = 64 <= 1024 → accepted.
    REQUIRE( pmm::detail::validate_block_index<AT>( 1024, 2 ) );
}

TEST_CASE( "validation: validate_link_index accepts no_block sentinel", "[test_issue257]" )
{
    REQUIRE( pmm::detail::validate_link_index<AT>( 1024, AT::no_block ) );
}

TEST_CASE( "validation: validate_link_index rejects out-of-range index", "[test_issue257]" )
{
    REQUIRE_FALSE( pmm::detail::validate_link_index<AT>( 1024, 100 ) );
}

TEST_CASE( "validation: pptr_from_byte_offset rejects misaligned offset", "[test_issue257]" )
{
    setup_clean_image();

    // Odd byte offset — not divisible by granule_size.
    auto p = Mgr::pptr_from_byte_offset<int>( 17 );
    REQUIRE( p.is_null() );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "validation: pptr_from_byte_offset rejects zero offset", "[test_issue257]" )
{
    setup_clean_image();

    auto p = Mgr::pptr_from_byte_offset<int>( 0 );
    REQUIRE( p.is_null() );

    Mgr::destroy();
}

TEST_CASE( "validation: resolve rejects out-of-bounds pptr", "[test_issue257]" )
{
    setup_clean_image();
    std::size_t total_size = Mgr::backend().total_size();

    // Create a pptr with offset beyond total_size.
    std::size_t    bad_offset = total_size / AT::granule_size + 1;
    Mgr::pptr<int> bad_p( static_cast<AT::index_type>( bad_offset ) );

    auto* resolved = Mgr::resolve( bad_p );
    REQUIRE( resolved == nullptr );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

// ─── C. Header integrity tests ────────────────────────────────────────────────

TEST_CASE( "validation: corrupted root_offset on allocated block detected by verify", "[test_issue257]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Post-#369: state is determined by NodeType, not by weight==0. The
    // canonical inconsistency for an allocated block is root_offset != own_idx.
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, 0xDEAD );

    // verify() should detect inconsistency.
    pmm::VerifyResult result = Mgr::verify();
    REQUIRE_FALSE( result.ok );

    bool found_block_state_issue = false;
    for ( std::size_t i = 0; i < result.entry_count; ++i )
    {
        if ( result.entries[i].type == pmm::ViolationType::BlockStateInconsistent )
            found_block_state_issue = true;
    }
    REQUIRE( found_block_state_issue );

    Mgr::destroy();
}

TEST_CASE( "validation: corrupted root_offset detected by verify", "[test_issue257]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt: set root_offset to a wrong value.
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );
    auto          orig    = pmm::BlockStateBase<AT>::get_root_offset( blk_raw );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, orig + 100 );

    pmm::VerifyResult result = Mgr::verify();
    REQUIRE_FALSE( result.ok );

    bool found = false;
    for ( std::size_t i = 0; i < result.entry_count; ++i )
    {
        if ( result.entries[i].type == pmm::ViolationType::BlockStateInconsistent )
            found = true;
    }
    REQUIRE( found );

    Mgr::destroy();
}

TEST_CASE( "validation: corrupted next_offset detected by full verify", "[test_issue257]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt next_offset to point beyond image.
    std::uint8_t* base       = Mgr::backend().base_ptr();
    std::size_t   total_size = Mgr::backend().total_size();
    std::size_t   usr_off    = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw    = base + usr_off - sizeof( pmm::Block<AT> );

    // Set next_offset to an index far beyond image bounds.
    AT::index_type bad_next = static_cast<AT::index_type>( total_size / AT::granule_size + 100 );
    pmm::BlockStateBase<AT>::set_next_offset_of( blk_raw, bad_next );

    pmm::VerifyResult result = Mgr::verify();
    REQUIRE_FALSE( result.ok );

    bool found = false;
    for ( std::size_t i = 0; i < result.entry_count; ++i )
    {
        if ( result.entries[i].type == pmm::ViolationType::BlockStateInconsistent )
            found = true;
    }
    REQUIRE( found );

    Mgr::destroy();
}

TEST_CASE( "validation: corrupted node_type detected by full verify", "[test_issue257]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt node_type to an invalid value.
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );
    pmm::BlockStateBase<AT>::set_node_type_of( blk_raw, static_cast<pmm::NodeType>( 0xEF ) );

    pmm::VerifyResult result = Mgr::verify();
    REQUIRE_FALSE( result.ok );

    bool found = false;
    for ( std::size_t i = 0; i < result.entry_count; ++i )
    {
        if ( result.entries[i].type == pmm::ViolationType::BlockStateInconsistent )
            found = true;
    }
    REQUIRE( found );

    Mgr::destroy();
}

TEST_CASE( "validation: block data exceeding image bounds detected", "[test_issue257]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt weight to a huge value that exceeds image bounds.
    std::uint8_t* base    = Mgr::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );
    pmm::BlockStateBase<AT>::set_weight_of( blk_raw, 0x7FFFFFFFU );

    pmm::VerifyResult result = Mgr::verify();
    REQUIRE_FALSE( result.ok );

    Mgr::destroy();
}

// ─── D. validate_user_ptr tests ────────────────────────────────────────────────

TEST_CASE( "validation: validate_user_ptr rejects null", "[test_issue257]" )
{
    std::uint8_t dummy[256] = {};
    std::size_t  min_offset =
        sizeof( pmm::Block<AT> ) + sizeof( pmm::detail::ManagerHeader<AT> ) + sizeof( pmm::Block<AT> );
    REQUIRE_FALSE( pmm::detail::validate_user_ptr<AT>( dummy, 256, nullptr, min_offset ) );
}

TEST_CASE( "validation: validate_user_ptr rejects pointer before min address", "[test_issue257]" )
{
    std::uint8_t dummy[256] = {};
    std::size_t  min_offset =
        sizeof( pmm::Block<AT> ) + sizeof( pmm::detail::ManagerHeader<AT> ) + sizeof( pmm::Block<AT> );
    // Pointer to start of buffer (before min address).
    REQUIRE_FALSE( pmm::detail::validate_user_ptr<AT>( dummy, 256, dummy + 16, min_offset ) );
}

TEST_CASE( "validation: validate_user_ptr rejects image-end pointer", "[test_issue257]" )
{
    std::uint8_t dummy[256] = {};
    std::size_t  min_offset =
        sizeof( pmm::Block<AT> ) + sizeof( pmm::detail::ManagerHeader<AT> ) + sizeof( pmm::Block<AT> );
    REQUIRE_FALSE( pmm::detail::validate_user_ptr<AT>( dummy, 256, dummy + 256, min_offset ) );
}

TEST_CASE( "validation: validate_user_ptr rejects misaligned pointer", "[test_issue257]" )
{
    std::uint8_t dummy[256] = {};
    std::size_t  min_offset =
        sizeof( pmm::Block<AT> ) + sizeof( pmm::detail::ManagerHeader<AT> ) + sizeof( pmm::Block<AT> );
    // Pointer at min_offset + 1 (misaligned: block header would be at odd offset).
    REQUIRE_FALSE( pmm::detail::validate_user_ptr<AT>( dummy, 256, dummy + min_offset + 1, min_offset ) );
}

TEST_CASE( "validation: validate_user_ptr accepts valid pointer", "[test_issue257]" )
{
    std::uint8_t dummy[256] = {};
    std::size_t  min_offset =
        sizeof( pmm::Block<AT> ) + sizeof( pmm::detail::ManagerHeader<AT> ) + sizeof( pmm::Block<AT> );
    // Valid pointer: at min_offset, and (min_offset - sizeof(Block<AT>)) % granule_size == 0.
    // min_offset = 32 + 64 + 32 = 128. Block header candidate = 128 - 32 = 96. 96 % 16 == 0. Valid.
    REQUIRE( pmm::detail::validate_user_ptr<AT>( dummy, 256, dummy + min_offset, min_offset ) );
}

TEST_CASE( "validation: header_from_ptr uses validate_user_ptr for cheap rejection", "[test_issue257]" )
{
    std::uint8_t dummy[256] = {};
    std::size_t  min_offset =
        sizeof( pmm::Block<AT> ) + sizeof( pmm::detail::ManagerHeader<AT> ) + sizeof( pmm::Block<AT> );
    void* cases[] = {
        nullptr,
        dummy + 16,
        dummy + 256,
        dummy + min_offset + 1,
    };

    for ( void* ptr : cases )
    {
        CHECK_FALSE( pmm::detail::validate_user_ptr<AT>( dummy, 256, ptr, min_offset ) );
        CHECK( pmm::detail::header_from_ptr_t<AT>( dummy, ptr, 256 ) == nullptr );
    }
}

// ─── E. ArenaAddress::block (canonical address layer) tests ─────────────────

TEST_CASE( "validation: ArenaAddress::block returns nullptr for no_block", "[test_issue257]" )
{
    std::uint8_t                  dummy[256] = {};
    pmm::detail::ArenaAddress<AT> addr{ dummy, std::size_t{ 256 } };
    REQUIRE( addr.block( AT::no_block ) == nullptr );
}

TEST_CASE( "validation: ArenaAddress::block returns nullptr for out-of-range index", "[test_issue257]" )
{
    std::uint8_t                  dummy[256] = {};
    pmm::detail::ArenaAddress<AT> addr{ dummy, std::size_t{ 256 } };
    REQUIRE( addr.block( 20 ) == nullptr );
}

TEST_CASE( "validation: ArenaAddress::block returns valid pointer for in-range index", "[test_issue257]" )
{
    std::uint8_t                  dummy[256] = {};
    pmm::detail::ArenaAddress<AT> addr{ dummy, std::size_t{ 256 } };
    auto*                         blk = addr.block( 2 );
    REQUIRE( blk != nullptr );
    REQUIRE( reinterpret_cast<std::uint8_t*>( blk ) == dummy + 2 * 16 );
}

// ─── F. ArenaAddress::try_user_ptr tests ────────────────────────────────────

TEST_CASE( "validation: ArenaAddress::try_user_ptr returns nullptr for zero idx", "[test_issue257]" )
{
    std::uint8_t                  dummy[256] = {};
    pmm::detail::ArenaAddress<AT> addr{ dummy, std::size_t{ 256 } };
    REQUIRE( addr.try_user_ptr( 0 ) == nullptr );
}

TEST_CASE( "validation: ArenaAddress::try_user_ptr returns nullptr for out-of-bounds idx", "[test_issue257]" )
{
    std::uint8_t                  dummy[256] = {};
    pmm::detail::ArenaAddress<AT> addr{ dummy, std::size_t{ 256 } };
    REQUIRE( addr.try_user_ptr( 20 ) == nullptr );
}

TEST_CASE( "validation: ArenaAddress::try_user_ptr returns valid ptr for in-range idx", "[test_issue257]" )
{
    std::uint8_t                  dummy[256] = {};
    pmm::detail::ArenaAddress<AT> addr{ dummy, std::size_t{ 256 } };
    REQUIRE( addr.try_user_ptr( 3 ) == dummy + 48 );
}

// ─── G. ArenaAddress::try_user_idx_from_raw tests ───────────────────────────

TEST_CASE( "validation: ArenaAddress::try_user_idx_from_raw nullopt for null", "[test_issue257]" )
{
    std::uint8_t                  dummy[256] = {};
    pmm::detail::ArenaAddress<AT> addr{ dummy, std::size_t{ 256 } };
    REQUIRE_FALSE( addr.try_user_idx_from_raw( nullptr ).has_value() );
}

TEST_CASE( "validation: ArenaAddress::try_user_idx_from_raw nullopt for out-of-bounds", "[test_issue257]" )
{
    std::uint8_t                  dummy[256] = {};
    pmm::detail::ArenaAddress<AT> addr{ dummy, std::size_t{ 256 } };
    const void* oob_ptr = reinterpret_cast<const void*>( reinterpret_cast<std::uintptr_t>( dummy ) + 300 );
    REQUIRE_FALSE( addr.try_user_idx_from_raw( oob_ptr ).has_value() );
}

TEST_CASE( "validation: ArenaAddress::try_user_idx_from_raw nullopt for misaligned", "[test_issue257]" )
{
    std::uint8_t                  dummy[256] = {};
    pmm::detail::ArenaAddress<AT> addr{ dummy, std::size_t{ 256 } };
    REQUIRE_FALSE( addr.try_user_idx_from_raw( dummy + 7 ).has_value() );
}

TEST_CASE( "validation: ArenaAddress::try_user_idx_from_raw returns correct index", "[test_issue257]" )
{
    std::uint8_t                  dummy[256] = {};
    pmm::detail::ArenaAddress<AT> addr{ dummy, std::size_t{ 256 } };
    auto                          idx = addr.try_user_idx_from_raw( dummy + 48 );
    REQUIRE( idx.has_value() );
    REQUIRE( *idx == 3 );
}

// ─── H. validate_block_header_full tests ──────────────────────────────────────

TEST_CASE( "validation: validate_block_header_full on clean image reports no issues", "[test_issue257]" )
{
    setup_clean_image();
    std::uint8_t* base       = Mgr::backend().base_ptr();
    std::size_t   total_size = Mgr::backend().total_size();

    auto p = Mgr::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Get the block index.
    std::size_t             usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    typename AT::index_type blk_idx =
        static_cast<typename AT::index_type>( ( usr_off - sizeof( pmm::Block<AT> ) ) / AT::granule_size );

    pmm::VerifyResult result;
    pmm::detail::validate_block_header_full<AT>( base, total_size, blk_idx, result );
    REQUIRE( result.ok );

    Mgr::destroy();
}

TEST_CASE( "validation: validate_block_header_full detects invalid index", "[test_issue257]" )
{
    pmm::VerifyResult result;
    std::uint8_t      dummy[256] = {};
    pmm::detail::validate_block_header_full<AT>( dummy, 256, AT::no_block, result );
    REQUIRE_FALSE( result.ok );
}

// ─── I. End-to-end: load repairs detectable corruption ────────────────────────

// Use unique instance IDs for file-backed tests to avoid conflicts.
using MgrSave = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2570>;
using MgrLoad = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2571>;

TEST_CASE( "validation: load repairs inconsistent block state", "[test_issue257]" )
{
    static constexpr const char* kFile = "test_issue257_repair.pmm";

    MgrSave::destroy();
    REQUIRE( MgrSave::create( 64 * 1024 ) );

    auto p = MgrSave::allocate_typed<std::uint64_t>( 4 );
    REQUIRE( !p.is_null() );

    // Corrupt root_offset.
    std::uint8_t* base    = MgrSave::backend().base_ptr();
    std::size_t   usr_off = static_cast<std::size_t>( p.offset() ) * AT::granule_size;
    void*         blk_raw = base + usr_off - sizeof( pmm::Block<AT> );
    auto          orig    = pmm::BlockStateBase<AT>::get_root_offset( blk_raw );
    pmm::BlockStateBase<AT>::set_root_offset_of( blk_raw, orig + 50 );

    // Save corrupted image to file.
    REQUIRE( pmm::save_manager<MgrSave>( kFile ) );
    MgrSave::destroy();

    // Load into a fresh manager — create() allocates the buffer, then load overwrites it.
    MgrLoad::destroy();
    REQUIRE( MgrLoad::create( 64 * 1024 ) );
    pmm::VerifyResult load_result;
    REQUIRE( pmm::load_manager_from_file<MgrLoad>( kFile, load_result ) );

    // The repair phase should have fixed the inconsistency.
    pmm::VerifyResult verify_after = MgrLoad::verify();
    REQUIRE( verify_after.ok );

    MgrLoad::destroy();
    std::remove( kFile );
}

// ─── J. Clean image: verify reports no violations ─────────────────────────────

TEST_CASE( "validation: clean image verify produces no violations", "[test_issue257]" )
{
    setup_clean_image();

    auto p1 = Mgr::allocate_typed<std::uint64_t>( 8 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 4 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );

    pmm::VerifyResult result = Mgr::verify();
    REQUIRE( result.ok );
    REQUIRE( result.violation_count == 0 );

    Mgr::deallocate_typed( p1 );
    Mgr::deallocate_typed( p2 );

    result = Mgr::verify();
    REQUIRE( result.ok );
    REQUIRE( result.violation_count == 0 );

    Mgr::destroy();
}

// ─── G. Caller nullptr-propagation tests (owner feedback) ────────────────────
// Verify that wrapper APIs above block_raw_ptr_from_pptr / block_raw_mut_ptr_from_pptr
// correctly handle nullptr for forged/out-of-bounds pptrs instead of dereferencing.

TEST_CASE( "validation: get_tree_idx_field returns 0 and sets InvalidPointer for OOB pptr", "[test_issue257]" )
{
    setup_clean_image();

    // Forge a pptr with an absurdly large offset — well past the managed region.
    Mgr::pptr<int> bad( 0xFFFF );

    Mgr::clear_error();
    auto left = Mgr::get_tree_left_offset( bad );
    CHECK( left == 0 );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::clear_error();
    auto right = Mgr::get_tree_right_offset( bad );
    CHECK( right == 0 );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::clear_error();
    auto parent = Mgr::get_tree_parent_offset( bad );
    CHECK( parent == 0 );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "validation: set_tree_idx_field is no-op and sets InvalidPointer for OOB pptr", "[test_issue257]" )
{
    setup_clean_image();

    Mgr::pptr<int> bad( 0xFFFF );

    Mgr::clear_error();
    Mgr::set_tree_left_offset( bad, 42 );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::clear_error();
    Mgr::set_tree_right_offset( bad, 42 );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::clear_error();
    Mgr::set_tree_parent_offset( bad, 42 );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "validation: get_tree_weight returns 0 and sets InvalidPointer for OOB pptr", "[test_issue257]" )
{
    setup_clean_image();

    Mgr::pptr<int> bad( 0xFFFF );

    Mgr::clear_error();
    auto w = Mgr::get_tree_weight( bad );
    CHECK( w == 0 );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "validation: set_tree_weight is no-op and sets InvalidPointer for OOB pptr", "[test_issue257]" )
{
    setup_clean_image();

    Mgr::pptr<int> bad( 0xFFFF );

    Mgr::clear_error();
    Mgr::set_tree_weight( bad, 10 );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "validation: get_tree_height returns 0 and sets InvalidPointer for OOB pptr", "[test_issue257]" )
{
    setup_clean_image();

    Mgr::pptr<int> bad( 0xFFFF );

    Mgr::clear_error();
    auto h = Mgr::get_tree_height( bad );
    CHECK( h == 0 );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "validation: set_tree_height is no-op and sets InvalidPointer for OOB pptr", "[test_issue257]" )
{
    setup_clean_image();

    Mgr::pptr<int> bad( 0xFFFF );

    Mgr::clear_error();
    Mgr::set_tree_height( bad, 5 );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "validation: try_tree_node returns nullptr and sets InvalidPointer for OOB pptr", "[test_issue257]" )
{
    setup_clean_image();

    Mgr::pptr<int> bad( 0xFFFF );

    Mgr::clear_error();
    auto* tn = Mgr::try_tree_node( bad );
    CHECK( tn == nullptr );
    CHECK( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "validation: try_tree_node returns nullptr for null pptr without error", "[test_issue257]" )
{
    setup_clean_image();

    Mgr::pptr<int> null_p;
    REQUIRE( null_p.is_null() );

    Mgr::clear_error();
    auto* tn = Mgr::try_tree_node( null_p );
    CHECK( tn == nullptr );

    Mgr::destroy();
}

TEST_CASE( "validation: try_tree_node never returns a writable sentinel for invalid pptr", "[test_issue257]" )
{
    setup_clean_image();

    Mgr::pptr<int> bad1( 0xFFFF );
    Mgr::pptr<int> bad2( 0xFFFE );

    Mgr::clear_error();
    auto* tn1 = Mgr::try_tree_node( bad1 );
    REQUIRE( tn1 == nullptr );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::clear_error();
    auto* tn2 = Mgr::try_tree_node( bad2 );
    REQUIRE( tn2 == nullptr );
    REQUIRE( Mgr::last_error() == pmm::PmmError::InvalidPointer );

    Mgr::destroy();
}

TEST_CASE( "validation: valid pptr still works correctly through wrapper APIs", "[test_issue257]" )
{
    setup_clean_image();

    auto p = Mgr::allocate_typed<int>();
    REQUIRE( !p.is_null() );

    // The valid pptr should not set InvalidPointer.
    Mgr::clear_error();
    auto w = Mgr::get_tree_weight( p );
    CHECK( Mgr::last_error() == pmm::PmmError::Ok );
    CHECK( w > 0 ); // allocated block has a positive weight

    Mgr::clear_error();
    auto h = Mgr::get_tree_height( p );
    CHECK( Mgr::last_error() == pmm::PmmError::Ok );
    (void)h;

    Mgr::clear_error();
    auto left = Mgr::get_tree_left_offset( p );
    CHECK( Mgr::last_error() == pmm::PmmError::Ok );
    (void)left;

    Mgr::deallocate_typed( p );
    Mgr::destroy();
}
