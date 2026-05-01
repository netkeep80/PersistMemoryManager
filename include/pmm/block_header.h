#pragma once
#include "pmm/address_traits.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace pmm
{
/*
## pmm-nodetype
req: dr-001, dr-006, fr-007
*/
enum class NodeType : std::uint8_t
{
    Free           = 0,
    ManagerHeader  = 1,
    Generic        = 2,
    ReadOnlyLocked = 3,
    PStringView    = 4,
    PString        = 5,
    PArray         = 6,
    PMap           = 7,
    PPtr           = 8,
};
/*
### pmm-nodetype-helpers
*/
constexpr bool is_free( NodeType t ) noexcept
{
    return t == NodeType::Free;
}
constexpr bool is_allocated( NodeType t ) noexcept
{
    switch ( t )
    {
    case NodeType::ManagerHeader:
    case NodeType::Generic:
    case NodeType::ReadOnlyLocked:
    case NodeType::PStringView:
    case NodeType::PString:
    case NodeType::PArray:
    case NodeType::PMap:
    case NodeType::PPtr:
        return true;
    case NodeType::Free:
        return false;
    }
    return false;
}
constexpr bool is_mutable( NodeType t ) noexcept
{
    switch ( t )
    {
    case NodeType::Free:
    case NodeType::ManagerHeader:
    case NodeType::Generic:
    case NodeType::PString:
    case NodeType::PArray:
    case NodeType::PMap:
    case NodeType::PPtr:
        return true;
    case NodeType::ReadOnlyLocked:
    case NodeType::PStringView:
        return false;
    }
    return false;
}
constexpr bool can_be_deleted_from_pap( NodeType t ) noexcept
{
    switch ( t )
    {
    case NodeType::Generic:
    case NodeType::PStringView:
    case NodeType::PString:
    case NodeType::PArray:
    case NodeType::PMap:
    case NodeType::PPtr:
        return true;
    case NodeType::Free:
    case NodeType::ManagerHeader:
    case NodeType::ReadOnlyLocked:
        return false;
    }
    return false;
}
constexpr bool participates_in_free_tree( NodeType t ) noexcept
{
    return t == NodeType::Free;
}
/*
### pmm-nodetype-for
*/
template <typename T> struct node_type_for
{
    static constexpr NodeType value = NodeType::Generic;
};
template <typename T> inline constexpr NodeType node_type_for_v = node_type_for<T>::value;
constexpr bool                                  is_known_node_type( std::uint8_t v ) noexcept
{
    switch ( static_cast<NodeType>( v ) )
    {
    case NodeType::Free:
    case NodeType::ManagerHeader:
    case NodeType::Generic:
    case NodeType::ReadOnlyLocked:
    case NodeType::PStringView:
    case NodeType::PString:
    case NodeType::PArray:
    case NodeType::PMap:
    case NodeType::PPtr:
        return true;
    }
    return false;
}
namespace detail
{
template <typename AT> struct BlockHeaderCoreFields
{
    using index_type = typename AT::index_type;
    index_type weight;
    index_type left_offset;
    index_type right_offset;
    index_type parent_offset;
    index_type root_offset;
    index_type prev_offset;
    index_type next_offset;
};
template <typename AT> constexpr std::size_t block_header_core_size_v = sizeof( BlockHeaderCoreFields<AT> );
template <typename AT> constexpr std::size_t block_header_min_size_v  = block_header_core_size_v<AT> + 2;
template <typename AT>
constexpr std::size_t block_header_target_size_v =
    ( ( block_header_min_size_v<AT> + AT::granule_size - 1 ) / AT::granule_size ) * AT::granule_size;
template <typename AT>
constexpr std::size_t block_header_trailer_pad_v = block_header_target_size_v<AT> - block_header_core_size_v<AT> - 2;
template <typename AT, std::size_t Pad> struct BlockHeaderStorageImpl
{
    using index_type = typename AT::index_type;
    index_type   weight;
    index_type   left_offset;
    index_type   right_offset;
    index_type   parent_offset;
    index_type   root_offset;
    index_type   prev_offset;
    index_type   next_offset;
    std::uint8_t _trailer_padding[Pad];
    std::uint8_t avl_height;
    NodeType     node_type;
};
template <typename AT> struct BlockHeaderStorageImpl<AT, 0>
{
    using index_type = typename AT::index_type;
    index_type   weight;
    index_type   left_offset;
    index_type   right_offset;
    index_type   parent_offset;
    index_type   root_offset;
    index_type   prev_offset;
    index_type   next_offset;
    std::uint8_t avl_height;
    NodeType     node_type;
};
template <typename AT> using BlockHeaderStorage = BlockHeaderStorageImpl<AT, block_header_trailer_pad_v<AT>>;
}
/*
## pmm-blockheader
req: dr-001, dr-002, dr-003, dr-004, dr-006, qa-mem-001, dr-005, dr-014, dr-015, dr-016, dr-018, dr-019
*/
template <typename AT> struct BlockHeader : detail::BlockHeaderStorage<AT>
{
    using address_traits = AT;
    using index_type     = typename AT::index_type;
};
/*
## pmm-blocklayoutcontract
req: dr-001, dr-004, qa-mem-001, asm-002, con-006
*/
template <typename AT> struct BlockLayoutContract
{
    using H = BlockHeader<AT>;
    using I = typename AT::index_type;
    static_assert( std::is_standard_layout_v<H> );
    static_assert( std::is_trivially_copyable_v<H> );
    static_assert( std::is_unsigned_v<I> );
    static_assert( AT::granule_size >= alignof( H ) );
    static_assert( ( AT::granule_size % alignof( H ) ) == 0 );
    static_assert( ( sizeof( H ) % AT::granule_size ) == 0 );
    static constexpr std::size_t tree_slot_size = offsetof( H, prev_offset );
    static constexpr std::size_t layout_size    = sizeof( H );
};
static_assert( std::is_standard_layout_v<BlockHeader<DefaultAddressTraits>> );
static_assert( std::is_trivially_copyable_v<BlockHeader<DefaultAddressTraits>> );
static_assert( sizeof( BlockHeader<DefaultAddressTraits> ) == 32 );
static_assert( offsetof( BlockHeader<DefaultAddressTraits>, weight ) == 0 );
static_assert( offsetof( BlockHeader<DefaultAddressTraits>, left_offset ) == 4 );
static_assert( offsetof( BlockHeader<DefaultAddressTraits>, right_offset ) == 8 );
static_assert( offsetof( BlockHeader<DefaultAddressTraits>, parent_offset ) == 12 );
static_assert( offsetof( BlockHeader<DefaultAddressTraits>, root_offset ) == 16 );
static_assert( offsetof( BlockHeader<DefaultAddressTraits>, prev_offset ) == 20 );
static_assert( offsetof( BlockHeader<DefaultAddressTraits>, next_offset ) == 24 );
static_assert( offsetof( BlockHeader<DefaultAddressTraits>, avl_height ) ==
               sizeof( BlockHeader<DefaultAddressTraits> ) - 2 );
static_assert( offsetof( BlockHeader<DefaultAddressTraits>, node_type ) ==
               sizeof( BlockHeader<DefaultAddressTraits> ) - 1 );
static_assert( ( sizeof( BlockHeader<SmallAddressTraits> ) % SmallAddressTraits::granule_size ) == 0 );
static_assert( ( sizeof( BlockHeader<DefaultAddressTraits> ) % DefaultAddressTraits::granule_size ) == 0 );
static_assert( ( sizeof( BlockHeader<LargeAddressTraits> ) % LargeAddressTraits::granule_size ) == 0 );
namespace detail
{
template <typename AT> inline BlockHeader<AT>* block_header_at( void* raw ) noexcept
{
    assert( raw != nullptr );
    assert( reinterpret_cast<std::uintptr_t>( raw ) % alignof( BlockHeader<AT> ) == 0 );
    return reinterpret_cast<BlockHeader<AT>*>( raw );
}
template <typename AT> inline const BlockHeader<AT>* block_header_at( const void* raw ) noexcept
{
    assert( raw != nullptr );
    assert( reinterpret_cast<std::uintptr_t>( raw ) % alignof( BlockHeader<AT> ) == 0 );
    return reinterpret_cast<const BlockHeader<AT>*>( raw );
}
}
}
