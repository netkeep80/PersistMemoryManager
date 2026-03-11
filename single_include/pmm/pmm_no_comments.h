
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace pmm
{

template <typename IndexT, std::size_t GranuleSz> struct AddressTraits
{
    static_assert( std::is_unsigned<IndexT>::value, "AddressTraits: IndexT must be an unsigned integer type" );
    static_assert( GranuleSz >= 4, "AddressTraits: GranuleSz must be >= 4 (minimum architecture word size)" );
    static_assert( ( GranuleSz & ( GranuleSz - 1 ) ) == 0, "AddressTraits: GranuleSz must be a power of 2" );

    using index_type = IndexT;

    static constexpr std::size_t granule_size = GranuleSz;

    static constexpr index_type no_block = std::numeric_limits<IndexT>::max();

    static constexpr index_type bytes_to_granules( std::size_t bytes ) noexcept
    {
        if ( bytes == 0 )
            return static_cast<index_type>( 0 );
        
        if ( bytes > std::numeric_limits<std::size_t>::max() - ( granule_size - 1 ) )
            return static_cast<index_type>( 0 ); 
        std::size_t granules = ( bytes + granule_size - 1 ) / granule_size;
        if ( granules > static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) )
            return static_cast<index_type>( 0 ); 
        return static_cast<index_type>( granules );
    }

    static constexpr std::size_t granules_to_bytes( index_type granules ) noexcept
    {
        return static_cast<std::size_t>( granules ) * granule_size;
    }

    static constexpr std::size_t idx_to_byte_off( index_type idx ) noexcept
    {
        return static_cast<std::size_t>( idx ) * granule_size;
    }

    static index_type byte_off_to_idx( std::size_t byte_off ) noexcept
    {
        assert( byte_off % granule_size == 0 );
        assert( byte_off / granule_size <= static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) );
        return static_cast<index_type>( byte_off / granule_size );
    }
};

using SmallAddressTraits = AddressTraits<std::uint16_t, 16>;

using DefaultAddressTraits = AddressTraits<std::uint32_t, 16>;

using LargeAddressTraits = AddressTraits<std::uint64_t, 64>;

} 

#include <mutex>
#include <shared_mutex>

namespace pmm
{
namespace config
{

struct SharedMutexLock
{
    using mutex_type       = std::shared_mutex;
    using shared_lock_type = std::shared_lock<std::shared_mutex>;
    using unique_lock_type = std::unique_lock<std::shared_mutex>;
};

struct NoLock
{
    struct mutex_type
    {
        void lock() {}
        void unlock() {}
        void lock_shared() {}
        void unlock_shared() {}
        bool try_lock() { return true; }
        bool try_lock_shared() { return true; }
    };

    struct shared_lock_type
    {
        explicit shared_lock_type( mutex_type& ) {}
    };

    struct unique_lock_type
    {
        explicit unique_lock_type( mutex_type& ) {}
    };
};

inline constexpr std::size_t kDefaultGrowNumerator = 5;

inline constexpr std::size_t kDefaultGrowDenominator = 4;

} 
} 

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

enum : std::uint16_t
{
    kNodeReadWrite = 0,
    kNodeReadOnly  = 1,
};

template <typename AddressTraitsT> struct TreeNode
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

    index_type get_left() const noexcept { return left_offset; }

    index_type get_right() const noexcept { return right_offset; }

    index_type get_parent() const noexcept { return parent_offset; }

    index_type get_root() const noexcept { return root_offset; }

    index_type get_weight() const noexcept { return weight; }

    std::int16_t get_height() const noexcept { return avl_height; }

    std::uint16_t get_node_type() const noexcept { return node_type; }

    void set_left( index_type v ) noexcept { left_offset = v; }

    void set_right( index_type v ) noexcept { right_offset = v; }

    void set_parent( index_type v ) noexcept { parent_offset = v; }

    void set_root( index_type v ) noexcept { root_offset = v; }

    void set_weight( index_type v ) noexcept { weight = v; }

    void set_height( std::int16_t v ) noexcept { avl_height = v; }

    void set_node_type( std::uint16_t v ) noexcept { node_type = v; }

  protected:
    
    index_type weight;
    
    index_type left_offset;
    
    index_type right_offset;
    
    index_type parent_offset;
    
    index_type root_offset;
    
    std::int16_t avl_height;
    
    std::uint16_t node_type;
};

static_assert( std::is_standard_layout<pmm::TreeNode<pmm::DefaultAddressTraits>>::value,
               "TreeNode must be standard-layout (Issue #87)" );

} 

#include <cstdint>
#include <type_traits>

namespace pmm
{

template <typename AddressTraitsT> struct Block : TreeNode<AddressTraitsT>
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

  protected:
    
    index_type prev_offset;
    
    index_type next_offset;
};

static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32,
               "Block<DefaultAddressTraits> must be 32 bytes (Issue #87, #138)" );

} 

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

    static constexpr std::size_t kOffsetPrevOffset = sizeof( TNode );
    
    static constexpr std::size_t kOffsetNextOffset = sizeof( TNode ) + sizeof( index_type );
    
    static constexpr std::size_t kOffsetWeight = 0;
    
    static constexpr std::size_t kOffsetLeftOffset = sizeof( index_type );
    
    static constexpr std::size_t kOffsetRightOffset = 2 * sizeof( index_type );
    
    static constexpr std::size_t kOffsetParentOffset = 3 * sizeof( index_type );
    
    static constexpr std::size_t kOffsetRootOffset = 4 * sizeof( index_type );
    
    static constexpr std::size_t kOffsetAvlHeight = 5 * sizeof( index_type );
    
    static constexpr std::size_t kOffsetNodeType = 5 * sizeof( index_type ) + 2;

    BlockStateBase() = delete;

    index_type weight() const noexcept { return TNode::weight; }

    index_type prev_offset() const noexcept { return Block<AddressTraitsT>::prev_offset; }
    index_type next_offset() const noexcept { return Block<AddressTraitsT>::next_offset; }

    index_type   left_offset() const noexcept { return TNode::left_offset; }
    index_type   right_offset() const noexcept { return TNode::right_offset; }
    index_type   parent_offset() const noexcept { return TNode::parent_offset; }
    std::int16_t avl_height() const noexcept { return TNode::avl_height; }

    index_type root_offset() const noexcept { return TNode::root_offset; }

    std::uint16_t node_type() const noexcept { return TNode::node_type; }

    bool is_free() const noexcept { return weight() == 0 && root_offset() == 0; }

    bool is_allocated( index_type own_idx ) const noexcept { return weight() > 0 && root_offset() == own_idx; }

    bool is_permanently_locked() const noexcept { return node_type() == pmm::kNodeReadOnly; }

    static void recover_state( void* raw_blk, index_type own_idx ) noexcept
    {
        auto* blk = reinterpret_cast<BlockStateBase*>( raw_blk );
        
        if ( blk->weight() > 0 && blk->root_offset() != own_idx )
            blk->set_root_offset( own_idx );
        
        if ( blk->weight() == 0 && blk->root_offset() != 0 )
            blk->set_root_offset( 0 );
    }

    static void reset_avl_fields_of( void* raw_blk ) noexcept
    {
        auto* blk = reinterpret_cast<BlockStateBase*>( raw_blk );
        blk->set_left_offset( AddressTraitsT::no_block );
        blk->set_right_offset( AddressTraitsT::no_block );
        blk->set_parent_offset( AddressTraitsT::no_block );
        blk->set_avl_height( 0 );
    }

    static void repair_prev_offset( void* raw_blk, index_type prev_idx ) noexcept
    {
        auto* blk = reinterpret_cast<BlockStateBase*>( raw_blk );
        blk->set_prev_offset( prev_idx );
    }

    static index_type get_prev_offset( const void* raw_blk ) noexcept
    {
        const auto* blk = reinterpret_cast<const BlockStateBase*>( raw_blk );
        return blk->prev_offset();
    }

    static index_type get_next_offset( const void* raw_blk ) noexcept
    {
        const auto* blk = reinterpret_cast<const BlockStateBase*>( raw_blk );
        return blk->next_offset();
    }

    static index_type get_weight( const void* raw_blk ) noexcept
    {
        const auto* blk = reinterpret_cast<const BlockStateBase*>( raw_blk );
        return blk->weight();
    }

    static void init_fields( void* raw_blk, index_type prev_idx, index_type next_idx, std::int16_t avl_height_val,
                             index_type weight_val, index_type root_offset_val ) noexcept
    {
        auto* blk = reinterpret_cast<BlockStateBase*>( raw_blk );
        blk->set_prev_offset( prev_idx );
        blk->set_next_offset( next_idx );
        blk->set_left_offset( AddressTraitsT::no_block );
        blk->set_right_offset( AddressTraitsT::no_block );
        blk->set_parent_offset( AddressTraitsT::no_block );
        blk->set_avl_height( avl_height_val );
        blk->set_weight( weight_val );
        blk->set_root_offset( root_offset_val );
    }

    static void set_next_offset_of( void* raw_blk, index_type next_idx ) noexcept
    {
        auto* blk = reinterpret_cast<BlockStateBase*>( raw_blk );
        blk->set_next_offset( next_idx );
    }

    static index_type get_left_offset( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->left_offset();
    }
    
    static index_type get_right_offset( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->right_offset();
    }
    
    static index_type get_parent_offset( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->parent_offset();
    }
    
    static std::int16_t get_avl_height( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->avl_height();
    }
    
    static void set_left_offset_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_left_offset( v );
    }
    
    static void set_right_offset_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_right_offset( v );
    }
    
    static void set_parent_offset_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_parent_offset( v );
    }
    
    static void set_avl_height_of( void* raw_blk, std::int16_t v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_avl_height( v );
    }
    
    static index_type get_root_offset( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->root_offset();
    }
    
    static void set_prev_offset_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_prev_offset( v );
    }
    
    static void set_weight_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_weight( v );
    }
    
    static void set_root_offset_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_root_offset( v );
    }
    
    static std::uint16_t get_node_type( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->node_type();
    }
    
    static void set_node_type_of( void* raw_blk, std::uint16_t v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_node_type( v );
    }

  protected:
    
    void set_weight( index_type v ) noexcept { TNode::weight = v; }
    void set_prev_offset( index_type v ) noexcept { Block<AddressTraitsT>::prev_offset = v; }
    void set_next_offset( index_type v ) noexcept { Block<AddressTraitsT>::next_offset = v; }
    void set_left_offset( index_type v ) noexcept { TNode::left_offset = v; }
    void set_right_offset( index_type v ) noexcept { TNode::right_offset = v; }
    void set_parent_offset( index_type v ) noexcept { TNode::parent_offset = v; }
    void set_avl_height( std::int16_t v ) noexcept { TNode::avl_height = v; }
    void set_root_offset( index_type v ) noexcept { TNode::root_offset = v; }
    void set_node_type( std::uint16_t v ) noexcept { TNode::node_type = v; }

    void reset_avl_fields() noexcept
    {
        set_left_offset( AddressTraitsT::no_block );
        set_right_offset( AddressTraitsT::no_block );
        set_parent_offset( AddressTraitsT::no_block );
        set_avl_height( 0 );
    }
};

static_assert( sizeof( BlockStateBase<DefaultAddressTraits> ) == sizeof( Block<DefaultAddressTraits> ),
               "BlockStateBase<A> must have same size as Block<A> (Issue #93)" );
static_assert( sizeof( BlockStateBase<DefaultAddressTraits> ) == 32,
               "BlockStateBase<DefaultAddressTraits> must be 32 bytes (Issue #93)" );

template <typename AddressTraitsT> class FreeBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static FreeBlock* cast_from_raw( void* raw ) noexcept
    {
        assert( raw != nullptr );
        assert( reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->is_free() &&
                "cast_from_raw<FreeBlock>: block is not in FreeBlock state (weight!=0 or root_offset!=0)" );
        return reinterpret_cast<FreeBlock*>( raw );
    }

    static const FreeBlock* cast_from_raw( const void* raw ) noexcept
    {
        assert( raw != nullptr );
        assert( reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->is_free() &&
                "cast_from_raw<FreeBlock>: block is not in FreeBlock state (weight!=0 or root_offset!=0)" );
        return reinterpret_cast<const FreeBlock*>( raw );
    }

    bool verify_invariants() const noexcept { return Base::is_free(); }

    FreeBlockRemovedAVL<AddressTraitsT>* remove_from_avl() noexcept
    {
        
        return reinterpret_cast<FreeBlockRemovedAVL<AddressTraitsT>*>( this );
    }
};

template <typename AddressTraitsT> class FreeBlockRemovedAVL : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static FreeBlockRemovedAVL* cast_from_raw( void* raw ) noexcept
    {
        return reinterpret_cast<FreeBlockRemovedAVL*>( raw );
    }

    AllocatedBlock<AddressTraitsT>* mark_as_allocated( index_type data_granules, index_type own_idx ) noexcept
    {
        Base::set_weight( data_granules );
        Base::set_root_offset( own_idx );
        Base::reset_avl_fields();
        return reinterpret_cast<AllocatedBlock<AddressTraitsT>*>( this );
    }

    SplittingBlock<AddressTraitsT>* begin_splitting() noexcept
    {
        return reinterpret_cast<SplittingBlock<AddressTraitsT>*>( this );
    }

    FreeBlock<AddressTraitsT>* insert_to_avl() noexcept { return reinterpret_cast<FreeBlock<AddressTraitsT>*>( this ); }
};

template <typename AddressTraitsT> class SplittingBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static SplittingBlock* cast_from_raw( void* raw ) noexcept { return reinterpret_cast<SplittingBlock*>( raw ); }

    void initialize_new_block( void* new_blk_ptr, [[maybe_unused]] index_type new_idx, index_type own_idx ) noexcept
    {
        std::memset( new_blk_ptr, 0, sizeof( Block<AddressTraitsT> ) );
        
        auto* new_blk = reinterpret_cast<SplittingBlock<AddressTraitsT>*>( new_blk_ptr );
        new_blk->set_prev_offset( own_idx );
        new_blk->set_next_offset( Base::next_offset() );
        new_blk->set_left_offset( AddressTraitsT::no_block );
        new_blk->set_right_offset( AddressTraitsT::no_block );
        new_blk->set_parent_offset( AddressTraitsT::no_block );
        new_blk->set_avl_height( 1 ); 
        new_blk->set_weight( 0 );
        new_blk->set_root_offset( 0 );
    }

    void link_new_block( void* old_next_blk, index_type new_idx ) noexcept
    {
        if ( old_next_blk != nullptr )
        {
            auto* old_next_blk_state = reinterpret_cast<SplittingBlock<AddressTraitsT>*>( old_next_blk );
            old_next_blk_state->set_prev_offset( new_idx );
        }
        Base::set_next_offset( new_idx );
    }

    AllocatedBlock<AddressTraitsT>* finalize_split( index_type data_granules, index_type own_idx ) noexcept
    {
        Base::set_weight( data_granules );
        Base::set_root_offset( own_idx );
        Base::reset_avl_fields();
        return reinterpret_cast<AllocatedBlock<AddressTraitsT>*>( this );
    }
};

template <typename AddressTraitsT> class AllocatedBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static AllocatedBlock* cast_from_raw( void* raw ) noexcept
    {
        assert( raw != nullptr );
        assert( reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->weight() > 0 &&
                "cast_from_raw<AllocatedBlock>: block is not allocated (weight==0)" );
        return reinterpret_cast<AllocatedBlock*>( raw );
    }

    static const AllocatedBlock* cast_from_raw( const void* raw ) noexcept
    {
        assert( raw != nullptr );
        assert( reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->weight() > 0 &&
                "cast_from_raw<AllocatedBlock>: block is not allocated (weight==0)" );
        return reinterpret_cast<const AllocatedBlock*>( raw );
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
        return reinterpret_cast<FreeBlockNotInAVL<AddressTraitsT>*>( this );
    }
};

template <typename AddressTraitsT> class FreeBlockNotInAVL : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static FreeBlockNotInAVL* cast_from_raw( void* raw ) noexcept
    {
        return reinterpret_cast<FreeBlockNotInAVL*>( raw );
    }

    CoalescingBlock<AddressTraitsT>* begin_coalescing() noexcept
    {
        return reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( this );
    }

    FreeBlock<AddressTraitsT>* insert_to_avl() noexcept
    {
        Base::set_avl_height( 1 ); 
        return reinterpret_cast<FreeBlock<AddressTraitsT>*>( this );
    }
};

template <typename AddressTraitsT> class CoalescingBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    static CoalescingBlock* cast_from_raw( void* raw ) noexcept { return reinterpret_cast<CoalescingBlock*>( raw ); }

    void coalesce_with_next( void* next_blk, void* next_next_blk, index_type own_idx ) noexcept
    {
        auto* nxt = reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( next_blk );

        Base::set_next_offset( nxt->next_offset() );
        if ( next_next_blk != nullptr )
        {
            auto* nxt_nxt = reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( next_next_blk );
            nxt_nxt->set_prev_offset( own_idx );
        }

        std::memset( next_blk, 0, sizeof( Block<AddressTraitsT> ) );
    }

    CoalescingBlock<AddressTraitsT>* coalesce_with_prev( void* prev_blk, void* next_blk, index_type prev_idx ) noexcept
    {
        auto* prv = reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( prev_blk );
        prv->set_next_offset( Base::next_offset() );

        if ( next_blk != nullptr )
        {
            auto* nxt = reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( next_blk );
            nxt->set_prev_offset( prev_idx );
        }

        std::memset( this, 0, sizeof( Block<AddressTraitsT> ) );

        return reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( prev_blk );
    }

    FreeBlock<AddressTraitsT>* finalize_coalesce() noexcept
    {
        Base::set_avl_height( 1 ); 
        return reinterpret_cast<FreeBlock<AddressTraitsT>*>( this );
    }
};

template <typename AddressTraitsT>
int detect_block_state( const void* raw_blk, typename AddressTraitsT::index_type own_idx ) noexcept
{
    const auto* base = reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw_blk );
    if ( base->is_free() )
        return 0; 
    if ( base->is_allocated( own_idx ) )
        return 1; 
    return -1;    
}

template <typename AddressTraitsT>
void recover_block_state( void* raw_blk, typename AddressTraitsT::index_type own_idx ) noexcept
{
    BlockStateBase<AddressTraitsT>::recover_state( raw_blk, own_idx );
}

} 

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>

namespace pmm
{

inline constexpr std::size_t kGranuleSize = 16;
static_assert( ( kGranuleSize & ( kGranuleSize - 1 ) ) == 0, "kGranuleSize must be a power of 2 (Issue #83)" );
static_assert( kGranuleSize == pmm::DefaultAddressTraits::granule_size,
               "kGranuleSize must match DefaultAddressTraits::granule_size (Issue #87)" );

inline constexpr std::uint64_t kMagic =
    0x504D4D5F56303938ULL; 

struct MemoryStats
{
    std::size_t total_blocks;
    std::size_t free_blocks;
    std::size_t allocated_blocks;
    std::size_t largest_free;
    std::size_t smallest_free;
    std::size_t total_fragmentation;
};

struct ManagerInfo
{
    std::uint64_t  magic;
    std::size_t    total_size;
    std::size_t    used_size;
    std::size_t    block_count;
    std::size_t    free_count;
    std::size_t    alloc_count;
    std::ptrdiff_t first_block_offset;
    std::ptrdiff_t first_free_offset; 
    std::size_t    manager_header_size;
};

struct BlockView
{
    std::size_t    index;
    std::ptrdiff_t offset;      
    std::size_t    total_size;  
    std::size_t    header_size; 
    std::size_t    user_size;   
    std::size_t    alignment;   
    bool           used;
};

struct FreeBlockView
{
    std::ptrdiff_t offset;
    std::size_t    total_size;
    std::size_t    free_size;
    std::ptrdiff_t left_offset;
    std::ptrdiff_t right_offset;
    std::ptrdiff_t parent_offset;
    int            avl_height;
    int            avl_depth;
};

namespace detail
{

static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32,
               "Block<DefaultAddressTraits> must be 32 bytes (Issue #87, #112)" );
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) % kGranuleSize == 0,
               "Block<DefaultAddressTraits> must be granule-aligned (Issue #59, #73 FR-03)" );

static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) ==
                   sizeof( pmm::TreeNode<pmm::DefaultAddressTraits> ) + 2 * sizeof( std::uint32_t ),
               "Block<DefaultAddressTraits> must have TreeNode + 2 index_type list fields (Issue #87, #138)" );

static_assert( sizeof( pmm::TreeNode<pmm::DefaultAddressTraits> ) == 5 * sizeof( std::uint32_t ) + 4,
               "TreeNode<DefaultAddressTraits> must be 24 bytes (Issue #87, #126)" );

inline constexpr std::uint32_t kNoBlock = 0xFFFFFFFFU; 
static_assert( kNoBlock == pmm::DefaultAddressTraits::no_block,
               "kNoBlock must match DefaultAddressTraits::no_block (Issue #87)" );

template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kNoBlock_v = AddressTraitsT::no_block;

template <typename AddressTraitsT = DefaultAddressTraits> struct ManagerHeader
{
    using index_type = typename AddressTraitsT::index_type;

    std::uint64_t magic;              
    std::uint64_t total_size;         
    index_type    used_size;          
    index_type    block_count;        
    index_type    free_count;         
    index_type    alloc_count;        
    index_type    first_block_offset; 
    index_type    last_block_offset;  
    index_type    free_tree_root;     
    bool          owns_memory;        
    std::uint8_t  _pad;               
    std::uint16_t granule_size;       
    std::uint64_t prev_total_size;    
    std::uint8_t  _reserved[8];       
};

static_assert( sizeof( ManagerHeader<DefaultAddressTraits> ) == 64,
               "ManagerHeader<DefaultAddressTraits> must be exactly 64 bytes (Issue #59, #73 FR-03, #175)" );
static_assert( sizeof( ManagerHeader<DefaultAddressTraits> ) % kGranuleSize == 0,
               "ManagerHeader<DefaultAddressTraits> must be granule-aligned (Issue #59, #73 FR-03)" );

inline constexpr std::uint32_t kManagerHeaderGranules = sizeof( ManagerHeader<DefaultAddressTraits> ) / kGranuleSize;

inline constexpr std::size_t kMinBlockSize = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + kGranuleSize;

inline constexpr std::size_t kMinMemorySize = sizeof( pmm::Block<pmm::DefaultAddressTraits> ) +
                                              sizeof( ManagerHeader<pmm::DefaultAddressTraits> ) +
                                              sizeof( pmm::Block<pmm::DefaultAddressTraits> ) + kMinBlockSize;

template <typename AddressTraitsT> inline typename AddressTraitsT::index_type bytes_to_granules_t( std::size_t bytes )
{
    using IndexT                         = typename AddressTraitsT::index_type;
    static constexpr std::size_t kGranSz = AddressTraitsT::granule_size;
    if ( bytes > std::numeric_limits<std::size_t>::max() - ( kGranSz - 1 ) )
        return static_cast<IndexT>( 0 );
    std::size_t granules = ( bytes + kGranSz - 1 ) / kGranSz;
    if ( granules > static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) )
        return static_cast<IndexT>( 0 );
    return static_cast<IndexT>( granules );
}

template <typename AddressTraitsT> inline typename AddressTraitsT::index_type bytes_to_idx_t( std::size_t bytes )
{
    static constexpr std::size_t kGranSz = AddressTraitsT::granule_size;
    using IndexT                         = typename AddressTraitsT::index_type;
    if ( bytes == 0 )
        return static_cast<IndexT>( 0 );
    if ( bytes > std::numeric_limits<std::size_t>::max() - ( kGranSz - 1 ) )
        return AddressTraitsT::no_block; 
    std::size_t granules = ( bytes + kGranSz - 1 ) / kGranSz;
    if ( granules > static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) )
        return AddressTraitsT::no_block; 
    return static_cast<IndexT>( granules );
}

template <typename AddressTraitsT> inline std::size_t idx_to_byte_off_t( typename AddressTraitsT::index_type idx )
{
    return static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size;
}

template <typename AddressTraitsT> inline typename AddressTraitsT::index_type byte_off_to_idx_t( std::size_t byte_off )
{
    using IndexT = typename AddressTraitsT::index_type;
    assert( byte_off % AddressTraitsT::granule_size == 0 );
    assert( byte_off / AddressTraitsT::granule_size <= static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) );
    return static_cast<IndexT>( byte_off / AddressTraitsT::granule_size );
}

inline std::uint32_t bytes_to_granules( std::size_t bytes )
{
    return bytes_to_granules_t<pmm::DefaultAddressTraits>( bytes );
}

inline std::size_t granules_to_bytes( std::uint32_t granules )
{
    return pmm::DefaultAddressTraits::granules_to_bytes( granules );
}

inline std::size_t idx_to_byte_off( std::uint32_t idx )
{
    return pmm::DefaultAddressTraits::idx_to_byte_off( idx );
}

inline std::uint32_t byte_off_to_idx( std::size_t byte_off )
{
    return byte_off_to_idx_t<pmm::DefaultAddressTraits>( byte_off );
}

inline bool is_valid_alignment( std::size_t align )
{
    return align == kGranuleSize;
}

template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline pmm::Block<AddressTraitsT>* block_at( std::uint8_t* base, typename AddressTraitsT::index_type idx )
{
    assert( idx != kNoBlock_v<AddressTraitsT> );
    return reinterpret_cast<pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                     AddressTraitsT::granule_size );
}

template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline const pmm::Block<AddressTraitsT>* block_at( const std::uint8_t* base, typename AddressTraitsT::index_type idx )
{
    assert( idx != kNoBlock_v<AddressTraitsT> );
    return reinterpret_cast<const pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                           AddressTraitsT::granule_size );
}

inline std::uint32_t block_idx( const std::uint8_t* base, const pmm::Block<pmm::DefaultAddressTraits>* block )
{
    std::size_t byte_off = reinterpret_cast<const std::uint8_t*>( block ) - base;
    assert( byte_off % kGranuleSize == 0 );
    return static_cast<std::uint32_t>( byte_off / kGranuleSize );
}

template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type block_idx_t( const std::uint8_t*               base,
                                                        const pmm::Block<AddressTraitsT>* block )
{
    std::size_t byte_off = reinterpret_cast<const std::uint8_t*>( block ) - base;
    assert( byte_off % AddressTraitsT::granule_size == 0 );
    return static_cast<typename AddressTraitsT::index_type>( byte_off / AddressTraitsT::granule_size );
}

template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kBlockHeaderGranules_t =
    static_cast<typename AddressTraitsT::index_type>(
        ( sizeof( pmm::Block<AddressTraitsT> ) + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size );

template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kManagerHeaderGranules_t =
    static_cast<typename AddressTraitsT::index_type>(
        ( sizeof( ManagerHeader<AddressTraitsT> ) + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size );

template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type to_u32_idx( typename AddressTraitsT::index_type v )
{
    return v;
}

template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type from_u32_idx( typename AddressTraitsT::index_type v )
{
    return v;
}

template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type block_total_granules( const std::uint8_t*                  base,
                                                                 const ManagerHeader<AddressTraitsT>* hdr,
                                                                 const pmm::Block<AddressTraitsT>*    blk )
{
    using BlockState                     = pmm::BlockStateBase<AddressTraitsT>;
    static constexpr std::size_t kGranSz = AddressTraitsT::granule_size;
    using IndexT                         = typename AddressTraitsT::index_type;
    static constexpr IndexT kNoBlk       = AddressTraitsT::no_block;

    std::size_t byte_off   = reinterpret_cast<const std::uint8_t*>( blk ) - base;
    IndexT      this_idx   = static_cast<IndexT>( byte_off / kGranSz );
    IndexT      next_off   = BlockState::get_next_offset( blk );
    IndexT      total_gran = static_cast<IndexT>( hdr->total_size / kGranSz );
    if ( next_off != kNoBlk )
        return static_cast<IndexT>( next_off - this_idx );
    return static_cast<IndexT>( total_gran - this_idx );
}

inline bool is_valid_block( const std::uint8_t* base, const ManagerHeader<pmm::DefaultAddressTraits>* hdr,
                            std::uint32_t idx )
{
    using BlockState = pmm::BlockStateBase<pmm::DefaultAddressTraits>;
    if ( idx == kNoBlock )
        return false;
    if ( idx_to_byte_off( idx ) + sizeof( pmm::Block<pmm::DefaultAddressTraits> ) > hdr->total_size )
        return false;

    const void*   blk        = base + idx_to_byte_off( idx );
    auto          next_off   = BlockState::get_next_offset( blk );
    std::uint32_t total_gran = ( next_off != kNoBlock )
                                   ? ( next_off - idx )
                                   : ( byte_off_to_idx( static_cast<std::size_t>( hdr->total_size ) ) - idx );
    if ( BlockState::get_weight( blk ) >= total_gran )
        return false;
    auto prev_off = BlockState::get_prev_offset( blk );
    if ( prev_off != kNoBlock && prev_off >= idx )
        return false;
    if ( next_off != kNoBlock && next_off <= idx )
        return false;
    if ( BlockState::get_avl_height( blk ) >= 32 )
        return false;
    auto       left_off   = BlockState::get_left_offset( blk );
    auto       right_off  = BlockState::get_right_offset( blk );
    auto       parent_off = BlockState::get_parent_offset( blk );
    const bool l          = ( left_off != kNoBlock );
    const bool r          = ( right_off != kNoBlock );
    const bool p          = ( parent_off != kNoBlock );
    if ( ( l || r || p ) && ( ( l && r && left_off == right_off ) || ( l && p && left_off == parent_off ) ||
                              ( r && p && right_off == parent_off ) ) )
        return false;
    return true;
}

template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline void* user_ptr( pmm::Block<AddressTraitsT>* block )
{
    return reinterpret_cast<std::uint8_t*>( block ) + sizeof( pmm::Block<AddressTraitsT> );
}

template <typename AddressTraitsT>
inline pmm::Block<AddressTraitsT>* header_from_ptr_t( std::uint8_t* base, void* ptr, std::size_t total_size )
{
    using BlockState                        = pmm::BlockStateBase<AddressTraitsT>;
    static constexpr std::size_t kGranSz    = AddressTraitsT::granule_size;
    static constexpr std::size_t kBlockSize = sizeof( pmm::Block<AddressTraitsT> );

    if ( ptr == nullptr )
        return nullptr;
    std::uint8_t* raw_ptr = reinterpret_cast<std::uint8_t*>( ptr );
    
    std::uint8_t* min_addr = base + kBlockSize + sizeof( ManagerHeader<AddressTraitsT> ) + kBlockSize;
    if ( raw_ptr < min_addr )
        return nullptr;
    if ( raw_ptr > base + total_size )
        return nullptr;
    std::uint8_t* cand_addr = raw_ptr - kBlockSize;
    if ( ( reinterpret_cast<std::size_t>( cand_addr ) - reinterpret_cast<std::size_t>( base ) ) % kGranSz != 0 )
        return nullptr;
    
    if ( BlockState::get_weight( cand_addr ) == 0 )
        return nullptr;
    
    if ( cand_addr < base || cand_addr + kBlockSize > base + total_size )
        return nullptr;
    return reinterpret_cast<pmm::Block<AddressTraitsT>*>( cand_addr );
}

inline std::uint32_t required_block_granules( std::size_t user_bytes )
{
    std::uint32_t data_granules = bytes_to_granules( user_bytes );
    if ( data_granules == 0 )
        data_granules = 1;
    return kBlockHeaderGranules_t<pmm::DefaultAddressTraits> + data_granules;
}

template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type required_block_granules_t( std::size_t user_bytes )
{
    using index_type         = typename AddressTraitsT::index_type;
    index_type data_granules = bytes_to_granules_t<AddressTraitsT>( user_bytes );
    if ( data_granules == 0 )
        data_granules = 1;
    return kBlockHeaderGranules_t<AddressTraitsT> + data_granules;
}

} 

} 

#include <cassert>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace pmm
{

template <typename Policy, typename AddressTraitsT>
concept FreeBlockTreePolicyForTraitsConcept = requires( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr,
                                                        typename AddressTraitsT::index_type idx ) {
    { Policy::insert( base, hdr, idx ) };
    { Policy::remove( base, hdr, idx ) };
    { Policy::find_best_fit( base, hdr, idx ) } -> std::convertible_to<typename AddressTraitsT::index_type>;
};

template <typename Policy>
concept FreeBlockTreePolicyConcept = FreeBlockTreePolicyForTraitsConcept<Policy, DefaultAddressTraits>;

template <typename Policy> inline constexpr bool is_free_block_tree_policy_v = FreeBlockTreePolicyConcept<Policy>;

template <typename AddressTraitsT = DefaultAddressTraits> struct AvlFreeTree
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;
    using BlockT         = Block<AddressTraitsT>;
    using BlockState     = BlockStateBase<AddressTraitsT>;

    AvlFreeTree()                                = delete;
    AvlFreeTree( const AvlFreeTree& )            = delete;
    AvlFreeTree& operator=( const AvlFreeTree& ) = delete;

    static void insert( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type blk_idx )
    {
        void* blk = detail::block_at<AddressTraitsT>( base, blk_idx );
        BlockState::set_left_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_right_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_parent_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_avl_height_of( blk, 1 );
        if ( hdr->free_tree_root == AddressTraitsT::no_block )
        {
            hdr->free_tree_root = blk_idx;
            return;
        }
        
        index_type total_gran = detail::byte_off_to_idx_t<AddressTraitsT>( hdr->total_size );
        index_type blk_next   = BlockState::get_next_offset( blk );
        index_type blk_gran =
            ( blk_next != AddressTraitsT::no_block ) ? ( blk_next - blk_idx ) : ( total_gran - blk_idx );
        index_type cur = hdr->free_tree_root, parent = AddressTraitsT::no_block;
        bool       go_left = false;
        while ( cur != AddressTraitsT::no_block )
        {
            parent              = cur;
            const void* n       = detail::block_at<AddressTraitsT>( base, cur );
            index_type  n_next  = BlockState::get_next_offset( n );
            index_type  n_gran  = ( n_next != AddressTraitsT::no_block ) ? ( n_next - cur ) : ( total_gran - cur );
            bool        smaller = ( blk_gran < n_gran ) || ( blk_gran == n_gran && blk_idx < cur );
            go_left             = smaller;
            cur                 = smaller ? BlockState::get_left_offset( n ) : BlockState::get_right_offset( n );
        }
        BlockState::set_parent_offset_of( blk, parent );
        if ( go_left )
            BlockState::set_left_offset_of( detail::block_at<AddressTraitsT>( base, parent ), blk_idx );
        else
            BlockState::set_right_offset_of( detail::block_at<AddressTraitsT>( base, parent ), blk_idx );
        rebalance_up( base, hdr, parent );
    }

    static void remove( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type blk_idx )
    {
        void*      blk    = detail::block_at<AddressTraitsT>( base, blk_idx );
        index_type parent = BlockState::get_parent_offset( blk );
        index_type left   = BlockState::get_left_offset( blk );
        index_type right  = BlockState::get_right_offset( blk );
        index_type rebal  = AddressTraitsT::no_block;

        if ( left == AddressTraitsT::no_block && right == AddressTraitsT::no_block )
        {
            set_child( base, hdr, parent, blk_idx, AddressTraitsT::no_block );
            rebal = parent;
        }
        else if ( left == AddressTraitsT::no_block || right == AddressTraitsT::no_block )
        {
            index_type child = ( left != AddressTraitsT::no_block ) ? left : right;
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, child ), parent );
            set_child( base, hdr, parent, blk_idx, child );
            rebal = parent;
        }
        else
        {
            index_type succ_idx    = min_node( base, right );
            void*      succ        = detail::block_at<AddressTraitsT>( base, succ_idx );
            index_type succ_parent = BlockState::get_parent_offset( succ );
            index_type succ_right  = BlockState::get_right_offset( succ );

            if ( succ_parent != blk_idx )
            {
                set_child( base, hdr, succ_parent, succ_idx, succ_right );
                if ( succ_right != AddressTraitsT::no_block )
                    BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, succ_right ),
                                                      succ_parent );
                BlockState::set_right_offset_of( succ, right );
                BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, right ), succ_idx );
                rebal = succ_parent;
            }
            else
            {
                rebal = succ_idx;
            }
            BlockState::set_left_offset_of( succ, left );
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, left ), succ_idx );
            BlockState::set_parent_offset_of( succ, parent );
            set_child( base, hdr, parent, blk_idx, succ_idx );
            update_height( base, succ_idx );
        }
        BlockState::set_left_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_right_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_parent_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_avl_height_of( blk, 0 );
        rebalance_up( base, hdr, rebal );
    }

    static index_type find_best_fit( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr,
                                     index_type needed_granules )
    {
        
        index_type total_gran = detail::byte_off_to_idx_t<AddressTraitsT>( hdr->total_size );
        index_type cur = hdr->free_tree_root, result = AddressTraitsT::no_block;
        while ( cur != AddressTraitsT::no_block )
        {
            const void* node      = detail::block_at<AddressTraitsT>( base, cur );
            index_type  node_next = BlockState::get_next_offset( node );
            
            index_type cur_gran =
                ( node_next != AddressTraitsT::no_block ) ? ( node_next - cur ) : ( total_gran - cur );
            if ( cur_gran >= needed_granules )
            {
                result = cur;
                cur    = BlockState::get_left_offset( node );
            }
            else
            {
                cur = BlockState::get_right_offset( node );
            }
        }
        return result;
    }

  private:
    static std::int32_t height( std::uint8_t* base, index_type idx )
    {
        return ( idx == AddressTraitsT::no_block ) ? 0
                                                   : static_cast<std::int32_t>( BlockState::get_avl_height(
                                                         detail::block_at<AddressTraitsT>( base, idx ) ) );
    }

    static void update_height( std::uint8_t* base, index_type node_idx )
    {
        void*        node = detail::block_at<AddressTraitsT>( base, node_idx );
        std::int32_t h    = 1 + ( std::max )( height( base, BlockState::get_left_offset( node ) ),
                                           height( base, BlockState::get_right_offset( node ) ) );
        assert( h <= std::numeric_limits<std::int16_t>::max() ); 
        BlockState::set_avl_height_of( node, static_cast<std::int16_t>( h ) );
    }

    static std::int32_t balance_factor( std::uint8_t* base, index_type node_idx )
    {
        const void* node = detail::block_at<AddressTraitsT>( base, node_idx );
        return height( base, BlockState::get_left_offset( node ) ) -
               height( base, BlockState::get_right_offset( node ) );
    }

    static void set_child( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type parent,
                           index_type old_child, index_type new_child )
    {
        if ( parent == AddressTraitsT::no_block )
        {
            hdr->free_tree_root = new_child;
            return;
        }
        void* p = detail::block_at<AddressTraitsT>( base, parent );
        if ( BlockState::get_left_offset( p ) == old_child )
            BlockState::set_left_offset_of( p, new_child );
        else
            BlockState::set_right_offset_of( p, new_child );
    }

    static index_type rotate_right( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type y_idx )
    {
        void*      y        = detail::block_at<AddressTraitsT>( base, y_idx );
        index_type x_idx    = BlockState::get_left_offset( y );
        void*      x        = detail::block_at<AddressTraitsT>( base, x_idx );
        index_type t2       = BlockState::get_right_offset( x );
        index_type y_parent = BlockState::get_parent_offset( y );

        BlockState::set_right_offset_of( x, y_idx );
        BlockState::set_left_offset_of( y, t2 );
        
        BlockState::set_parent_offset_of( x, y_parent );
        BlockState::set_parent_offset_of( y, x_idx );
        if ( t2 != AddressTraitsT::no_block )
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, t2 ), y_idx );
        set_child( base, hdr, y_parent, y_idx, x_idx );
        update_height( base, y_idx );
        update_height( base, x_idx );
        return x_idx;
    }

    static index_type rotate_left( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type x_idx )
    {
        void*      x        = detail::block_at<AddressTraitsT>( base, x_idx );
        index_type y_idx    = BlockState::get_right_offset( x );
        void*      y        = detail::block_at<AddressTraitsT>( base, y_idx );
        index_type t2       = BlockState::get_left_offset( y );
        index_type x_parent = BlockState::get_parent_offset( x );

        BlockState::set_left_offset_of( y, x_idx );
        BlockState::set_right_offset_of( x, t2 );
        
        BlockState::set_parent_offset_of( y, x_parent );
        BlockState::set_parent_offset_of( x, y_idx );
        if ( t2 != AddressTraitsT::no_block )
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, t2 ), x_idx );
        set_child( base, hdr, x_parent, x_idx, y_idx );
        update_height( base, x_idx );
        update_height( base, y_idx );
        return y_idx;
    }

    static void rebalance_up( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type node_idx )
    {
        index_type cur = node_idx;
        while ( cur != AddressTraitsT::no_block )
        {
            update_height( base, cur );
            std::int32_t bf = balance_factor( base, cur );
            if ( bf > 1 )
            {
                void*      node     = detail::block_at<AddressTraitsT>( base, cur );
                index_type left_idx = BlockState::get_left_offset( node );
                if ( balance_factor( base, left_idx ) < 0 )
                    rotate_left( base, hdr, left_idx );
                cur = rotate_right( base, hdr, cur );
            }
            else if ( bf < -1 )
            {
                void*      node      = detail::block_at<AddressTraitsT>( base, cur );
                index_type right_idx = BlockState::get_right_offset( node );
                if ( balance_factor( base, right_idx ) > 0 )
                    rotate_right( base, hdr, right_idx );
                cur = rotate_left( base, hdr, cur );
            }
            cur = BlockState::get_parent_offset( detail::block_at<AddressTraitsT>( base, cur ) );
        }
    }

    static index_type min_node( std::uint8_t* base, index_type node_idx )
    {
        while ( node_idx != AddressTraitsT::no_block )
        {
            index_type left = BlockState::get_left_offset( detail::block_at<AddressTraitsT>( base, node_idx ) );
            if ( left == AddressTraitsT::no_block )
                break;
            node_idx = left;
        }
        return node_idx;
    }
};

static_assert( is_free_block_tree_policy_v<AvlFreeTree<DefaultAddressTraits>>,
               "AvlFreeTree<DefaultAddressTraits> must satisfy FreeBlockTreePolicy" );

using PersistentAvlTree = AvlFreeTree<DefaultAddressTraits>;

} 

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

template <typename Backend>
concept StorageBackendConcept = requires( Backend& b, const Backend& cb, std::size_t n ) {
    { b.base_ptr() } -> std::convertible_to<std::uint8_t*>;
    { cb.total_size() } -> std::convertible_to<std::size_t>;
    { b.expand( n ) } -> std::convertible_to<bool>;
    { cb.owns_memory() } -> std::convertible_to<bool>;
};

template <typename Backend> inline constexpr bool is_storage_backend_v = StorageBackendConcept<Backend>;

} 

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace pmm
{

template <typename AddressTraitsT = DefaultAddressTraits> class HeapStorage
{
  public:
    using address_traits = AddressTraitsT;

    HeapStorage() noexcept = default;

    explicit HeapStorage( std::size_t initial_size ) noexcept
    {
        if ( initial_size == 0 )
            return;
        
        std::size_t aligned = ( ( initial_size + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size ) *
                              AddressTraitsT::granule_size;
        _buffer = static_cast<std::uint8_t*>( std::malloc( aligned ) );
        if ( _buffer != nullptr )
        {
            _size        = aligned;
            _owns_memory = true;
        }
    }

    HeapStorage( const HeapStorage& )            = delete;
    HeapStorage& operator=( const HeapStorage& ) = delete;

    HeapStorage( HeapStorage&& other ) noexcept
        : _buffer( other._buffer ), _size( other._size ), _owns_memory( other._owns_memory )
    {
        other._buffer      = nullptr;
        other._size        = 0;
        other._owns_memory = false;
    }

    HeapStorage& operator=( HeapStorage&& other ) noexcept
    {
        if ( this != &other )
        {
            if ( _owns_memory && _buffer != nullptr )
                std::free( _buffer );
            _buffer            = other._buffer;
            _size              = other._size;
            _owns_memory       = other._owns_memory;
            other._buffer      = nullptr;
            other._size        = 0;
            other._owns_memory = false;
        }
        return *this;
    }

    ~HeapStorage()
    {
        if ( _owns_memory && _buffer != nullptr )
            std::free( _buffer );
    }

    void attach( void* memory, std::size_t size ) noexcept
    {
        if ( _owns_memory && _buffer != nullptr )
            std::free( _buffer );
        _buffer      = static_cast<std::uint8_t*>( memory );
        _size        = size;
        _owns_memory = false;
    }

    std::uint8_t*       base_ptr() noexcept { return _buffer; }
    const std::uint8_t* base_ptr() const noexcept { return _buffer; }

    std::size_t total_size() const noexcept { return _size; }

    bool expand( std::size_t additional_bytes ) noexcept
    {
        if ( additional_bytes == 0 )
            return _size > 0;
        
        std::size_t growth   = ( _size > 0 ) ? ( _size / 4 + additional_bytes ) : additional_bytes;
        std::size_t new_size = _size + growth;
        
        new_size = ( ( new_size + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size ) *
                   AddressTraitsT::granule_size;
        if ( new_size <= _size )
            return false;

        void* new_buf = std::malloc( new_size );
        if ( new_buf == nullptr )
            return false;
        if ( _buffer != nullptr )
            std::memcpy( new_buf, _buffer, _size );
        if ( _owns_memory && _buffer != nullptr )
            std::free( _buffer );
        _buffer      = static_cast<std::uint8_t*>( new_buf );
        _size        = new_size;
        _owns_memory = true;
        return true;
    }

    bool owns_memory() const noexcept { return _owns_memory; }

  private:
    std::uint8_t* _buffer      = nullptr;
    std::size_t   _size        = 0;
    bool          _owns_memory = false;
};

static_assert( is_storage_backend_v<HeapStorage<>>, "HeapStorage must satisfy StorageBackendConcept" );

} 

#include <cstddef>
#include <cstdint>

namespace pmm
{

template <std::size_t Size, typename AddressTraitsT = DefaultAddressTraits> class StaticStorage
{
    static_assert( Size > 0, "StaticStorage: Size must be > 0" );
    static_assert( Size % AddressTraitsT::granule_size == 0, "StaticStorage: Size must be a multiple of granule_size" );

  public:
    using address_traits = AddressTraitsT;

    StaticStorage() noexcept                         = default;
    StaticStorage( const StaticStorage& )            = delete;
    StaticStorage& operator=( const StaticStorage& ) = delete;
    StaticStorage( StaticStorage&& )                 = delete;
    StaticStorage& operator=( StaticStorage&& )      = delete;

    std::uint8_t*       base_ptr() noexcept { return _buffer; }
    const std::uint8_t* base_ptr() const noexcept { return _buffer; }

    constexpr std::size_t total_size() const noexcept { return Size; }

    bool expand( std::size_t  ) noexcept { return false; }

    constexpr bool owns_memory() const noexcept { return false; }

  private:
    alignas( AddressTraitsT::granule_size ) std::uint8_t _buffer[Size]{};
};

static_assert( is_storage_backend_v<StaticStorage<64>>, "StaticStorage must satisfy StorageBackendConcept" );

} 

#include <concepts>
#include <cstddef>

namespace pmm
{

inline constexpr std::size_t kMinGranuleSize = 4;

template <typename AT>
concept ValidPmmAddressTraits =
    ( AT::granule_size >= kMinGranuleSize ) && ( ( AT::granule_size & ( AT::granule_size - 1 ) ) == 0 );

static_assert( ValidPmmAddressTraits<DefaultAddressTraits>, "DefaultAddressTraits must satisfy ValidPmmAddressTraits" );
static_assert( ValidPmmAddressTraits<SmallAddressTraits>, "SmallAddressTraits must satisfy ValidPmmAddressTraits" );
static_assert( ValidPmmAddressTraits<LargeAddressTraits>, "LargeAddressTraits must satisfy ValidPmmAddressTraits" );

template <typename AddressTraitsT = DefaultAddressTraits, typename LockPolicyT = config::NoLock,
          std::size_t GrowNum = config::kDefaultGrowNumerator, std::size_t GrowDen = config::kDefaultGrowDenominator,
          std::size_t MaxMemoryGB = 64>
struct BasicConfig
{
    static_assert( ValidPmmAddressTraits<AddressTraitsT>,
                   "BasicConfig: AddressTraitsT must satisfy ValidPmmAddressTraits" );

    using address_traits                          = AddressTraitsT;
    using storage_backend                         = HeapStorage<AddressTraitsT>;
    using free_block_tree                         = AvlFreeTree<AddressTraitsT>;
    using lock_policy                             = LockPolicyT;
    static constexpr std::size_t granule_size     = AddressTraitsT::granule_size;
    static constexpr std::size_t max_memory_gb    = MaxMemoryGB;
    static constexpr std::size_t grow_numerator   = GrowNum;
    static constexpr std::size_t grow_denominator = GrowDen;
};

template <std::size_t BufferSize = 1024> struct SmallEmbeddedStaticConfig
{
    
    using address_traits                          = SmallAddressTraits;
    using storage_backend                         = StaticStorage<BufferSize, SmallAddressTraits>;
    using free_block_tree                         = AvlFreeTree<SmallAddressTraits>;
    using lock_policy                             = config::NoLock;
    static constexpr std::size_t granule_size     = SmallAddressTraits::granule_size;
    static constexpr std::size_t max_memory_gb    = 0; 
    static constexpr std::size_t grow_numerator   = 3;
    static constexpr std::size_t grow_denominator = 2;
};

template <std::size_t BufferSize = 4096> struct EmbeddedStaticConfig
{
    
    using address_traits                          = DefaultAddressTraits;
    using storage_backend                         = StaticStorage<BufferSize, DefaultAddressTraits>;
    using free_block_tree                         = AvlFreeTree<DefaultAddressTraits>;
    using lock_policy                             = config::NoLock;
    static constexpr std::size_t granule_size     = DefaultAddressTraits::granule_size;
    static constexpr std::size_t max_memory_gb    = 0; 
    static constexpr std::size_t grow_numerator   = 3;
    static constexpr std::size_t grow_denominator = 2;
};

using CacheManagerConfig = BasicConfig<DefaultAddressTraits, config::NoLock, config::kDefaultGrowNumerator,
                                       config::kDefaultGrowDenominator, 64>;

using PersistentDataConfig = BasicConfig<DefaultAddressTraits, config::SharedMutexLock, config::kDefaultGrowNumerator,
                                         config::kDefaultGrowDenominator, 64>;

using EmbeddedManagerConfig = BasicConfig<DefaultAddressTraits, config::NoLock, 3, 2, 64>;

using IndustrialDBConfig = BasicConfig<DefaultAddressTraits, config::SharedMutexLock, 2, 1, 64>;

using LargeDBConfig = BasicConfig<LargeAddressTraits, config::SharedMutexLock, 2, 1, 0>;

} 

#if defined( _MSVC_LANG )
#if _MSVC_LANG < 202002L
#error "pmm.h requires C++20 or later. Please compile with /std:c++20 on MSVC."
#endif
#elif __cplusplus < 202002L
#error "pmm.h requires C++20 or later. Please compile with -std=c++20."
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pmm
{

template <typename FreeBlockTreeT = AvlFreeTree<DefaultAddressTraits>, typename AddressTraitsT = DefaultAddressTraits>
class AllocatorPolicy
{
    
    static_assert( FreeBlockTreePolicyForTraitsConcept<FreeBlockTreeT, AddressTraitsT>,
                   "AllocatorPolicy: FreeBlockTreeT must satisfy FreeBlockTreePolicy for AddressTraitsT" );

  public:
    using address_traits  = AddressTraitsT;
    using free_block_tree = FreeBlockTreeT;
    using index_type      = typename AddressTraitsT::index_type;
    using BlockT          = Block<AddressTraitsT>;
    using BlockState      = BlockStateBase<AddressTraitsT>;

    AllocatorPolicy()                                    = delete;
    AllocatorPolicy( const AllocatorPolicy& )            = delete;
    AllocatorPolicy& operator=( const AllocatorPolicy& ) = delete;

    static void* allocate_from_block( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr,
                                      index_type blk_idx, std::size_t user_size )
    {
        
        FreeBlockTreeT::remove( base, hdr, blk_idx );
        FreeBlock<AddressTraitsT>* fb =
            FreeBlock<AddressTraitsT>::cast_from_raw( detail::block_at<AddressTraitsT>( base, blk_idx ) );
        FreeBlockRemovedAVL<AddressTraitsT>* removed = fb->remove_from_avl();

        static constexpr index_type kBlkHdrGran =
            detail::kBlockHeaderGranules_t<AddressTraitsT>; 

        index_type blk_total_gran =
            detail::block_total_granules( base, hdr, detail::block_at<AddressTraitsT>( base, blk_idx ) );
        index_type data_gran    = detail::bytes_to_granules_t<AddressTraitsT>( user_size );
        index_type needed_gran  = kBlkHdrGran + data_gran;
        index_type min_rem_gran = kBlkHdrGran + 1;
        bool       can_split    = ( blk_total_gran >= needed_gran + min_rem_gran );

        if ( can_split )
        {
            
            SplittingBlock<AddressTraitsT>* splitting = removed->begin_splitting();

            index_type new_idx     = blk_idx + needed_gran;
            void*      new_blk_ptr = detail::block_at<AddressTraitsT>( base, new_idx );

            index_type curr_next = splitting->next_offset();
            BlockT*    old_next  = ( curr_next != AddressTraitsT::no_block )
                                       ? detail::block_at<AddressTraitsT>( base, curr_next )
                                       : nullptr;

            splitting->initialize_new_block( new_blk_ptr, new_idx, blk_idx );

            splitting->link_new_block( old_next, new_idx );
            if ( old_next == nullptr )
                hdr->last_block_offset = new_idx;

            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += kBlkHdrGran;
            FreeBlockTreeT::insert( base, hdr, new_idx );

            AllocatedBlock<AddressTraitsT>* alloc = splitting->finalize_split( data_gran, blk_idx );
            (void)alloc; 
        }
        else
        {
            
            AllocatedBlock<AddressTraitsT>* alloc = removed->mark_as_allocated( data_gran, blk_idx );
            (void)alloc; 
        }

        hdr->alloc_count++;
        hdr->free_count--;
        hdr->used_size += data_gran;

        return detail::user_ptr<AddressTraitsT>( detail::block_at<AddressTraitsT>( base, blk_idx ) );
    }

    static void coalesce( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type blk_idx )
    {
        
        FreeBlockNotInAVL<AddressTraitsT>* not_avl =
            FreeBlockNotInAVL<AddressTraitsT>::cast_from_raw( detail::block_at<AddressTraitsT>( base, blk_idx ) );
        CoalescingBlock<AddressTraitsT>* coalescing = not_avl->begin_coalescing();

        static constexpr index_type kBlkHdrGran = detail::kBlockHeaderGranules_t<AddressTraitsT>;

        index_type b_idx = blk_idx;

        index_type curr_next = coalescing->next_offset();
        if ( curr_next != AddressTraitsT::no_block )
        {
            const BlockStateBase<AddressTraitsT>* nxt_state = reinterpret_cast<const BlockStateBase<AddressTraitsT>*>(
                detail::block_at<AddressTraitsT>( base, curr_next ) );
            if ( nxt_state->weight() == 0 ) 
            {
                index_type nxt_idx     = curr_next;
                index_type nxt_next    = nxt_state->next_offset();
                BlockT*    nxt_nxt_blk = ( nxt_next != AddressTraitsT::no_block )
                                             ? detail::block_at<AddressTraitsT>( base, nxt_next )
                                             : nullptr;

                FreeBlockTreeT::remove( base, hdr, nxt_idx );

                coalescing->coalesce_with_next( detail::block_at<AddressTraitsT>( base, nxt_idx ), nxt_nxt_blk, b_idx );

                if ( nxt_nxt_blk == nullptr )
                    hdr->last_block_offset = b_idx;

                hdr->block_count--;
                hdr->free_count--;
                if ( hdr->used_size >= kBlkHdrGran )
                    hdr->used_size -= kBlkHdrGran;
            }
        }

        index_type curr_prev = coalescing->prev_offset();
        if ( curr_prev != AddressTraitsT::no_block )
        {
            const BlockStateBase<AddressTraitsT>* prv_state = reinterpret_cast<const BlockStateBase<AddressTraitsT>*>(
                detail::block_at<AddressTraitsT>( base, curr_prev ) );
            if ( prv_state->weight() == 0 ) 
            {
                index_type prv_idx  = curr_prev;
                index_type blk_next = coalescing->next_offset();
                BlockT*    next_blk = ( blk_next != AddressTraitsT::no_block )
                                          ? detail::block_at<AddressTraitsT>( base, blk_next )
                                          : nullptr;

                FreeBlockTreeT::remove( base, hdr, prv_idx );

                CoalescingBlock<AddressTraitsT>* result_coalescing = coalescing->coalesce_with_prev(
                    detail::block_at<AddressTraitsT>( base, prv_idx ), next_blk, prv_idx );

                if ( next_blk == nullptr )
                    hdr->last_block_offset = prv_idx;

                hdr->block_count--;
                hdr->free_count--;
                if ( hdr->used_size >= kBlkHdrGran )
                    hdr->used_size -= kBlkHdrGran;

                FreeBlock<AddressTraitsT>* fb = result_coalescing->finalize_coalesce();
                (void)fb;
                FreeBlockTreeT::insert( base, hdr, prv_idx );
                return;
            }
        }

        FreeBlock<AddressTraitsT>* fb = coalescing->finalize_coalesce();
        (void)fb;
        FreeBlockTreeT::insert( base, hdr, b_idx );
    }

    static void rebuild_free_tree( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr )
    {
        hdr->free_tree_root    = AddressTraitsT::no_block;
        hdr->last_block_offset = AddressTraitsT::no_block;
        index_type idx         = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );

            BlockState::reset_avl_fields_of( blk_ptr );

            BlockState::recover_state( blk_ptr, idx );

            if ( BlockState::get_weight( blk_ptr ) == 0 ) 
                FreeBlockTreeT::insert( base, hdr, idx );
            
            index_type next_idx = BlockState::get_next_offset( blk_ptr );
            if ( next_idx == AddressTraitsT::no_block )
                hdr->last_block_offset = idx;
            idx = next_idx;
        }
    }

    static void repair_linked_list( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr )
    {
        index_type idx  = hdr->first_block_offset;
        index_type prev = AddressTraitsT::no_block;
        while ( idx != AddressTraitsT::no_block )
        {
            
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            BlockState::repair_prev_offset( blk_ptr, prev ); 
            prev                   = idx;
            index_type next_offset = BlockState::get_next_offset( blk_ptr );
            idx                    = next_offset;
        }
    }

    static void recompute_counters( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr )
    {
        
        static constexpr index_type kBlkHdrGran = detail::kBlockHeaderGranules_t<AddressTraitsT>;

        index_type block_count = 0, free_count = 0, alloc_count = 0;
        index_type used_gran = 0;
        index_type idx       = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            block_count++;
            used_gran += kBlkHdrGran;
            index_type w = BlockState::get_weight( blk_ptr ); 
            if ( w > 0 )                                      
            {
                alloc_count++;
                used_gran += w;
            }
            else
            {
                free_count++;
            }
            idx = BlockState::get_next_offset( blk_ptr );
        }
        hdr->block_count = block_count;
        hdr->free_count  = free_count;
        hdr->alloc_count = alloc_count;
        hdr->used_size   = used_gran;
    }
};

using DefaultAllocatorPolicy = AllocatorPolicy<AvlFreeTree<DefaultAddressTraits>, DefaultAddressTraits>;

} 

#include <cstdint>

namespace pmm
{
namespace detail
{

template <typename PPtr> static constexpr auto pptr_no_block() noexcept
{
    return PPtr::manager_type::address_traits::no_block;
}

template <typename PPtr> static PPtr pptr_get_left( PPtr p ) noexcept
{
    auto idx = p.tree_node().get_left();
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : PPtr( idx );
}

template <typename PPtr> static PPtr pptr_get_right( PPtr p ) noexcept
{
    auto idx = p.tree_node().get_right();
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : PPtr( idx );
}

template <typename PPtr> static PPtr pptr_get_parent( PPtr p ) noexcept
{
    auto idx = p.tree_node().get_parent();
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : PPtr( idx );
}

template <typename PPtr> static void pptr_set_left( PPtr p, PPtr child ) noexcept
{
    auto idx = child.is_null() ? pptr_no_block<PPtr>() : child.offset();
    p.tree_node().set_left( idx );
}

template <typename PPtr> static void pptr_set_right( PPtr p, PPtr child ) noexcept
{
    auto idx = child.is_null() ? pptr_no_block<PPtr>() : child.offset();
    p.tree_node().set_right( idx );
}

template <typename PPtr> static void pptr_set_parent( PPtr p, PPtr parent ) noexcept
{
    auto idx = parent.is_null() ? pptr_no_block<PPtr>() : parent.offset();
    p.tree_node().set_parent( idx );
}

template <typename PPtr> static std::int16_t avl_height( PPtr p ) noexcept
{
    if ( p.is_null() )
        return 0;
    return p.tree_node().get_height();
}

template <typename PPtr> static void avl_update_height( PPtr p ) noexcept
{
    if ( p.is_null() )
        return;
    std::int16_t lh = avl_height( pptr_get_left( p ) );
    std::int16_t rh = avl_height( pptr_get_right( p ) );
    std::int16_t h  = static_cast<std::int16_t>( 1 + ( lh > rh ? lh : rh ) );
    p.tree_node().set_height( h );
}

template <typename PPtr> static std::int16_t avl_balance_factor( PPtr p ) noexcept
{
    if ( p.is_null() )
        return 0;
    std::int16_t lh = avl_height( pptr_get_left( p ) );
    std::int16_t rh = avl_height( pptr_get_right( p ) );
    return static_cast<std::int16_t>( lh - rh );
}

template <typename PPtr, typename IndexType>
static void avl_set_child( PPtr parent, PPtr old_child, PPtr new_child, IndexType& root_idx ) noexcept
{
    if ( parent.is_null() )
    {
        root_idx = new_child.offset();
        return;
    }
    PPtr left_of_parent = pptr_get_left( parent );
    if ( left_of_parent == old_child )
        pptr_set_left( parent, new_child );
    else
        pptr_set_right( parent, new_child );
}

template <typename PPtr, typename IndexType> static PPtr avl_rotate_right( PPtr y, IndexType& root_idx ) noexcept
{
    PPtr x     = pptr_get_left( y );
    PPtr b     = pptr_get_right( x );
    PPtr y_par = pptr_get_parent( y );

    pptr_set_right( x, y );
    pptr_set_parent( y, x );

    pptr_set_left( y, b );
    if ( !b.is_null() )
        pptr_set_parent( b, y );

    pptr_set_parent( x, y_par );

    avl_set_child( y_par, y, x, root_idx );

    avl_update_height( y );
    avl_update_height( x );
    return x;
}

template <typename PPtr, typename IndexType> static PPtr avl_rotate_left( PPtr x, IndexType& root_idx ) noexcept
{
    PPtr y     = pptr_get_right( x );
    PPtr b     = pptr_get_left( y );
    PPtr x_par = pptr_get_parent( x );

    pptr_set_left( y, x );
    pptr_set_parent( x, y );

    pptr_set_right( x, b );
    if ( !b.is_null() )
        pptr_set_parent( b, x );

    pptr_set_parent( y, x_par );

    avl_set_child( x_par, x, y, root_idx );

    avl_update_height( x );
    avl_update_height( y );
    return y;
}

template <typename PPtr, typename IndexType> static void avl_rebalance_up( PPtr p, IndexType& root_idx ) noexcept
{
    while ( !p.is_null() )
    {
        avl_update_height( p );
        std::int16_t bf = avl_balance_factor( p );
        if ( bf > 1 )
        {
            PPtr left = pptr_get_left( p );
            if ( avl_balance_factor( left ) < 0 )
                avl_rotate_left( left, root_idx );
            p = avl_rotate_right( p, root_idx );
        }
        else if ( bf < -1 )
        {
            PPtr right = pptr_get_right( p );
            if ( avl_balance_factor( right ) > 0 )
                avl_rotate_right( right, root_idx );
            p = avl_rotate_left( p, root_idx );
        }
        p = pptr_get_parent( p );
    }
}

template <typename PPtr, typename IndexType, typename CompareThreeWayFn, typename ResolveFn>
static PPtr avl_find( IndexType root_idx, CompareThreeWayFn&& compare_three_way, ResolveFn&& resolve ) noexcept
{
    PPtr cur( root_idx );
    while ( !cur.is_null() )
    {
        if ( resolve( cur ) == nullptr )
            break;
        int cmp = compare_three_way( cur );
        if ( cmp == 0 )
            return cur;
        else if ( cmp < 0 )
            cur = pptr_get_left( cur );
        else
            cur = pptr_get_right( cur );
    }
    return PPtr(); 
}

template <typename PPtr, typename IndexType, typename GoLeftFn, typename ResolveFn>
static void avl_insert( PPtr new_node, IndexType& root_idx, GoLeftFn&& go_left, ResolveFn&& resolve ) noexcept
{
    if ( new_node.is_null() )
        return;
    if ( resolve( new_node ) == nullptr )
        return;

    if ( root_idx == static_cast<IndexType>( 0 ) )
    {
        pptr_set_left( new_node, PPtr() );
        pptr_set_right( new_node, PPtr() );
        pptr_set_parent( new_node, PPtr() );
        new_node.tree_node().set_height( static_cast<std::int16_t>( 1 ) );
        root_idx = new_node.offset();
        return;
    }

    PPtr cur( root_idx );
    PPtr parent;
    bool left = false;

    while ( !cur.is_null() )
    {
        if ( resolve( cur ) == nullptr )
            break;
        parent = cur;
        left   = go_left( cur );
        if ( left )
            cur = pptr_get_left( cur );
        else
            cur = pptr_get_right( cur );
    }

    pptr_set_parent( new_node, parent );
    if ( left )
        pptr_set_left( parent, new_node );
    else
        pptr_set_right( parent, new_node );

    avl_rebalance_up( parent, root_idx );
}

} 
} 

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

template <typename _K, typename _V, typename ManagerT> struct pmap;

template <typename _K, typename _V> struct pmap_node
{
    _K key;   
    _V value; 
};

template <typename _K, typename _V, typename ManagerT> struct pmap
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using node_type    = pmap_node<_K, _V>;
    using node_pptr    = typename ManagerT::template pptr<node_type>;

    index_type _root_idx;

    pmap() noexcept : _root_idx( static_cast<index_type>( 0 ) ) {}

    bool empty() const noexcept { return _root_idx == static_cast<index_type>( 0 ); }

    node_pptr insert( const _K& key, const _V& val ) noexcept
    {
        
        node_pptr existing = _avl_find( key );
        if ( !existing.is_null() )
        {
            
            node_type* obj = ManagerT::template resolve<node_type>( existing );
            if ( obj != nullptr )
                obj->value = val;
            return existing;
        }

        node_pptr new_node = ManagerT::template allocate_typed<node_type>();
        if ( new_node.is_null() )
            return node_pptr();

        node_type* obj = ManagerT::template resolve<node_type>( new_node );
        if ( obj == nullptr )
            return node_pptr();

        obj->key   = key;
        obj->value = val;

        auto& tn = new_node.tree_node();
        tn.set_left( static_cast<index_type>( 0 ) );
        tn.set_right( static_cast<index_type>( 0 ) );
        tn.set_parent( static_cast<index_type>( 0 ) );
        tn.set_height( static_cast<std::int16_t>( 1 ) );

        _avl_insert( new_node );

        return new_node;
    }

    node_pptr find( const _K& key ) const noexcept { return _avl_find( key ); }

    bool contains( const _K& key ) const noexcept { return !_avl_find( key ).is_null(); }

    void reset() noexcept { _root_idx = static_cast<index_type>( 0 ); }

  private:
    
    node_pptr _avl_find( const _K& key ) const noexcept
    {
        return detail::avl_find<node_pptr>(
            _root_idx,
            [&]( node_pptr cur ) -> int
            {
                node_type* obj = ManagerT::template resolve<node_type>( cur );
                if ( obj == nullptr )
                    return 0;
                if ( key == obj->key )
                    return 0;
                return ( key < obj->key ) ? -1 : 1;
            },
            []( node_pptr p ) -> node_type* { return ManagerT::template resolve<node_type>( p ); } );
    }

    void _avl_insert( node_pptr new_node ) noexcept
    {
        node_type* new_obj = ManagerT::template resolve<node_type>( new_node );
        detail::avl_insert(
            new_node, _root_idx,
            [&]( node_pptr cur ) -> bool
            {
                node_type* obj = ManagerT::template resolve<node_type>( cur );
                return ( obj != nullptr ) && ( new_obj->key < obj->key );
            },
            []( node_pptr p ) -> node_type* { return ManagerT::template resolve<node_type>( p ); } );
    }
};

} 

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

namespace detail
{

template <typename ManagerT> struct manager_index_type
{
    using type = std::uint32_t;
};

template <typename ManagerT>
    requires requires { typename ManagerT::address_traits::index_type; }
struct manager_index_type<ManagerT>
{
    using type = typename ManagerT::address_traits::index_type;
};

} 

template <class T, class ManagerT>
    requires( !std::is_void_v<ManagerT> )
class pptr
{

  public:
    
    using element_type = T;

    using manager_type = ManagerT;

    using index_type = typename detail::manager_index_type<ManagerT>::type;

  private:
    index_type _idx; 

  public:
    constexpr pptr() noexcept : _idx( 0 ) {}
    constexpr explicit pptr( index_type idx ) noexcept : _idx( idx ) {}
    constexpr pptr( const pptr& ) noexcept            = default;
    constexpr pptr& operator=( const pptr& ) noexcept = default;
    ~pptr() noexcept                                  = default;

    pptr& operator++()      = delete;
    pptr  operator++( int ) = delete;
    pptr& operator--()      = delete;
    pptr  operator--( int ) = delete;

    constexpr bool is_null() const noexcept { return _idx == 0; }

    constexpr explicit operator bool() const noexcept { return _idx != 0; }

    constexpr index_type offset() const noexcept { return _idx; }

    constexpr bool operator==( const pptr& other ) const noexcept { return _idx == other._idx; }
    constexpr bool operator!=( const pptr& other ) const noexcept { return _idx != other._idx; }

    T& operator*() const noexcept { return *ManagerT::template resolve<T>( *this ); }

    T* operator->() const noexcept { return ManagerT::template resolve<T>( *this ); }

    T* resolve() const noexcept { return ManagerT::template resolve<T>( *this ); }

    auto& tree_node() const noexcept { return ManagerT::tree_node( *this ); }
};

} 

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm
{

template <typename ManagerT> struct pstringview;

template <typename ManagerT> struct pstringview
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using psview_pptr  = typename ManagerT::template pptr<pstringview>;
    using char_pptr    = typename ManagerT::template pptr<char>;

    index_type    chars_idx; 
    std::uint32_t length;    

    explicit pstringview( const char* s ) noexcept : chars_idx( 0 ), length( 0 ) { _interned = _intern( s ); }

    operator psview_pptr() const noexcept { return _interned; }

    const char* c_str() const noexcept
    {
        if ( chars_idx == 0 )
            return "";
        char_pptr   p( chars_idx );
        const char* raw = ManagerT::template resolve<char>( p );
        return ( raw != nullptr ) ? raw : "";
    }

    std::size_t size() const noexcept { return static_cast<std::size_t>( length ); }

    bool empty() const noexcept { return length == 0; }

    bool operator==( const char* s ) const noexcept
    {
        if ( s == nullptr )
            return length == 0;
        return std::strcmp( c_str(), s ) == 0;
    }

    bool operator==( const pstringview& other ) const noexcept { return chars_idx == other.chars_idx; }

    bool operator!=( const char* s ) const noexcept { return !( *this == s ); }

    bool operator!=( const pstringview& other ) const noexcept { return !( *this == other ); }

    bool operator<( const pstringview& other ) const noexcept { return std::strcmp( c_str(), other.c_str() ) < 0; }

    static psview_pptr intern( const char* s ) noexcept { return _intern( s ); }

    static void reset() noexcept { _root_idx = static_cast<index_type>( 0 ); }

    static inline index_type _root_idx = static_cast<index_type>( 0 );

    ~pstringview() = default;

  private:
    psview_pptr _interned; 

    pstringview() noexcept : chars_idx( 0 ), length( 0 ) {}

    static psview_pptr _intern( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";

        psview_pptr found = _avl_find( s );
        if ( !found.is_null() )
            return found;

        auto len = static_cast<std::uint32_t>( std::strlen( s ) );

        index_type new_chars = _create_chars( s, len );
        if ( new_chars == static_cast<index_type>( 0 ) && len > 0 )
            return psview_pptr();

        psview_pptr new_node = ManagerT::template allocate_typed<pstringview>();
        if ( new_node.is_null() )
            return psview_pptr();

        pstringview* obj = ManagerT::template resolve<pstringview>( new_node );
        if ( obj == nullptr )
            return psview_pptr();
        obj->chars_idx = new_chars;
        obj->length    = len;

        auto& tn = new_node.tree_node();
        tn.set_left( static_cast<index_type>( 0 ) );
        tn.set_right( static_cast<index_type>( 0 ) );
        tn.set_parent( static_cast<index_type>( 0 ) );
        tn.set_height( static_cast<std::int16_t>( 1 ) );

        ManagerT::lock_block_permanent( obj );

        _avl_insert( new_node );

        return new_node;
    }

    static index_type _create_chars( const char* s, std::uint32_t len ) noexcept
    {
        if ( len == 0 )
        {
            
            char_pptr arr = ManagerT::template allocate_typed<char>( 1 );
            if ( arr.is_null() )
                return static_cast<index_type>( 0 );
            char* dst = ManagerT::template resolve<char>( arr );
            if ( dst != nullptr )
                dst[0] = '\0';
            if ( dst != nullptr )
                ManagerT::lock_block_permanent( dst );
            return arr.offset();
        }

        char_pptr arr = ManagerT::template allocate_typed<char>( static_cast<std::size_t>( len + 1 ) );
        if ( arr.is_null() )
            return static_cast<index_type>( 0 );
        char* dst = ManagerT::template resolve<char>( arr );
        if ( dst != nullptr )
            std::memcpy( dst, s, static_cast<std::size_t>( len + 1 ) );
        if ( dst != nullptr )
            ManagerT::lock_block_permanent( dst );
        return arr.offset();
    }

    static psview_pptr _avl_find( const char* s ) noexcept
    {
        return detail::avl_find<psview_pptr>(
            _root_idx,
            [&]( psview_pptr cur ) -> int
            {
                pstringview* obj = ManagerT::template resolve<pstringview>( cur );
                return ( obj != nullptr ) ? std::strcmp( s, obj->c_str() ) : 0;
            },
            []( psview_pptr p ) -> pstringview* { return ManagerT::template resolve<pstringview>( p ); } );
    }

    static void _avl_insert( psview_pptr new_node ) noexcept
    {
        pstringview* new_obj = ManagerT::template resolve<pstringview>( new_node );
        const char*  new_str = ( new_obj != nullptr ) ? new_obj->c_str() : "";
        detail::avl_insert(
            new_node, _root_idx,
            [&]( psview_pptr cur ) -> bool
            {
                pstringview* obj = ManagerT::template resolve<pstringview>( cur );
                return ( obj != nullptr ) && ( std::strcmp( new_str, obj->c_str() ) < 0 );
            },
            []( psview_pptr p ) -> pstringview* { return ManagerT::template resolve<pstringview>( p ); } );
    }
};

} 

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace pmm
{

template <typename ConfigT = CacheManagerConfig, std::size_t InstanceId = 0> class PersistMemoryManager
{
  public:
    
    using address_traits  = typename ConfigT::address_traits;
    using storage_backend = typename ConfigT::storage_backend;
    using free_block_tree = typename ConfigT::free_block_tree;
    using thread_policy   = typename ConfigT::lock_policy;
    using allocator       = AllocatorPolicy<free_block_tree, address_traits>;
    using index_type      = typename address_traits::index_type;

    using manager_type = PersistMemoryManager<ConfigT, InstanceId>;

    template <typename T> using pptr = pmm::pptr<T, manager_type>;

    using pstringview = pmm::pstringview<manager_type>;

    template <typename _K, typename _V> using pmap = pmm::pmap<_K, _V, manager_type>;

    static bool create( std::size_t initial_size ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( initial_size < detail::kMinMemorySize )
            return false;
        
        static constexpr std::size_t kGranSzCreate = address_traits::granule_size;
        if ( initial_size > std::numeric_limits<std::size_t>::max() - ( kGranSzCreate - 1 ) )
            return false; 
        std::size_t aligned = ( ( initial_size + kGranSzCreate - 1 ) / kGranSzCreate ) * kGranSzCreate;
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < aligned )
        {
            
            std::size_t additional =
                ( _backend.total_size() < aligned ) ? ( aligned - _backend.total_size() ) : aligned;
            if ( !_backend.expand( additional ) )
                return false;
        }
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < aligned )
            return false;
        return init_layout( _backend.base_ptr(), _backend.total_size() );
    }

    static bool create() noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < detail::kMinMemorySize )
            return false;
        return init_layout( _backend.base_ptr(), _backend.total_size() );
    }

    static bool load() noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < detail::kMinMemorySize )
            return false;
        std::uint8_t*                          base = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr  = get_header( base );
        if ( hdr->magic != kMagic || hdr->total_size != _backend.total_size() )
            return false;
        
        if ( hdr->granule_size != static_cast<std::uint16_t>( address_traits::granule_size ) )
            return false;
        hdr->owns_memory     = false;
        hdr->prev_total_size = 0;
        allocator::repair_linked_list( base, hdr );
        allocator::recompute_counters( base, hdr );
        allocator::rebuild_free_tree( base, hdr );
        _initialized = true;
        return true;
    }

    static void destroy() noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return;
        std::uint8_t*                          base = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr  = ( base != nullptr ) ? get_header( base ) : nullptr;
        if ( hdr != nullptr )
            hdr->magic = 0;
        _initialized = false;
    }

    static bool is_initialized() noexcept { return _initialized.load( std::memory_order_acquire ); }

    static void* allocate( std::size_t user_size ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized || user_size == 0 )
            return nullptr;

        std::uint8_t*                          base = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr  = get_header( base );
        
        index_type data_gran = detail::bytes_to_granules_t<address_traits>( user_size );
        if ( data_gran == 0 )
            data_gran = 1;
        index_type needed = kBlockHdrGranules + data_gran;
        index_type idx    = free_block_tree::find_best_fit( base, hdr, needed );

        if ( idx != address_traits::no_block )
            return allocator::allocate_from_block( base, hdr, idx, user_size );

        if ( !do_expand( user_size ) )
            return nullptr;

        base = _backend.base_ptr();
        hdr  = get_header( base );
        idx  = free_block_tree::find_best_fit( base, hdr, needed );
        if ( idx != address_traits::no_block )
            return allocator::allocate_from_block( base, hdr, idx, user_size );
        return nullptr;
    }

    static void deallocate( void* ptr ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized || ptr == nullptr )
            return;
        pmm::Block<address_traits>* blk = find_block_from_user_ptr( ptr );
        if ( blk == nullptr )
            return;
        index_type freed = BlockStateBase<address_traits>::get_weight( blk );
        if ( freed == 0 )
            return;

        if ( BlockStateBase<address_traits>::get_node_type( blk ) == pmm::kNodeReadOnly )
            return;

        std::uint8_t*                          base    = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr     = get_header( base );
        index_type                             blk_idx = detail::block_idx_t<address_traits>( base, blk );

        AllocatedBlock<address_traits>* alloc = AllocatedBlock<address_traits>::cast_from_raw( blk );
        alloc->mark_as_free();

        hdr->alloc_count--;
        hdr->free_count++;
        if ( hdr->used_size >= freed )
            hdr->used_size -= freed;
        allocator::coalesce( base, hdr, blk_idx );
    }

    static bool lock_block_permanent( void* ptr ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized || ptr == nullptr )
            return false;
        pmm::Block<address_traits>* blk = find_block_from_user_ptr( ptr );
        if ( blk == nullptr )
            return false;
        index_type w = BlockStateBase<address_traits>::get_weight( blk );
        if ( w == 0 )
            return false; 
        BlockStateBase<address_traits>::set_node_type_of( blk, pmm::kNodeReadOnly );
        return true;
    }

    static bool is_permanently_locked( const void* ptr ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized || ptr == nullptr )
            return false;
        const pmm::Block<address_traits>* blk = find_block_from_user_ptr( ptr );
        if ( blk == nullptr )
            return false;
        return BlockStateBase<address_traits>::get_node_type( blk ) == pmm::kNodeReadOnly;
    }

    template <typename T> static pptr<T> allocate_typed() noexcept
    {
        void* raw = allocate( sizeof( T ) );
        if ( raw == nullptr )
            return pptr<T>();
        return make_pptr_from_raw<T>( raw );
    }

    template <typename T> static pptr<T> allocate_typed( std::size_t count ) noexcept
    {
        if ( count == 0 )
            return pptr<T>();
        
        if ( sizeof( T ) > 0 && count > ( std::numeric_limits<std::size_t>::max )() / sizeof( T ) )
            return pptr<T>();
        void* raw = allocate( sizeof( T ) * count );
        if ( raw == nullptr )
            return pptr<T>();
        return make_pptr_from_raw<T>( raw );
    }

    template <typename T> static void deallocate_typed( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        std::uint8_t* base = _backend.base_ptr();
        void*         raw  = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        deallocate( raw );
    }

    template <typename T, typename... Args> static pptr<T> create_typed( Args&&... args ) noexcept
    {
        void* raw = allocate( sizeof( T ) );
        if ( raw == nullptr )
            return pptr<T>();
        
        ::new ( raw ) T( static_cast<Args&&>( args )... );
        return make_pptr_from_raw<T>( raw );
    }

    template <typename T> static void destroy_typed( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        std::uint8_t* base = _backend.base_ptr();
        void*         raw  = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        
        reinterpret_cast<T*>( raw )->~T();
        deallocate( raw );
    }

    template <typename T> static T* resolve( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return nullptr;
        std::uint8_t* base = _backend.base_ptr();
        return reinterpret_cast<T*>( base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size );
    }

    template <typename T> static T* resolve_at( pptr<T> p, std::size_t i ) noexcept
    {
        T* base_elem = resolve( p );
        return ( base_elem == nullptr ) ? nullptr : base_elem + i;
    }

    template <typename T> static index_type get_tree_left_offset( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        index_type left = BlockStateBase<address_traits>::get_left_offset( block_raw_ptr_from_pptr( p ) );
        return ( left == address_traits::no_block ) ? static_cast<index_type>( 0 ) : left;
    }

    template <typename T> static index_type get_tree_right_offset( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        index_type right = BlockStateBase<address_traits>::get_right_offset( block_raw_ptr_from_pptr( p ) );
        return ( right == address_traits::no_block ) ? static_cast<index_type>( 0 ) : right;
    }

    template <typename T> static index_type get_tree_parent_offset( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        index_type parent = BlockStateBase<address_traits>::get_parent_offset( block_raw_ptr_from_pptr( p ) );
        return ( parent == address_traits::no_block ) ? static_cast<index_type>( 0 ) : parent;
    }

    template <typename T> static void set_tree_left_offset( pptr<T> p, index_type left ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        index_type v = ( left == 0 ) ? address_traits::no_block : left;
        BlockStateBase<address_traits>::set_left_offset_of( block_raw_mut_ptr_from_pptr( p ), v );
    }

    template <typename T> static void set_tree_right_offset( pptr<T> p, index_type right ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        index_type v = ( right == 0 ) ? address_traits::no_block : right;
        BlockStateBase<address_traits>::set_right_offset_of( block_raw_mut_ptr_from_pptr( p ), v );
    }

    template <typename T> static void set_tree_parent_offset( pptr<T> p, index_type parent ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        index_type v = ( parent == 0 ) ? address_traits::no_block : parent;
        BlockStateBase<address_traits>::set_parent_offset_of( block_raw_mut_ptr_from_pptr( p ), v );
    }

    template <typename T> static index_type get_tree_weight( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        return BlockStateBase<address_traits>::get_weight( block_raw_ptr_from_pptr( p ) );
    }

    template <typename T> static void set_tree_weight( pptr<T> p, index_type w ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        BlockStateBase<address_traits>::set_weight_of( block_raw_mut_ptr_from_pptr( p ), w );
    }

    template <typename T> static std::int16_t get_tree_height( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        return BlockStateBase<address_traits>::get_avl_height( block_raw_ptr_from_pptr( p ) );
    }

    template <typename T> static void set_tree_height( pptr<T> p, std::int16_t h ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        BlockStateBase<address_traits>::set_avl_height_of( block_raw_mut_ptr_from_pptr( p ), h );
    }

    template <typename T> static TreeNode<address_traits>& tree_node( pptr<T> p ) noexcept
    {
        
        assert( !p.is_null() && "tree_node: pptr must not be null" );
        assert( _initialized && "tree_node: manager must be initialized before calling tree_node" );
        return *reinterpret_cast<TreeNode<address_traits>*>( block_raw_mut_ptr_from_pptr( p ) );
    }

    static std::size_t total_size() noexcept
    {
        if ( !_initialized.load( std::memory_order_acquire ) )
            return 0;
        typename thread_policy::shared_lock_type lock( _mutex );
        return _initialized.load( std::memory_order_relaxed ) ? _backend.total_size() : 0;
    }

    static std::size_t used_size() noexcept
    {
        if ( !_initialized.load( std::memory_order_acquire ) )
            return 0;
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized.load( std::memory_order_relaxed ) )
            return 0;
        const detail::ManagerHeader<address_traits>* hdr = get_header_c( _backend.base_ptr() );
        
        return address_traits::granules_to_bytes( hdr->used_size );
    }

    static std::size_t free_size() noexcept
    {
        if ( !_initialized.load( std::memory_order_acquire ) )
            return 0;
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized.load( std::memory_order_relaxed ) )
            return 0;
        const detail::ManagerHeader<address_traits>* hdr = get_header_c( _backend.base_ptr() );
        
        std::size_t used = address_traits::granules_to_bytes( hdr->used_size );
        return ( hdr->total_size > used ) ? ( hdr->total_size - used ) : 0;
    }

    static std::size_t block_count() noexcept
    {
        if ( !_initialized.load( std::memory_order_acquire ) )
            return 0;
        typename thread_policy::shared_lock_type lock( _mutex );
        return _initialized.load( std::memory_order_relaxed )
                   ? static_cast<std::size_t>( get_header_c( _backend.base_ptr() )->block_count )
                   : 0;
    }

    static std::size_t free_block_count() noexcept
    {
        if ( !_initialized.load( std::memory_order_acquire ) )
            return 0;
        typename thread_policy::shared_lock_type lock( _mutex );
        return _initialized.load( std::memory_order_relaxed )
                   ? static_cast<std::size_t>( get_header_c( _backend.base_ptr() )->free_count )
                   : 0;
    }

    static std::size_t alloc_block_count() noexcept
    {
        if ( !_initialized.load( std::memory_order_acquire ) )
            return 0;
        typename thread_policy::shared_lock_type lock( _mutex );
        return _initialized.load( std::memory_order_relaxed )
                   ? static_cast<std::size_t>( get_header_c( _backend.base_ptr() )->alloc_count )
                   : 0;
    }

    template <typename Callback> static bool for_each_block( Callback&& callback ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return false;
        const std::uint8_t* base                         = _backend.base_ptr();
        using BlockState                                 = BlockStateBase<address_traits>;
        const detail::ManagerHeader<address_traits>* hdr = get_header_c( base );
        index_type                                   idx = hdr->first_block_offset;
        
        static constexpr std::size_t kGranSz = address_traits::granule_size;
        while ( idx != address_traits::no_block )
        {
            if ( static_cast<std::size_t>( idx ) * kGranSz + sizeof( Block<address_traits> ) > hdr->total_size )
                break;
            const void*                  blk_raw    = base + static_cast<std::size_t>( idx ) * kGranSz;
            const Block<address_traits>* blk        = reinterpret_cast<const Block<address_traits>*>( blk_raw );
            index_type                   total_gran = detail::block_total_granules( base, hdr, blk );
            auto                         w          = BlockState::get_weight( blk_raw );
            bool                         is_used    = ( w > 0 );
            std::size_t                  hdr_bytes  = sizeof( Block<address_traits> );
            std::size_t                  data_bytes = is_used ? static_cast<std::size_t>( w ) * kGranSz : 0;

            BlockView view;
            view.index       = idx;
            view.offset      = static_cast<std::ptrdiff_t>( static_cast<std::size_t>( idx ) * kGranSz );
            view.total_size  = static_cast<std::size_t>( total_gran ) * kGranSz;
            view.header_size = hdr_bytes;
            view.user_size   = data_bytes;
            view.alignment   = kGranSz;
            view.used        = is_used;
            callback( view );
            idx = BlockState::get_next_offset( blk_raw );
        }
        return true;
    }

    template <typename Callback> static bool for_each_free_block( Callback&& callback ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return false;
        const std::uint8_t*                          base = _backend.base_ptr();
        const detail::ManagerHeader<address_traits>* hdr  = get_header_c( base );
        for_each_free_block_inorder( base, hdr, hdr->free_tree_root, 0, callback );
        return true;
    }

    static storage_backend& backend() noexcept { return _backend; }

  private:
    
    static inline storage_backend _backend{};

    static inline std::atomic<bool> _initialized{ false };

    static inline typename thread_policy::mutex_type _mutex{};

    static pmm::Block<address_traits>* find_block_from_user_ptr( void* ptr ) noexcept
    {
        std::uint8_t*                          base = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr  = get_header( base );
        return detail::header_from_ptr_t<address_traits>( base, ptr, static_cast<std::size_t>( hdr->total_size ) );
    }

    static const pmm::Block<address_traits>* find_block_from_user_ptr( const void* ptr ) noexcept
    {
        const std::uint8_t* base = _backend.base_ptr();
        return detail::header_from_ptr_t<address_traits>(
            const_cast<std::uint8_t*>( base ), const_cast<void*>( ptr ),
            static_cast<std::size_t>( get_header_c( base )->total_size ) );
    }

    template <typename T> static pptr<T> make_pptr_from_raw( void* raw ) noexcept
    {
        std::uint8_t* base     = _backend.base_ptr();
        std::size_t   byte_off = static_cast<std::uint8_t*>( raw ) - base;
        return pptr<T>( static_cast<index_type>( byte_off / address_traits::granule_size ) );
    }

    template <typename T> static const void* block_raw_ptr_from_pptr( pptr<T> p ) noexcept
    {
        const std::uint8_t* base = _backend.base_ptr();
        return base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
               sizeof( Block<address_traits> );
    }

    template <typename T> static void* block_raw_mut_ptr_from_pptr( pptr<T> p ) noexcept
    {
        std::uint8_t* base = _backend.base_ptr();
        return base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
               sizeof( Block<address_traits> );
    }

    template <typename Callback>
    static void for_each_free_block_inorder( const std::uint8_t* base, const detail::ManagerHeader<address_traits>* hdr,
                                             index_type node_idx, int depth, Callback&& callback ) noexcept
    {
        using BlockState = BlockStateBase<address_traits>;
        
        static constexpr std::size_t kGranSz = address_traits::granule_size;
        if ( node_idx == address_traits::no_block )
            return;
        if ( static_cast<std::size_t>( node_idx ) * kGranSz + sizeof( Block<address_traits> ) > hdr->total_size )
            return;
        const void*                  blk_raw = base + static_cast<std::size_t>( node_idx ) * kGranSz;
        const Block<address_traits>* blk     = reinterpret_cast<const Block<address_traits>*>( blk_raw );

        index_type left_off   = BlockState::get_left_offset( blk_raw );
        index_type right_off  = BlockState::get_right_offset( blk_raw );
        index_type parent_off = BlockState::get_parent_offset( blk_raw );

        for_each_free_block_inorder( base, hdr, left_off, depth + 1, callback );

        index_type    total_gran = detail::block_total_granules( base, hdr, blk );
        FreeBlockView view;
        view.offset        = static_cast<std::ptrdiff_t>( static_cast<std::size_t>( node_idx ) * kGranSz );
        view.total_size    = static_cast<std::size_t>( total_gran ) * kGranSz;
        view.free_size     = static_cast<std::size_t>( total_gran - kBlockHdrGranules ) * kGranSz;
        view.left_offset   = ( left_off != address_traits::no_block )
                                 ? static_cast<std::ptrdiff_t>( static_cast<std::size_t>( left_off ) * kGranSz )
                                 : -1;
        view.right_offset  = ( right_off != address_traits::no_block )
                                 ? static_cast<std::ptrdiff_t>( static_cast<std::size_t>( right_off ) * kGranSz )
                                 : -1;
        view.parent_offset = ( parent_off != address_traits::no_block )
                                 ? static_cast<std::ptrdiff_t>( static_cast<std::size_t>( parent_off ) * kGranSz )
                                 : -1;
        view.avl_height    = BlockState::get_avl_height( blk_raw );
        view.avl_depth     = depth;
        callback( view );

        for_each_free_block_inorder( base, hdr, right_off, depth + 1, callback );
    }

    static constexpr std::size_t kBlockHdrByteSize =
        ( ( sizeof( Block<address_traits> ) + address_traits::granule_size - 1 ) / address_traits::granule_size ) *
        address_traits::granule_size;

    static constexpr index_type kBlockHdrGranules =
        static_cast<index_type>( kBlockHdrByteSize / address_traits::granule_size );

    static constexpr index_type kMgrHdrGranules = detail::kManagerHeaderGranules_t<address_traits>;

    static constexpr index_type kFreeBlkIdxLayout = kBlockHdrGranules + kMgrHdrGranules;

    static detail::ManagerHeader<address_traits>* get_header( std::uint8_t* base ) noexcept
    {
        
        return reinterpret_cast<detail::ManagerHeader<address_traits>*>( base + kBlockHdrByteSize );
    }

    static const detail::ManagerHeader<address_traits>* get_header_c( const std::uint8_t* base ) noexcept
    {
        return reinterpret_cast<const detail::ManagerHeader<address_traits>*>( base + kBlockHdrByteSize );
    }

    static bool init_layout( std::uint8_t* base, std::size_t size ) noexcept
    {
        using BlockState                         = BlockStateBase<address_traits>;
        static constexpr index_type  kHdrBlkIdx  = 0;
        static constexpr index_type  kFreeBlkIdx = kFreeBlkIdxLayout;
        static constexpr std::size_t kGranSz     = address_traits::granule_size;

        static constexpr std::size_t kMinBlockDataSize = kGranSz; 
        if ( static_cast<std::size_t>( kFreeBlkIdx ) * kGranSz + sizeof( Block<address_traits> ) + kMinBlockDataSize >
             size )
            return false;

        void* hdr_blk = base;
        std::memset( hdr_blk, 0, kBlockHdrByteSize ); 
        BlockState::init_fields( hdr_blk,
                                  address_traits::no_block,
                                  kFreeBlkIdx,
                                  0,
                                  kMgrHdrGranules,
                                  kHdrBlkIdx );

        detail::ManagerHeader<address_traits>* hdr = get_header( base );
        std::memset( hdr, 0, sizeof( detail::ManagerHeader<address_traits> ) );
        hdr->magic              = kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = kHdrBlkIdx;
        hdr->last_block_offset  = address_traits::no_block;
        hdr->free_tree_root     = address_traits::no_block;
        hdr->granule_size       = static_cast<std::uint16_t>( kGranSz );

        void* blk = base + static_cast<std::size_t>( kFreeBlkIdx ) * kGranSz;
        std::memset( blk, 0, sizeof( Block<address_traits> ) );
        BlockState::init_fields( blk,
                                  kHdrBlkIdx,
                                  address_traits::no_block,
                                  1,
                                  0,
                                  0 );

        hdr->last_block_offset = kFreeBlkIdx;
        hdr->free_tree_root    = kFreeBlkIdx;
        hdr->block_count       = 2;
        hdr->free_count        = 1;
        hdr->alloc_count       = 1;
        hdr->used_size         = kFreeBlkIdx + kBlockHdrGranules;

        _initialized = true;
        return true;
    }

    static bool do_expand( std::size_t user_size ) noexcept
    {
        using BlockState = BlockStateBase<address_traits>;
        if ( !_initialized )
            return false;
        std::uint8_t*                          base     = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr      = get_header( base );
        std::size_t                            old_size = hdr->total_size;

        static constexpr std::size_t kGranSz        = address_traits::granule_size;
        index_type                   data_gran_need = detail::bytes_to_granules_t<address_traits>( user_size );
        if ( data_gran_need == 0 )
            data_gran_need = 1;
        
        std::size_t min_need =
            static_cast<std::size_t>( kBlockHdrGranules + data_gran_need + kBlockHdrGranules ) * kGranSz;
        std::size_t growth = old_size / 4;
        if ( growth < min_need )
            growth = min_need;

        if ( !_backend.expand( growth ) )
            return false;

        std::uint8_t* new_base = _backend.base_ptr();
        std::size_t   new_size = _backend.total_size();
        if ( new_base == nullptr || new_size <= old_size )
            return false;

        hdr = get_header( new_base );

        index_type  extra_idx  = detail::byte_off_to_idx_t<address_traits>( old_size );
        std::size_t extra_size = new_size - old_size;

        void* last_blk_raw =
            ( hdr->last_block_offset != address_traits::no_block )
                ? static_cast<void*>( new_base + static_cast<std::size_t>( hdr->last_block_offset ) * kGranSz )
                : nullptr;

        if ( last_blk_raw != nullptr && BlockState::get_weight( last_blk_raw ) == 0 )
        {
            Block<address_traits>* last_blk = reinterpret_cast<Block<address_traits>*>( last_blk_raw );
            index_type             loff     = detail::block_idx_t<address_traits>( new_base, last_blk );
            free_block_tree::remove( new_base, hdr, loff );
            hdr->total_size = new_size;
            free_block_tree::insert( new_base, hdr, loff );
        }
        else
        {
            
            if ( extra_size < sizeof( Block<address_traits> ) + kGranSz )
                return false;
            void* nb_blk = new_base + static_cast<std::size_t>( extra_idx ) * kGranSz;
            std::memset( nb_blk, 0, sizeof( Block<address_traits> ) );
            if ( last_blk_raw != nullptr )
            {
                Block<address_traits>* last_blk = reinterpret_cast<Block<address_traits>*>( last_blk_raw );
                index_type             loff     = detail::block_idx_t<address_traits>( new_base, last_blk );
                BlockState::init_fields( nb_blk,
                                          loff,
                                          address_traits::no_block,
                                          1,
                                          0,
                                          0 );
                BlockState::set_next_offset_of( last_blk_raw, static_cast<index_type>( extra_idx ) );
            }
            else
            {
                BlockState::init_fields( nb_blk,
                                          address_traits::no_block,
                                          address_traits::no_block,
                                          1,
                                          0,
                                          0 );
                hdr->first_block_offset = extra_idx;
            }
            hdr->last_block_offset = extra_idx;
            hdr->block_count++;
            hdr->free_count++;
            hdr->total_size = new_size;
            free_block_tree::insert( new_base, hdr, extra_idx );
        }
        return true;
    }
};

} 

#include <cstdint>
#include <cstdio>

namespace pmm
{

template <typename MgrT> inline bool save_manager( const char* filename )
{
    if ( filename == nullptr || !MgrT::is_initialized() )
        return false;
    const std::uint8_t* data  = MgrT::backend().base_ptr();
    std::size_t         total = MgrT::backend().total_size();
    if ( data == nullptr || total == 0 )
        return false;
    std::FILE* f = std::fopen( filename, "wb" );
    if ( f == nullptr )
        return false;
    std::size_t written = std::fwrite( data, 1, total, f );
    std::fclose( f );
    return written == total;
}

template <typename MgrT> inline bool load_manager_from_file( const char* filename )
{
    if ( filename == nullptr )
        return false;

    std::uint8_t* buf  = MgrT::backend().base_ptr();
    std::size_t   size = MgrT::backend().total_size();
    if ( buf == nullptr || size < detail::kMinMemorySize )
        return false;

    std::FILE* f = std::fopen( filename, "rb" );
    if ( f == nullptr )
        return false;

    if ( std::fseek( f, 0, SEEK_END ) != 0 )
    {
        std::fclose( f );
        return false;
    }
    long file_size_long = std::ftell( f );
    if ( file_size_long <= 0 )
    {
        std::fclose( f );
        return false;
    }
    std::rewind( f );

    std::size_t file_size = static_cast<std::size_t>( file_size_long );
    if ( file_size > size )
    {
        std::fclose( f );
        return false;
    }

    std::size_t read_bytes = std::fread( buf, 1, file_size, f );
    std::fclose( f );

    if ( read_bytes != file_size )
        return false;

    return MgrT::load();
}

} 

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined( _WIN32 ) || defined( _WIN64 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pmm
{

template <typename AddressTraitsT = DefaultAddressTraits> class MMapStorage
{
  public:
    using address_traits = AddressTraitsT;

    MMapStorage() noexcept = default;

    MMapStorage( const MMapStorage& )            = delete;
    MMapStorage& operator=( const MMapStorage& ) = delete;

    MMapStorage( MMapStorage&& other ) noexcept
        : _base( other._base ), _size( other._size ), _mapped( other._mapped )
#if defined( _WIN32 ) || defined( _WIN64 )
          ,
          _file_handle( other._file_handle ), _map_handle( other._map_handle )
#else
          ,
          _fd( other._fd )
#endif
    {
        other._base   = nullptr;
        other._size   = 0;
        other._mapped = false;
#if defined( _WIN32 ) || defined( _WIN64 )
        other._file_handle = INVALID_HANDLE_VALUE;
        other._map_handle  = nullptr;
#else
        other._fd = -1;
#endif
    }

    ~MMapStorage() { close(); }

    bool open( const char* path, std::size_t size_bytes ) noexcept
    {
        if ( _mapped )
            return false; 
        if ( path == nullptr || size_bytes == 0 )
            return false;
        
        size_bytes = ( ( size_bytes + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size ) *
                     AddressTraitsT::granule_size;
        return open_impl( path, size_bytes );
    }

    void close() noexcept
    {
        if ( !_mapped )
            return;
        close_impl();
        _base   = nullptr;
        _size   = 0;
        _mapped = false;
    }

    bool is_open() const noexcept { return _mapped; }

    std::uint8_t*       base_ptr() noexcept { return _base; }
    const std::uint8_t* base_ptr() const noexcept { return _base; }

    std::size_t total_size() const noexcept { return _size; }

    bool expand( std::size_t  ) noexcept { return false; }

    bool owns_memory() const noexcept { return false; }

  private:
#if defined( _WIN32 ) || defined( _WIN64 )
    std::uint8_t* _base        = nullptr;
    std::size_t   _size        = 0;
    bool          _mapped      = false;
    HANDLE        _file_handle = INVALID_HANDLE_VALUE;
    HANDLE        _map_handle  = nullptr;

    bool open_impl( const char* path, std::size_t size_bytes ) noexcept
    {
        _file_handle = CreateFileA( path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
        if ( _file_handle == INVALID_HANDLE_VALUE )
            return false;

        LARGE_INTEGER existing_size{};
        if ( !GetFileSizeEx( _file_handle, &existing_size ) )
        {
            CloseHandle( _file_handle );
            _file_handle = INVALID_HANDLE_VALUE;
            return false;
        }

        if ( static_cast<std::size_t>( existing_size.QuadPart ) < size_bytes )
        {
            LARGE_INTEGER new_size_li{};
            new_size_li.QuadPart = static_cast<LONGLONG>( size_bytes );
            if ( !SetFilePointerEx( _file_handle, new_size_li, nullptr, FILE_BEGIN ) || !SetEndOfFile( _file_handle ) )
            {
                CloseHandle( _file_handle );
                _file_handle = INVALID_HANDLE_VALUE;
                return false;
            }
        }

        DWORD size_hi = static_cast<DWORD>( size_bytes >> 32 );
        DWORD size_lo = static_cast<DWORD>( size_bytes & 0xFFFFFFFF );
        _map_handle   = CreateFileMappingA( _file_handle, nullptr, PAGE_READWRITE, size_hi, size_lo, nullptr );
        if ( _map_handle == nullptr )
        {
            CloseHandle( _file_handle );
            _file_handle = INVALID_HANDLE_VALUE;
            return false;
        }

        void* view = MapViewOfFile( _map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size_bytes );
        if ( view == nullptr )
        {
            CloseHandle( _map_handle );
            CloseHandle( _file_handle );
            _map_handle  = nullptr;
            _file_handle = INVALID_HANDLE_VALUE;
            return false;
        }

        _base   = static_cast<std::uint8_t*>( view );
        _size   = size_bytes;
        _mapped = true;
        return true;
    }

    void close_impl() noexcept
    {
        if ( _base != nullptr )
        {
            FlushViewOfFile( _base, _size );
            UnmapViewOfFile( _base );
        }
        if ( _map_handle != nullptr )
        {
            CloseHandle( _map_handle );
            _map_handle = nullptr;
        }
        if ( _file_handle != INVALID_HANDLE_VALUE )
        {
            CloseHandle( _file_handle );
            _file_handle = INVALID_HANDLE_VALUE;
        }
    }

#else  

    std::uint8_t* _base   = nullptr;
    std::size_t   _size   = 0;
    bool          _mapped = false;
    int           _fd     = -1;

    bool open_impl( const char* path, std::size_t size_bytes ) noexcept
    {
        _fd = ::open( path, O_RDWR | O_CREAT, 0600 );
        if ( _fd < 0 )
            return false;

        struct stat st
        {
        };
        if ( ::fstat( _fd, &st ) != 0 )
        {
            ::close( _fd );
            _fd = -1;
            return false;
        }

        if ( static_cast<std::size_t>( st.st_size ) < size_bytes )
        {
            if ( ::ftruncate( _fd, static_cast<off_t>( size_bytes ) ) != 0 )
            {
                ::close( _fd );
                _fd = -1;
                return false;
            }
        }

        void* addr = ::mmap( nullptr, size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0 );
        if ( addr == MAP_FAILED )
        {
            ::close( _fd );
            _fd = -1;
            return false;
        }

        _base   = static_cast<std::uint8_t*>( addr );
        _size   = size_bytes;
        _mapped = true;
        return true;
    }

    void close_impl() noexcept
    {
        if ( _base != nullptr )
            ::munmap( _base, _size );
        if ( _fd >= 0 )
        {
            ::close( _fd );
            _fd = -1;
        }
    }
#endif 
};

static_assert( is_storage_backend_v<MMapStorage<>>, "MMapStorage must satisfy StorageBackendConcept" );

} 

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace pmm
{

template <typename T>
concept PersistMemoryManagerConcept = requires {
    typename T::manager_type;
    typename T::address_traits;
    typename T::storage_backend;
    { T::is_initialized() };
    { T::allocate( std::size_t{} ) } -> std::convertible_to<void*>;
    { T::deallocate( static_cast<void*>( nullptr ) ) };
    { T::total_size() } -> std::convertible_to<std::size_t>;
    { T::destroy() };
};

template <typename T> struct is_persist_memory_manager : std::bool_constant<PersistMemoryManagerConcept<T>>
{
};

template <typename T> inline constexpr bool is_persist_memory_manager_v = PersistMemoryManagerConcept<T>;

} 
