#pragma once
#include "pmm/address_traits.h"
#include "pmm/block.h"
#include "pmm/block_header.h"
#include "pmm/diagnostics.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
namespace pmm
{
template <typename AT> class FreeBlock;
template <typename AT> class AllocatedBlock;
template <typename AT> class FreeBlockRemovedAVL;
template <typename AT> class FreeBlockNotInAVL;
template <typename AT> class SplittingBlock;
template <typename AT> class CoalescingBlock;
/*
## pmm-blockstatebase
req: feat-007, fr-009, fr-010, fr-018, dr-006, rule-002, dr-005, dr-014
*/
template <typename AT> class BlockStateBase
{
  public:
    using address_traits = AT;
    using index_type     = typename AT::index_type;
    using Header         = BlockHeader<AT>;
    BlockStateBase()     = delete;
    static Header*       header_of( void* raw_blk ) noexcept { return detail::block_header_at<AT>( raw_blk ); }
    static const Header* header_of( const void* raw_blk ) noexcept { return detail::block_header_at<AT>( raw_blk ); }
    static bool is_free_raw( const void* raw_blk ) noexcept { return pmm::is_free( header_of( raw_blk )->node_type ); }
    static bool is_allocated_raw( const void* raw_blk, index_type own_idx ) noexcept
    {
        const Header* h = header_of( raw_blk );
        return pmm::is_allocated( h->node_type ) && h->root_offset == own_idx;
    }
/*
### pmm-blockstatebase-recover_state
*/
    static void recover_state( void* raw_blk, index_type own_idx ) noexcept
    {
        Header* h = header_of( raw_blk );
        if ( pmm::is_allocated( h->node_type ) && h->root_offset != own_idx )
            h->root_offset = own_idx;
        if ( pmm::is_free( h->node_type ) && h->root_offset != 0 )
            h->root_offset = 0;
    }
/*
### pmm-blockstatebase-verify_state
*/
    static void verify_state( const void* raw_blk, index_type own_idx, VerifyResult& result ) noexcept
    {
        const Header* h = header_of( raw_blk );
        if ( pmm::is_allocated( h->node_type ) && h->root_offset != own_idx )
        {
            result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                        static_cast<uint64_t>( own_idx ), static_cast<uint64_t>( own_idx ),
                        static_cast<uint64_t>( h->root_offset ) );
        }
        if ( pmm::is_free( h->node_type ) && h->root_offset != 0 )
        {
            result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                        static_cast<uint64_t>( own_idx ), 0, static_cast<uint64_t>( h->root_offset ) );
        }
    }
    static void reset_avl_fields_of( void* raw_blk ) noexcept
    {
        Header* h        = header_of( raw_blk );
        h->left_offset   = AT::no_block;
        h->right_offset  = AT::no_block;
        h->parent_offset = AT::no_block;
        h->avl_height    = 0;
    }
    static void repair_prev_offset( void* raw_blk, index_type prev_idx ) noexcept
    {
        header_of( raw_blk )->prev_offset = prev_idx;
    }
    static index_type   get_prev_offset( const void* raw_blk ) noexcept { return header_of( raw_blk )->prev_offset; }
    static index_type   get_next_offset( const void* raw_blk ) noexcept { return header_of( raw_blk )->next_offset; }
    static index_type   get_weight( const void* raw_blk ) noexcept { return header_of( raw_blk )->weight; }
    static index_type   get_left_offset( const void* b ) noexcept { return header_of( b )->left_offset; }
    static index_type   get_right_offset( const void* b ) noexcept { return header_of( b )->right_offset; }
    static index_type   get_parent_offset( const void* b ) noexcept { return header_of( b )->parent_offset; }
    static index_type   get_root_offset( const void* b ) noexcept { return header_of( b )->root_offset; }
    static std::uint8_t get_avl_height( const void* raw_blk ) noexcept { return header_of( raw_blk )->avl_height; }
    static NodeType     get_node_type( const void* raw_blk ) noexcept { return header_of( raw_blk )->node_type; }
    static void init_fields( void* raw_blk, index_type prev_idx, index_type next_idx, std::uint8_t avl_height_val,
                             index_type weight_val, index_type root_offset_val, NodeType node_type_val ) noexcept
    {
        Header* h        = header_of( raw_blk );
        h->prev_offset   = prev_idx;
        h->next_offset   = next_idx;
        h->left_offset   = AT::no_block;
        h->right_offset  = AT::no_block;
        h->parent_offset = AT::no_block;
        h->avl_height    = avl_height_val;
        h->weight        = weight_val;
        h->root_offset   = root_offset_val;
        h->node_type     = node_type_val;
    }
    static void set_next_offset_of( void* b, index_type v ) noexcept { header_of( b )->next_offset = v; }
    static void set_prev_offset_of( void* b, index_type v ) noexcept { header_of( b )->prev_offset = v; }
    static void set_left_offset_of( void* b, index_type v ) noexcept { header_of( b )->left_offset = v; }
    static void set_right_offset_of( void* b, index_type v ) noexcept { header_of( b )->right_offset = v; }
    static void set_parent_offset_of( void* b, index_type v ) noexcept { header_of( b )->parent_offset = v; }
    static void set_weight_of( void* b, index_type v ) noexcept { header_of( b )->weight = v; }
    static void set_root_offset_of( void* b, index_type v ) noexcept { header_of( b )->root_offset = v; }
    static void set_avl_height_of( void* b, std::uint8_t v ) noexcept { header_of( b )->avl_height = v; }
    static void set_node_type_of( void* b, NodeType v ) noexcept { header_of( b )->node_type = v; }
};
/*
## pmm-freeblock
req: feat-002, fr-005, fr-013, dr-005, rule-002
*/
template <typename AT> class FreeBlock
{
  public:
    using address_traits = AT;
    using index_type     = typename AT::index_type;
    using Header         = BlockHeader<AT>;
    using Base           = BlockStateBase<AT>;
    explicit FreeBlock( Header& h ) noexcept : h_( &h ) {}
    Header&       header() noexcept { return *h_; }
    const Header& header() const noexcept { return *h_; }
    index_type    weight() const noexcept { return h_->weight; }
    index_type    prev_offset() const noexcept { return h_->prev_offset; }
    index_type    next_offset() const noexcept { return h_->next_offset; }
    index_type    left_offset() const noexcept { return h_->left_offset; }
    index_type    right_offset() const noexcept { return h_->right_offset; }
    index_type    parent_offset() const noexcept { return h_->parent_offset; }
    index_type    root_offset() const noexcept { return h_->root_offset; }
    std::uint8_t  avl_height() const noexcept { return h_->avl_height; }
    NodeType      node_type() const noexcept { return h_->node_type; }
    bool          is_free() const noexcept { return pmm::is_free( h_->node_type ); }
/*
### pmm-freeblock-cast_from_raw
*/
    static FreeBlock cast_from_raw( void* raw ) noexcept
    {
        assert( raw != nullptr );
        Header* h = detail::block_header_at<AT>( raw );
        assert( pmm::is_free( h->node_type ) && "cast_from_raw<FreeBlock>: block is not in FreeBlock state" );
        return FreeBlock( *h );
    }
/*
### pmm-freeblock-can_cast_from_raw
*/
    static bool can_cast_from_raw( const void* raw ) noexcept
    {
        if ( raw == nullptr )
            return false;
        if ( reinterpret_cast<std::uintptr_t>( raw ) % alignof( Header ) != 0 )
            return false;
        const Header* h = reinterpret_cast<const Header*>( raw );
        return pmm::is_free( h->node_type );
    }
/*
### pmm-freeblock-try_cast_from_raw
*/
    static std::optional<FreeBlock> try_cast_from_raw( void* raw ) noexcept
    {
        if ( !can_cast_from_raw( raw ) )
            return std::nullopt;
        return FreeBlock( *reinterpret_cast<Header*>( raw ) );
    }
/*
### pmm-freeblock-verify_invariants
*/
    bool                    verify_invariants() const noexcept { return is_free(); }
    FreeBlockRemovedAVL<AT> remove_from_avl() noexcept;

  private:
    Header* h_;
};
/*
## pmm-freeblockremovedavl
req: feat-002, fr-004, fr-013, dr-005, rule-002
*/
template <typename AT> class FreeBlockRemovedAVL
{
  public:
    using address_traits = AT;
    using index_type     = typename AT::index_type;
    using Header         = BlockHeader<AT>;
    explicit FreeBlockRemovedAVL( Header& h ) noexcept : h_( &h ) {}
    Header&                    header() noexcept { return *h_; }
    const Header&              header() const noexcept { return *h_; }
    index_type                 weight() const noexcept { return h_->weight; }
    index_type                 prev_offset() const noexcept { return h_->prev_offset; }
    index_type                 next_offset() const noexcept { return h_->next_offset; }
    index_type                 root_offset() const noexcept { return h_->root_offset; }
    static FreeBlockRemovedAVL cast_from_raw( void* raw ) noexcept
    {
        return FreeBlockRemovedAVL( *detail::block_header_at<AT>( raw ) );
    }
    AllocatedBlock<AT> mark_as_allocated( index_type data_granules, index_type own_idx ) noexcept;
    SplittingBlock<AT> begin_splitting() noexcept;
    FreeBlock<AT>      insert_to_avl() noexcept { return FreeBlock<AT>( *h_ ); }

  private:
    Header* h_;
};
/*
## pmm-splittingblock
req: feat-002, fr-004, fr-021, dr-005, rule-002
*/
template <typename AT> class SplittingBlock
{
  public:
    using address_traits = AT;
    using index_type     = typename AT::index_type;
    using Header         = BlockHeader<AT>;
    explicit SplittingBlock( Header& h ) noexcept : h_( &h ) {}
    Header&               header() noexcept { return *h_; }
    const Header&         header() const noexcept { return *h_; }
    index_type            next_offset() const noexcept { return h_->next_offset; }
    static SplittingBlock cast_from_raw( void* raw ) noexcept
    {
        return SplittingBlock( *detail::block_header_at<AT>( raw ) );
    }
    void initialize_new_block( void* new_blk_ptr, [[maybe_unused]] index_type new_idx, index_type own_idx,
                               index_type new_block_total_granules ) noexcept
    {
        std::memset( new_blk_ptr, 0, sizeof( BlockHeader<AT> ) );
        Header* nh        = detail::block_header_at<AT>( new_blk_ptr );
        nh->prev_offset   = own_idx;
        nh->next_offset   = h_->next_offset;
        nh->left_offset   = AT::no_block;
        nh->right_offset  = AT::no_block;
        nh->parent_offset = AT::no_block;
        nh->avl_height    = 1;
        nh->weight        = new_block_total_granules;
        nh->root_offset   = 0;
        nh->node_type     = NodeType::Free;
    }
    void link_new_block( void* old_next_blk, index_type new_idx ) noexcept
    {
        if ( old_next_blk != nullptr )
            detail::block_header_at<AT>( old_next_blk )->prev_offset = new_idx;
        h_->next_offset = new_idx;
    }
    AllocatedBlock<AT> finalize_split( index_type data_granules, index_type own_idx ) noexcept;

  private:
    Header* h_;
};
/*
## pmm-allocatedblock
req: feat-002, fr-004, fr-005, fr-022, dr-005, rule-002
*/
template <typename AT> class AllocatedBlock
{
  public:
    using address_traits = AT;
    using index_type     = typename AT::index_type;
    using Header         = BlockHeader<AT>;
    explicit AllocatedBlock( Header& h ) noexcept : h_( &h ) {}
    Header&       header() noexcept { return *h_; }
    const Header& header() const noexcept { return *h_; }
    index_type    weight() const noexcept { return h_->weight; }
    index_type    root_offset() const noexcept { return h_->root_offset; }
    NodeType      node_type() const noexcept { return h_->node_type; }
/*
### pmm-allocatedblock-cast_from_raw
*/
    static AllocatedBlock cast_from_raw( void* raw ) noexcept
    {
        assert( raw != nullptr );
        Header* h = detail::block_header_at<AT>( raw );
        assert( pmm::is_allocated( h->node_type ) && "cast_from_raw<AllocatedBlock>: block is not allocated" );
        return AllocatedBlock( *h );
    }
/*
### pmm-allocatedblock-can_cast_from_raw
*/
    static bool can_cast_from_raw( const void* raw ) noexcept
    {
        if ( raw == nullptr )
            return false;
        if ( reinterpret_cast<std::uintptr_t>( raw ) % alignof( Header ) != 0 )
            return false;
        const Header* h = reinterpret_cast<const Header*>( raw );
        return pmm::is_allocated( h->node_type );
    }
/*
### pmm-allocatedblock-try_cast_from_raw
*/
    static std::optional<AllocatedBlock> try_cast_from_raw( void* raw ) noexcept
    {
        if ( !can_cast_from_raw( raw ) )
            return std::nullopt;
        return AllocatedBlock( *reinterpret_cast<Header*>( raw ) );
    }
/*
### pmm-allocatedblock-verify_invariants
*/
    bool verify_invariants( index_type own_idx ) const noexcept
    {
        return pmm::is_allocated( h_->node_type ) && h_->root_offset == own_idx;
    }
    void*       user_ptr() noexcept { return reinterpret_cast<uint8_t*>( h_ ) + sizeof( Header ); }
    const void* user_ptr() const noexcept { return reinterpret_cast<const uint8_t*>( h_ ) + sizeof( Header ); }
    FreeBlockNotInAVL<AT> mark_as_free( index_type total_granules ) noexcept;

  private:
    Header* h_;
};
/*
## pmm-freeblocknotinavl
req: feat-002, fr-005, fr-013, dr-005, rule-002
*/
template <typename AT> class FreeBlockNotInAVL
{
  public:
    using address_traits = AT;
    using index_type     = typename AT::index_type;
    using Header         = BlockHeader<AT>;
    explicit FreeBlockNotInAVL( Header& h ) noexcept : h_( &h ) {}
    Header&                  header() noexcept { return *h_; }
    const Header&            header() const noexcept { return *h_; }
    index_type               weight() const noexcept { return h_->weight; }
    index_type               root_offset() const noexcept { return h_->root_offset; }
    static FreeBlockNotInAVL cast_from_raw( void* raw ) noexcept
    {
        return FreeBlockNotInAVL( *detail::block_header_at<AT>( raw ) );
    }
    CoalescingBlock<AT> begin_coalescing() noexcept;
    FreeBlock<AT>       insert_to_avl() noexcept
    {
        h_->avl_height = 1;
        return FreeBlock<AT>( *h_ );
    }

  private:
    Header* h_;
};
/*
## pmm-coalescingblock
req: feat-002, fr-005, fr-022, dr-005, rule-002
*/
template <typename AT> class CoalescingBlock
{
  public:
    using address_traits = AT;
    using index_type     = typename AT::index_type;
    using Header         = BlockHeader<AT>;
    explicit CoalescingBlock( Header& h ) noexcept : h_( &h ) {}
    Header&                header() noexcept { return *h_; }
    const Header&          header() const noexcept { return *h_; }
    index_type             next_offset() const noexcept { return h_->next_offset; }
    index_type             prev_offset() const noexcept { return h_->prev_offset; }
    index_type             weight() const noexcept { return h_->weight; }
    static CoalescingBlock cast_from_raw( void* raw ) noexcept
    {
        return CoalescingBlock( *detail::block_header_at<AT>( raw ) );
    }
    void coalesce_with_next( void* next_blk, void* next_next_blk, index_type own_idx,
                             index_type next_block_granules ) noexcept
    {
        Header* nx      = detail::block_header_at<AT>( next_blk );
        h_->next_offset = nx->next_offset;
        if ( next_next_blk != nullptr )
            detail::block_header_at<AT>( next_next_blk )->prev_offset = own_idx;
        std::memset( next_blk, 0, sizeof( Header ) );
        h_->weight = static_cast<index_type>( h_->weight + next_block_granules );
    }
    CoalescingBlock coalesce_with_prev( void* prev_blk, void* next_blk, index_type prev_idx,
                                        index_type self_block_granules ) noexcept
    {
        Header* prev      = detail::block_header_at<AT>( prev_blk );
        prev->next_offset = h_->next_offset;
        if ( next_blk != nullptr )
            detail::block_header_at<AT>( next_blk )->prev_offset = prev_idx;
        std::memset( h_, 0, sizeof( Header ) );
        prev->weight = static_cast<index_type>( prev->weight + self_block_granules );
        return CoalescingBlock( *prev );
    }
    FreeBlock<AT> finalize_coalesce() noexcept
    {
        h_->avl_height = 1;
        h_->node_type  = NodeType::Free;
        return FreeBlock<AT>( *h_ );
    }

  private:
    Header* h_;
};
template <typename AT> FreeBlockRemovedAVL<AT> FreeBlock<AT>::remove_from_avl() noexcept
{
    return FreeBlockRemovedAVL<AT>( *h_ );
}
template <typename AT>
AllocatedBlock<AT> FreeBlockRemovedAVL<AT>::mark_as_allocated( index_type data_granules, index_type own_idx ) noexcept
{
    h_->weight        = data_granules;
    h_->root_offset   = own_idx;
    h_->left_offset   = AT::no_block;
    h_->right_offset  = AT::no_block;
    h_->parent_offset = AT::no_block;
    h_->avl_height    = 0;
    h_->node_type     = NodeType::Generic;
    return AllocatedBlock<AT>( *h_ );
}
template <typename AT> SplittingBlock<AT> FreeBlockRemovedAVL<AT>::begin_splitting() noexcept
{
    return SplittingBlock<AT>( *h_ );
}
template <typename AT>
AllocatedBlock<AT> SplittingBlock<AT>::finalize_split( index_type data_granules, index_type own_idx ) noexcept
{
    h_->weight        = data_granules;
    h_->root_offset   = own_idx;
    h_->left_offset   = AT::no_block;
    h_->right_offset  = AT::no_block;
    h_->parent_offset = AT::no_block;
    h_->avl_height    = 0;
    h_->node_type     = NodeType::Generic;
    return AllocatedBlock<AT>( *h_ );
}
template <typename AT> FreeBlockNotInAVL<AT> AllocatedBlock<AT>::mark_as_free( index_type total_granules ) noexcept
{
    h_->weight      = total_granules;
    h_->root_offset = 0;
    h_->node_type   = NodeType::Free;
    return FreeBlockNotInAVL<AT>( *h_ );
}
template <typename AT> CoalescingBlock<AT> FreeBlockNotInAVL<AT>::begin_coalescing() noexcept
{
    return CoalescingBlock<AT>( *h_ );
}
template <typename AT> int detect_block_state( const void* raw_blk, typename AT::index_type own_idx ) noexcept
{
    using BlockState = BlockStateBase<AT>;
    if ( BlockState::is_free_raw( raw_blk ) )
        return 0;
    if ( BlockState::is_allocated_raw( raw_blk, own_idx ) )
        return 1;
    return -1;
}
template <typename AT> inline void recover_block_state( void* raw_blk, typename AT::index_type own_idx ) noexcept
{
    BlockStateBase<AT>::recover_state( raw_blk, own_idx );
}
template <typename AT>
inline void verify_block_state( const void* raw_blk, typename AT::index_type own_idx, VerifyResult& result ) noexcept
{
    BlockStateBase<AT>::verify_state( raw_blk, own_idx, result );
}
}
