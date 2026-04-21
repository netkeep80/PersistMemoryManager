#pragma once

#include "pmm/address_traits.h"
#include "pmm/block.h"
#include "pmm/block_field.h"
#include "pmm/diagnostics.h"
#include "pmm/tree_node.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm
{

template <typename AddressTraitsT> class FreeBlock;
template <typename AddressTraitsT> class AllocatedBlock;
template <typename AddressTraitsT> class FreeBlockRemovedAVL;
template <typename AddressTraitsT> class FreeBlockNotInAVL;
template <typename AddressTraitsT> class SplittingBlock;
template <typename AddressTraitsT> class CoalescingBlock;

template <typename AddressTraitsT> class BlockStateBase : private Block<AddressTraitsT>
{
  private:
    using TNode = TreeNode<AddressTraitsT>;

  public:
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;
    using BaseBlock      = Block<AddressTraitsT>;

    template <typename FieldTag> using field_value_type = detail::block_field_value_t<AddressTraitsT, FieldTag>;

    template <typename FieldTag>
    static constexpr std::size_t field_offset = detail::block_field_offset_v<AddressTraitsT, FieldTag>;

    template <typename FieldTag> static field_value_type<FieldTag> get_field_of( const void* raw_blk ) noexcept
    {
        return detail::read_block_field<AddressTraitsT, FieldTag>( raw_blk );
    }

    template <typename FieldTag> static void set_field_of( void* raw_blk, field_value_type<FieldTag> value ) noexcept
    {
        detail::write_block_field<AddressTraitsT, FieldTag>( raw_blk, value );
    }

    static bool is_free_raw( const void* raw_blk ) noexcept
    {
        return get_weight( raw_blk ) == 0 && get_root_offset( raw_blk ) == 0;
    }

    static bool is_allocated_raw( const void* raw_blk, index_type own_idx ) noexcept
    {
        return get_weight( raw_blk ) > 0 && get_root_offset( raw_blk ) == own_idx;
    }

    static constexpr std::size_t kOffsetPrevOffset = field_offset<detail::BlockPrevOffsetField>;

    static constexpr std::size_t kOffsetNextOffset = field_offset<detail::BlockNextOffsetField>;

    static constexpr std::size_t kOffsetWeight = field_offset<detail::BlockWeightField>;

    static constexpr std::size_t kOffsetLeftOffset = field_offset<detail::BlockLeftOffsetField>;

    static constexpr std::size_t kOffsetRightOffset = field_offset<detail::BlockRightOffsetField>;

    static constexpr std::size_t kOffsetParentOffset = field_offset<detail::BlockParentOffsetField>;

    static constexpr std::size_t kOffsetRootOffset = field_offset<detail::BlockRootOffsetField>;

    static constexpr std::size_t kOffsetAvlHeight = field_offset<detail::BlockAvlHeightField>;

    static constexpr std::size_t kOffsetNodeType = field_offset<detail::BlockNodeTypeField>;

    static_assert( detail::block_tree_slot_size_v<AddressTraitsT> == sizeof( TNode ),
                   "Block field descriptors must match TreeNode layout" );
    static_assert( detail::block_layout_size_v<AddressTraitsT> == sizeof( BaseBlock ),
                   "Block field descriptors must match Block layout" );

    BlockStateBase() = delete;

    index_type weight() const noexcept { return get_weight( this ); }

    index_type prev_offset() const noexcept { return get_prev_offset( this ); }
    index_type next_offset() const noexcept { return get_next_offset( this ); }

    index_type   left_offset() const noexcept { return get_left_offset( this ); }
    index_type   right_offset() const noexcept { return get_right_offset( this ); }
    index_type   parent_offset() const noexcept { return get_parent_offset( this ); }
    std::int16_t avl_height() const noexcept { return get_avl_height( this ); }

    index_type root_offset() const noexcept { return get_root_offset( this ); }

    std::uint16_t node_type() const noexcept { return get_node_type( this ); }

    bool is_free() const noexcept { return is_free_raw( this ); }

    bool is_allocated( index_type own_idx ) const noexcept { return is_allocated_raw( this, own_idx ); }

    bool is_permanently_locked() const noexcept { return node_type() == pmm::kNodeReadOnly; }

    static void recover_state( void* raw_blk, index_type own_idx ) noexcept
    {
        const index_type weight_val = get_weight( raw_blk );
        const index_type root_val   = get_root_offset( raw_blk );

        if ( weight_val > 0 && root_val != own_idx )
            set_root_offset_of( raw_blk, own_idx );

        if ( weight_val == 0 && root_val != 0 )
            set_root_offset_of( raw_blk, 0 );
    }

    static void verify_state( const void* raw_blk, index_type own_idx, VerifyResult& result ) noexcept
    {
        const index_type weight_val = get_weight( raw_blk );
        const index_type root_val   = get_root_offset( raw_blk );
        if ( weight_val > 0 && root_val != own_idx )
        {
            result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( own_idx ), static_cast<std::uint64_t>( own_idx ),
                        static_cast<std::uint64_t>( root_val ) );
        }
        if ( weight_val == 0 && root_val != 0 )
        {
            result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( own_idx ), 0, static_cast<std::uint64_t>( root_val ) );
        }
    }

    static void reset_avl_fields_of( void* raw_blk ) noexcept
    {
        set_left_offset_of( raw_blk, AddressTraitsT::no_block );
        set_right_offset_of( raw_blk, AddressTraitsT::no_block );
        set_parent_offset_of( raw_blk, AddressTraitsT::no_block );
        set_avl_height_of( raw_blk, 0 );
    }

    static void repair_prev_offset( void* raw_blk, index_type prev_idx ) noexcept
    {
        set_prev_offset_of( raw_blk, prev_idx );
    }

    static index_type get_prev_offset( const void* raw_blk ) noexcept
    {
        return get_field_of<detail::BlockPrevOffsetField>( raw_blk );
    }

    static index_type get_next_offset( const void* raw_blk ) noexcept
    {
        return get_field_of<detail::BlockNextOffsetField>( raw_blk );
    }

    static index_type get_weight( const void* raw_blk ) noexcept
    {
        return get_field_of<detail::BlockWeightField>( raw_blk );
    }

    static void init_fields( void* raw_blk, index_type prev_idx, index_type next_idx, std::int16_t avl_height_val,
                             index_type weight_val, index_type root_offset_val ) noexcept
    {
        set_prev_offset_of( raw_blk, prev_idx );
        set_next_offset_of( raw_blk, next_idx );
        set_left_offset_of( raw_blk, AddressTraitsT::no_block );
        set_right_offset_of( raw_blk, AddressTraitsT::no_block );
        set_parent_offset_of( raw_blk, AddressTraitsT::no_block );
        set_avl_height_of( raw_blk, avl_height_val );
        set_weight_of( raw_blk, weight_val );
        set_root_offset_of( raw_blk, root_offset_val );
    }

    static void set_next_offset_of( void* raw_blk, index_type next_idx ) noexcept
    {
        set_field_of<detail::BlockNextOffsetField>( raw_blk, next_idx );
    }

    static index_type get_left_offset( const void* b ) noexcept
    {
        return get_field_of<detail::BlockLeftOffsetField>( b );
    }
    static index_type get_right_offset( const void* b ) noexcept
    {
        return get_field_of<detail::BlockRightOffsetField>( b );
    }
    static index_type get_parent_offset( const void* b ) noexcept
    {
        return get_field_of<detail::BlockParentOffsetField>( b );
    }
    static index_type get_root_offset( const void* b ) noexcept
    {
        return get_field_of<detail::BlockRootOffsetField>( b );
    }
    static void set_left_offset_of( void* b, index_type v ) noexcept
    {
        set_field_of<detail::BlockLeftOffsetField>( b, v );
    }
    static void set_right_offset_of( void* b, index_type v ) noexcept
    {
        set_field_of<detail::BlockRightOffsetField>( b, v );
    }
    static void set_parent_offset_of( void* b, index_type v ) noexcept
    {
        set_field_of<detail::BlockParentOffsetField>( b, v );
    }
    static void set_prev_offset_of( void* b, index_type v ) noexcept
    {
        set_field_of<detail::BlockPrevOffsetField>( b, v );
    }
    static void set_weight_of( void* b, index_type v ) noexcept { set_field_of<detail::BlockWeightField>( b, v ); }
    static void set_root_offset_of( void* b, index_type v ) noexcept
    {
        set_field_of<detail::BlockRootOffsetField>( b, v );
    }

    static std::int16_t get_avl_height( const void* raw_blk ) noexcept
    {
        return get_field_of<detail::BlockAvlHeightField>( raw_blk );
    }
    static void set_avl_height_of( void* raw_blk, std::int16_t v ) noexcept
    {
        set_field_of<detail::BlockAvlHeightField>( raw_blk, v );
    }
    static std::uint16_t get_node_type( const void* raw_blk ) noexcept
    {
        return get_field_of<detail::BlockNodeTypeField>( raw_blk );
    }
    static void set_node_type_of( void* raw_blk, std::uint16_t v ) noexcept
    {
        set_field_of<detail::BlockNodeTypeField>( raw_blk, v );
    }

  protected:
    template <typename StateT> static StateT* state_from_raw( void* raw ) noexcept
    {
        return reinterpret_cast<StateT*>( raw );
    }

    template <typename StateT> static const StateT* state_from_raw( const void* raw ) noexcept
    {
        return reinterpret_cast<const StateT*>( raw );
    }

    template <typename StateT> StateT* state_as() noexcept { return reinterpret_cast<StateT*>( this ); }

    void set_weight( index_type v ) noexcept { set_weight_of( this, v ); }
    void set_prev_offset( index_type v ) noexcept { set_prev_offset_of( this, v ); }
    void set_next_offset( index_type v ) noexcept { set_next_offset_of( this, v ); }
    void set_left_offset( index_type v ) noexcept { set_left_offset_of( this, v ); }
    void set_right_offset( index_type v ) noexcept { set_right_offset_of( this, v ); }
    void set_parent_offset( index_type v ) noexcept { set_parent_offset_of( this, v ); }
    void set_avl_height( std::int16_t v ) noexcept { set_avl_height_of( this, v ); }
    void set_root_offset( index_type v ) noexcept { set_root_offset_of( this, v ); }
    void set_node_type( std::uint16_t v ) noexcept { set_node_type_of( this, v ); }

    void reset_avl_fields() noexcept
    {
        set_left_offset( AddressTraitsT::no_block );
        set_right_offset( AddressTraitsT::no_block );
        set_parent_offset( AddressTraitsT::no_block );
        set_avl_height( 0 );
    }
};

static_assert( sizeof( BlockStateBase<DefaultAddressTraits> ) == sizeof( Block<DefaultAddressTraits> ),
               "BlockStateBase<A> must have same size as Block<A> " );
static_assert( sizeof( BlockStateBase<DefaultAddressTraits> ) == 32,
               "BlockStateBase<DefaultAddressTraits> must be 32 bytes " );

template <typename AddressTraitsT> class FreeBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static FreeBlock* cast_from_raw( void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( !Base::is_free_raw( raw ) )
        {
            assert( false && "cast_from_raw<FreeBlock>: block is not in FreeBlock state" );
            return nullptr;
        }
        return Base::template state_from_raw<FreeBlock<AddressTraitsT>>( raw );
    }

    static const FreeBlock* cast_from_raw( const void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( !Base::is_free_raw( raw ) )
        {
            assert( false && "cast_from_raw<FreeBlock>: block is not in FreeBlock state" );
            return nullptr;
        }
        return Base::template state_from_raw<FreeBlock<AddressTraitsT>>( raw );
    }

    bool verify_invariants() const noexcept { return Base::is_free(); }

    FreeBlockRemovedAVL<AddressTraitsT>* remove_from_avl() noexcept
    {

        return this->template state_as<FreeBlockRemovedAVL<AddressTraitsT>>();
    }
};

template <typename AddressTraitsT> class FreeBlockRemovedAVL : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static FreeBlockRemovedAVL* cast_from_raw( void* raw ) noexcept
    {
        return Base::template state_from_raw<FreeBlockRemovedAVL<AddressTraitsT>>( raw );
    }

    AllocatedBlock<AddressTraitsT>* mark_as_allocated( index_type data_granules, index_type own_idx ) noexcept
    {
        Base::set_weight( data_granules );
        Base::set_root_offset( own_idx );
        Base::reset_avl_fields();
        return this->template state_as<AllocatedBlock<AddressTraitsT>>();
    }

    SplittingBlock<AddressTraitsT>* begin_splitting() noexcept
    {
        return this->template state_as<SplittingBlock<AddressTraitsT>>();
    }

    FreeBlock<AddressTraitsT>* insert_to_avl() noexcept { return this->template state_as<FreeBlock<AddressTraitsT>>(); }
};

template <typename AddressTraitsT> class SplittingBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static SplittingBlock* cast_from_raw( void* raw ) noexcept
    {
        return Base::template state_from_raw<SplittingBlock<AddressTraitsT>>( raw );
    }

    void initialize_new_block( void* new_blk_ptr, [[maybe_unused]] index_type new_idx, index_type own_idx ) noexcept
    {
        std::memset( new_blk_ptr, 0, sizeof( Block<AddressTraitsT> ) );
        Base::init_fields( new_blk_ptr, own_idx, this->next_offset(), 1, 0, 0 );
    }

    void link_new_block( void* old_next_blk, index_type new_idx ) noexcept
    {
        if ( old_next_blk != nullptr )
        {
            Base::set_prev_offset_of( old_next_blk, new_idx );
        }
        Base::set_next_offset( new_idx );
    }

    AllocatedBlock<AddressTraitsT>* finalize_split( index_type data_granules, index_type own_idx ) noexcept
    {
        Base::set_weight( data_granules );
        Base::set_root_offset( own_idx );
        Base::reset_avl_fields();
        return this->template state_as<AllocatedBlock<AddressTraitsT>>();
    }
};

template <typename AddressTraitsT> class AllocatedBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static AllocatedBlock* cast_from_raw( void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( Base::get_weight( raw ) == 0 )
        {
            assert( false && "cast_from_raw<AllocatedBlock>: block is not allocated (weight==0)" );
            return nullptr;
        }
        return Base::template state_from_raw<AllocatedBlock<AddressTraitsT>>( raw );
    }

    static const AllocatedBlock* cast_from_raw( const void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( Base::get_weight( raw ) == 0 )
        {
            assert( false && "cast_from_raw<AllocatedBlock>: block is not allocated (weight==0)" );
            return nullptr;
        }
        return Base::template state_from_raw<AllocatedBlock<AddressTraitsT>>( raw );
    }

    bool verify_invariants( index_type own_idx ) const noexcept { return Base::is_allocated( own_idx ); }

    void* user_ptr() noexcept { return reinterpret_cast<std::uint8_t*>( this ) + sizeof( Block<AddressTraitsT> ); }

    const void* user_ptr() const noexcept
    {
        return reinterpret_cast<const std::uint8_t*>( this ) + sizeof( Block<AddressTraitsT> );
    }

    FreeBlockNotInAVL<AddressTraitsT>* mark_as_free() noexcept
    {
        Base::set_weight( 0 );
        Base::set_root_offset( 0 );
        return this->template state_as<FreeBlockNotInAVL<AddressTraitsT>>();
    }
};

template <typename AddressTraitsT> class FreeBlockNotInAVL : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static FreeBlockNotInAVL* cast_from_raw( void* raw ) noexcept
    {
        return Base::template state_from_raw<FreeBlockNotInAVL<AddressTraitsT>>( raw );
    }

    CoalescingBlock<AddressTraitsT>* begin_coalescing() noexcept
    {
        return this->template state_as<CoalescingBlock<AddressTraitsT>>();
    }

    FreeBlock<AddressTraitsT>* insert_to_avl() noexcept
    {
        Base::set_avl_height( 1 );
        return this->template state_as<FreeBlock<AddressTraitsT>>();
    }
};

template <typename AddressTraitsT> class CoalescingBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static CoalescingBlock* cast_from_raw( void* raw ) noexcept
    {
        return Base::template state_from_raw<CoalescingBlock<AddressTraitsT>>( raw );
    }

    void coalesce_with_next( void* next_blk, void* next_next_blk, index_type own_idx ) noexcept
    {

        Base::set_next_offset( Base::get_next_offset( next_blk ) );
        if ( next_next_blk != nullptr )
        {
            Base::set_prev_offset_of( next_next_blk, own_idx );
        }

        std::memset( next_blk, 0, sizeof( Block<AddressTraitsT> ) );
    }

    CoalescingBlock<AddressTraitsT>* coalesce_with_prev( void* prev_blk, void* next_blk, index_type prev_idx ) noexcept
    {
        Base::set_next_offset_of( prev_blk, Base::next_offset() );

        if ( next_blk != nullptr )
        {
            Base::set_prev_offset_of( next_blk, prev_idx );
        }

        std::memset( this, 0, sizeof( Block<AddressTraitsT> ) );

        return Base::template state_from_raw<CoalescingBlock<AddressTraitsT>>( prev_blk );
    }

    FreeBlock<AddressTraitsT>* finalize_coalesce() noexcept
    {
        Base::set_avl_height( 1 );
        return this->template state_as<FreeBlock<AddressTraitsT>>();
    }
};

template <typename AddressTraitsT>
int detect_block_state( const void* raw_blk, typename AddressTraitsT::index_type own_idx ) noexcept
{
    using BlockState = BlockStateBase<AddressTraitsT>;
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

} // namespace pmm
