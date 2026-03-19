/**
 * @file test_issue175_64bit_indexes.cpp
 * @brief Tests for Issue #175: ManagerHeader<AT> template — all index fields use AT::index_type.
 *
 * Verifies:
 *   - ManagerHeader<AT> is a template parameterized on AddressTraitsT (Issue #175)
 *   - All 7 ManagerHeader index/counter fields use AT::index_type (not hardcoded uint32_t)
 *   - ManagerHeader<DefaultAddressTraits> has index fields of type uint32_t (backward compat)
 *   - ManagerHeader<SmallAddressTraits>   has index fields of type uint16_t
 *   - ManagerHeader<LargeAddressTraits>   has index fields of type uint64_t
 *   - kManagerHeaderGranules_t<AT> returns AT::index_type (not uint32_t)
 *   - kBlockHeaderGranules_t<AT>   returns AT::index_type (not uint32_t)
 *   - required_block_granules_t<AT> returns AT::index_type
 *   - FreeBlockTreePolicyForTraitsConcept<Policy, AT> is satisfied by AvlFreeTree<AT>
 *   - SmallAddressTraits heap: create/alloc/dealloc/destroy with correct uint16_t indexes
 *   - LargeAddressTraits heap: create/alloc/dealloc/destroy with correct uint64_t indexes
 *
 * @see include/pmm/types.h           — ManagerHeader<AT>, kManagerHeaderGranules_t<AT>
 * @see include/pmm/free_block_tree.h — AvlFreeTree<AT>, FreeBlockTreePolicyForTraitsConcept<P,AT>
 * @see include/pmm/allocator_policy.h — AllocatorPolicy<AT, ...>
 * @version 0.1 (Issue #175 — ManagerHeader<AT> template, all fields use index_type)
 */

#include "pmm_single_threaded_heap.h"
#include "pmm_small_embedded_static_heap.h"
#include "pmm_large_db_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

// =============================================================================
// Section A: ManagerHeader<AT> is a template — field types follow index_type
// =============================================================================

/// @brief ManagerHeader<DefaultAddressTraits> has uint32_t index fields.
TEST_CASE( "I175-A1: ManagerHeader<DefaultAddressTraits> has uint32_t fields", "[test_issue175_64bit_indexes]" )
{
    using AT  = pmm::DefaultAddressTraits;
    using Hdr = pmm::detail::ManagerHeader<AT>;

    // ManagerHeader::index_type must match AT::index_type
    static_assert( std::is_same<Hdr::index_type, std::uint32_t>::value,
                   "ManagerHeader<DefaultAddressTraits>::index_type must be uint32_t" );

    // ManagerHeader fields are index_type (uint32_t for DefaultAddressTraits)
    static_assert( std::is_same<decltype( std::declval<Hdr>().block_count ), std::uint32_t>::value,
                   "ManagerHeader<DefaultAddressTraits>::block_count must be uint32_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().free_count ), std::uint32_t>::value,
                   "ManagerHeader<DefaultAddressTraits>::free_count must be uint32_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().alloc_count ), std::uint32_t>::value,
                   "ManagerHeader<DefaultAddressTraits>::alloc_count must be uint32_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().first_block_offset ), std::uint32_t>::value,
                   "ManagerHeader<DefaultAddressTraits>::first_block_offset must be uint32_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().last_block_offset ), std::uint32_t>::value,
                   "ManagerHeader<DefaultAddressTraits>::last_block_offset must be uint32_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().free_tree_root ), std::uint32_t>::value,
                   "ManagerHeader<DefaultAddressTraits>::free_tree_root must be uint32_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().used_size ), std::uint32_t>::value,
                   "ManagerHeader<DefaultAddressTraits>::used_size must be uint32_t" );
}

/// @brief ManagerHeader<SmallAddressTraits> has uint16_t index fields (Issue #175).
TEST_CASE( "I175-A2: ManagerHeader<SmallAddressTraits> has uint16_t fields", "[test_issue175_64bit_indexes]" )
{
    using AT  = pmm::SmallAddressTraits;
    using Hdr = pmm::detail::ManagerHeader<AT>;

    static_assert( std::is_same<Hdr::index_type, std::uint16_t>::value,
                   "ManagerHeader<SmallAddressTraits>::index_type must be uint16_t" );

    static_assert( std::is_same<decltype( std::declval<Hdr>().block_count ), std::uint16_t>::value,
                   "ManagerHeader<SmallAddressTraits>::block_count must be uint16_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().free_count ), std::uint16_t>::value,
                   "ManagerHeader<SmallAddressTraits>::free_count must be uint16_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().alloc_count ), std::uint16_t>::value,
                   "ManagerHeader<SmallAddressTraits>::alloc_count must be uint16_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().first_block_offset ), std::uint16_t>::value,
                   "ManagerHeader<SmallAddressTraits>::first_block_offset must be uint16_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().last_block_offset ), std::uint16_t>::value,
                   "ManagerHeader<SmallAddressTraits>::last_block_offset must be uint16_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().free_tree_root ), std::uint16_t>::value,
                   "ManagerHeader<SmallAddressTraits>::free_tree_root must be uint16_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().used_size ), std::uint16_t>::value,
                   "ManagerHeader<SmallAddressTraits>::used_size must be uint16_t" );
}

/// @brief ManagerHeader<LargeAddressTraits> has uint64_t index fields (Issue #175).
TEST_CASE( "I175-A3: ManagerHeader<LargeAddressTraits> has uint64_t fields", "[test_issue175_64bit_indexes]" )
{
    using AT  = pmm::LargeAddressTraits;
    using Hdr = pmm::detail::ManagerHeader<AT>;

    static_assert( std::is_same<Hdr::index_type, std::uint64_t>::value,
                   "ManagerHeader<LargeAddressTraits>::index_type must be uint64_t" );

    static_assert( std::is_same<decltype( std::declval<Hdr>().block_count ), std::uint64_t>::value,
                   "ManagerHeader<LargeAddressTraits>::block_count must be uint64_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().free_count ), std::uint64_t>::value,
                   "ManagerHeader<LargeAddressTraits>::free_count must be uint64_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().alloc_count ), std::uint64_t>::value,
                   "ManagerHeader<LargeAddressTraits>::alloc_count must be uint64_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().first_block_offset ), std::uint64_t>::value,
                   "ManagerHeader<LargeAddressTraits>::first_block_offset must be uint64_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().last_block_offset ), std::uint64_t>::value,
                   "ManagerHeader<LargeAddressTraits>::last_block_offset must be uint64_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().free_tree_root ), std::uint64_t>::value,
                   "ManagerHeader<LargeAddressTraits>::free_tree_root must be uint64_t" );
    static_assert( std::is_same<decltype( std::declval<Hdr>().used_size ), std::uint64_t>::value,
                   "ManagerHeader<LargeAddressTraits>::used_size must be uint64_t" );
}

// =============================================================================
// Section B: kManagerHeaderGranules_t<AT> and kBlockHeaderGranules_t<AT> return index_type
// =============================================================================

/// @brief kManagerHeaderGranules_t<DefaultAddressTraits> returns uint32_t and equals 4.
TEST_CASE( "I175-B1: kManagerHeaderGranules_t<DefaultAddressTraits> is uint32_t == 4", "[test_issue175_64bit_indexes]" )
{
    using AT = pmm::DefaultAddressTraits;

    static_assert( std::is_same<decltype( pmm::detail::kManagerHeaderGranules_t<AT> ), const std::uint32_t>::value,
                   "kManagerHeaderGranules_t<DefaultAddressTraits> must have type const uint32_t (AT::index_type)" );

    // sizeof(ManagerHeader<DefaultAddressTraits>) = 64, granule_size = 16 → 64/16 = 4
    static_assert( pmm::detail::kManagerHeaderGranules_t<AT> == 4,
                   "kManagerHeaderGranules_t<DefaultAddressTraits> must be 4 (64 bytes / 16 byte granule)" );
}

/// @brief kManagerHeaderGranules_t<SmallAddressTraits> returns uint16_t (Issue #175).
TEST_CASE( "I175-B2: kManagerHeaderGranules_t<SmallAddressTraits> is uint16_t", "[test_issue175_64bit_indexes]" )
{
    using AT = pmm::SmallAddressTraits;

    static_assert( std::is_same<decltype( pmm::detail::kManagerHeaderGranules_t<AT> ), const std::uint16_t>::value,
                   "kManagerHeaderGranules_t<SmallAddressTraits> must have type const uint16_t (AT::index_type)" );

    // sizeof(ManagerHeader<SmallAddressTraits>) — fits in a few granules
    static_assert( pmm::detail::kManagerHeaderGranules_t<AT> >= 1,
                   "kManagerHeaderGranules_t<SmallAddressTraits> must be >= 1" );
}

/// @brief kManagerHeaderGranules_t<LargeAddressTraits> returns uint64_t (Issue #175).
TEST_CASE( "I175-B3: kManagerHeaderGranules_t<LargeAddressTraits> is uint64_t", "[test_issue175_64bit_indexes]" )
{
    using AT = pmm::LargeAddressTraits;

    static_assert( std::is_same<decltype( pmm::detail::kManagerHeaderGranules_t<AT> ), const std::uint64_t>::value,
                   "kManagerHeaderGranules_t<LargeAddressTraits> must have type const uint64_t (AT::index_type)" );

    static_assert( pmm::detail::kManagerHeaderGranules_t<AT> >= 1,
                   "kManagerHeaderGranules_t<LargeAddressTraits> must be >= 1" );
}

/// @brief kBlockHeaderGranules_t<AT> returns AT::index_type (Issue #175).
TEST_CASE( "I175-B4: kBlockHeaderGranules_t<AT> returns AT::index_type for all AT", "[test_issue175_64bit_indexes]" )
{
    using DAT = pmm::DefaultAddressTraits;
    using SAT = pmm::SmallAddressTraits;
    using LAT = pmm::LargeAddressTraits;

    static_assert( std::is_same<decltype( pmm::detail::kBlockHeaderGranules_t<DAT> ), const std::uint32_t>::value,
                   "kBlockHeaderGranules_t<DefaultAddressTraits> must have type const uint32_t (AT::index_type)" );
    static_assert( std::is_same<decltype( pmm::detail::kBlockHeaderGranules_t<SAT> ), const std::uint16_t>::value,
                   "kBlockHeaderGranules_t<SmallAddressTraits> must have type const uint16_t (AT::index_type)" );
    static_assert( std::is_same<decltype( pmm::detail::kBlockHeaderGranules_t<LAT> ), const std::uint64_t>::value,
                   "kBlockHeaderGranules_t<LargeAddressTraits> must have type const uint64_t (AT::index_type)" );
}

// =============================================================================
// Section C: FreeBlockTreePolicyForTraitsConcept<Policy, AT>
// =============================================================================

/// @brief AvlFreeTree<SmallAddressTraits> satisfies FreeBlockTreePolicyForTraitsConcept (Issue #175).
TEST_CASE( "I175-C1: AvlFreeTree<SmallAT> satisfies FreeBlockTreePolicyForTraitsConcept<P, SmallAT>",
           "[test_issue175_64bit_indexes]" )
{
    using AT     = pmm::SmallAddressTraits;
    using Policy = pmm::AvlFreeTree<AT>;

    static_assert( pmm::FreeBlockTreePolicyForTraitsConcept<Policy, AT>,
                   "AvlFreeTree<SmallAddressTraits> must satisfy FreeBlockTreePolicyForTraitsConcept<P, SmallAT>" );
}

/// @brief AvlFreeTree<LargeAddressTraits> satisfies FreeBlockTreePolicyForTraitsConcept (Issue #175).
TEST_CASE( "I175-C2: AvlFreeTree<LargeAT> satisfies FreeBlockTreePolicyForTraitsConcept<P, LargeAT>",
           "[test_issue175_64bit_indexes]" )
{
    using AT     = pmm::LargeAddressTraits;
    using Policy = pmm::AvlFreeTree<AT>;

    static_assert( pmm::FreeBlockTreePolicyForTraitsConcept<Policy, AT>,
                   "AvlFreeTree<LargeAddressTraits> must satisfy FreeBlockTreePolicyForTraitsConcept<P, LargeAT>" );
}

/// @brief AvlFreeTree<DefaultAddressTraits> does NOT satisfy FreeBlockTreePolicyForTraitsConcept<P, SmallAT>.
TEST_CASE( "I175-C3: AvlFreeTree<DefaultAT> does NOT satisfy FreeBlockTreePolicyForTraitsConcept<P, SmallAT>",
           "[test_issue175_64bit_indexes]" )
{
    using DefaultPolicy = pmm::AvlFreeTree<pmm::DefaultAddressTraits>;
    using SmallAT       = pmm::SmallAddressTraits;

    // A policy for DefaultAddressTraits (uint32_t indices) should NOT satisfy SmallAddressTraits (uint16_t)
    static_assert( !pmm::FreeBlockTreePolicyForTraitsConcept<DefaultPolicy, SmallAT>,
                   "AvlFreeTree<DefaultAT> must NOT satisfy FreeBlockTreePolicyForTraitsConcept<P, SmallAT>" );
}

// =============================================================================
// Section D: Runtime functional tests — SmallAddressTraits (uint16_t indexes)
// =============================================================================

/// @brief SmallEmbeddedStaticHeap: correct sentinel (no_block = 0xFFFF, not 0xFFFFFFFF).
TEST_CASE( "I175-D1: SmallAddressTraits::no_block == 0xFFFF (correct 16-bit sentinel)",
           "[test_issue175_64bit_indexes]" )
{
    using AT = pmm::SmallAddressTraits;

    static_assert( AT::no_block == std::uint16_t( 0xFFFFU ),
                   "SmallAddressTraits::no_block must be 0xFFFF (uint16_t max)" );
    static_assert( AT::no_block != pmm::detail::kNoBlock,
                   "SmallAddressTraits::no_block (0xFFFF) != kNoBlock (0xFFFFFFFF) — different types!" );
    static_assert( pmm::detail::kNoBlock_v<AT> == AT::no_block,
                   "kNoBlock_v<SmallAddressTraits> must equal SmallAddressTraits::no_block" );
}

/// @brief SmallEmbeddedStaticHeap lifecycle with uint16_t indexes.
TEST_CASE( "I175-D2: SmallEmbeddedStaticHeap lifecycle with uint16_t indexes", "[test_issue175_64bit_indexes]" )
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<512>, 17510>;

    REQUIRE( !SESH::is_initialized() );
    REQUIRE( SESH::create( 512 ) );
    REQUIRE( SESH::is_initialized() );
    REQUIRE( SESH::total_size() == 512 );
    REQUIRE( SESH::block_count() >= 1 );
    REQUIRE( SESH::free_block_count() >= 1 );

    void* p = SESH::allocate( 32 );
    REQUIRE( p != nullptr );
    std::memset( p, 0xAB, 32 );
    SESH::deallocate( p );

    SESH::destroy();
    REQUIRE( !SESH::is_initialized() );
}

/// @brief SmallEmbeddedStaticHeap: alloc/dealloc cycle preserves counter correctness.
TEST_CASE( "I175-D3: SmallEmbeddedStaticHeap alloc/dealloc counter correctness", "[test_issue175_64bit_indexes]" )
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<512>, 17511>;

    REQUIRE( SESH::create( 512 ) );

    std::size_t initial_free  = SESH::free_block_count();
    std::size_t initial_alloc = SESH::alloc_block_count();

    void* p1 = SESH::allocate( 32 );
    REQUIRE( p1 != nullptr );

    // After one allocation, free count may decrease, alloc count increases
    REQUIRE( SESH::alloc_block_count() >= initial_alloc );

    void* p2 = SESH::allocate( 32 );
    REQUIRE( p2 != nullptr );
    REQUIRE( p1 != p2 );

    SESH::deallocate( p1 );
    SESH::deallocate( p2 );

    // After dealloc, free count should be back to initial
    REQUIRE( SESH::free_block_count() >= initial_free );

    SESH::destroy();
}

/// @brief SmallEmbeddedStaticHeap: typed alloc with pptr<T> works with uint16_t indexes.
TEST_CASE( "I175-D4: SmallEmbeddedStaticHeap typed pptr<T> with uint16_t index (2-byte pptr)",
           "[test_issue175_64bit_indexes]" )
{
    using SESH = pmm::PersistMemoryManager<pmm::SmallEmbeddedStaticConfig<512>, 17512>;

    REQUIRE( SESH::create( 512 ) );

    // pptr<int> with uint16_t index is 2 bytes
    static_assert( sizeof( SESH::pptr<int> ) == 2, "SmallEmbeddedStaticHeap pptr<int> must be 2 bytes (16-bit index)" );

    SESH::pptr<std::uint32_t> p = SESH::allocate_typed<std::uint32_t>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 0xDEADBEEFU;
    REQUIRE( *p.resolve() == 0xDEADBEEFU );

    SESH::deallocate_typed( p );
    SESH::destroy();
}

// =============================================================================
// Section E: Runtime functional tests — LargeAddressTraits (uint64_t indexes)
// =============================================================================

/// @brief LargeAddressTraits no_block sentinel is 0xFFFFFFFFFFFFFFFF.
TEST_CASE( "I175-E1: LargeAddressTraits::no_block == 0xFFFFFFFFFFFFFFFF (correct 64-bit sentinel)",
           "[test_issue175_64bit_indexes]" )
{
    using AT = pmm::LargeAddressTraits;

    static_assert( AT::no_block == std::uint64_t( 0xFFFFFFFFFFFFFFFFULL ),
                   "LargeAddressTraits::no_block must be 0xFFFFFFFFFFFFFFFF (uint64_t max)" );
    static_assert( pmm::detail::kNoBlock_v<AT> == AT::no_block,
                   "kNoBlock_v<LargeAddressTraits> must equal LargeAddressTraits::no_block" );
}

/// @brief LargeDBHeap lifecycle with uint64_t indexes.
TEST_CASE( "I175-E2: LargeDBHeap lifecycle with uint64_t indexes", "[test_issue175_64bit_indexes]" )
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 17520>;

    REQUIRE( !LDB::is_initialized() );
    REQUIRE( LDB::create( 4096 ) );
    REQUIRE( LDB::is_initialized() );
    REQUIRE( LDB::total_size() == 4096 );
    REQUIRE( LDB::block_count() >= 1 );
    REQUIRE( LDB::free_block_count() >= 1 );

    void* p = LDB::allocate( 256 );
    REQUIRE( p != nullptr );
    std::memset( p, 0xCC, 256 );
    LDB::deallocate( p );

    LDB::destroy();
    REQUIRE( !LDB::is_initialized() );
}

/// @brief LargeDBHeap: typed alloc with pptr<T> works with uint64_t indexes.
TEST_CASE( "I175-E3: LargeDBHeap typed pptr<T> with uint64_t index (8-byte pptr)", "[test_issue175_64bit_indexes]" )
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 17521>;

    REQUIRE( LDB::create( 4096 ) );

    // pptr<int> with uint64_t index is 8 bytes
    static_assert( sizeof( LDB::pptr<int> ) == 8, "LargeDBHeap pptr<int> must be 8 bytes (64-bit index)" );

    LDB::pptr<std::uint64_t> p = LDB::allocate_typed<std::uint64_t>();
    REQUIRE( !p.is_null() );
    *p.resolve() = 0xCAFEBABEDEADBEEFULL;
    REQUIRE( *p.resolve() == 0xCAFEBABEDEADBEEFULL );

    LDB::deallocate_typed( p );
    LDB::destroy();
}

/// @brief LargeDBHeap: multiple alloc/dealloc cycles with uint64_t indexes.
TEST_CASE( "I175-E4: LargeDBHeap multiple alloc/dealloc with uint64_t indexes", "[test_issue175_64bit_indexes]" )
{
    using LDB = pmm::PersistMemoryManager<pmm::LargeDBConfig, 17522>;

    REQUIRE( LDB::create( 8192 ) );

    void* ptrs[4];
    for ( int i = 0; i < 4; ++i )
    {
        ptrs[i] = LDB::allocate( 64 );
        REQUIRE( ptrs[i] != nullptr );
        std::memset( ptrs[i], i + 1, 64 );
    }

    for ( int i = 0; i < 4; ++i )
    {
        auto* bytes = static_cast<std::uint8_t*>( ptrs[i] );
        REQUIRE( bytes[0] == static_cast<std::uint8_t>( i + 1 ) );
    }

    for ( int i = 0; i < 4; ++i )
        LDB::deallocate( ptrs[i] );

    REQUIRE( LDB::is_initialized() );
    LDB::destroy();
}

// =============================================================================
// main
// =============================================================================
