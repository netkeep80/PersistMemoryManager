/**
 * @file test_issue258_structural.cpp
 * @brief Structural invariant tests.
 *
 * Covers test matrix group C: linked-list topology, block count
 * consistency, free-tree AVL balance, weight/state consistency,
 * no overlapping blocks, total size equals sum of blocks.
 *
 * @see docs/test_matrix.md — C1–C8
 * @see docs/core_invariants.md — B1a, B1b, B3b, B4a, B4b
 */

#include "pmm/persist_memory_manager.h"
#include "pmm/pmm_presets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <vector>

using AT  = pmm::DefaultAddressTraits;
using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 25820>;

// ─── Helper: walk the linked list and collect block info ─────────────────────

struct BlockInfo
{
    AT::index_type idx;
    AT::index_type prev;
    AT::index_type next;
    AT::index_type weight;
    AT::index_type root_offset;
    AT::index_type total_granules;
    bool           is_free;
};

static std::vector<BlockInfo> walk_blocks( std::uint8_t* base, std::size_t total_size )
{
    using BlockState              = pmm::BlockStateBase<AT>;
    constexpr std::size_t kGranSz = AT::granule_size;

    auto* hdr = pmm::detail::manager_header_at<AT>( base );

    std::vector<BlockInfo> blocks;
    AT::index_type         idx = hdr->first_block_offset;

    while ( idx != AT::no_block )
    {
        void* blk_raw = base + static_cast<std::size_t>( idx ) * kGranSz;

        BlockInfo bi;
        bi.idx         = idx;
        bi.prev        = BlockState::get_prev_offset( blk_raw );
        bi.next        = BlockState::get_next_offset( blk_raw );
        bi.weight      = BlockState::get_weight( blk_raw );
        bi.root_offset = BlockState::get_root_offset( blk_raw );
        bi.is_free     = ( bi.weight == 0 && bi.root_offset == 0 );

        AT::index_type total_gran = static_cast<AT::index_type>( total_size / kGranSz );
        if ( bi.next != AT::no_block )
            bi.total_granules = bi.next - idx;
        else
            bi.total_granules = total_gran - idx;

        blocks.push_back( bi );
        idx = bi.next;
    }

    return blocks;
}

// ─── C1: Linked-list topology ───────────────────────────────────────────────

TEST_CASE( "structural: linked-list prev/next consistency", "[issue258][structural]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    auto p1 = Mgr::allocate_typed<std::uint32_t>( 10 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 20 );
    auto p3 = Mgr::allocate_typed<std::uint32_t>( 30 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );
    REQUIRE( !p3.is_null() );
    Mgr::deallocate_typed( p2 );

    auto blocks = walk_blocks( Mgr::backend().base_ptr(), Mgr::total_size() );
    REQUIRE( blocks.size() >= 3 );

    for ( std::size_t i = 0; i < blocks.size(); ++i )
    {
        if ( i == 0 )
        {
            REQUIRE( blocks[i].prev == AT::no_block );
        }
        else
        {
            REQUIRE( blocks[i].prev == blocks[i - 1].idx );
        }

        if ( i < blocks.size() - 1 )
        {
            REQUIRE( blocks[i].next == blocks[i + 1].idx );
        }
        else
        {
            REQUIRE( blocks[i].next == AT::no_block );
        }
    }

    Mgr::destroy();
}

// ─── C2: Block count consistency ────────────────────────────────────────────

TEST_CASE( "structural: block count matches walk count", "[issue258][structural]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    auto p1 = Mgr::allocate_typed<std::uint32_t>( 8 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 16 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );

    auto blocks = walk_blocks( Mgr::backend().base_ptr(), Mgr::total_size() );

    REQUIRE( blocks.size() == Mgr::block_count() );

    std::size_t free_count  = 0;
    std::size_t alloc_count = 0;
    for ( const auto& b : blocks )
    {
        if ( b.is_free )
            ++free_count;
        else
            ++alloc_count;
    }
    REQUIRE( free_count == Mgr::free_block_count() );
    REQUIRE( alloc_count == Mgr::alloc_block_count() );

    Mgr::destroy();
}

// ─── C3: Free-tree AVL balance ──────────────────────────────────────────────

TEST_CASE( "structural: free-tree AVL balance factor within [-1,+1]", "[issue258][structural]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    // Create several free blocks of varying sizes to build a non-trivial tree.
    std::vector<typename Mgr::template pptr<std::uint32_t>> ptrs;
    for ( int i = 0; i < 12; ++i )
    {
        auto p = Mgr::allocate_typed<std::uint32_t>( 8 + i * 4 );
        REQUIRE( !p.is_null() );
        ptrs.push_back( p );
    }
    // Insert barriers to prevent coalescing of freed blocks.
    std::vector<typename Mgr::template pptr<std::uint8_t>> barriers;
    for ( int i = 0; i < 6; ++i )
    {
        auto b = Mgr::allocate_typed<std::uint8_t>( 16 );
        REQUIRE( !b.is_null() );
        barriers.push_back( b );
    }
    // Free every other block to populate the free tree with varied-size entries.
    for ( int i = 0; i < 12; i += 2 )
    {
        Mgr::deallocate_typed( ptrs[static_cast<std::size_t>( i )] );
    }

    // Collect all free-tree nodes with their structural info.
    std::map<std::ptrdiff_t, pmm::FreeBlockView> nodes;
    Mgr::for_each_free_block( [&]( const pmm::FreeBlockView& v ) { nodes[v.offset] = v; } );

    REQUIRE( nodes.size() >= 2 );

    for ( const auto& [off, v] : nodes )
    {
        int left_h  = 0;
        int right_h = 0;

        if ( v.left_offset != -1 )
        {
            auto it = nodes.find( v.left_offset );
            REQUIRE( it != nodes.end() );
            left_h = it->second.avl_height;
        }
        if ( v.right_offset != -1 )
        {
            auto it = nodes.find( v.right_offset );
            REQUIRE( it != nodes.end() );
            right_h = it->second.avl_height;
        }

        int balance = left_h - right_h;
        // AVL invariant: balance factor must be -1, 0, or +1.
        REQUIRE( balance >= -1 );
        REQUIRE( balance <= 1 );

        // Stored height must equal max(left_h, right_h) + 1.
        int expected_h = ( ( left_h > right_h ) ? left_h : right_h ) + 1;
        REQUIRE( v.avl_height == expected_h );
    }

    Mgr::destroy();
}

// ─── C4: Tree ownership (root_offset match) ─────────────────────────────────

TEST_CASE( "structural: root_offset consistency for all blocks", "[issue258][structural]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    auto p1 = Mgr::allocate_typed<std::uint64_t>( 4 );
    auto p2 = Mgr::allocate_typed<std::uint64_t>( 8 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );
    Mgr::deallocate_typed( p1 );

    auto blocks = walk_blocks( Mgr::backend().base_ptr(), Mgr::total_size() );

    for ( const auto& b : blocks )
    {
        if ( b.is_free )
        {
            REQUIRE( b.weight == 0 );
            REQUIRE( b.root_offset == 0 );
        }
        else
        {
            REQUIRE( b.weight > 0 );
            REQUIRE( b.root_offset == b.idx );
        }
    }

    Mgr::destroy();
}

// ─── C5: Domain membership consistency ──────────────────────────────────────

TEST_CASE( "structural: system domains exist and have correct flags", "[issue258][structural]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    REQUIRE( Mgr::has_domain( pmm::detail::kSystemDomainFreeTree ) );
    REQUIRE( Mgr::has_domain( pmm::detail::kSystemDomainSymbols ) );
    REQUIRE( Mgr::has_domain( pmm::detail::kSystemDomainRegistry ) );

    REQUIRE( Mgr::find_domain_by_name( pmm::detail::kSystemDomainFreeTree ) != 0 );
    REQUIRE( Mgr::find_domain_by_name( pmm::detail::kSystemDomainSymbols ) != 0 );
    REQUIRE( Mgr::find_domain_by_name( pmm::detail::kSystemDomainRegistry ) != 0 );

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE( v.ok );

    Mgr::destroy();
}

// ─── C6: Weight/state consistency for all blocks after operations ───────────

TEST_CASE( "structural: weight/state consistent after alloc/dealloc mix", "[issue258][structural]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    std::vector<typename Mgr::template pptr<std::uint32_t>> ptrs;
    for ( int i = 0; i < 10; ++i )
    {
        auto p = Mgr::allocate_typed<std::uint32_t>( 4 + i * 2 );
        REQUIRE( !p.is_null() );
        ptrs.push_back( p );
    }

    for ( int i = 0; i < 10; i += 2 )
    {
        Mgr::deallocate_typed( ptrs[static_cast<std::size_t>( i )] );
    }

    auto blocks = walk_blocks( Mgr::backend().base_ptr(), Mgr::total_size() );

    for ( const auto& b : blocks )
    {
        if ( b.weight == 0 )
        {
            REQUIRE( b.root_offset == 0 );
        }
        else
        {
            REQUIRE( b.root_offset == b.idx );
        }
    }

    pmm::VerifyResult v = Mgr::verify();
    REQUIRE( v.ok );

    Mgr::destroy();
}

// ─── C7: No overlapping blocks ──────────────────────────────────────────────

TEST_CASE( "structural: no overlapping blocks", "[issue258][structural]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    auto p1 = Mgr::allocate_typed<std::uint32_t>( 16 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 32 );
    auto p3 = Mgr::allocate_typed<std::uint32_t>( 64 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );
    REQUIRE( !p3.is_null() );
    Mgr::deallocate_typed( p2 );

    auto blocks = walk_blocks( Mgr::backend().base_ptr(), Mgr::total_size() );

    for ( std::size_t i = 0; i + 1 < blocks.size(); ++i )
    {
        AT::index_type end_of_block = blocks[i].idx + blocks[i].total_granules;
        REQUIRE( end_of_block == blocks[i + 1].idx );
    }

    Mgr::destroy();
}

// ─── C8: Total size equals sum of blocks ────────────────────────────────────

TEST_CASE( "structural: total size equals sum of all block sizes", "[issue258][structural]" )
{
    Mgr::destroy();
    REQUIRE( Mgr::create( 64 * 1024 ) );

    auto p1 = Mgr::allocate_typed<std::uint32_t>( 10 );
    auto p2 = Mgr::allocate_typed<std::uint32_t>( 20 );
    REQUIRE( !p1.is_null() );
    REQUIRE( !p2.is_null() );
    Mgr::deallocate_typed( p1 );

    auto blocks = walk_blocks( Mgr::backend().base_ptr(), Mgr::total_size() );

    std::size_t sum_granules = 0;
    for ( const auto& b : blocks )
        sum_granules += b.total_granules;

    // First block starts at first_block_offset, not at 0
    // The area before first_block_offset is Block_0 + ManagerHeader
    auto*          hdr            = pmm::detail::manager_header_at<AT>( Mgr::backend().base_ptr() );
    AT::index_type total_granules = static_cast<AT::index_type>( Mgr::total_size() / AT::granule_size );
    AT::index_type block_area     = total_granules - hdr->first_block_offset;
    REQUIRE( sum_granules == block_area );

    Mgr::destroy();
}
