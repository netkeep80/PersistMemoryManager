
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
               "TreeNode must be standard-layout " );

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

static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32, "Block<DefaultAddressTraits> must be 32 bytes " );

} 

#include <cstddef>
#include <cstdint>

namespace pmm
{

enum class RecoveryMode : std::uint8_t
{
    Verify = 0, 
    Repair = 1, 
};

enum class ViolationType : std::uint8_t
{
    None = 0,                 
    BlockStateInconsistent,   
    PrevOffsetMismatch,       
    CounterMismatch,          
    FreeTreeStale,            
    ForestRegistryMissing,    
    ForestDomainMissing,      
    ForestDomainFlagsMissing, 
    HeaderCorruption,         
};

enum class DiagnosticAction : std::uint8_t
{
    NoAction = 0, 
    Repaired,     
    Rebuilt,      
    Aborted,      
};

struct DiagnosticEntry
{
    ViolationType    type        = ViolationType::None;        
    DiagnosticAction action      = DiagnosticAction::NoAction; 
    std::uint64_t    block_index = 0;                          
    std::uint64_t    expected    = 0;                          
    std::uint64_t    actual      = 0;                          
};

inline constexpr std::size_t kMaxDiagnosticEntries = 64;

struct VerifyResult
{
    RecoveryMode mode = RecoveryMode::Verify; 
    bool         ok   = true;                 

    std::size_t violation_count = 0;

    DiagnosticEntry entries[kMaxDiagnosticEntries] = {};

    std::size_t entry_count = 0;

    void add( ViolationType type, DiagnosticAction action, std::uint64_t block_index = 0, std::uint64_t expected = 0,
              std::uint64_t actual = 0 ) noexcept
    {
        ok = false;
        violation_count++;
        if ( entry_count < kMaxDiagnosticEntries )
        {
            entries[entry_count].type        = type;
            entries[entry_count].action      = action;
            entries[entry_count].block_index = block_index;
            entries[entry_count].expected    = expected;
            entries[entry_count].actual      = actual;
            entry_count++;
        }
    }
};

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

    static void verify_state( const void* raw_blk, index_type own_idx, VerifyResult& result ) noexcept
    {
        const auto* blk = reinterpret_cast<const BlockStateBase*>( raw_blk );
        if ( blk->weight() > 0 && blk->root_offset() != own_idx )
        {
            result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( own_idx ), static_cast<std::uint64_t>( own_idx ),
                        static_cast<std::uint64_t>( blk->root_offset() ) );
        }
        if ( blk->weight() == 0 && blk->root_offset() != 0 )
        {
            result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( own_idx ), 0, static_cast<std::uint64_t>( blk->root_offset() ) );
        }
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

    static index_type field_read_idx( const void* raw_blk, std::size_t offset ) noexcept
    {
        index_type v;
        std::memcpy( &v, static_cast<const std::uint8_t*>( raw_blk ) + offset, sizeof( v ) );
        return v;
    }
    
    static void field_write_idx( void* raw_blk, std::size_t offset, index_type v ) noexcept
    {
        std::memcpy( static_cast<std::uint8_t*>( raw_blk ) + offset, &v, sizeof( v ) );
    }

    static index_type get_left_offset( const void* b ) noexcept { return field_read_idx( b, kOffsetLeftOffset ); }
    static index_type get_right_offset( const void* b ) noexcept { return field_read_idx( b, kOffsetRightOffset ); }
    static index_type get_parent_offset( const void* b ) noexcept { return field_read_idx( b, kOffsetParentOffset ); }
    static index_type get_root_offset( const void* b ) noexcept { return field_read_idx( b, kOffsetRootOffset ); }
    static void set_left_offset_of( void* b, index_type v ) noexcept { field_write_idx( b, kOffsetLeftOffset, v ); }
    static void set_right_offset_of( void* b, index_type v ) noexcept { field_write_idx( b, kOffsetRightOffset, v ); }
    static void set_parent_offset_of( void* b, index_type v ) noexcept { field_write_idx( b, kOffsetParentOffset, v ); }
    static void set_prev_offset_of( void* b, index_type v ) noexcept { field_write_idx( b, kOffsetPrevOffset, v ); }
    static void set_weight_of( void* b, index_type v ) noexcept { field_write_idx( b, kOffsetWeight, v ); }
    static void set_root_offset_of( void* b, index_type v ) noexcept { field_write_idx( b, kOffsetRootOffset, v ); }

    static std::int16_t get_avl_height( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->avl_height();
    }
    static void set_avl_height_of( void* raw_blk, std::int16_t v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_avl_height( v );
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
        if ( !reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->is_free() )
        {
            assert( false && "cast_from_raw<FreeBlock>: block is not in FreeBlock state" );
            return nullptr;
        }
        return reinterpret_cast<FreeBlock*>( raw );
    }

    static const FreeBlock* cast_from_raw( const void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( !reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->is_free() )
        {
            assert( false && "cast_from_raw<FreeBlock>: block is not in FreeBlock state" );
            return nullptr;
        }
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
        if ( raw == nullptr )
            return nullptr;
        if ( reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->weight() == 0 )
        {
            assert( false && "cast_from_raw<AllocatedBlock>: block is not allocated (weight==0)" );
            return nullptr;
        }
        return reinterpret_cast<AllocatedBlock*>( raw );
    }

    static const AllocatedBlock* cast_from_raw( const void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->weight() == 0 )
        {
            assert( false && "cast_from_raw<AllocatedBlock>: block is not allocated (weight==0)" );
            return nullptr;
        }
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

#include <cstddef>
#include <cstdint>
#include <limits>

namespace pmm
{
namespace detail
{

template <typename AT> inline bool validate_block_index( std::size_t total_size, typename AT::index_type idx ) noexcept
{
    if ( idx == AT::no_block )
        return false;
    std::size_t byte_off = static_cast<std::size_t>( idx ) * AT::granule_size;
    
    if ( idx != 0 && byte_off / AT::granule_size != static_cast<std::size_t>( idx ) )
        return false;
    if ( byte_off + sizeof( pmm::Block<AT> ) > total_size )
        return false;
    return true;
}

template <typename AT>
inline bool validate_user_ptr( const std::uint8_t* base, std::size_t total_size, const void* ptr,
                               std::size_t min_user_offset ) noexcept
{
    if ( ptr == nullptr || base == nullptr )
        return false;
    if ( min_user_offset < sizeof( pmm::Block<AT> ) )
        return false;
    if ( total_size < min_user_offset )
        return false;
    const auto*          raw_ptr   = static_cast<const std::uint8_t*>( ptr );
    const std::uintptr_t raw_addr  = reinterpret_cast<std::uintptr_t>( raw_ptr );
    const std::uintptr_t base_addr = reinterpret_cast<std::uintptr_t>( base );
    if ( raw_addr < base_addr )
        return false;
    const std::size_t byte_off = static_cast<std::size_t>( raw_addr - base_addr );
    
    if ( byte_off >= total_size )
        return false;
    
    if ( byte_off < min_user_offset )
        return false;
    
    static constexpr std::size_t kBlockSize = sizeof( pmm::Block<AT> );
    std::size_t                  cand_off   = byte_off - kBlockSize;
    if ( cand_off % AT::granule_size != 0 )
        return false;
    return true;
}

template <typename AT> inline bool validate_link_index( std::size_t total_size, typename AT::index_type idx ) noexcept
{
    if ( idx == AT::no_block )
        return true; 
    return validate_block_index<AT>( total_size, idx );
}

template <typename AT>
inline void validate_block_header_full( const std::uint8_t* base, std::size_t total_size, typename AT::index_type idx,
                                        VerifyResult& result ) noexcept
{
    using BlockState = pmm::BlockStateBase<AT>;
    using index_type = typename AT::index_type;

    if ( !validate_block_index<AT>( total_size, idx ) )
    {
        result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                    static_cast<std::uint64_t>( idx ), 0, 0 );
        return;
    }

    const void* blk_raw = base + static_cast<std::size_t>( idx ) * AT::granule_size;

    BlockState::verify_state( blk_raw, idx, result );

    index_type next = BlockState::get_next_offset( blk_raw );
    if ( next != AT::no_block && !validate_block_index<AT>( total_size, next ) )
    {
        result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                    static_cast<std::uint64_t>( idx ), static_cast<std::uint64_t>( AT::no_block ),
                    static_cast<std::uint64_t>( next ) );
    }

    index_type prev = BlockState::get_prev_offset( blk_raw );
    if ( prev != AT::no_block && !validate_block_index<AT>( total_size, prev ) )
    {
        result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                    static_cast<std::uint64_t>( idx ), static_cast<std::uint64_t>( AT::no_block ),
                    static_cast<std::uint64_t>( prev ) );
    }

    std::uint16_t nt = BlockState::get_node_type( blk_raw );
    if ( nt != pmm::kNodeReadWrite && nt != pmm::kNodeReadOnly )
    {
        result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                    static_cast<std::uint64_t>( idx ), 0, static_cast<std::uint64_t>( nt ) );
    }

    index_type w = BlockState::get_weight( blk_raw );
    if ( w > 0 )
    {
        static constexpr std::size_t kBlkHdrBytes = sizeof( pmm::Block<AT> );
        std::size_t                  blk_byte_off = static_cast<std::size_t>( idx ) * AT::granule_size;
        std::size_t                  data_bytes   = static_cast<std::size_t>( w ) * AT::granule_size;
        if ( blk_byte_off + kBlkHdrBytes + data_bytes > total_size )
        {
            result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( idx ), total_size,
                        static_cast<std::uint64_t>( blk_byte_off + kBlkHdrBytes + data_bytes ) );
        }
    }
}

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

enum class PmmError : std::uint8_t
{
    Ok              = 0,  
    NotInitialized  = 1,  
    InvalidSize     = 2,  
    Overflow        = 3,  
    OutOfMemory     = 4,  
    ExpandFailed    = 5,  
    InvalidMagic    = 6,  
    CrcMismatch     = 7,  
    SizeMismatch    = 8,  
    GranuleMismatch = 9,  
    BackendError    = 10, 
    InvalidPointer  = 11, 
    BlockLocked     = 12, 
};

inline constexpr std::size_t kGranuleSize = 16;
static_assert( ( kGranuleSize & ( kGranuleSize - 1 ) ) == 0, "kGranuleSize must be a power of 2 " );
static_assert( kGranuleSize == pmm::DefaultAddressTraits::granule_size,
               "kGranuleSize must match DefaultAddressTraits::granule_size " );

inline constexpr std::uint64_t kMagic = 0x504D4D5F56303938ULL; 

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

inline std::uint32_t crc32_accumulate_byte( std::uint32_t crc, std::uint8_t byte ) noexcept
{
    crc ^= byte;
    for ( int bit = 0; bit < 8; ++bit )
        crc = ( crc >> 1 ) ^ ( 0xEDB88320U & ( ~( crc & 1U ) + 1U ) );
    return crc;
}

inline std::uint32_t compute_crc32( const std::uint8_t* data, std::size_t length ) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for ( std::size_t i = 0; i < length; ++i )
        crc = crc32_accumulate_byte( crc, data[i] );
    return crc ^ 0xFFFFFFFFU;
}

static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32, "Block<DefaultAddressTraits> must be 32 bytes " );
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) % kGranuleSize == 0,
               "Block<DefaultAddressTraits> must be granule-aligned " );

static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) ==
                   sizeof( pmm::TreeNode<pmm::DefaultAddressTraits> ) + 2 * sizeof( std::uint32_t ),
               "Block<DefaultAddressTraits> must have TreeNode + 2 index_type list fields " );

static_assert( sizeof( pmm::TreeNode<pmm::DefaultAddressTraits> ) == 5 * sizeof( std::uint32_t ) + 4,
               "TreeNode<DefaultAddressTraits> must be 24 bytes " );

inline constexpr std::uint32_t kNoBlock = 0xFFFFFFFFU; 
static_assert( kNoBlock == pmm::DefaultAddressTraits::no_block, "kNoBlock must match DefaultAddressTraits::no_block " );

template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kNoBlock_v = AddressTraitsT::no_block;

template <typename AddressTraitsT>
inline constexpr typename AddressTraitsT::index_type kNullIdx_v = static_cast<typename AddressTraitsT::index_type>( 0 );

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
    std::uint32_t crc32;              
    index_type    root_offset;        
};

static_assert( sizeof( ManagerHeader<DefaultAddressTraits> ) == 64,
               "ManagerHeader<DefaultAddressTraits> must be exactly 64 bytes " );
static_assert( sizeof( ManagerHeader<DefaultAddressTraits> ) % kGranuleSize == 0,
               "ManagerHeader<DefaultAddressTraits> must be granule-aligned " );

template <typename AddressTraitsT>
inline std::uint32_t compute_image_crc32( const std::uint8_t* data, std::size_t length ) noexcept
{
    
    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<AddressTraitsT> );
    constexpr std::size_t kCrcOffset = kHdrOffset + offsetof( ManagerHeader<AddressTraitsT>, crc32 );
    constexpr std::size_t kCrcSize   = sizeof( std::uint32_t );
    constexpr std::size_t kAfterCrc  = kCrcOffset + kCrcSize;

    std::uint32_t crc = 0xFFFFFFFFU;
    for ( std::size_t i = 0; i < kCrcOffset && i < length; ++i )
        crc = crc32_accumulate_byte( crc, data[i] );
    
    for ( std::size_t i = 0; i < kCrcSize; ++i )
        crc = crc32_accumulate_byte( crc, 0x00U );
    
    for ( std::size_t i = kAfterCrc; i < length; ++i )
        crc = crc32_accumulate_byte( crc, data[i] );
    return crc ^ 0xFFFFFFFFU;
}

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

template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline pmm::Block<AddressTraitsT>* block_at_checked( std::uint8_t* base, std::size_t total_size,
                                                     typename AddressTraitsT::index_type idx ) noexcept
{
    if ( !validate_block_index<AddressTraitsT>( total_size, idx ) )
        return nullptr;
    return reinterpret_cast<pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                     AddressTraitsT::granule_size );
}

template <typename AddressTraitsT = pmm::DefaultAddressTraits>
inline const pmm::Block<AddressTraitsT>* block_at_checked( const std::uint8_t* base, std::size_t total_size,
                                                           typename AddressTraitsT::index_type idx ) noexcept
{
    if ( !validate_block_index<AddressTraitsT>( total_size, idx ) )
        return nullptr;
    return reinterpret_cast<const pmm::Block<AddressTraitsT>*>( base + static_cast<std::size_t>( idx ) *
                                                                           AddressTraitsT::granule_size );
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

template <typename AddressTraitsT>
inline void* resolve_granule_ptr( std::uint8_t* base, typename AddressTraitsT::index_type idx ) noexcept
{
    if ( idx == static_cast<typename AddressTraitsT::index_type>( 0 ) )
        return nullptr;
    return base + static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size;
}

template <typename AddressTraitsT>
inline void* resolve_granule_ptr_checked( std::uint8_t* base, std::size_t total_size,
                                          typename AddressTraitsT::index_type idx ) noexcept
{
    if ( idx == static_cast<typename AddressTraitsT::index_type>( 0 ) )
        return nullptr;
    std::size_t byte_off = static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size;
    if ( byte_off >= total_size )
        return nullptr;
    return base + byte_off;
}

template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type ptr_to_granule_idx( const std::uint8_t* base, const void* ptr ) noexcept
{
    using IndexT         = typename AddressTraitsT::index_type;
    std::size_t byte_off = static_cast<const std::uint8_t*>( ptr ) - base;
    return static_cast<IndexT>( byte_off / AddressTraitsT::granule_size );
}

template <typename AddressTraitsT>
inline typename AddressTraitsT::index_type ptr_to_granule_idx_checked( const std::uint8_t* base, std::size_t total_size,
                                                                       const void* ptr ) noexcept
{
    using IndexT = typename AddressTraitsT::index_type;
    if ( ptr == nullptr || base == nullptr )
        return AddressTraitsT::no_block;
    const auto* raw = static_cast<const std::uint8_t*>( ptr );
    if ( raw < base || raw >= base + total_size )
        return AddressTraitsT::no_block;
    std::size_t byte_off = static_cast<std::size_t>( raw - base );
    if ( byte_off % AddressTraitsT::granule_size != 0 )
        return AddressTraitsT::no_block;
    std::size_t idx = byte_off / AddressTraitsT::granule_size;
    if ( idx > static_cast<std::size_t>( std::numeric_limits<IndexT>::max() ) )
        return AddressTraitsT::no_block;
    return static_cast<IndexT>( idx );
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
    static constexpr std::size_t kBlockSize = sizeof( pmm::Block<AddressTraitsT> );

    const std::size_t min_user_offset = kBlockSize + sizeof( ManagerHeader<AddressTraitsT> ) + kBlockSize;
    if ( !validate_user_ptr<AddressTraitsT>( base, total_size, ptr, min_user_offset ) )
        return nullptr;

    std::uint8_t* cand_addr = static_cast<std::uint8_t*>( ptr ) - kBlockSize;
    if ( BlockState::get_weight( cand_addr ) == 0 )
        return nullptr;
    return reinterpret_cast<pmm::Block<AddressTraitsT>*>( cand_addr );
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

#include <cstddef>
#include <cstdint>

namespace pmm
{
namespace detail
{

template <typename PPtr> static constexpr auto pptr_no_block() noexcept
{
    return PPtr::manager_type::address_traits::no_block;
}

template <typename PPtr> static PPtr pptr_make( PPtr , typename PPtr::index_type idx ) noexcept
{
    return PPtr( idx );
}

template <typename PPtr> static PPtr pptr_get_left( PPtr p ) noexcept
{
    auto idx = p.tree_node().get_left();
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : pptr_make( p, idx );
}

template <typename PPtr> static PPtr pptr_get_right( PPtr p ) noexcept
{
    auto idx = p.tree_node().get_right();
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : pptr_make( p, idx );
}

template <typename PPtr> static PPtr pptr_get_parent( PPtr p ) noexcept
{
    auto idx = p.tree_node().get_parent();
    return ( idx == pptr_no_block<PPtr>() ) ? PPtr() : pptr_make( p, idx );
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

struct AvlUpdateHeightOnly
{
    template <typename PPtr> void operator()( PPtr p ) const noexcept { avl_update_height( p ); }
};

template <typename PPtr, typename IndexType, typename NodeUpdateFn = AvlUpdateHeightOnly>
static PPtr avl_rotate_right( PPtr y, IndexType& root_idx, NodeUpdateFn update_node = {} ) noexcept
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

    update_node( y );
    update_node( x );
    return x;
}

template <typename PPtr, typename IndexType, typename NodeUpdateFn = AvlUpdateHeightOnly>
static PPtr avl_rotate_left( PPtr x, IndexType& root_idx, NodeUpdateFn update_node = {} ) noexcept
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

    update_node( x );
    update_node( y );
    return y;
}

template <typename PPtr, typename IndexType, typename NodeUpdateFn = AvlUpdateHeightOnly>
static void avl_rebalance_up( PPtr p, IndexType& root_idx, NodeUpdateFn update_node = {} ) noexcept
{
    while ( !p.is_null() )
    {
        update_node( p );
        std::int16_t bf = avl_balance_factor( p );
        if ( bf > 1 )
        {
            PPtr left = pptr_get_left( p );
            if ( avl_balance_factor( left ) < 0 )
                avl_rotate_left( left, root_idx, update_node );
            p = avl_rotate_right( p, root_idx, update_node );
        }
        else if ( bf < -1 )
        {
            PPtr right = pptr_get_right( p );
            if ( avl_balance_factor( right ) > 0 )
                avl_rotate_right( right, root_idx, update_node );
            p = avl_rotate_left( p, root_idx, update_node );
        }
        p = pptr_get_parent( p );
    }
}

template <typename PPtr> static PPtr avl_min_node( PPtr p ) noexcept
{
    while ( !p.is_null() )
    {
        PPtr left = pptr_get_left( p );
        if ( left.is_null() )
            break;
        p = left;
    }
    return p;
}

template <typename PPtr> static PPtr avl_max_node( PPtr p ) noexcept
{
    while ( !p.is_null() )
    {
        PPtr right = pptr_get_right( p );
        if ( right.is_null() )
            break;
        p = right;
    }
    return p;
}

template <typename PPtr, typename IndexType, typename NodeUpdateFn = AvlUpdateHeightOnly>
static void avl_remove( PPtr target, IndexType& root_idx, NodeUpdateFn update_node = {} ) noexcept
{
    PPtr left_p  = pptr_get_left( target );
    PPtr right_p = pptr_get_right( target );
    PPtr par_p   = pptr_get_parent( target );

    if ( left_p.is_null() && right_p.is_null() )
    {
        
        avl_set_child( par_p, target, PPtr(), root_idx );
        if ( !par_p.is_null() )
            avl_rebalance_up( par_p, root_idx, update_node );
    }
    else if ( left_p.is_null() )
    {
        
        pptr_set_parent( right_p, par_p );
        avl_set_child( par_p, target, right_p, root_idx );
        if ( !par_p.is_null() )
            avl_rebalance_up( par_p, root_idx, update_node );
        else
            update_node( right_p );
    }
    else if ( right_p.is_null() )
    {
        
        pptr_set_parent( left_p, par_p );
        avl_set_child( par_p, target, left_p, root_idx );
        if ( !par_p.is_null() )
            avl_rebalance_up( par_p, root_idx, update_node );
        else
            update_node( left_p );
    }
    else
    {
        
        PPtr successor = avl_min_node( right_p );

        auto succ_par_idx = successor.tree_node().get_parent();
        PPtr succ_rgt     = pptr_get_right( successor );

        if ( succ_par_idx == target.offset() )
        {
            
            pptr_set_left( successor, left_p );
            pptr_set_parent( left_p, successor );
            
            pptr_set_parent( successor, par_p );
            avl_set_child( par_p, target, successor, root_idx );
            avl_rebalance_up( successor, root_idx, update_node );
        }
        else
        {
            
            PPtr succ_par( succ_par_idx );
            if ( !succ_rgt.is_null() )
            {
                pptr_set_parent( succ_rgt, succ_par );
                pptr_set_left( succ_par, succ_rgt );
            }
            else
            {
                pptr_set_left( succ_par, PPtr() );
            }

            pptr_set_left( successor, left_p );
            pptr_set_parent( left_p, successor );
            pptr_set_right( successor, right_p );
            pptr_set_parent( right_p, successor );
            pptr_set_parent( successor, par_p );
            avl_set_child( par_p, target, successor, root_idx );

            avl_rebalance_up( succ_par, root_idx, update_node );
        }
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

template <typename PPtr> static PPtr avl_inorder_successor( PPtr cur ) noexcept
{
    if ( cur.is_null() )
        return PPtr();

    PPtr right = pptr_get_right( cur );
    if ( !right.is_null() )
        return avl_min_node( right );

    while ( true )
    {
        PPtr parent = pptr_get_parent( cur );
        if ( parent.is_null() )
            return PPtr(); 
        PPtr parent_left = pptr_get_left( parent );
        if ( !parent_left.is_null() && parent_left.offset() == cur.offset() )
            return parent; 
        cur = parent;
    }
}

template <typename PPtr> static void avl_init_node( PPtr p ) noexcept
{
    auto& tn = p.tree_node();
    tn.set_left( pptr_no_block<PPtr>() );
    tn.set_right( pptr_no_block<PPtr>() );
    tn.set_parent( pptr_no_block<PPtr>() );
    tn.set_height( static_cast<std::int16_t>( 1 ) );
}

template <typename PPtr> static std::size_t avl_subtree_count( PPtr p ) noexcept
{
    if ( p.is_null() )
        return 0;
    return 1 + avl_subtree_count( pptr_get_left( p ) ) + avl_subtree_count( pptr_get_right( p ) );
}

template <typename PPtr, typename DeallocFn> static void avl_clear_subtree( PPtr p, DeallocFn&& dealloc ) noexcept
{
    if ( p.is_null() )
        return;
    PPtr left_p  = pptr_get_left( p );
    PPtr right_p = pptr_get_right( p );
    avl_clear_subtree( left_p, dealloc );
    avl_clear_subtree( right_p, dealloc );
    dealloc( p );
}

template <typename PPtr, typename IndexType, typename GoLeftFn, typename ResolveFn,
          typename NodeUpdateFn = AvlUpdateHeightOnly>
static void avl_insert( PPtr new_node, IndexType& root_idx, GoLeftFn&& go_left, ResolveFn&& resolve,
                        NodeUpdateFn update_node = {} ) noexcept
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

    avl_rebalance_up( parent, root_idx, update_node );
}

template <typename AddressTraitsT> struct BlockPPtrManagerTag
{
    using address_traits = AddressTraitsT;
};

template <typename AddressTraitsT> struct BlockTreeNodeProxy
{
    using index_type = typename AddressTraitsT::index_type;
    void* _blk;

    explicit BlockTreeNodeProxy( void* blk ) noexcept : _blk( blk ) {}

    index_type   get_left() const noexcept { return BlockStateBase<AddressTraitsT>::get_left_offset( _blk ); }
    index_type   get_right() const noexcept { return BlockStateBase<AddressTraitsT>::get_right_offset( _blk ); }
    index_type   get_parent() const noexcept { return BlockStateBase<AddressTraitsT>::get_parent_offset( _blk ); }
    std::int16_t get_height() const noexcept { return BlockStateBase<AddressTraitsT>::get_avl_height( _blk ); }

    void set_left( index_type v ) noexcept { BlockStateBase<AddressTraitsT>::set_left_offset_of( _blk, v ); }
    void set_right( index_type v ) noexcept { BlockStateBase<AddressTraitsT>::set_right_offset_of( _blk, v ); }
    void set_parent( index_type v ) noexcept { BlockStateBase<AddressTraitsT>::set_parent_offset_of( _blk, v ); }
    void set_height( std::int16_t v ) noexcept { BlockStateBase<AddressTraitsT>::set_avl_height_of( _blk, v ); }
};

template <typename AddressTraitsT> struct BlockPPtr
{
    using manager_type = BlockPPtrManagerTag<AddressTraitsT>;
    using index_type   = typename AddressTraitsT::index_type;

    std::uint8_t* _base;
    index_type    _idx;

    BlockPPtr() noexcept : _base( nullptr ), _idx( AddressTraitsT::no_block ) {}

    BlockPPtr( std::uint8_t* base, index_type idx ) noexcept : _base( base ), _idx( idx ) {}

    bool       is_null() const noexcept { return _idx == AddressTraitsT::no_block; }
    index_type offset() const noexcept { return _idx; }

    bool operator==( const BlockPPtr& other ) const noexcept { return _idx == other._idx; }
    bool operator!=( const BlockPPtr& other ) const noexcept { return _idx != other._idx; }

    BlockTreeNodeProxy<AddressTraitsT> tree_node() const noexcept
    {
        return BlockTreeNodeProxy<AddressTraitsT>( block_at<AddressTraitsT>( _base, _idx ) );
    }
};

template <typename AddressTraitsT>
static BlockPPtr<AddressTraitsT> pptr_make( BlockPPtr<AddressTraitsT>           source,
                                            typename AddressTraitsT::index_type idx ) noexcept
{
    return BlockPPtr<AddressTraitsT>( source._base, idx );
}

template <typename NodePPtr> struct AvlInorderIterator
{
    using index_type = typename NodePPtr::index_type;
    using value_type = typename NodePPtr::element_type;
    using pointer    = NodePPtr;

    static constexpr index_type no_block = NodePPtr::manager_type::address_traits::no_block;

    index_type _current_idx;

    AvlInorderIterator() noexcept : _current_idx( static_cast<index_type>( 0 ) ) {}
    explicit AvlInorderIterator( index_type idx ) noexcept : _current_idx( idx ) {}

    bool operator==( const AvlInorderIterator& other ) const noexcept { return _current_idx == other._current_idx; }
    bool operator!=( const AvlInorderIterator& other ) const noexcept { return _current_idx != other._current_idx; }

    NodePPtr operator*() const noexcept
    {
        if ( _current_idx == static_cast<index_type>( 0 ) || _current_idx == no_block )
            return NodePPtr();
        return NodePPtr( _current_idx );
    }

    AvlInorderIterator& operator++() noexcept
    {
        if ( _current_idx == static_cast<index_type>( 0 ) || _current_idx == no_block )
            return *this;

        NodePPtr next = avl_inorder_successor( NodePPtr( _current_idx ) );
        _current_idx  = next.is_null() ? static_cast<index_type>( 0 ) : next.offset();
        return *this;
    }
};

} 
} 

#include <concepts>
#include <cstdint>
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

template <typename AddressTraitsT = DefaultAddressTraits> struct AvlFreeTree
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;
    using BlockT         = Block<AddressTraitsT>;
    using BlockState     = BlockStateBase<AddressTraitsT>;
    using BPPtr          = detail::BlockPPtr<AddressTraitsT>;

    static constexpr const char* kForestDomainName = "system/free_tree";

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
            parent             = cur;
            const void* n      = detail::block_at<AddressTraitsT>( base, cur );
            index_type  n_next = BlockState::get_next_offset( n );
            index_type  n_gran = ( n_next != AddressTraitsT::no_block ) ? ( n_next - cur ) : ( total_gran - cur );
            
            bool smaller = ( blk_gran < n_gran ) || ( blk_gran == n_gran && blk_idx < cur );
            go_left      = smaller;
            cur          = smaller ? BlockState::get_left_offset( n ) : BlockState::get_right_offset( n );
        }
        BlockState::set_parent_offset_of( blk, parent );
        if ( go_left )
            BlockState::set_left_offset_of( detail::block_at<AddressTraitsT>( base, parent ), blk_idx );
        else
            BlockState::set_right_offset_of( detail::block_at<AddressTraitsT>( base, parent ), blk_idx );
        
        detail::avl_rebalance_up( BPPtr( base, parent ), hdr->free_tree_root );
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
            
            BPPtr      succ        = detail::avl_min_node( BPPtr( base, right ) );
            index_type succ_idx    = succ.offset();
            void*      succ_raw    = detail::block_at<AddressTraitsT>( base, succ_idx );
            index_type succ_parent = BlockState::get_parent_offset( succ_raw );
            index_type succ_right  = BlockState::get_right_offset( succ_raw );

            if ( succ_parent != blk_idx )
            {
                set_child( base, hdr, succ_parent, succ_idx, succ_right );
                if ( succ_right != AddressTraitsT::no_block )
                    BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, succ_right ),
                                                      succ_parent );
                BlockState::set_right_offset_of( succ_raw, right );
                BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, right ), succ_idx );
                rebal = succ_parent;
            }
            else
            {
                rebal = succ_idx;
            }
            BlockState::set_left_offset_of( succ_raw, left );
            BlockState::set_parent_offset_of( detail::block_at<AddressTraitsT>( base, left ), succ_idx );
            BlockState::set_parent_offset_of( succ_raw, parent );
            set_child( base, hdr, parent, blk_idx, succ_idx );
            
            detail::avl_update_height( BPPtr( base, succ_idx ) );
        }
        BlockState::set_left_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_right_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_parent_offset_of( blk, AddressTraitsT::no_block );
        BlockState::set_avl_height_of( blk, 0 );
        
        detail::avl_rebalance_up( BPPtr( base, rebal ), hdr->free_tree_root );
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
};

static_assert( FreeBlockTreePolicyForTraitsConcept<AvlFreeTree<DefaultAddressTraits>, DefaultAddressTraits>,
               "AvlFreeTree<DefaultAddressTraits> must satisfy FreeBlockTreePolicyForTraitsConcept" );

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
        
        static constexpr std::size_t kMinInitialSize = 4096;
        std::size_t                  growth =
            ( _size > 0 ) ? ( _size / 4 + additional_bytes ) : std::max( additional_bytes, kMinInitialSize );
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
#include <cstdio>

namespace pmm
{
namespace logging
{

struct NoLogging
{
    
    static void on_allocation_failure( std::size_t , PmmError  ) noexcept {}

    static void on_expand( std::size_t , std::size_t  ) noexcept {}

    static void on_corruption_detected( PmmError  ) noexcept {}

    static void on_create( std::size_t  ) noexcept {}

    static void on_destroy() noexcept {}

    static void on_load() noexcept {}
};

struct StderrLogging
{
    static void on_allocation_failure( std::size_t user_size, PmmError err ) noexcept
    {
        std::fprintf( stderr, "[pmm] allocation_failure: size=%zu error=%d\n", user_size, static_cast<int>( err ) );
    }

    static void on_expand( std::size_t old_size, std::size_t new_size ) noexcept
    {
        std::fprintf( stderr, "[pmm] expand: %zu -> %zu\n", old_size, new_size );
    }

    static void on_corruption_detected( PmmError err ) noexcept
    {
        std::fprintf( stderr, "[pmm] corruption_detected: error=%d\n", static_cast<int>( err ) );
    }

    static void on_create( std::size_t initial_size ) noexcept
    {
        std::fprintf( stderr, "[pmm] create: size=%zu\n", initial_size );
    }

    static void on_destroy() noexcept { std::fprintf( stderr, "[pmm] destroy\n" ); }

    static void on_load() noexcept { std::fprintf( stderr, "[pmm] load\n" ); }
};

} 
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
          std::size_t MaxMemoryGB = 64, typename LoggingPolicyT = logging::NoLogging>
struct BasicConfig
{
    static_assert( ValidPmmAddressTraits<AddressTraitsT>,
                   "BasicConfig: AddressTraitsT must satisfy ValidPmmAddressTraits" );

    using address_traits                          = AddressTraitsT;
    using storage_backend                         = HeapStorage<AddressTraitsT>;
    using free_block_tree                         = AvlFreeTree<AddressTraitsT>;
    using lock_policy                             = LockPolicyT;
    using logging_policy                          = LoggingPolicyT;
    static constexpr std::size_t granule_size     = AddressTraitsT::granule_size;
    static constexpr std::size_t max_memory_gb    = MaxMemoryGB;
    static constexpr std::size_t grow_numerator   = GrowNum;
    static constexpr std::size_t grow_denominator = GrowDen;
};

template <typename AddressTraitsT, std::size_t BufferSize, std::size_t GrowNum = 3, std::size_t GrowDen = 2>
struct StaticConfig
{
    static_assert( ValidPmmAddressTraits<AddressTraitsT>,
                   "StaticConfig: AddressTraitsT must satisfy ValidPmmAddressTraits" );

    using address_traits                          = AddressTraitsT;
    using storage_backend                         = StaticStorage<BufferSize, AddressTraitsT>;
    using free_block_tree                         = AvlFreeTree<AddressTraitsT>;
    using lock_policy                             = config::NoLock;
    using logging_policy                          = logging::NoLogging;
    static constexpr std::size_t granule_size     = AddressTraitsT::granule_size;
    static constexpr std::size_t max_memory_gb    = 0; 
    static constexpr std::size_t grow_numerator   = GrowNum;
    static constexpr std::size_t grow_denominator = GrowDen;
};

template <std::size_t BufferSize = 1024> using SmallEmbeddedStaticConfig = StaticConfig<SmallAddressTraits, BufferSize>;

template <std::size_t BufferSize = 4096> using EmbeddedStaticConfig = StaticConfig<DefaultAddressTraits, BufferSize>;

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
#include <limits>

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
        index_type data_gran = detail::bytes_to_granules_t<AddressTraitsT>( user_size );

        if ( data_gran > std::numeric_limits<index_type>::max() - kBlkHdrGran )
            return nullptr; 

        index_type needed_gran  = kBlkHdrGran + data_gran;
        index_type min_rem_gran = kBlkHdrGran + 1;

        bool can_split = false;
        if ( needed_gran <= std::numeric_limits<index_type>::max() - min_rem_gran )
            can_split = ( blk_total_gran >= needed_gran + min_rem_gran );

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

            BlockState::recover_state( blk_ptr, idx );

            if ( BlockState::get_weight( blk_ptr ) == 0 ) 
            {
                
                BlockState::reset_avl_fields_of( blk_ptr );
                FreeBlockTreeT::insert( base, hdr, idx );
            }
            
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

    static void verify_linked_list( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                    VerifyResult& result ) noexcept
    {
        index_type idx  = hdr->first_block_offset;
        index_type prev = AddressTraitsT::no_block;
        while ( idx != AddressTraitsT::no_block )
        {
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr     = detail::block_at<AddressTraitsT>( base, idx );
            index_type  stored_prev = BlockState::get_prev_offset( blk_ptr );
            if ( stored_prev != prev )
            {
                result.add( ViolationType::PrevOffsetMismatch, DiagnosticAction::NoAction,
                            static_cast<std::uint64_t>( idx ), static_cast<std::uint64_t>( prev ),
                            static_cast<std::uint64_t>( stored_prev ) );
            }
            prev                   = idx;
            index_type next_offset = BlockState::get_next_offset( blk_ptr );
            idx                    = next_offset;
        }
    }

    static void verify_counters( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                 VerifyResult& result ) noexcept
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
        if ( hdr->block_count != block_count || hdr->free_count != free_count || hdr->alloc_count != alloc_count ||
             hdr->used_size != used_gran )
        {
            result.add( ViolationType::CounterMismatch, DiagnosticAction::NoAction, 0,
                        static_cast<std::uint64_t>( block_count ), static_cast<std::uint64_t>( hdr->block_count ) );
        }
    }

    static void verify_block_states( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                     VerifyResult& result ) noexcept
    {
        index_type idx = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            BlockState::verify_state( blk_ptr, idx, result );
            idx = BlockState::get_next_offset( blk_ptr );
        }
    }

    static void verify_free_tree( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                  VerifyResult& result ) noexcept
    {
        std::size_t expected_count = 0;
        index_type  idx            = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            if ( BlockState::get_weight( blk_ptr ) == 0 )
                ++expected_count;
            idx = BlockState::get_next_offset( blk_ptr );
        }

        const bool root_present = ( hdr->free_tree_root != AddressTraitsT::no_block );
        if ( expected_count == 0 )
        {
            if ( root_present )
            {
                result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction, 0, 0,
                            static_cast<std::uint64_t>( hdr->free_tree_root ) );
            }
            return;
        }
        if ( !root_present )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction, 0,
                        static_cast<std::uint64_t>( expected_count ),
                        static_cast<std::uint64_t>( hdr->free_tree_root ) );
            return;
        }
        if ( !detail::validate_block_index<AddressTraitsT>( hdr->total_size, hdr->free_tree_root ) )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( hdr->free_tree_root ), 1, 0 );
            return;
        }
        const void* root = detail::block_at<AddressTraitsT>( base, hdr->free_tree_root );
        if ( BlockState::get_weight( root ) != 0 || BlockState::get_parent_offset( root ) != AddressTraitsT::no_block )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( hdr->free_tree_root ), 0,
                        static_cast<std::uint64_t>( BlockState::get_parent_offset( root ) ) );
        }

        std::size_t visited_count = 0;
        verify_free_tree_node( base, hdr, hdr->free_tree_root, AddressTraitsT::no_block, {}, false, {}, false,
                               expected_count, visited_count, result );

        idx = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            if ( BlockState::get_weight( blk_ptr ) == 0 &&
                 !free_tree_contains( base, hdr, hdr->free_tree_root, idx, expected_count ) )
            {
                result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction, static_cast<std::uint64_t>( idx ),
                            1, 0 );
            }
            idx = BlockState::get_next_offset( blk_ptr );
        }
        if ( visited_count != expected_count )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction, 0,
                        static_cast<std::uint64_t>( expected_count ), static_cast<std::uint64_t>( visited_count ) );
        }
    }

    static index_type free_tree_block_granules( const std::uint8_t*                          base,
                                                const detail::ManagerHeader<AddressTraitsT>* hdr,
                                                index_type                                   block_idx ) noexcept
    {
        const void* n      = detail::block_at<AddressTraitsT>( base, block_idx );
        index_type  n_next = BlockState::get_next_offset( n );
        index_type  total  = detail::byte_off_to_idx_t<AddressTraitsT>( hdr->total_size );
        return ( n_next != AddressTraitsT::no_block ) ? static_cast<index_type>( n_next - block_idx )
                                                      : static_cast<index_type>( total - block_idx );
    }

    static bool free_tree_less_key( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                    index_type a, index_type b ) noexcept
    {
        index_type a_gran = free_tree_block_granules( base, hdr, a );
        index_type b_gran = free_tree_block_granules( base, hdr, b );
        return ( a_gran < b_gran ) || ( a_gran == b_gran && a < b );
    }

    static bool free_tree_contains( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                    index_type node_idx, index_type target, std::size_t step_limit ) noexcept
    {
        while ( node_idx != AddressTraitsT::no_block && step_limit-- > 0 )
        {
            if ( !detail::validate_block_index<AddressTraitsT>( hdr->total_size, node_idx ) )
                return false;
            const void* node = detail::block_at<AddressTraitsT>( base, node_idx );
            if ( BlockState::get_weight( node ) != 0 )
                return false;
            if ( node_idx == target )
                return true;
            node_idx = free_tree_less_key( base, hdr, target, node_idx ) ? BlockState::get_left_offset( node )
                                                                         : BlockState::get_right_offset( node );
        }
        return false;
    }

    static std::int16_t verify_free_tree_node( const std::uint8_t*                          base,
                                               const detail::ManagerHeader<AddressTraitsT>* hdr, index_type node_idx,
                                               index_type parent, index_type lower, bool has_lower, index_type upper,
                                               bool has_upper, std::size_t expected_count, std::size_t& visited_count,
                                               VerifyResult& result ) noexcept
    {
        if ( node_idx == AddressTraitsT::no_block )
            return 0;
        if ( visited_count >= expected_count )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), 1, 2 );
            return 0;
        }
        ++visited_count;

        if ( !detail::validate_block_index<AddressTraitsT>( hdr->total_size, node_idx ) )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction, static_cast<std::uint64_t>( parent ),
                        0, static_cast<std::uint64_t>( node_idx ) );
            return 0;
        }

        const void* node = detail::block_at<AddressTraitsT>( base, node_idx );
        if ( BlockState::get_weight( node ) != 0 )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), 0,
                        static_cast<std::uint64_t>( BlockState::get_weight( node ) ) );
            return 0;
        }
        if ( BlockState::get_parent_offset( node ) != parent )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), static_cast<std::uint64_t>( parent ),
                        static_cast<std::uint64_t>( BlockState::get_parent_offset( node ) ) );
        }
        if ( has_lower && !free_tree_less_key( base, hdr, lower, node_idx ) )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), static_cast<std::uint64_t>( lower ),
                        static_cast<std::uint64_t>( node_idx ) );
        }
        if ( has_upper && !free_tree_less_key( base, hdr, node_idx, upper ) )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), static_cast<std::uint64_t>( node_idx ),
                        static_cast<std::uint64_t>( upper ) );
        }

        index_type left  = BlockState::get_left_offset( node );
        index_type right = BlockState::get_right_offset( node );

        std::int16_t left_h     = verify_free_tree_node( base, hdr, left, node_idx, lower, has_lower, node_idx, true,
                                                         expected_count, visited_count, result );
        std::int16_t right_h    = verify_free_tree_node( base, hdr, right, node_idx, node_idx, true, upper, has_upper,
                                                         expected_count, visited_count, result );
        std::int16_t expected_h = static_cast<std::int16_t>( 1 + ( left_h > right_h ? left_h : right_h ) );
        std::int16_t stored_h   = BlockState::get_avl_height( node );
        if ( stored_h != expected_h || left_h - right_h > 1 || right_h - left_h > 1 )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), static_cast<std::uint64_t>( expected_h ),
                        static_cast<std::uint64_t>( stored_h ) );
        }
        return expected_h;
    }

    static void realloc_shrink( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type blk_idx,
                                void* blk_raw, index_type old_data_gran, index_type new_data_gran ) noexcept
    {
        static constexpr index_type kBlkHdrGran = detail::kBlockHeaderGranules_t<AddressTraitsT>;
        index_type                  remainder   = old_data_gran - new_data_gran;
        if ( remainder >= kBlkHdrGran + 1 )
        {
            index_type new_free_idx = blk_idx + kBlkHdrGran + new_data_gran;
            void*      new_free_blk = detail::block_at<AddressTraitsT>( base, new_free_idx );
            index_type old_next     = BlockState::get_next_offset( blk_raw );
            auto*      old_next_blk =
                ( old_next != AddressTraitsT::no_block ) ? detail::block_at<AddressTraitsT>( base, old_next ) : nullptr;
            std::memset( new_free_blk, 0, sizeof( BlockT ) );
            BlockState::init_fields( new_free_blk, blk_idx, old_next, 1, 0, 0 );
            BlockState::set_next_offset_of( blk_raw, new_free_idx );
            if ( old_next_blk != nullptr )
                BlockState::set_prev_offset_of( old_next_blk, new_free_idx );
            else
                hdr->last_block_offset = new_free_idx;
            BlockState::set_weight_of( blk_raw, new_data_gran );
            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += kBlkHdrGran;
            hdr->used_size -= ( old_data_gran - new_data_gran );
            coalesce( base, hdr, new_free_idx );
        }
        else
        {
            BlockState::set_weight_of( blk_raw, new_data_gran );
            hdr->used_size -= ( old_data_gran - new_data_gran );
        }
    }

    static bool realloc_grow( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type blk_idx,
                              void* blk_raw, index_type old_data_gran, index_type new_data_gran ) noexcept
    {
        static constexpr index_type kBlkHdrGran = detail::kBlockHeaderGranules_t<AddressTraitsT>;
        index_type                  next_idx    = BlockState::get_next_offset( blk_raw );
        if ( next_idx == AddressTraitsT::no_block )
            return false;
        void* next_blk = detail::block_at<AddressTraitsT>( base, next_idx );
        if ( BlockState::get_weight( next_blk ) != 0 )
            return false;
        index_type next_total =
            detail::block_total_granules( base, hdr, detail::block_at<AddressTraitsT>( base, next_idx ) );
        index_type available = old_data_gran + next_total;
        if ( available < new_data_gran )
            return false;
        FreeBlockTreeT::remove( base, hdr, next_idx );
        index_type next_next = BlockState::get_next_offset( next_blk );
        BlockState::set_next_offset_of( blk_raw, next_next );
        if ( next_next != AddressTraitsT::no_block )
            BlockState::set_prev_offset_of( detail::block_at<AddressTraitsT>( base, next_next ), blk_idx );
        else
            hdr->last_block_offset = blk_idx;
        std::memset( next_blk, 0, sizeof( BlockT ) );
        hdr->block_count--;
        hdr->free_count--;
        if ( hdr->used_size >= kBlkHdrGran )
            hdr->used_size -= kBlkHdrGran;
        index_type rem = available - new_data_gran;
        if ( rem >= kBlkHdrGran + 1 )
        {
            index_type rem_idx      = blk_idx + kBlkHdrGran + new_data_gran;
            void*      rem_blk      = detail::block_at<AddressTraitsT>( base, rem_idx );
            index_type blk_new_next = BlockState::get_next_offset( blk_raw );
            std::memset( rem_blk, 0, sizeof( BlockT ) );
            BlockState::init_fields( rem_blk, blk_idx, blk_new_next, 1, 0, 0 );
            BlockState::set_next_offset_of( blk_raw, rem_idx );
            if ( blk_new_next != AddressTraitsT::no_block )
                BlockState::set_prev_offset_of( detail::block_at<AddressTraitsT>( base, blk_new_next ), rem_idx );
            else
                hdr->last_block_offset = rem_idx;
            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += kBlkHdrGran;
            FreeBlockTreeT::insert( base, hdr, rem_idx );
        }
        BlockState::set_weight_of( blk_raw, new_data_gran );
        hdr->used_size += ( new_data_gran - old_data_gran );
        return true;
    }
};

using DefaultAllocatorPolicy = AllocatorPolicy<AvlFreeTree<DefaultAddressTraits>, DefaultAddressTraits>;

} 

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm::detail
{

inline constexpr std::size_t kForestDomainNameCapacity = 48;
inline constexpr std::size_t kMaxForestDomains         = 32;

inline constexpr const char*   kSystemDomainFreeTree         = "system/free_tree";
inline constexpr const char*   kSystemDomainSymbols          = "system/symbols";
inline constexpr const char*   kSystemDomainRegistry         = "system/domain_registry";
inline constexpr const char*   kSystemTypeForestRegistry     = "type/forest_registry";
inline constexpr const char*   kSystemTypeForestDomainRecord = "type/forest_domain_record";
inline constexpr const char*   kSystemTypePstringview        = "type/pstringview";
inline constexpr const char*   kServiceNameLegacyRoot        = "service/legacy_root";
inline constexpr const char*   kServiceNameDomainRoot        = "service/domain_root";
inline constexpr const char*   kServiceNameDomainSymbol      = "service/domain_symbol";
inline constexpr std::uint32_t kForestRegistryMagic          = 0x50465247U; 
inline constexpr std::uint16_t kForestRegistryVersion        = 1;
inline constexpr std::uint8_t  kForestBindingDirectRoot      = 0;
inline constexpr std::uint8_t  kForestBindingFreeTree        = 1;
inline constexpr std::uint8_t  kForestDomainFlagSystem       = 0x01;

template <typename AddressTraitsT> struct ForestDomainRecord
{
    using index_type = typename AddressTraitsT::index_type;

    index_type    binding_id;
    index_type    root_offset;
    index_type    symbol_offset;
    std::uint8_t  binding_kind;
    std::uint8_t  flags;
    std::uint16_t reserved;
    char          name[kForestDomainNameCapacity];

    constexpr ForestDomainRecord() noexcept
        : binding_id( 0 ), root_offset( 0 ), symbol_offset( 0 ), binding_kind( kForestBindingDirectRoot ), flags( 0 ),
          reserved( 0 ), name{}
    {
    }
};

template <typename AddressTraitsT> struct ForestDomainRegistry
{
    using index_type = typename AddressTraitsT::index_type;

    std::uint32_t                      magic;
    std::uint16_t                      version;
    std::uint16_t                      domain_count;
    index_type                         legacy_root_offset;
    index_type                         next_binding_id;
    ForestDomainRecord<AddressTraitsT> domains[kMaxForestDomains];

    constexpr ForestDomainRegistry() noexcept
        : magic( kForestRegistryMagic ), version( kForestRegistryVersion ), domain_count( 0 ), legacy_root_offset( 0 ),
          next_binding_id( 1 ), domains{}
    {
    }
};

template <typename AddressTraitsT>
inline bool forest_domain_name_equals( const ForestDomainRecord<AddressTraitsT>& rec, const char* name ) noexcept
{
    if ( name == nullptr )
        return false;
    return std::strncmp( rec.name, name, kForestDomainNameCapacity ) == 0;
}

inline bool forest_domain_name_fits( const char* name ) noexcept
{
    if ( name == nullptr || name[0] == '\0' )
        return false;
    return std::strlen( name ) < kForestDomainNameCapacity;
}

template <typename AddressTraitsT>
inline bool forest_domain_name_copy( ForestDomainRecord<AddressTraitsT>& rec, const char* name ) noexcept
{
    if ( !forest_domain_name_fits( name ) )
        return false;
    std::memset( rec.name, 0, sizeof( rec.name ) );
    std::memcpy( rec.name, name, std::strlen( name ) );
    return true;
}

static_assert( std::is_trivially_copyable_v<ForestDomainRecord<DefaultAddressTraits>>,
               "ForestDomainRecord must be trivially copyable" );
static_assert( std::is_nothrow_default_constructible_v<ForestDomainRegistry<DefaultAddressTraits>>,
               "ForestDomainRegistry must be nothrow-default-constructible" );

} 

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pmm::detail
{

template <typename ManagerAccess> struct ManagerLayoutOps
{
    using address_traits  = typename ManagerAccess::address_traits;
    using free_block_tree = typename ManagerAccess::free_block_tree;
    using index_type      = typename address_traits::index_type;
    using logging_policy  = typename ManagerAccess::logging_policy;
    using storage_backend = typename ManagerAccess::storage_backend;
    using BlockState      = BlockStateBase<address_traits>;

    static bool init_layout( storage_backend& backend, std::uint8_t* base, std::size_t size ) noexcept
    {
        static constexpr index_type  kHdrBlkIdx  = 0;
        static constexpr index_type  kFreeBlkIdx = ManagerAccess::kFreeBlkIdxLayout;
        static constexpr std::size_t kGranSz     = address_traits::granule_size;

        static constexpr std::size_t kMinBlockDataSize = kGranSz;
        if ( static_cast<std::size_t>( kFreeBlkIdx ) * kGranSz + sizeof( Block<address_traits> ) + kMinBlockDataSize >
             size )
            return false;

        void* hdr_blk = base;
        std::memset( hdr_blk, 0, ManagerAccess::kBlockHdrByteSize );
        BlockState::init_fields( hdr_blk,
                                  address_traits::no_block,
                                  kFreeBlkIdx,
                                  0,
                                  ManagerAccess::kMgrHdrGranules,
                                  kHdrBlkIdx );

        ManagerHeader<address_traits>* hdr = ManagerAccess::get_header( base );
        std::memset( hdr, 0, sizeof( ManagerHeader<address_traits> ) );
        hdr->magic              = ManagerAccess::kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = kHdrBlkIdx;
        hdr->last_block_offset  = address_traits::no_block;
        hdr->free_tree_root     = address_traits::no_block;
        hdr->granule_size       = static_cast<std::uint16_t>( kGranSz );
        hdr->root_offset        = address_traits::no_block;

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
        hdr->used_size         = kFreeBlkIdx + ManagerAccess::kBlockHdrGranules;

        (void)backend;
        ManagerAccess::set_initialized();
        return true;
    }

    static bool do_expand( storage_backend& backend, bool initialized, std::size_t user_size ) noexcept
    {
        if ( !initialized )
            return false;
        std::uint8_t*                  base     = backend.base_ptr();
        ManagerHeader<address_traits>* hdr      = ManagerAccess::get_header( base );
        std::size_t                    old_size = hdr->total_size;

        static constexpr std::size_t kGranSz        = address_traits::granule_size;
        index_type                   data_gran_need = bytes_to_granules_t<address_traits>( user_size );
        if ( data_gran_need == 0 )
            data_gran_need = 1;
        std::size_t min_need = static_cast<std::size_t>( ManagerAccess::kBlockHdrGranules + data_gran_need +
                                                         ManagerAccess::kBlockHdrGranules ) *
                               kGranSz;
        std::size_t growth = old_size / 4;
        if ( growth < min_need )
            growth = min_need;

        if ( !backend.expand( growth ) )
            return false;

        std::uint8_t* new_base = backend.base_ptr();
        std::size_t   new_size = backend.total_size();
        if ( new_base == nullptr || new_size <= old_size )
            return false;

        logging_policy::on_expand( old_size, new_size );
        hdr = ManagerAccess::get_header( new_base );

        index_type  extra_idx  = byte_off_to_idx_t<address_traits>( old_size );
        std::size_t extra_size = new_size - old_size;

        void* last_blk_raw =
            ( hdr->last_block_offset != address_traits::no_block )
                ? static_cast<void*>( new_base + static_cast<std::size_t>( hdr->last_block_offset ) * kGranSz )
                : nullptr;

        if ( last_blk_raw != nullptr && BlockState::get_weight( last_blk_raw ) == 0 )
        {
            Block<address_traits>* last_blk = reinterpret_cast<Block<address_traits>*>( last_blk_raw );
            index_type             loff     = block_idx_t<address_traits>( new_base, last_blk );
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
                index_type             loff     = block_idx_t<address_traits>( new_base, last_blk );
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

#include <cstddef>
#include <limits>
#include <new>

namespace pmm
{

template <typename T, typename ManagerT> struct pallocator
{
    
    using value_type      = T;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::true_type;

    constexpr pallocator() noexcept = default;

    constexpr pallocator( const pallocator& ) noexcept = default;

    template <typename U> constexpr pallocator( const pallocator<U, ManagerT>& ) noexcept {}

    [[nodiscard]] T* allocate( std::size_t n )
    {
        if ( n == 0 )
            throw std::bad_alloc();

        if ( n > max_size() )
            throw std::bad_alloc();

        void* raw = ManagerT::allocate( n * sizeof( T ) );
        if ( raw == nullptr )
            throw std::bad_alloc();

        return static_cast<T*>( raw );
    }

    void deallocate( T* p, std::size_t  ) noexcept { ManagerT::deallocate( static_cast<void*>( p ) ); }

    std::size_t max_size() const noexcept { return ( std::numeric_limits<std::size_t>::max )() / sizeof( T ); }

    template <typename U> bool operator==( const pallocator<U, ManagerT>& ) const noexcept { return true; }

    template <typename U> bool operator!=( const pallocator<U, ManagerT>& ) const noexcept { return false; }
};

} 

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm
{

template <typename T, typename ManagerT> struct parray
{
    static_assert( std::is_trivially_copyable_v<T>, "parray<T>: T must be trivially copyable for PAP persistence" );

    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using value_type   = T;

    std::uint32_t _size;     
    std::uint32_t _capacity; 
    index_type    _data_idx; 

    parray() noexcept : _size( 0 ), _capacity( 0 ), _data_idx( detail::kNullIdx_v<typename ManagerT::address_traits> )
    {
    }

    ~parray() noexcept = default;

    std::size_t size() const noexcept { return static_cast<std::size_t>( _size ); }

    bool empty() const noexcept { return _size == 0; }

    std::size_t capacity() const noexcept { return static_cast<std::size_t>( _capacity ); }

    T* at( std::size_t i ) noexcept
    {
        if ( i >= static_cast<std::size_t>( _size ) )
            return nullptr;
        T* data = resolve_data();
        return ( data != nullptr ) ? ( data + i ) : nullptr;
    }

    const T* at( std::size_t i ) const noexcept
    {
        if ( i >= static_cast<std::size_t>( _size ) )
            return nullptr;
        const T* data = resolve_data();
        return ( data != nullptr ) ? ( data + i ) : nullptr;
    }

    T operator[]( std::size_t i ) const noexcept
    {
        const T* data = resolve_data();
        return ( data != nullptr ) ? data[i] : T{};
    }

    T* front() noexcept { return at( 0 ); }

    const T* front() const noexcept { return at( 0 ); }

    T* back() noexcept { return ( _size > 0 ) ? at( static_cast<std::size_t>( _size ) - 1 ) : nullptr; }

    const T* back() const noexcept { return ( _size > 0 ) ? at( static_cast<std::size_t>( _size ) - 1 ) : nullptr; }

    T* data() noexcept { return resolve_data(); }

    const T* data() const noexcept { return resolve_data(); }

    bool push_back( const T& value ) noexcept
    {
        if ( !ensure_capacity( _size + 1 ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;
        d[_size] = value;
        ++_size;
        return true;
    }

    void pop_back() noexcept
    {
        if ( _size > 0 )
            --_size;
    }

    bool set( std::size_t i, const T& value ) noexcept
    {
        if ( i >= static_cast<std::size_t>( _size ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;
        d[i] = value;
        return true;
    }

    bool reserve( std::size_t n ) noexcept
    {
        if ( n > static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) )
            return false;
        return ensure_capacity( static_cast<std::uint32_t>( n ) );
    }

    bool resize( std::size_t n ) noexcept
    {
        if ( n > static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) )
            return false;
        auto new_size = static_cast<std::uint32_t>( n );
        if ( new_size > _size )
        {
            if ( !ensure_capacity( new_size ) )
                return false;
            T* d = resolve_data();
            if ( d == nullptr )
                return false;
            
            std::memset( d + _size, 0, static_cast<std::size_t>( new_size - _size ) * sizeof( T ) );
        }
        _size = new_size;
        return true;
    }

    bool insert( std::size_t index, const T& value ) noexcept
    {
        if ( index > static_cast<std::size_t>( _size ) )
            return false;
        if ( !ensure_capacity( _size + 1 ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;
        
        if ( index < static_cast<std::size_t>( _size ) )
            std::memmove( d + index + 1, d + index, ( static_cast<std::size_t>( _size ) - index ) * sizeof( T ) );
        d[index] = value;
        ++_size;
        return true;
    }

    bool erase( std::size_t index ) noexcept
    {
        if ( index >= static_cast<std::size_t>( _size ) )
            return false;
        T* d = resolve_data();
        if ( d == nullptr )
            return false;
        
        if ( index + 1 < static_cast<std::size_t>( _size ) )
            std::memmove( d + index, d + index + 1, ( static_cast<std::size_t>( _size ) - index - 1 ) * sizeof( T ) );
        --_size;
        return true;
    }

    void clear() noexcept { _size = 0; }

    void free_data() noexcept
    {
        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            ManagerT::deallocate( detail::resolve_granule_ptr<typename ManagerT::address_traits>(
                ManagerT::backend().base_ptr(), _data_idx ) );
            _data_idx = detail::kNullIdx_v<typename ManagerT::address_traits>;
        }
        _size     = 0;
        _capacity = 0;
    }

    bool operator==( const parray& other ) const noexcept
    {
        if ( this == &other )
            return true;
        if ( _size != other._size )
            return false;
        if ( _size == 0 )
            return true;
        const T* a = resolve_data();
        const T* b = other.resolve_data();
        if ( a == nullptr || b == nullptr )
            return ( a == b );
        return std::memcmp( a, b, static_cast<std::size_t>( _size ) * sizeof( T ) ) == 0;
    }

    bool operator!=( const parray& other ) const noexcept { return !( *this == other ); }

  private:
    
    T* resolve_data() const noexcept
    {
        return reinterpret_cast<T*>( detail::resolve_granule_ptr<typename ManagerT::address_traits>(
            ManagerT::backend().base_ptr(), _data_idx ) );
    }

    bool ensure_capacity( std::uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return true;

        std::uint32_t new_cap = _capacity * 2;
        if ( new_cap < required )
            new_cap = required;
        if ( new_cap < 4 )
            new_cap = 4;

        std::size_t alloc_size = static_cast<std::size_t>( new_cap ) * sizeof( T );
        if ( sizeof( T ) > 0 && alloc_size / sizeof( T ) != static_cast<std::size_t>( new_cap ) )
            return false; 

        void* new_raw = ManagerT::allocate( alloc_size );
        if ( new_raw == nullptr )
            return false;

        std::uint8_t* base        = ManagerT::backend().base_ptr();
        index_type    new_dat_idx = detail::ptr_to_granule_idx<typename ManagerT::address_traits>( base, new_raw );

        if ( _size > 0 && _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            T* old_data = resolve_data();
            if ( old_data != nullptr )
                std::memcpy( new_raw, old_data, static_cast<std::size_t>( _size ) * sizeof( T ) );
        }

        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
            ManagerT::deallocate( detail::resolve_granule_ptr<typename ManagerT::address_traits>( base, _data_idx ) );

        _data_idx = new_dat_idx;
        _capacity = new_cap;
        return true;
    }
};

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

    static constexpr index_type no_block = ManagerT::address_traits::no_block;

    index_type _root_idx;

    pmap() noexcept : _root_idx( static_cast<index_type>( 0 ) ) {}

    bool empty() const noexcept { return _root_idx == static_cast<index_type>( 0 ); }

    std::size_t size() const noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
            return 0;
        return detail::avl_subtree_count( node_pptr( _root_idx ) );
    }

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

        detail::avl_init_node( new_node );

        _avl_insert( new_node );

        return new_node;
    }

    node_pptr find( const _K& key ) const noexcept { return _avl_find( key ); }

    bool contains( const _K& key ) const noexcept { return !_avl_find( key ).is_null(); }

    bool erase( const _K& key ) noexcept
    {
        node_pptr target = _avl_find( key );
        if ( target.is_null() )
            return false;

        detail::avl_remove( target, _root_idx );
        ManagerT::template deallocate_typed<node_type>( target );
        return true;
    }

    void clear() noexcept
    {
        if ( _root_idx != static_cast<index_type>( 0 ) )
            detail::avl_clear_subtree( node_pptr( _root_idx ),
                                       []( node_pptr p ) { ManagerT::template deallocate_typed<node_type>( p ); } );
        _root_idx = static_cast<index_type>( 0 );
    }

    void reset() noexcept { _root_idx = static_cast<index_type>( 0 ); }

    using iterator = detail::AvlInorderIterator<node_pptr>;

    iterator begin() const noexcept
    {
        if ( _root_idx == static_cast<index_type>( 0 ) )
            return iterator();
        node_pptr min = detail::avl_min_node( node_pptr( _root_idx ) );
        return iterator( min.offset() );
    }

    iterator end() const noexcept { return iterator( static_cast<index_type>( 0 ) ); }

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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm
{

template <typename T, typename ManagerT> struct ppool
{
    static_assert( std::is_trivially_copyable_v<T>, "ppool<T>: T must be trivially copyable for PAP persistence" );

    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;
    using value_type   = T;

    static constexpr std::size_t granule_size = ManagerT::address_traits::granule_size;

    static constexpr std::size_t min_slot_bytes =
        ( sizeof( T ) >= sizeof( index_type ) ) ? sizeof( T ) : sizeof( index_type );

    static constexpr std::size_t granules_per_slot = ( min_slot_bytes + granule_size - 1 ) / granule_size;

    static constexpr std::size_t slot_bytes = granules_per_slot * granule_size;

    static constexpr std::uint32_t default_objects_per_chunk = 64;

    index_type    _free_head_idx;     
    index_type    _chunk_head_idx;    
    std::uint32_t _objects_per_chunk; 
    std::uint32_t _total_allocated;   
    std::uint32_t _total_capacity;    

    ppool() noexcept
        : _free_head_idx( detail::kNullIdx_v<typename ManagerT::address_traits> ),
          _chunk_head_idx( detail::kNullIdx_v<typename ManagerT::address_traits> ),
          _objects_per_chunk( default_objects_per_chunk ), _total_allocated( 0 ), _total_capacity( 0 )
    {
    }

    ~ppool() noexcept = default;

    void set_objects_per_chunk( std::uint32_t n ) noexcept
    {
        if ( n >= 1 && _chunk_head_idx == detail::kNullIdx_v<typename ManagerT::address_traits> )
            _objects_per_chunk = n;
    }

    std::uint32_t allocated_count() const noexcept { return _total_allocated; }

    std::uint32_t total_capacity() const noexcept { return _total_capacity; }

    std::uint32_t free_count() const noexcept { return _total_capacity - _total_allocated; }

    bool empty() const noexcept { return _total_allocated == 0; }

    T* allocate() noexcept
    {
        
        if ( _free_head_idx == detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            if ( !allocate_chunk() )
                return nullptr;
        }

        std::uint8_t* slot_raw =
            reinterpret_cast<std::uint8_t*>( detail::resolve_granule_ptr<typename ManagerT::address_traits>(
                ManagerT::backend().base_ptr(), _free_head_idx ) );

        index_type next_free;
        std::memcpy( &next_free, slot_raw, sizeof( index_type ) );

        _free_head_idx = next_free;
        ++_total_allocated;

        std::memset( slot_raw, 0, slot_bytes );

        return reinterpret_cast<T*>( slot_raw );
    }

    void deallocate( T* ptr ) noexcept
    {
        if ( ptr == nullptr )
            return;

        std::uint8_t* slot_raw = reinterpret_cast<std::uint8_t*>( ptr );

        index_type slot_idx =
            detail::ptr_to_granule_idx<typename ManagerT::address_traits>( ManagerT::backend().base_ptr(), slot_raw );

        std::memcpy( slot_raw, &_free_head_idx, sizeof( index_type ) );

        _free_head_idx = slot_idx;
        --_total_allocated;
    }

    void free_all() noexcept
    {
        
        std::uint8_t* base      = ManagerT::backend().base_ptr();
        index_type    chunk_idx = _chunk_head_idx;
        while ( chunk_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            std::uint8_t* chunk_raw = reinterpret_cast<std::uint8_t*>(
                detail::resolve_granule_ptr<typename ManagerT::address_traits>( base, chunk_idx ) );

            index_type next_chunk;
            std::memcpy( &next_chunk, chunk_raw, sizeof( index_type ) );

            ManagerT::deallocate( chunk_raw );

            chunk_idx = next_chunk;
        }

        _free_head_idx   = detail::kNullIdx_v<typename ManagerT::address_traits>;
        _chunk_head_idx  = detail::kNullIdx_v<typename ManagerT::address_traits>;
        _total_allocated = 0;
        _total_capacity  = 0;
    }

  private:
    
    bool allocate_chunk() noexcept
    {
        std::size_t n_objects = static_cast<std::size_t>( _objects_per_chunk );

        std::size_t total_granules = 1 + n_objects * granules_per_slot;
        std::size_t alloc_size     = total_granules * granule_size;

        if ( n_objects > 0 && ( alloc_size / granule_size - 1 ) / granules_per_slot != n_objects )
            return false;

        void* raw = ManagerT::allocate( alloc_size );
        if ( raw == nullptr )
            return false;

        std::uint8_t* chunk_raw = static_cast<std::uint8_t*>( raw );
        std::uint8_t* base      = ManagerT::backend().base_ptr();

        index_type chunk_idx = detail::ptr_to_granule_idx<typename ManagerT::address_traits>( base, chunk_raw );

        std::memset( chunk_raw, 0, granule_size );
        std::memcpy( chunk_raw, &_chunk_head_idx, sizeof( index_type ) );
        _chunk_head_idx = chunk_idx;

        std::uint8_t* slots_start = chunk_raw + granule_size;

        for ( std::size_t i = 0; i < n_objects; ++i )
        {
            std::uint8_t* slot     = slots_start + i * slot_bytes;
            index_type    slot_idx = detail::ptr_to_granule_idx<typename ManagerT::address_traits>( base, slot );

            std::memset( slot, 0, slot_bytes );
            std::memcpy( slot, &_free_head_idx, sizeof( index_type ) );
            _free_head_idx = slot_idx;
        }

        _total_capacity += _objects_per_chunk;
        return true;
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

    constexpr std::size_t byte_offset() const noexcept
    {
        return static_cast<std::size_t>( _idx ) * ManagerT::address_traits::granule_size;
    }

    constexpr bool operator==( const pptr& other ) const noexcept { return _idx == other._idx; }
    constexpr bool operator!=( const pptr& other ) const noexcept { return _idx != other._idx; }

    bool operator<( const pptr& other ) const noexcept
    {
        
        static_assert(
            requires( const T& a, const T& b ) {
                { a < b } -> std::convertible_to<bool>;
            }, "pptr<T>::operator< requires T to support operator<. "
               "Provide bool operator<(const T&, const T&) or use pptr::offset() for index-based ordering." );
        
        if ( is_null() && !other.is_null() )
            return true;
        if ( !is_null() && other.is_null() )
            return false;
        if ( is_null() && other.is_null() )
            return false;
        
        return **this < *other;
    }

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

template <typename ManagerT> struct pstring
{
    using manager_type = ManagerT;
    using index_type   = typename ManagerT::index_type;

    std::uint32_t _length; 
    std::uint32_t _capacity; 
    index_type _data_idx;    

    pstring() noexcept
        : _length( 0 ), _capacity( 0 ), _data_idx( detail::kNullIdx_v<typename ManagerT::address_traits> )
    {
    }

    ~pstring() noexcept = default;

    const char* c_str() const noexcept
    {
        if ( _data_idx == detail::kNullIdx_v<typename ManagerT::address_traits> )
            return "";
        char* data = resolve_data();
        return ( data != nullptr ) ? data : "";
    }

    std::size_t size() const noexcept { return static_cast<std::size_t>( _length ); }

    bool empty() const noexcept { return _length == 0; }

    char operator[]( std::size_t i ) const noexcept
    {
        char* data = resolve_data();
        return ( data != nullptr ) ? data[i] : '\0';
    }

    bool assign( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";
        auto len = static_cast<std::uint32_t>( std::strlen( s ) );
        if ( !ensure_capacity( len ) )
            return false;
        char* data = resolve_data();
        if ( data == nullptr )
            return false;
        std::memcpy( data, s, static_cast<std::size_t>( len ) + 1 );
        _length = len;
        return true;
    }

    bool append( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";
        auto add_len = static_cast<std::uint32_t>( std::strlen( s ) );
        if ( add_len == 0 )
            return true;
        std::uint32_t new_len = _length + add_len;
        if ( new_len < _length )
            return false; 
        if ( !ensure_capacity( new_len ) )
            return false;
        char* data = resolve_data();
        if ( data == nullptr )
            return false;
        std::memcpy( data + _length, s, static_cast<std::size_t>( add_len ) + 1 );
        _length = new_len;
        return true;
    }

    void clear() noexcept
    {
        _length = 0;
        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            char* data = resolve_data();
            if ( data != nullptr )
                data[0] = '\0';
        }
    }

    void free_data() noexcept
    {
        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            ManagerT::deallocate( detail::resolve_granule_ptr<typename ManagerT::address_traits>(
                ManagerT::backend().base_ptr(), _data_idx ) );
            _data_idx = detail::kNullIdx_v<typename ManagerT::address_traits>;
        }
        _length   = 0;
        _capacity = 0;
    }

    bool operator==( const char* s ) const noexcept
    {
        if ( s == nullptr )
            return _length == 0;
        return std::strcmp( c_str(), s ) == 0;
    }

    bool operator!=( const char* s ) const noexcept { return !( *this == s ); }

    bool operator==( const pstring& other ) const noexcept
    {
        if ( this == &other )
            return true;
        if ( _length != other._length )
            return false;
        if ( _length == 0 )
            return true;
        return std::strcmp( c_str(), other.c_str() ) == 0;
    }

    bool operator!=( const pstring& other ) const noexcept { return !( *this == other ); }

    bool operator<( const pstring& other ) const noexcept { return std::strcmp( c_str(), other.c_str() ) < 0; }

  private:
    
    char* resolve_data() const noexcept
    {
        return reinterpret_cast<char*>( detail::resolve_granule_ptr<typename ManagerT::address_traits>(
            ManagerT::backend().base_ptr(), _data_idx ) );
    }

    bool ensure_capacity( std::uint32_t required ) noexcept
    {
        if ( required <= _capacity )
            return true;

        std::uint32_t new_cap = _capacity * 2;
        if ( new_cap < required )
            new_cap = required;
        if ( new_cap < 16 )
            new_cap = 16;

        std::size_t alloc_size = static_cast<std::size_t>( new_cap ) + 1;
        void*       new_raw    = ManagerT::allocate( alloc_size );
        if ( new_raw == nullptr )
            return false;

        std::uint8_t* base        = ManagerT::backend().base_ptr();
        index_type    new_dat_idx = detail::ptr_to_granule_idx<typename ManagerT::address_traits>( base, new_raw );

        if ( _length > 0 && _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
        {
            char* old_data = resolve_data();
            if ( old_data != nullptr )
                std::memcpy( new_raw, old_data, static_cast<std::size_t>( _length ) + 1 );
        }
        else
        {
            
            static_cast<char*>( new_raw )[0] = '\0';
        }

        if ( _data_idx != detail::kNullIdx_v<typename ManagerT::address_traits> )
            ManagerT::deallocate( detail::resolve_granule_ptr<typename ManagerT::address_traits>( base, _data_idx ) );

        _data_idx = new_dat_idx;
        _capacity = new_cap;
        return true;
    }
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

    std::uint32_t length; 
    char          str[1]; 

    explicit pstringview( const char* s ) noexcept : length( 0 ), str{ '\0' } { _interned = _intern( s ); }

    operator psview_pptr() const noexcept { return _interned; }

    const char* c_str() const noexcept { return str; }

    std::size_t size() const noexcept { return static_cast<std::size_t>( length ); }

    bool empty() const noexcept { return length == 0; }

    bool operator==( const char* s ) const noexcept
    {
        if ( s == nullptr )
            return length == 0;
        return std::strcmp( c_str(), s ) == 0;
    }

    bool operator==( const pstringview& other ) const noexcept
    {
        
        if ( this == &other )
            return true;
        
        if ( length != other.length )
            return false;
        return std::strcmp( str, other.str ) == 0;
    }

    bool operator!=( const char* s ) const noexcept { return !( *this == s ); }

    bool operator!=( const pstringview& other ) const noexcept { return !( *this == other ); }

    bool operator<( const pstringview& other ) const noexcept { return std::strcmp( c_str(), other.c_str() ) < 0; }

    static psview_pptr intern( const char* s ) noexcept { return _intern( s ); }

    static void reset() noexcept
    {
        if ( !ManagerT::is_initialized() )
            return;
        typename ManagerT::thread_policy::unique_lock_type lock( ManagerT::_mutex );
        ManagerT::reset_symbol_domain_unlocked();
    }

    static index_type root_index() noexcept
    {
        if ( !ManagerT::is_initialized() )
            return static_cast<index_type>( 0 );
        typename ManagerT::thread_policy::shared_lock_type lock( ManagerT::_mutex );
        return ManagerT::symbol_domain_root_offset_unlocked();
    }

    ~pstringview() = default;

  private:
    psview_pptr _interned; 

    static psview_pptr _intern( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";

        psview_pptr found = _avl_find( s );
        if ( !found.is_null() )
            return found;

        auto len = static_cast<std::uint32_t>( std::strlen( s ) );

        std::size_t alloc_size = offsetof( pstringview, str ) + static_cast<std::size_t>( len ) + 1;

        void* raw = ManagerT::allocate( alloc_size );
        if ( raw == nullptr )
            return psview_pptr();

        std::uint8_t* base = ManagerT::backend().base_ptr();
        psview_pptr   new_node( detail::ptr_to_granule_idx<typename ManagerT::address_traits>( base, raw ) );

        pstringview* obj = static_cast<pstringview*>( raw );
        obj->length      = len;
        
        std::memcpy( obj->str, s, static_cast<std::size_t>( len ) + 1 );

        detail::avl_init_node( new_node );

        ManagerT::lock_block_permanent( obj );

        _avl_insert( new_node );

        return new_node;
    }

    static psview_pptr _avl_find( const char* s ) noexcept
    {
        auto* domain = ManagerT::symbol_domain_record_unlocked();
        if ( domain == nullptr )
            return psview_pptr();
        return detail::avl_find<psview_pptr>(
            domain->root_offset,
            [&]( psview_pptr cur ) -> int
            {
                pstringview* obj = ManagerT::template resolve<pstringview>( cur );
                return ( obj != nullptr ) ? std::strcmp( s, obj->c_str() ) : 0;
            },
            []( psview_pptr p ) -> pstringview* { return ManagerT::template resolve<pstringview>( p ); } );
    }

    static void _avl_insert( psview_pptr new_node ) noexcept
    {
        auto* domain = ManagerT::symbol_domain_record_unlocked();
        if ( domain == nullptr )
            return;
        pstringview* new_obj = ManagerT::template resolve<pstringview>( new_node );
        const char*  new_str = ( new_obj != nullptr ) ? new_obj->c_str() : "";
        detail::avl_insert(
            new_node, domain->root_offset,
            [&]( psview_pptr cur ) -> bool
            {
                pstringview* obj = ManagerT::template resolve<pstringview>( cur );
                return ( obj != nullptr ) && ( std::strcmp( new_str, obj->c_str() ) < 0 );
            },
            []( psview_pptr p ) -> pstringview* { return ManagerT::template resolve<pstringview>( p ); } );
    }
};

} 

#include <type_traits>
#include <utility>

namespace pmm
{

template <typename T>
concept HasFreeData = requires( T& t ) {
    { t.free_data() } noexcept;
};

template <typename T>
concept HasFreeAll = requires( T& t ) {
    { t.free_all() } noexcept;
};

template <typename T>
concept HasPersistentCleanup = HasFreeData<T> || HasFreeAll<T>;

template <typename T, typename ManagerT> class typed_guard
{
  public:
    using pptr_type = typename ManagerT::template pptr<T>;

    explicit typed_guard( pptr_type p ) noexcept : _ptr( p ) {}

    typed_guard() noexcept = default;

    typed_guard( const typed_guard& )            = delete;
    typed_guard& operator=( const typed_guard& ) = delete;

    typed_guard( typed_guard&& other ) noexcept : _ptr( other._ptr ) { other._ptr = pptr_type(); }

    typed_guard& operator=( typed_guard&& other ) noexcept
    {
        if ( this != &other )
        {
            reset();
            _ptr       = other._ptr;
            other._ptr = pptr_type();
        }
        return *this;
    }

    ~typed_guard() { reset(); }

    void reset() noexcept
    {
        if ( !_ptr.is_null() )
        {
            cleanup( *_ptr );
            ManagerT::destroy_typed( _ptr );
            _ptr = pptr_type();
        }
    }

    pptr_type release() noexcept
    {
        pptr_type p = _ptr;
        _ptr        = pptr_type();
        return p;
    }

    T* operator->() const noexcept { return &( *_ptr ); }
    T& operator*() const noexcept { return *_ptr; }

    pptr_type get() const noexcept { return _ptr; }

    explicit operator bool() const noexcept { return !_ptr.is_null(); }

  private:
    pptr_type _ptr;

    static void cleanup( T& obj ) noexcept
    {
        if constexpr ( HasFreeData<T> )
            obj.free_data();
        else if constexpr ( HasFreeAll<T> )
            obj.free_all();
        
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

namespace detail
{
template <typename C, typename = void> struct config_logging_policy
{
    using type = logging::NoLogging; 
};
template <typename C> struct config_logging_policy<C, std::void_t<typename C::logging_policy>>
{
    using type = typename C::logging_policy;
};
} 

template <typename ConfigT = CacheManagerConfig, std::size_t InstanceId = 0> class PersistMemoryManager
{
  public:
    
    using address_traits  = typename ConfigT::address_traits;
    using storage_backend = typename ConfigT::storage_backend;
    using free_block_tree = typename ConfigT::free_block_tree;
    using thread_policy   = typename ConfigT::lock_policy;
    using logging_policy  = typename detail::config_logging_policy<ConfigT>::type;
    using allocator       = AllocatorPolicy<free_block_tree, address_traits>;
    using index_type      = typename address_traits::index_type;
    using forest_registry = detail::ForestDomainRegistry<address_traits>;
    using forest_domain   = detail::ForestDomainRecord<address_traits>;

    using manager_type = PersistMemoryManager<ConfigT, InstanceId>;

    template <typename> friend struct pstringview;

    template <typename T> using pptr = pmm::pptr<T, manager_type>;

    using pstringview = pmm::pstringview<manager_type>;

    using pstring = pmm::pstring<manager_type>;

    template <typename _K, typename _V> using pmap = pmm::pmap<_K, _V, manager_type>;

    template <typename T> using parray = pmm::parray<T, manager_type>;

    template <typename T> using pallocator = pmm::pallocator<T, manager_type>;

    template <typename T> using ppool = pmm::ppool<T, manager_type>;

    static PmmError last_error() noexcept { return _last_error; }

    static void clear_error() noexcept { _last_error = PmmError::Ok; }

    static void set_last_error( PmmError err ) noexcept { _last_error = err; }

    static bool create( std::size_t initial_size ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( initial_size < detail::kMinMemorySize )
        {
            _last_error = PmmError::InvalidSize;
            return false;
        }
        
        static constexpr std::size_t kGranSzCreate = address_traits::granule_size;
        if ( initial_size > std::numeric_limits<std::size_t>::max() - ( kGranSzCreate - 1 ) )
        {
            _last_error = PmmError::Overflow;
            return false;
        }
        std::size_t aligned = ( ( initial_size + kGranSzCreate - 1 ) / kGranSzCreate ) * kGranSzCreate;
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < aligned )
        {
            
            std::size_t additional =
                ( _backend.total_size() < aligned ) ? ( aligned - _backend.total_size() ) : aligned;
            if ( !_backend.expand( additional ) )
            {
                _last_error = PmmError::ExpandFailed;
                return false;
            }
        }
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < aligned )
        {
            _last_error = PmmError::BackendError;
            return false;
        }
        bool ok = init_layout( _backend.base_ptr(), _backend.total_size() );
        if ( ok )
            ok = bootstrap_forest_registry_unlocked();
        if ( ok )
            ok = validate_bootstrap_invariants_unlocked();
        if ( ok )
        {
            _last_error = PmmError::Ok;
            logging_policy::on_create( _backend.total_size() );
        }
        return ok;
    }

    static bool create() noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < detail::kMinMemorySize )
        {
            _last_error = ( _backend.base_ptr() == nullptr ) ? PmmError::BackendError : PmmError::InvalidSize;
            return false;
        }
        bool ok = init_layout( _backend.base_ptr(), _backend.total_size() );
        if ( ok )
            ok = bootstrap_forest_registry_unlocked();
        if ( ok )
            ok = validate_bootstrap_invariants_unlocked();
        if ( ok )
        {
            _last_error = PmmError::Ok;
            logging_policy::on_create( _backend.total_size() );
        }
        return ok;
    }

    static bool load( VerifyResult& result ) noexcept
    {
        result.mode = RecoveryMode::Repair;
        result.ok   = true;
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < detail::kMinMemorySize )
        {
            _last_error = ( _backend.base_ptr() == nullptr ) ? PmmError::BackendError : PmmError::InvalidSize;
            result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted );
            return false;
        }
        std::uint8_t*                          base = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr  = get_header( base );
        if ( hdr->magic != kMagic )
        {
            _last_error = PmmError::InvalidMagic;
            logging_policy::on_corruption_detected( PmmError::InvalidMagic );
            result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0,
                        static_cast<std::uint64_t>( kMagic ), static_cast<std::uint64_t>( hdr->magic ) );
            return false;
        }
        if ( hdr->total_size != _backend.total_size() )
        {
            _last_error = PmmError::SizeMismatch;
            logging_policy::on_corruption_detected( PmmError::SizeMismatch );
            result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, _backend.total_size(),
                        static_cast<std::uint64_t>( hdr->total_size ) );
            return false;
        }
        if ( hdr->granule_size != static_cast<std::uint16_t>( address_traits::granule_size ) )
        {
            _last_error = PmmError::GranuleMismatch;
            logging_policy::on_corruption_detected( PmmError::GranuleMismatch );
            result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, address_traits::granule_size,
                        static_cast<std::uint64_t>( hdr->granule_size ) );
            return false;
        }
        
        auto mark_entries = []( VerifyResult& r, std::size_t from, DiagnosticAction act )
        {
            for ( std::size_t i = from; i < r.entry_count; ++i )
                r.entries[i].action = act;
        };
        std::size_t pre = result.entry_count;
        allocator::verify_block_states( base, hdr, result ); 
        mark_entries( result, pre, DiagnosticAction::Repaired );
        pre = result.entry_count;
        allocator::verify_linked_list( base, hdr, result ); 
        mark_entries( result, pre, DiagnosticAction::Repaired );
        pre = result.entry_count;
        allocator::verify_counters( base, hdr, result ); 
        mark_entries( result, pre, DiagnosticAction::Rebuilt );
        pre = result.entry_count;
        allocator::verify_free_tree( base, hdr, result ); 
        mark_entries( result, pre, DiagnosticAction::Rebuilt );
        
        hdr->owns_memory     = false;
        hdr->prev_total_size = 0;
        allocator::repair_linked_list( base, hdr );
        allocator::recompute_counters( base, hdr );
        allocator::rebuild_free_tree( base, hdr );
        _initialized = true;
        
        {
            VerifyResult forest_verify;
            verify_forest_registry_unlocked( forest_verify );
            for ( std::size_t i = 0; i < forest_verify.entry_count; ++i )
            {
                const auto& e = forest_verify.entries[i];
                result.add( e.type, DiagnosticAction::Repaired, e.block_index, e.expected, e.actual );
            }
        }
        if ( !validate_or_bootstrap_forest_registry_unlocked() )
        {
            for ( std::size_t i = 0; i < result.entry_count; ++i )
            {
                if ( result.entries[i].type == ViolationType::ForestRegistryMissing ||
                     result.entries[i].type == ViolationType::ForestDomainMissing ||
                     result.entries[i].type == ViolationType::ForestDomainFlagsMissing )
                    result.entries[i].action = DiagnosticAction::Aborted;
            }
            _initialized = false;
            return false;
        }
        if ( !validate_bootstrap_invariants_unlocked() )
        {
            _initialized = false;
            return false;
        }
        _last_error = PmmError::Ok;
        logging_policy::on_load();
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
        logging_policy::on_destroy();
    }

    static bool is_initialized() noexcept { return _initialized.load( std::memory_order_acquire ); }

    static void* allocate( std::size_t user_size ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        return allocate_unlocked( user_size );
    }

    static void deallocate( void* ptr ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        deallocate_unlocked( ptr );
    }

    static bool lock_block_permanent( void* ptr ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        return lock_block_permanent_unlocked( ptr );
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

    template <typename T>
    static pptr<T> reallocate_typed( pptr<T> p, std::size_t old_count, std::size_t new_count ) noexcept
    {
        static_assert( std::is_trivially_copyable_v<T>,
                       "reallocate_typed<T>: T must be trivially copyable for safe memcpy reallocation." );
        if ( new_count == 0 )
        {
            _last_error = PmmError::InvalidSize;
            return pptr<T>();
        }
        if ( p.is_null() )
            return allocate_typed<T>( new_count );
        if ( sizeof( T ) > 0 && new_count > ( std::numeric_limits<std::size_t>::max )() / sizeof( T ) )
        {
            _last_error = PmmError::Overflow;
            return pptr<T>();
        }
        std::size_t                              new_user_size = sizeof( T ) * new_count;
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
        {
            _last_error = PmmError::NotInitialized;
            return pptr<T>();
        }
        std::uint8_t*                          base = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr  = get_header( base );
        
        static constexpr index_type kBlkHdrFloorGran =
            static_cast<index_type>( sizeof( Block<address_traits> ) / address_traits::granule_size );
        index_type blk_idx       = static_cast<index_type>( p.offset() - kBlkHdrFloorGran );
        void*      blk_raw       = detail::block_at<address_traits>( base, blk_idx );
        index_type old_data_gran = BlockStateBase<address_traits>::get_weight( blk_raw );
        index_type new_data_gran = detail::bytes_to_granules_t<address_traits>( new_user_size );
        if ( new_data_gran == 0 )
            new_data_gran = 1;
        if ( new_data_gran == old_data_gran )
        {
            _last_error = PmmError::Ok;
            return p;
        }
        
        static constexpr bool kBlockAligned = ( sizeof( Block<address_traits> ) % address_traits::granule_size == 0 );

        if constexpr ( kBlockAligned )
        {
            if ( new_data_gran < old_data_gran )
            {
                allocator::realloc_shrink( base, hdr, blk_idx, blk_raw, old_data_gran, new_data_gran );
                _last_error = PmmError::Ok;
                return p;
            }
            if ( new_data_gran > old_data_gran )
            {
                if ( allocator::realloc_grow( base, hdr, blk_idx, blk_raw, old_data_gran, new_data_gran ) )
                {
                    _last_error = PmmError::Ok;
                    return p;
                }
            }
        }
        
        static constexpr index_type kBlkHdrFloorGranFb =
            static_cast<index_type>( sizeof( Block<address_traits> ) / address_traits::granule_size );
        index_type new_data_gran_alloc = detail::bytes_to_granules_t<address_traits>( new_user_size );
        if ( new_data_gran_alloc == 0 )
            new_data_gran_alloc = 1;
        if ( new_data_gran_alloc > std::numeric_limits<index_type>::max() - kBlockHdrGranules )
        {
            _last_error = PmmError::Overflow;
            return pptr<T>();
        }
        index_type needed  = kBlockHdrGranules + new_data_gran_alloc;
        index_type new_idx = free_block_tree::find_best_fit( base, hdr, needed );
        if ( new_idx == address_traits::no_block )
        {
            if ( !do_expand( new_user_size ) )
            {
                _last_error = PmmError::OutOfMemory;
                logging_policy::on_allocation_failure( new_user_size, PmmError::OutOfMemory );
                return pptr<T>();
            }
            base    = _backend.base_ptr();
            hdr     = get_header( base );
            new_idx = free_block_tree::find_best_fit( base, hdr, needed );
            if ( new_idx == address_traits::no_block )
            {
                _last_error = PmmError::OutOfMemory;
                logging_policy::on_allocation_failure( new_user_size, PmmError::OutOfMemory );
                return pptr<T>();
            }
        }
        void* new_raw = allocator::allocate_from_block( base, hdr, new_idx, new_user_size );
        if ( new_raw == nullptr )
        {
            _last_error = PmmError::OutOfMemory;
            return pptr<T>();
        }
        pptr<T>     new_p   = make_pptr_from_raw<T>( new_raw );
        void*       new_dst = base + static_cast<std::size_t>( new_p.offset() ) * address_traits::granule_size;
        void*       old_src = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        std::size_t copy_sz = ( new_count < old_count ? new_count : old_count ) * sizeof( T );
        std::memmove( new_dst, old_src, copy_sz );
        
        index_type old_blk_idx = static_cast<index_type>( p.offset() - kBlkHdrFloorGranFb );
        void*      old_blk_raw = detail::block_at<address_traits>( base, old_blk_idx );
        index_type freed_w     = BlockStateBase<address_traits>::get_weight( old_blk_raw );
        if ( BlockStateBase<address_traits>::get_node_type( old_blk_raw ) != pmm::kNodeReadOnly )
        {
            auto* old_alloc = AllocatedBlock<address_traits>::cast_from_raw( old_blk_raw );
            if ( old_alloc != nullptr )
            {
                old_alloc->mark_as_free();
                hdr->alloc_count--;
                hdr->free_count++;
                if ( hdr->used_size >= freed_w )
                    hdr->used_size -= freed_w;
                allocator::coalesce( base, hdr, old_blk_idx );
            }
        }
        _last_error = PmmError::Ok;
        return new_p;
    }

    template <typename T, typename... Args> static pptr<T> create_typed( Args&&... args ) noexcept
    {
        
        static_assert( std::is_nothrow_constructible_v<T, Args...>,
                       "create_typed<T>: T must be nothrow-constructible from Args. "
                       "Use allocate_typed<T>() + manual placement new for throwing constructors." );

        void* raw = allocate( sizeof( T ) );
        if ( raw == nullptr )
            return pptr<T>();
        ::new ( raw ) T( static_cast<Args&&>( args )... );
        return make_pptr_from_raw<T>( raw );
    }

    template <typename T> static void destroy_typed( pptr<T> p ) noexcept
    {
        
        static_assert( std::is_nothrow_destructible_v<T>, "destroy_typed<T>: T must be nothrow-destructible." );

        if ( p.is_null() || !_initialized )
            return;
        std::uint8_t* base = _backend.base_ptr();
        void*         raw  = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        reinterpret_cast<T*>( raw )->~T();
        deallocate( raw );
    }

    template <typename T, typename... Args> static typed_guard<T, PersistMemoryManager> make_guard( Args&&... args )
    {
        return typed_guard<T, PersistMemoryManager>( create_typed<T>( static_cast<Args&&>( args )... ) );
    }

    template <typename T> static T* resolve( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return nullptr;
        std::uint8_t* base     = _backend.base_ptr();
        std::size_t   byte_off = static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        
        if ( byte_off + sizeof( T ) > _backend.total_size() )
        {
            _last_error = PmmError::InvalidPointer;
            return nullptr;
        }
        return reinterpret_cast<T*>( base + byte_off );
    }

    template <typename T> static T* resolve_at( pptr<T> p, std::size_t i ) noexcept
    {
        T* base_elem = resolve( p );
        return ( base_elem == nullptr ) ? nullptr : base_elem + i;
    }

    template <typename T> static pptr<T> pptr_from_byte_offset( std::size_t byte_off ) noexcept
    {
        if ( byte_off == 0 )
            return pptr<T>(); 
        if ( byte_off % address_traits::granule_size != 0 )
        {
            _last_error = PmmError::InvalidPointer;
            return pptr<T>();
        }
        std::size_t idx = byte_off / address_traits::granule_size;
        if ( idx > static_cast<std::size_t>( std::numeric_limits<index_type>::max() ) )
        {
            _last_error = PmmError::Overflow;
            return pptr<T>();
        }
        return pptr<T>( static_cast<index_type>( idx ) );
    }

    template <typename T> static bool is_valid_ptr( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return false;
        std::size_t byte_off = static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        return byte_off + sizeof( T ) <= _backend.total_size();
    }

    template <typename T> static void set_root( pptr<T> p ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return;
        set_legacy_root_offset_unlocked( p.is_null() ? static_cast<index_type>( 0 ) : p.offset() );
    }

    template <typename T> static pptr<T> get_root() noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return pptr<T>();
        index_type legacy_root = get_legacy_root_offset_unlocked();
        if ( legacy_root == static_cast<index_type>( 0 ) )
            return pptr<T>();
        return pptr<T>( legacy_root );
    }

    static index_type find_domain_by_name( const char* name ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return 0;
        const forest_domain* rec = find_domain_by_name_unlocked( name );
        return ( rec != nullptr ) ? rec->binding_id : static_cast<index_type>( 0 );
    }

    static index_type find_domain_by_symbol( pptr<pstringview> symbol ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return 0;
        const forest_domain* rec = find_domain_by_symbol_unlocked( symbol );
        return ( rec != nullptr ) ? rec->binding_id : static_cast<index_type>( 0 );
    }

    static bool has_domain( const char* name ) noexcept { return find_domain_by_name( name ) != 0; }

    static bool validate_bootstrap_invariants() noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        return validate_bootstrap_invariants_unlocked();
    }

    static bool register_domain( const char* name ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return false;
        return register_domain_unlocked( name, 0, detail::kForestBindingDirectRoot, 0 );
    }

    static bool register_system_domain( const char* name ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return false;
        return register_domain_unlocked( name, detail::kForestDomainFlagSystem, detail::kForestBindingDirectRoot, 0 );
    }

    static index_type get_domain_root_offset( const char* name ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return 0;
        const forest_domain* rec = find_domain_by_name_unlocked( name );
        return domain_root_offset_unlocked( rec, get_header_c( _backend.base_ptr() ) );
    }

    static index_type get_domain_root_offset( index_type binding_id ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return 0;
        const forest_domain* rec = find_domain_by_binding_unlocked( binding_id );
        return domain_root_offset_unlocked( rec, get_header_c( _backend.base_ptr() ) );
    }

    static index_type get_domain_root_offset( pptr<pstringview> symbol ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return 0;
        const forest_domain* rec = find_domain_by_symbol_unlocked( symbol );
        return domain_root_offset_unlocked( rec, get_header_c( _backend.base_ptr() ) );
    }

    template <typename T> static pptr<T> get_domain_root( const char* name ) noexcept
    {
        index_type root = get_domain_root_offset( name );
        return ( root == 0 ) ? pptr<T>() : pptr<T>( root );
    }

    template <typename T> static pptr<T> get_domain_root( index_type binding_id ) noexcept
    {
        index_type root = get_domain_root_offset( binding_id );
        return ( root == 0 ) ? pptr<T>() : pptr<T>( root );
    }

    template <typename T> static pptr<T> get_domain_root( pptr<pstringview> symbol ) noexcept
    {
        index_type root = get_domain_root_offset( symbol );
        return ( root == 0 ) ? pptr<T>() : pptr<T>( root );
    }

    template <typename T> static bool set_domain_root( const char* name, pptr<T> root ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return false;
        forest_domain* rec = find_domain_by_name_unlocked( name );
        if ( rec == nullptr || rec->binding_kind != detail::kForestBindingDirectRoot )
            return false;
        rec->root_offset = root.is_null() ? static_cast<index_type>( 0 ) : root.offset();
        return true;
    }

  private:
    
    template <typename T>
    static index_type get_tree_idx_field( pptr<T> p, index_type ( *getter )( const void* ) ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        const void* blk = block_raw_ptr_from_pptr( p );
        if ( blk == nullptr )
        {
            _last_error = PmmError::InvalidPointer;
            return 0;
        }
        index_type v = getter( blk );
        return ( v == address_traits::no_block ) ? static_cast<index_type>( 0 ) : v;
    }
    
    template <typename T>
    static void set_tree_idx_field( pptr<T> p, void ( *setter )( void*, index_type ), index_type val ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        void* blk = block_raw_mut_ptr_from_pptr( p );
        if ( blk == nullptr )
        {
            _last_error = PmmError::InvalidPointer;
            return;
        }
        setter( blk, ( val == 0 ) ? address_traits::no_block : val );
    }

  public:
    
    template <typename T> static index_type get_tree_left_offset( pptr<T> p ) noexcept
    {
        return get_tree_idx_field( p, &BlockStateBase<address_traits>::get_left_offset );
    }
    template <typename T> static index_type get_tree_right_offset( pptr<T> p ) noexcept
    {
        return get_tree_idx_field( p, &BlockStateBase<address_traits>::get_right_offset );
    }
    template <typename T> static index_type get_tree_parent_offset( pptr<T> p ) noexcept
    {
        return get_tree_idx_field( p, &BlockStateBase<address_traits>::get_parent_offset );
    }
    
    template <typename T> static void set_tree_left_offset( pptr<T> p, index_type v ) noexcept
    {
        set_tree_idx_field( p, &BlockStateBase<address_traits>::set_left_offset_of, v );
    }
    template <typename T> static void set_tree_right_offset( pptr<T> p, index_type v ) noexcept
    {
        set_tree_idx_field( p, &BlockStateBase<address_traits>::set_right_offset_of, v );
    }
    template <typename T> static void set_tree_parent_offset( pptr<T> p, index_type v ) noexcept
    {
        set_tree_idx_field( p, &BlockStateBase<address_traits>::set_parent_offset_of, v );
    }
    
    template <typename T> static index_type get_tree_weight( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        const void* blk = block_raw_ptr_from_pptr( p );
        if ( blk == nullptr )
        {
            _last_error = PmmError::InvalidPointer;
            return 0;
        }
        return BlockStateBase<address_traits>::get_weight( blk );
    }
    template <typename T> static void set_tree_weight( pptr<T> p, index_type w ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        void* blk = block_raw_mut_ptr_from_pptr( p );
        if ( blk == nullptr )
        {
            _last_error = PmmError::InvalidPointer;
            return;
        }
        BlockStateBase<address_traits>::set_weight_of( blk, w );
    }
    
    template <typename T> static std::int16_t get_tree_height( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        const void* blk = block_raw_ptr_from_pptr( p );
        if ( blk == nullptr )
        {
            _last_error = PmmError::InvalidPointer;
            return 0;
        }
        return BlockStateBase<address_traits>::get_avl_height( blk );
    }
    template <typename T> static void set_tree_height( pptr<T> p, std::int16_t h ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        void* blk = block_raw_mut_ptr_from_pptr( p );
        if ( blk == nullptr )
        {
            _last_error = PmmError::InvalidPointer;
            return;
        }
        BlockStateBase<address_traits>::set_avl_height_of( blk, h );
    }
    
    template <typename T> static TreeNode<address_traits>& tree_node( pptr<T> p ) noexcept
    {
        assert( !p.is_null() && "tree_node: pptr must not be null" );
        assert( _initialized && "tree_node: manager must be initialized before calling tree_node" );
        void* blk = block_raw_mut_ptr_from_pptr( p );
        if ( blk == nullptr )
        {
            _last_error = PmmError::InvalidPointer;
            
            static thread_local TreeNode<address_traits> sentinel{};
            sentinel = {};
            return sentinel;
        }
        return *reinterpret_cast<TreeNode<address_traits>*>( blk );
    }

  private:
    
    template <typename Fn> static std::size_t read_stat( Fn fn ) noexcept
    {
        if ( !_initialized.load( std::memory_order_acquire ) )
            return 0;
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized.load( std::memory_order_relaxed ) )
            return 0;
        return fn( get_header_c( _backend.base_ptr() ) );
    }

  public:
    
    static std::size_t total_size() noexcept
    {
        if ( !_initialized.load( std::memory_order_acquire ) )
            return 0;
        typename thread_policy::shared_lock_type lock( _mutex );
        return _initialized.load( std::memory_order_relaxed ) ? _backend.total_size() : 0;
    }
    static std::size_t used_size() noexcept
    {
        return read_stat( []( const auto* h ) { return address_traits::granules_to_bytes( h->used_size ); } );
    }
    static std::size_t free_size() noexcept
    {
        return read_stat(
            []( const auto* h )
            {
                std::size_t used = address_traits::granules_to_bytes( h->used_size );
                return ( h->total_size > used ) ? ( h->total_size - used ) : std::size_t( 0 );
            } );
    }
    static std::size_t block_count() noexcept
    {
        return read_stat( []( const auto* h ) { return static_cast<std::size_t>( h->block_count ); } );
    }
    static std::size_t free_block_count() noexcept
    {
        return read_stat( []( const auto* h ) { return static_cast<std::size_t>( h->free_count ); } );
    }
    static std::size_t alloc_block_count() noexcept
    {
        return read_stat( []( const auto* h ) { return static_cast<std::size_t>( h->alloc_count ); } );
    }

    static VerifyResult verify() noexcept
    {
        VerifyResult                             result;
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized || _backend.base_ptr() == nullptr )
        {
            result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted );
            return result;
        }
        verify_image_unlocked( result );
        return result;
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

    static inline thread_local PmmError _last_error{ PmmError::Ok };

    static bool is_valid_user_offset_unlocked( index_type off, std::size_t size_bytes ) noexcept
    {
        if ( off == 0 || _backend.base_ptr() == nullptr || _backend.total_size() == 0 )
            return false;
        std::size_t byte_off = static_cast<std::size_t>( off ) * address_traits::granule_size;
        return byte_off + size_bytes <= _backend.total_size();
    }

    static void* allocate_unlocked( std::size_t user_size ) noexcept
    {
        if ( !_initialized )
        {
            _last_error = PmmError::NotInitialized;
            logging_policy::on_allocation_failure( user_size, PmmError::NotInitialized );
            return nullptr;
        }
        if ( user_size == 0 )
        {
            _last_error = PmmError::InvalidSize;
            logging_policy::on_allocation_failure( user_size, PmmError::InvalidSize );
            return nullptr;
        }

        std::uint8_t*                          base      = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr       = get_header( base );
        index_type                             data_gran = detail::bytes_to_granules_t<address_traits>( user_size );
        if ( data_gran == 0 )
            data_gran = 1;
        if ( data_gran > std::numeric_limits<index_type>::max() - kBlockHdrGranules )
        {
            _last_error = PmmError::Overflow;
            logging_policy::on_allocation_failure( user_size, PmmError::Overflow );
            return nullptr;
        }

        index_type needed = kBlockHdrGranules + data_gran;
        index_type idx    = free_block_tree::find_best_fit( base, hdr, needed );
        if ( idx != address_traits::no_block )
        {
            _last_error = PmmError::Ok;
            return allocator::allocate_from_block( base, hdr, idx, user_size );
        }

        if ( !do_expand( user_size ) )
        {
            _last_error = PmmError::OutOfMemory;
            logging_policy::on_allocation_failure( user_size, PmmError::OutOfMemory );
            return nullptr;
        }

        base = _backend.base_ptr();
        hdr  = get_header( base );
        idx  = free_block_tree::find_best_fit( base, hdr, needed );
        if ( idx != address_traits::no_block )
        {
            _last_error = PmmError::Ok;
            return allocator::allocate_from_block( base, hdr, idx, user_size );
        }

        _last_error = PmmError::OutOfMemory;
        logging_policy::on_allocation_failure( user_size, PmmError::OutOfMemory );
        return nullptr;
    }

    static void deallocate_unlocked( void* ptr ) noexcept
    {
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

    static bool lock_block_permanent_unlocked( void* ptr ) noexcept
    {
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

    template <typename T, typename... Args> static pptr<T> create_typed_unlocked( Args&&... args ) noexcept
    {
        static_assert( std::is_nothrow_constructible_v<T, Args...>,
                       "create_typed_unlocked<T>: T must be nothrow-constructible" );
        void* raw = allocate_unlocked( sizeof( T ) );
        if ( raw == nullptr )
            return pptr<T>();
        ::new ( raw ) T( static_cast<Args&&>( args )... );
        return make_pptr_from_raw<T>( raw );
    }

static forest_registry* forest_registry_root_unlocked() noexcept
{
    if ( !_initialized || _backend.base_ptr() == nullptr )
        return nullptr;
    detail::ManagerHeader<address_traits>* hdr = get_header( _backend.base_ptr() );
    if ( hdr->root_offset == address_traits::no_block ||
         !is_valid_user_offset_unlocked( hdr->root_offset, sizeof( forest_registry ) ) )
        return nullptr;
    auto* reg = reinterpret_cast<forest_registry*>( _backend.base_ptr() + static_cast<std::size_t>( hdr->root_offset ) *
                                                                              address_traits::granule_size );
    if ( reg->magic != detail::kForestRegistryMagic || reg->version != detail::kForestRegistryVersion ||
         reg->domain_count > detail::kMaxForestDomains )
        return nullptr;
    return reg;
}

static forest_domain* find_domain_by_name_unlocked( const char* name ) noexcept
{
    if ( !detail::forest_domain_name_fits( name ) )
        return nullptr;
    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return nullptr;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( detail::forest_domain_name_equals( reg->domains[i], name ) )
            return &reg->domains[i];
    }
    return nullptr;
}

static forest_domain* find_domain_by_binding_unlocked( index_type binding_id ) noexcept
{
    if ( binding_id == 0 )
        return nullptr;
    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return nullptr;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( reg->domains[i].binding_id == binding_id )
            return &reg->domains[i];
    }
    return nullptr;
}

static forest_domain* find_domain_by_symbol_unlocked( pptr<pstringview> symbol ) noexcept
{
    if ( symbol.is_null() )
        return nullptr;
    pstringview* sym = resolve( symbol );
    if ( sym == nullptr )
        return nullptr;
    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return nullptr;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( reg->domains[i].symbol_offset == symbol.offset() ||
             std::strncmp( reg->domains[i].name, sym->c_str(), detail::kForestDomainNameCapacity ) == 0 )
        {
            reg->domains[i].symbol_offset = symbol.offset();
            return &reg->domains[i];
        }
    }
    return nullptr;
}

static index_type domain_root_offset_unlocked( const forest_domain*                         rec,
                                               const detail::ManagerHeader<address_traits>* hdr ) noexcept
{
    if ( rec == nullptr || hdr == nullptr )
        return 0;
    if ( rec->binding_kind == detail::kForestBindingFreeTree )
        return ( hdr->free_tree_root == address_traits::no_block ) ? static_cast<index_type>( 0 ) : hdr->free_tree_root;
    return rec->root_offset;
}

static index_type get_legacy_root_offset_unlocked() noexcept
{
    forest_registry* reg = forest_registry_root_unlocked();
    return ( reg != nullptr ) ? reg->legacy_root_offset : static_cast<index_type>( 0 );
}

static void set_legacy_root_offset_unlocked( index_type off ) noexcept
{
    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg != nullptr )
        reg->legacy_root_offset = off;
}

static forest_domain* symbol_domain_record_unlocked() noexcept
{
    return find_domain_by_name_unlocked( detail::kSystemDomainSymbols );
}

static index_type symbol_domain_root_offset_unlocked() noexcept
{
    forest_domain* rec = symbol_domain_record_unlocked();
    return ( rec != nullptr ) ? rec->root_offset : static_cast<index_type>( 0 );
}

static void reset_symbol_domain_unlocked() noexcept
{
    forest_domain* rec = symbol_domain_record_unlocked();
    if ( rec != nullptr )
        rec->root_offset = 0;
}

static bool register_domain_unlocked( const char* name, std::uint8_t flags, std::uint8_t binding_kind,
                                      index_type initial_root ) noexcept
{
    if ( !detail::forest_domain_name_fits( name ) )
        return false;

    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return false;

    if ( forest_domain* existing = find_domain_by_name_unlocked( name ) )
    {
        existing->flags |= flags;
        existing->binding_kind = binding_kind;
        if ( binding_kind == detail::kForestBindingDirectRoot && initial_root != 0 )
            existing->root_offset = initial_root;
        if ( existing->symbol_offset == 0 )
        {
            pptr<pstringview> symbol = intern_symbol_unlocked( name );
            if ( !symbol.is_null() )
                existing->symbol_offset = symbol.offset();
        }
        return true;
    }

    if ( reg->domain_count >= detail::kMaxForestDomains )
        return false;

    forest_domain rec{};
    if ( !detail::forest_domain_name_copy( rec, name ) )
        return false;

    rec.binding_id = reg->next_binding_id++;
    rec.root_offset =
        ( binding_kind == detail::kForestBindingDirectRoot ) ? initial_root : static_cast<index_type>( 0 );
    rec.binding_kind  = binding_kind;
    rec.flags         = flags;
    rec.symbol_offset = 0;

    pptr<pstringview> symbol = intern_symbol_unlocked( name );
    if ( !symbol.is_null() )
        rec.symbol_offset = symbol.offset();

    reg->domains[reg->domain_count++] = rec;
    return true;
}

static pptr<pstringview> intern_symbol_unlocked( const char* s ) noexcept
{
    if ( s == nullptr )
        s = "";

    forest_domain* symbol_domain = symbol_domain_record_unlocked();
    if ( symbol_domain == nullptr )
        return pptr<pstringview>();

    pptr<pstringview> found = detail::avl_find<pptr<pstringview>>(
        symbol_domain->root_offset,
        [&]( pptr<pstringview> cur ) -> int
        {
            pstringview* obj = resolve( cur );
            return ( obj != nullptr ) ? std::strcmp( s, obj->c_str() ) : 0;
        },
        []( pptr<pstringview> p ) -> pstringview* { return resolve( p ); } );
    if ( !found.is_null() )
        return found;

    std::uint32_t len        = static_cast<std::uint32_t>( std::strlen( s ) );
    std::size_t   alloc_size = offsetof( pstringview, str ) + static_cast<std::size_t>( len ) + 1;
    void*         raw        = allocate_unlocked( alloc_size );
    if ( raw == nullptr )
        return pptr<pstringview>();

    std::uint8_t*     base = _backend.base_ptr();
    pptr<pstringview> new_node( detail::ptr_to_granule_idx<address_traits>( base, raw ) );
    
    std::memcpy( raw, &len, sizeof( len ) );
    char* str_dst = static_cast<char*>( raw ) + offsetof( pstringview, str );
    std::memcpy( str_dst, s, static_cast<std::size_t>( len ) + 1 );

    detail::avl_init_node( new_node );
    if ( !lock_block_permanent_unlocked( raw ) )
        return pptr<pstringview>();

    const char* new_str = static_cast<const char*>( raw ) + offsetof( pstringview, str );
    detail::avl_insert(
        new_node, symbol_domain->root_offset,
        [&]( pptr<pstringview> cur ) -> bool
        {
            pstringview* cur_obj = resolve( cur );
            return ( cur_obj != nullptr ) && ( std::strcmp( new_str, cur_obj->c_str() ) < 0 );
        },
        []( pptr<pstringview> p ) -> pstringview* { return resolve( p ); } );

    return new_node;
}

static bool bootstrap_system_symbols_unlocked() noexcept
{
    static constexpr const char* kBootstrapSymbols[] = {
        detail::kSystemDomainFreeTree,     detail::kSystemDomainSymbols,          detail::kSystemDomainRegistry,
        detail::kSystemTypeForestRegistry, detail::kSystemTypeForestDomainRecord, detail::kSystemTypePstringview,
        detail::kServiceNameLegacyRoot,    detail::kServiceNameDomainRoot,        detail::kServiceNameDomainSymbol,
    };

    for ( const char* sym : kBootstrapSymbols )
    {
        if ( intern_symbol_unlocked( sym ).is_null() )
            return false;
    }

    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return false;
    for ( std::uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( reg->domains[i].name[0] == '\0' )
            continue;
        if ( reg->domains[i].symbol_offset != 0 )
            continue;
        pptr<pstringview> symbol = intern_symbol_unlocked( reg->domains[i].name );
        if ( symbol.is_null() )
            return false;
        reg->domains[i].symbol_offset = symbol.offset();
    }

    return true;
}

static bool create_forest_registry_root_unlocked( index_type legacy_root_offset ) noexcept
{
    static constexpr std::size_t kGranSz = address_traits::granule_size;

    void* raw = allocate_unlocked( sizeof( forest_registry ) + ( kGranSz - 1 ) );
    if ( raw == nullptr )
    {
        if ( _last_error == PmmError::Ok )
            _last_error = PmmError::OutOfMemory;
        return false;
    }

    std::uint8_t*    base        = _backend.base_ptr();
    std::size_t      raw_off     = static_cast<std::size_t>( static_cast<std::uint8_t*>( raw ) - base );
    std::size_t      aligned_off = ( raw_off + ( kGranSz - 1 ) ) & ~( kGranSz - 1 );
    forest_registry* reg         = reinterpret_cast<forest_registry*>( base + aligned_off );
    if ( reg == nullptr )
    {
        _last_error = PmmError::InvalidPointer;
        return false;
    }

    std::memset( reg, 0, sizeof( forest_registry ) );
    reg->magic              = detail::kForestRegistryMagic;
    reg->version            = detail::kForestRegistryVersion;
    reg->domain_count       = 0;
    reg->legacy_root_offset = legacy_root_offset;
    reg->next_binding_id    = 1;

    if ( !lock_block_permanent_unlocked( raw ) )
    {
        _last_error = PmmError::InvalidPointer;
        return false;
    }

    get_header( _backend.base_ptr() )->root_offset =
        detail::ptr_to_granule_idx<address_traits>( _backend.base_ptr(), reg );

    if ( !register_domain_unlocked( detail::kSystemDomainFreeTree, detail::kForestDomainFlagSystem,
                                    detail::kForestBindingFreeTree, 0 ) )
    {
        _last_error = PmmError::BackendError;
        return false;
    }
    if ( !register_domain_unlocked( detail::kSystemDomainSymbols, detail::kForestDomainFlagSystem,
                                    detail::kForestBindingDirectRoot, 0 ) )
    {
        _last_error = PmmError::BackendError;
        return false;
    }
    if ( !register_domain_unlocked( detail::kSystemDomainRegistry, detail::kForestDomainFlagSystem,
                                    detail::kForestBindingDirectRoot, get_header( _backend.base_ptr() )->root_offset ) )
    {
        _last_error = PmmError::BackendError;
        return false;
    }
    if ( !bootstrap_system_symbols_unlocked() )
    {
        _last_error = PmmError::BackendError;
        return false;
    }
    return true;
}

static bool bootstrap_forest_registry_unlocked() noexcept
{
    return create_forest_registry_root_unlocked( 0 );
}

static bool validate_bootstrap_invariants_unlocked() noexcept
{
    if ( !_initialized )
        return false;
    std::uint8_t* base = _backend.base_ptr();
    if ( base == nullptr )
        return false;
    const auto* hdr = get_header_c( base );
    
    if ( hdr->magic != kMagic )
        return false;
    if ( hdr->total_size != _backend.total_size() )
        return false;
    if ( hdr->granule_size != static_cast<std::uint16_t>( address_traits::granule_size ) )
        return false;
    
    const forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return false;
    if ( reg->magic != detail::kForestRegistryMagic )
        return false;
    if ( reg->version != detail::kForestRegistryVersion )
        return false;
    if ( reg->domain_count < 3 )
        return false; 
    
    static constexpr const char* kRequired[] = { detail::kSystemDomainFreeTree, detail::kSystemDomainSymbols,
                                                 detail::kSystemDomainRegistry };
    for ( const char* name : kRequired )
    {
        const forest_domain* rec = find_domain_by_name_unlocked( name );
        if ( rec == nullptr )
            return false;
        if ( ( rec->flags & detail::kForestDomainFlagSystem ) == 0 )
            return false;
        if ( rec->symbol_offset == 0 )
            return false; 
    }
    
    const forest_domain* free_rec = find_domain_by_name_unlocked( detail::kSystemDomainFreeTree );
    if ( free_rec->binding_kind != detail::kForestBindingFreeTree )
        return false;
    
    if ( symbol_domain_root_offset_unlocked() == 0 )
        return false;
    
    const forest_domain* reg_rec = find_domain_by_name_unlocked( detail::kSystemDomainRegistry );
    if ( reg_rec->root_offset != hdr->root_offset )
        return false;
    return true;
}

static bool validate_or_bootstrap_forest_registry_unlocked() noexcept
{
    detail::ManagerHeader<address_traits>* hdr = get_header( _backend.base_ptr() );
    if ( forest_registry_root_unlocked() != nullptr )
    {
        if ( !register_domain_unlocked( detail::kSystemDomainFreeTree, detail::kForestDomainFlagSystem,
                                        detail::kForestBindingFreeTree, 0 ) )
            return false;
        if ( !register_domain_unlocked( detail::kSystemDomainSymbols, detail::kForestDomainFlagSystem,
                                        detail::kForestBindingDirectRoot, symbol_domain_root_offset_unlocked() ) )
            return false;
        if ( !register_domain_unlocked( detail::kSystemDomainRegistry, detail::kForestDomainFlagSystem,
                                        detail::kForestBindingDirectRoot, hdr->root_offset ) )
            return false;

        if ( forest_domain* free_rec = find_domain_by_name_unlocked( detail::kSystemDomainFreeTree ) )
        {
            free_rec->flags |= detail::kForestDomainFlagSystem;
            free_rec->binding_kind = detail::kForestBindingFreeTree;
            free_rec->root_offset  = 0;
        }
        if ( forest_domain* symbols_rec = find_domain_by_name_unlocked( detail::kSystemDomainSymbols ) )
        {
            symbols_rec->flags |= detail::kForestDomainFlagSystem;
            symbols_rec->binding_kind = detail::kForestBindingDirectRoot;
        }
        if ( forest_domain* registry_rec = find_domain_by_name_unlocked( detail::kSystemDomainRegistry ) )
        {
            registry_rec->flags |= detail::kForestDomainFlagSystem;
            registry_rec->binding_kind = detail::kForestBindingDirectRoot;
            registry_rec->root_offset  = hdr->root_offset;
        }
        return bootstrap_system_symbols_unlocked();
    }

    index_type legacy_root = 0;
    if ( hdr->root_offset != address_traits::no_block &&
         is_valid_user_offset_unlocked( hdr->root_offset, sizeof( std::uint32_t ) ) )
    {
        if ( !is_valid_user_offset_unlocked( hdr->root_offset, sizeof( forest_registry ) ) )
        {
            legacy_root = hdr->root_offset;
        }
        else
        {
            auto* candidate = reinterpret_cast<const forest_registry*>(
                _backend.base_ptr() + static_cast<std::size_t>( hdr->root_offset ) * address_traits::granule_size );
            if ( candidate->magic != detail::kForestRegistryMagic )
                legacy_root = hdr->root_offset;
        }
    }

    hdr->root_offset = address_traits::no_block;
    return create_forest_registry_root_unlocked( legacy_root );
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

static void verify_image_unlocked( VerifyResult& result ) noexcept
{
    result.mode = RecoveryMode::Verify;
    result.ok   = true;

    const std::uint8_t*                          base = _backend.base_ptr();
    const detail::ManagerHeader<address_traits>* hdr  = get_header_c( base );

    if ( hdr->magic != kMagic )
    {
        result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, static_cast<std::uint64_t>( kMagic ),
                    static_cast<std::uint64_t>( hdr->magic ) );
        return; 
    }
    if ( hdr->total_size != _backend.total_size() )
    {
        result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, _backend.total_size(),
                    static_cast<std::uint64_t>( hdr->total_size ) );
        return; 
    }
    if ( hdr->granule_size != static_cast<std::uint16_t>( address_traits::granule_size ) )
    {
        result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, address_traits::granule_size,
                    static_cast<std::uint64_t>( hdr->granule_size ) );
        return; 
    }

    {
        using BlockState = BlockStateBase<address_traits>;
        index_type idx   = hdr->first_block_offset;
        while ( idx != address_traits::no_block )
        {
            if ( !detail::validate_block_index<address_traits>( hdr->total_size, idx ) )
                break;
            detail::validate_block_header_full<address_traits>( base, hdr->total_size, idx, result );
            const void* blk_ptr = base + static_cast<std::size_t>( idx ) * address_traits::granule_size;
            idx                  = BlockState::get_next_offset( blk_ptr );
        }
    }

    allocator::verify_block_states( base, hdr, result );

    allocator::verify_linked_list( base, hdr, result );

    allocator::verify_counters( base, hdr, result );

    allocator::verify_free_tree( base, hdr, result );

    verify_forest_registry_unlocked( result );
}

static void verify_forest_registry_unlocked( VerifyResult& result ) noexcept
{
    const forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
    {
        result.add( ViolationType::ForestRegistryMissing, DiagnosticAction::NoAction );
        return;
    }
    if ( reg->magic != detail::kForestRegistryMagic || reg->version != detail::kForestRegistryVersion )
    {
        result.add( ViolationType::ForestRegistryMissing, DiagnosticAction::NoAction, 0,
                    static_cast<std::uint64_t>( detail::kForestRegistryMagic ),
                    static_cast<std::uint64_t>( reg->magic ) );
        return;
    }

    static constexpr const char* kRequired[] = { detail::kSystemDomainFreeTree, detail::kSystemDomainSymbols,
                                                 detail::kSystemDomainRegistry };
    for ( const char* name : kRequired )
    {
        const forest_domain* rec = find_domain_by_name_unlocked( name );
        if ( rec == nullptr )
        {
            result.add( ViolationType::ForestDomainMissing, DiagnosticAction::NoAction );
            continue;
        }
        if ( ( rec->flags & detail::kForestDomainFlagSystem ) == 0 )
        {
            result.add( ViolationType::ForestDomainFlagsMissing, DiagnosticAction::NoAction );
        }
    }
}

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
        auto*         raw_byte = static_cast<std::uint8_t*>( raw );
        if ( raw_byte < base || raw_byte >= base + _backend.total_size() )
            return pptr<T>();
        std::size_t byte_off = static_cast<std::size_t>( raw_byte - base );
        std::size_t idx      = byte_off / address_traits::granule_size;
        if ( idx > static_cast<std::size_t>( std::numeric_limits<index_type>::max() ) )
            return pptr<T>();
        return pptr<T>( static_cast<index_type>( idx ) );
    }

    template <typename T> static const void* block_raw_ptr_from_pptr( pptr<T> p ) noexcept
    {
        const std::uint8_t* base     = _backend.base_ptr();
        std::size_t         byte_off = static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        if ( byte_off < sizeof( Block<address_traits> ) )
            return nullptr;
        std::size_t blk_off = byte_off - sizeof( Block<address_traits> );
        if ( blk_off + sizeof( Block<address_traits> ) > _backend.total_size() )
            return nullptr;
        return base + blk_off;
    }

    template <typename T> static void* block_raw_mut_ptr_from_pptr( pptr<T> p ) noexcept
    {
        std::uint8_t* base     = _backend.base_ptr();
        std::size_t   byte_off = static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        if ( byte_off < sizeof( Block<address_traits> ) )
            return nullptr;
        std::size_t blk_off = byte_off - sizeof( Block<address_traits> );
        if ( blk_off + sizeof( Block<address_traits> ) > _backend.total_size() )
            return nullptr;
        return base + blk_off;
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

    struct layout_access
    {
        using address_traits                                            = manager_type::address_traits;
        using free_block_tree                                           = manager_type::free_block_tree;
        using logging_policy                                            = manager_type::logging_policy;
        using storage_backend                                           = manager_type::storage_backend;
        using index_type                                                = manager_type::index_type;
        static constexpr std::uint64_t                kMagic            = pmm::kMagic;
        static constexpr std::size_t                  kBlockHdrByteSize = manager_type::kBlockHdrByteSize;
        static constexpr index_type                   kBlockHdrGranules = manager_type::kBlockHdrGranules;
        static constexpr index_type                   kMgrHdrGranules   = manager_type::kMgrHdrGranules;
        static constexpr index_type                   kFreeBlkIdxLayout = manager_type::kFreeBlkIdxLayout;
        static detail::ManagerHeader<address_traits>* get_header( std::uint8_t* base ) noexcept
        {
            return manager_type::get_header( base );
        }
        static void set_initialized() noexcept { manager_type::_initialized = true; }
    };

    static bool init_layout( std::uint8_t* base, std::size_t size ) noexcept
    {
        return detail::ManagerLayoutOps<layout_access>::init_layout( _backend, base, size );
    }

    static bool do_expand( std::size_t user_size ) noexcept
    {
        return detail::ManagerLayoutOps<layout_access>::do_expand( _backend, _initialized, user_size );
    }
};

} 

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined( _WIN32 ) || defined( _WIN64 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib> 
#endif

namespace pmm
{

namespace detail
{

inline bool atomic_rename( const char* tmp_path, const char* final_path ) noexcept
{
#if defined( _WIN32 ) || defined( _WIN64 )
    return MoveFileExA( tmp_path, final_path, MOVEFILE_REPLACE_EXISTING ) != 0;
#else
    return std::rename( tmp_path, final_path ) == 0;
#endif
}

} 

template <typename MgrT> inline bool save_manager( const char* filename )
{
    using address_traits = typename MgrT::address_traits;

    if ( filename == nullptr || !MgrT::is_initialized() )
        return false;
    std::uint8_t* data  = MgrT::backend().base_ptr();
    std::size_t   total = MgrT::backend().total_size();
    if ( data == nullptr || total == 0 )
        return false;

    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<address_traits> );
    auto*                 hdr        = reinterpret_cast<detail::ManagerHeader<address_traits>*>( data + kHdrOffset );
    hdr->crc32                       = 0; 
    hdr->crc32                       = detail::compute_image_crc32<address_traits>( data, total );

    std::string tmp_path = std::string( filename ) + ".tmp";

    std::FILE* f = std::fopen( tmp_path.c_str(), "wb" );
    if ( f == nullptr )
        return false;
    std::size_t written = std::fwrite( data, 1, total, f );
    if ( std::fflush( f ) != 0 )
    {
        std::fclose( f );
        std::remove( tmp_path.c_str() );
        return false;
    }
    std::fclose( f );

    if ( written != total )
    {
        std::remove( tmp_path.c_str() );
        return false;
    }

    if ( !detail::atomic_rename( tmp_path.c_str(), filename ) )
    {
        std::remove( tmp_path.c_str() );
        return false;
    }
    return true;
}

template <typename MgrT> inline bool load_manager_from_file( const char* filename, VerifyResult& result )
{
    using address_traits = typename MgrT::address_traits;

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

    constexpr std::size_t kHdrOffset = sizeof( pmm::Block<address_traits> );
    if ( file_size >= kHdrOffset + sizeof( detail::ManagerHeader<address_traits> ) )
    {
        auto*         hdr          = reinterpret_cast<detail::ManagerHeader<address_traits>*>( buf + kHdrOffset );
        std::uint32_t stored_crc   = hdr->crc32;
        std::uint32_t computed_crc = detail::compute_image_crc32<address_traits>( buf, file_size );
        if ( stored_crc != computed_crc )
        {
            MgrT::set_last_error( PmmError::CrcMismatch );
            MgrT::logging_policy::on_corruption_detected( PmmError::CrcMismatch );
            return false;
        }
    }

    return MgrT::load( result );
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

    bool expand( std::size_t additional_bytes ) noexcept
    {
        if ( !_mapped || additional_bytes == 0 )
            return _mapped && additional_bytes == 0;
        
        std::size_t growth   = _size / 4 + additional_bytes;
        std::size_t new_size = _size + growth;
        
        new_size = ( ( new_size + AddressTraitsT::granule_size - 1 ) / AddressTraitsT::granule_size ) *
                   AddressTraitsT::granule_size;
        if ( new_size <= _size )
            return false;
        return expand_impl( new_size );
    }

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

    bool expand_impl( std::size_t new_size ) noexcept
    {
        
        if ( _base != nullptr )
        {
            FlushViewOfFile( _base, _size );
            UnmapViewOfFile( _base );
            _base = nullptr;
        }
        if ( _map_handle != nullptr )
        {
            CloseHandle( _map_handle );
            _map_handle = nullptr;
        }

        LARGE_INTEGER new_size_li{};
        new_size_li.QuadPart = static_cast<LONGLONG>( new_size );
        if ( !SetFilePointerEx( _file_handle, new_size_li, nullptr, FILE_BEGIN ) || !SetEndOfFile( _file_handle ) )
        {
            
            DWORD hi    = static_cast<DWORD>( _size >> 32 );
            DWORD lo    = static_cast<DWORD>( _size & 0xFFFFFFFF );
            _map_handle = CreateFileMappingA( _file_handle, nullptr, PAGE_READWRITE, hi, lo, nullptr );
            if ( _map_handle != nullptr )
            {
                void* view = MapViewOfFile( _map_handle, FILE_MAP_ALL_ACCESS, 0, 0, _size );
                if ( view != nullptr )
                    _base = static_cast<std::uint8_t*>( view );
            }
            return false;
        }

        DWORD size_hi = static_cast<DWORD>( new_size >> 32 );
        DWORD size_lo = static_cast<DWORD>( new_size & 0xFFFFFFFF );
        _map_handle   = CreateFileMappingA( _file_handle, nullptr, PAGE_READWRITE, size_hi, size_lo, nullptr );
        if ( _map_handle == nullptr )
            return false;

        void* view = MapViewOfFile( _map_handle, FILE_MAP_ALL_ACCESS, 0, 0, new_size );
        if ( view == nullptr )
        {
            CloseHandle( _map_handle );
            _map_handle = nullptr;
            return false;
        }

        _base = static_cast<std::uint8_t*>( view );
        _size = new_size;
        return true;
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

    bool expand_impl( std::size_t new_size ) noexcept
    {
        
        if ( _base != nullptr )
        {
            ::munmap( _base, _size );
            _base = nullptr;
        }

        if ( ::ftruncate( _fd, static_cast<off_t>( new_size ) ) != 0 )
        {
            
            void* addr = ::mmap( nullptr, _size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0 );
            if ( addr != MAP_FAILED )
                _base = static_cast<std::uint8_t*>( addr );
            return false;
        }

        void* addr = ::mmap( nullptr, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0 );
        if ( addr == MAP_FAILED )
            return false;

        _base = static_cast<std::uint8_t*>( addr );
        _size = new_size;
        return true;
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
