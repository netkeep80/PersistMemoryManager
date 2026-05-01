#pragma once
#include "pmm/arena_internals.h"
#include "pmm/block_state.h"
#include "pmm/pptr.h"
#include "pmm/typed_guard.h"
#include "pmm/types.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
namespace pmm
{
template <class T, class ManagerT> struct node_type_for<pptr<T, ManagerT>>
{
    static constexpr NodeType value = NodeType::PPtr;
};
}
namespace pmm::detail
{
template <typename ManagerT> class PersistMemoryTypedApi
{
  public:
    template <typename T> static void assign_node_type_for( void* user_raw ) noexcept
    {
        if ( user_raw == nullptr )
            return;
        using address_traits = typename ManagerT::address_traits;
        constexpr size_t kHdrBytes =
            static_cast<size_t>( detail::kBlockHeaderGranules_t<address_traits> ) * address_traits::granule_size;
        auto* base = ManagerT::backend().base_ptr();
        if ( base == nullptr )
            return;
        const size_t user_off = static_cast<size_t>( static_cast<uint8_t*>( user_raw ) - base );
        if ( user_off < kHdrBytes )
            return;
        void* blk_raw = base + user_off - kHdrBytes;
        BlockStateBase<address_traits>::set_node_type_of( blk_raw, pmm::node_type_for_v<T> );
    }
    template <typename T> static pmm::pptr<T, ManagerT> allocate_typed() noexcept
    {
        using thread_policy = typename ManagerT::thread_policy;
        typename thread_policy::unique_lock_type lock( ManagerT::_mutex );
        void*                                    raw = ManagerT::allocate_unlocked( sizeof( T ) );
        if ( raw == nullptr )
            return pmm::pptr<T, ManagerT>();
        assign_node_type_for<T>( raw );
        return ManagerT::template make_pptr_from_raw<T>( raw );
    }
    template <typename T> static pmm::pptr<T, ManagerT> allocate_typed( size_t count ) noexcept
    {
        if ( count == 0 )
            return pmm::pptr<T, ManagerT>();
        if ( sizeof( T ) > 0 && count > ( std::numeric_limits<size_t>::max )() / sizeof( T ) )
            return pmm::pptr<T, ManagerT>();
        using thread_policy = typename ManagerT::thread_policy;
        typename thread_policy::unique_lock_type lock( ManagerT::_mutex );
        void*                                    raw = ManagerT::allocate_unlocked( sizeof( T ) * count );
        if ( raw == nullptr )
            return pmm::pptr<T, ManagerT>();
        assign_node_type_for<T>( raw );
        return ManagerT::template make_pptr_from_raw<T>( raw );
    }
    template <typename T> static void deallocate_typed( pmm::pptr<T, ManagerT> p ) noexcept
    {
        if ( p.is_null() || !ManagerT::_initialized )
            return;
        void* raw = ManagerT::template raw_block_user_ptr_from_pptr<T>( p );
        ManagerT::deallocate( raw );
    }
/*
#### pmm-detail-persistmemorytypedapi-reallocate_typed
req: dr-019, fr-005, fr-006, fr-029, rule-007, ur-002
*/
    template <typename T>
    static pmm::pptr<T, ManagerT> reallocate_typed( pmm::pptr<T, ManagerT> p, size_t old_count,
                                                    size_t new_count ) noexcept
    {
        using address_traits  = typename ManagerT::address_traits;
        using allocator       = typename ManagerT::allocator;
        using free_block_tree = typename ManagerT::free_block_tree;
        using index_type      = typename ManagerT::index_type;
        using logging_policy  = typename ManagerT::logging_policy;
        using thread_policy   = typename ManagerT::thread_policy;
        static_assert( std::is_trivially_copyable_v<T>, "" );
        if ( new_count == 0 )
        {
            ManagerT::_last_error = PmmError::InvalidSize;
            return pmm::pptr<T, ManagerT>();
        }
        if ( p.is_null() )
            return allocate_typed<T>( new_count );
        if ( sizeof( T ) > 0 && new_count > ( std::numeric_limits<size_t>::max )() / sizeof( T ) )
        {
            ManagerT::_last_error = PmmError::Overflow;
            return pmm::pptr<T, ManagerT>();
        }
        size_t                                   new_user_size = sizeof( T ) * new_count;
        typename thread_policy::unique_lock_type lock( ManagerT::_mutex );
        if ( !ManagerT::_initialized )
        {
            ManagerT::_last_error = PmmError::NotInitialized;
            return pmm::pptr<T, ManagerT>();
        }
        auto checked = pmm::detail::bytes_to_granules_checked<address_traits>( new_user_size );
        if ( !checked.has_value() )
        {
            ManagerT::_last_error = PmmError::Overflow;
            return pmm::pptr<T, ManagerT>();
        }
        index_type new_data_gran = checked->value;
        if ( new_data_gran == 0 )
        {
            ManagerT::_last_error = PmmError::InvalidSize;
            return pmm::pptr<T, ManagerT>();
        }
        if ( new_data_gran > std::numeric_limits<index_type>::max() - ManagerT::kBlockHdrGranules )
        {
            ManagerT::_last_error = PmmError::Overflow;
            return pmm::pptr<T, ManagerT>();
        }
        void* blk_raw = ManagerT::template try_checked_block_from_pptr<T>( p );
        if ( blk_raw == nullptr )
            return pmm::pptr<T, ManagerT>();
        uint8_t*                               base          = ManagerT::_backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr           = ManagerT::get_header( base );
        index_type                             blk_idx       = ManagerT::template block_idx_from_pptr<T>( p );
        index_type                             old_data_gran = BlockStateBase<address_traits>::get_weight( blk_raw );
        if ( new_data_gran == old_data_gran )
        {
            ManagerT::_last_error = PmmError::Ok;
            return p;
        }
        static constexpr bool kBlockAligned = ( sizeof( Block<address_traits> ) % address_traits::granule_size == 0 );
        detail::ArenaView<address_traits> arena{ base, hdr };
        if constexpr ( kBlockAligned )
        {
            if ( new_data_gran < old_data_gran )
            {
                allocator::realloc_shrink( arena, blk_idx, blk_raw, old_data_gran, new_data_gran );
                ManagerT::_last_error = PmmError::Ok;
                return p;
            }
            if ( new_data_gran > old_data_gran )
            {
                if ( allocator::realloc_grow( arena, blk_idx, blk_raw, old_data_gran, new_data_gran ) )
                {
                    ManagerT::_last_error = PmmError::Ok;
                    return p;
                }
            }
        }
        index_type needed  = ManagerT::kBlockHdrGranules + new_data_gran;
        index_type new_idx = free_block_tree::find_best_fit( base, hdr, needed );
        if ( new_idx == address_traits::no_block )
        {
            if ( !ManagerT::do_expand( new_data_gran ) )
            {
                ManagerT::_last_error = PmmError::OutOfMemory;
                logging_policy::on_allocation_failure( new_user_size, PmmError::OutOfMemory );
                return pmm::pptr<T, ManagerT>();
            }
            base    = ManagerT::_backend.base_ptr();
            hdr     = ManagerT::get_header( base );
            new_idx = free_block_tree::find_best_fit( base, hdr, needed );
            if ( new_idx == address_traits::no_block )
            {
                ManagerT::_last_error = PmmError::OutOfMemory;
                logging_policy::on_allocation_failure( new_user_size, PmmError::OutOfMemory );
                return pmm::pptr<T, ManagerT>();
            }
        }
        detail::ArenaView<address_traits> arena_after{ base, hdr };
        void* new_raw = allocator::allocate_from_block( arena_after, new_idx, new_data_gran );
        if ( new_raw == nullptr )
        {
            ManagerT::_last_error = PmmError::OutOfMemory;
            return pmm::pptr<T, ManagerT>();
        }
        assign_node_type_for<T>( new_raw );
        pmm::pptr<T, ManagerT> new_p = ManagerT::template make_pptr_from_raw<T>( new_raw );
        if ( new_p.is_null() )
        {
            ManagerT::_last_error = PmmError::InvalidPointer;
            return pmm::pptr<T, ManagerT>();
        }
        void*  new_dst = resolve_unchecked<T>( new_p );
        void*  old_src = resolve_unchecked<T>( p );
        size_t copy_sz = ( new_count < old_count ? new_count : old_count ) * sizeof( T );
        std::memmove( new_dst, old_src, copy_sz );
        void*               old_blk_raw = detail::block_at<address_traits>( base, blk_idx );
        index_type          freed_w     = BlockStateBase<address_traits>::get_weight( old_blk_raw );
        const pmm::NodeType nt_old      = BlockStateBase<address_traits>::get_node_type( old_blk_raw );
        if ( pmm::is_allocated( nt_old ) && pmm::can_be_deleted_from_pap( nt_old ) )
        {
            auto       old_alloc  = AllocatedBlock<address_traits>::cast_from_raw( old_blk_raw );
            index_type total_gran = detail::physical_block_total_granules<address_traits>(
                base, hdr, detail::block_at<address_traits>( base, blk_idx ) );
            old_alloc.mark_as_free( total_gran );
            hdr->alloc_count--;
            hdr->free_count++;
            if ( hdr->used_size >= freed_w )
                hdr->used_size -= freed_w;
            allocator::coalesce( arena_after, blk_idx );
        }
        ManagerT::_last_error = PmmError::Ok;
        return new_p;
    }
    template <typename T, typename... Args> static pmm::pptr<T, ManagerT> create_typed( Args&&... args ) noexcept
    {
        static_assert( std::is_nothrow_constructible_v<T, Args...>, "" );
        pmm::pptr<T, ManagerT> p;
        void*                  raw = nullptr;
        {
            using thread_policy = typename ManagerT::thread_policy;
            typename thread_policy::unique_lock_type lock( ManagerT::_mutex );
            raw = ManagerT::allocate_unlocked( sizeof( T ) );
            if ( raw == nullptr )
                return pmm::pptr<T, ManagerT>();
            assign_node_type_for<T>( raw );
            p = ManagerT::template make_pptr_from_raw<T>( raw );
        }
        T* obj = resolve_unchecked<T>( p );
        if ( obj == nullptr )
        {
            ManagerT::deallocate( raw );
            return pmm::pptr<T, ManagerT>();
        }
        ::new ( obj ) T( static_cast<Args&&>( args )... );
        return p;
    }
    template <typename T> static void destroy_typed( pmm::pptr<T, ManagerT> p ) noexcept
    {
        static_assert( std::is_nothrow_destructible_v<T>, "" );
        if ( p.is_null() || !ManagerT::_initialized )
            return;
        T*    obj = resolve_unchecked<T>( p );
        void* raw = ManagerT::template raw_block_user_ptr_from_pptr<T>( p );
        if ( obj == nullptr || raw == nullptr )
            return;
        obj->~T();
        ManagerT::deallocate( raw );
    }
    template <typename T, typename... Args> static typed_guard<T, ManagerT> make_guard( Args&&... args )
    {
        return typed_guard<T, ManagerT>( create_typed<T>( static_cast<Args&&>( args )... ) );
    }
    template <typename T> static T* resolve_unchecked( pmm::pptr<T, ManagerT> p ) noexcept
    {
        if ( p.is_null() || !ManagerT::_initialized )
            return nullptr;
        void* raw = ManagerT::template raw_user_ptr_from_pptr<T>( p );
        if ( raw == nullptr )
        {
            ManagerT::_last_error = PmmError::InvalidPointer;
            return nullptr;
        }
        ManagerT::_last_error = PmmError::Ok;
        return reinterpret_cast<T*>( raw );
    }
    template <typename T> static T* resolve_checked( pmm::pptr<T, ManagerT> p ) noexcept
    {
        using address_traits = typename ManagerT::address_traits;
        using index_type     = typename ManagerT::index_type;
        if ( p.is_null() || !ManagerT::_initialized )
            return nullptr;
        const void* blk_raw = ManagerT::template block_raw_ptr_from_pptr<T>( p );
        if ( blk_raw == nullptr )
        {
            ManagerT::_last_error = PmmError::InvalidPointer;
            return nullptr;
        }
        index_type          blk_idx   = ManagerT::template block_idx_from_pptr<T>( p );
        const pmm::NodeType node_type = BlockStateBase<address_traits>::get_node_type( blk_raw );
        if ( !pmm::is_allocated( node_type ) || BlockStateBase<address_traits>::get_root_offset( blk_raw ) != blk_idx )
        {
            ManagerT::_last_error = PmmError::InvalidPointer;
            return nullptr;
        }
        if ( !pmm::is_known_node_type( static_cast<std::uint8_t>( node_type ) ) )
        {
            ManagerT::_last_error = PmmError::InvalidPointer;
            return nullptr;
        }
        T* raw = resolve_unchecked<T>( p );
        if ( raw == nullptr )
            return nullptr;
        ManagerT::_last_error = PmmError::Ok;
        return raw;
    }
    template <typename T> static T* resolve( pmm::pptr<T, ManagerT> p ) noexcept { return resolve_checked<T>( p ); }
    template <typename T> static T* resolve_at( pmm::pptr<T, ManagerT> p, size_t i ) noexcept
    {
        T* base_elem = resolve_checked<T>( p );
        return ( base_elem == nullptr ) ? nullptr : base_elem + i;
    }
    template <typename T> static pmm::pptr<T, ManagerT> pptr_from_byte_offset( size_t byte_off ) noexcept
    {
        using address_traits = typename ManagerT::address_traits;
        using index_type     = typename ManagerT::index_type;
        if ( byte_off == 0 )
            return pmm::pptr<T, ManagerT>();
        if ( byte_off % address_traits::granule_size != 0 )
        {
            ManagerT::_last_error = PmmError::InvalidPointer;
            return pmm::pptr<T, ManagerT>();
        }
        size_t idx = byte_off / address_traits::granule_size;
        if ( idx > static_cast<size_t>( std::numeric_limits<index_type>::max() ) )
        {
            ManagerT::_last_error = PmmError::Overflow;
            return pmm::pptr<T, ManagerT>();
        }
        return pmm::pptr<T, ManagerT>( static_cast<index_type>( idx ) );
    }
    template <typename T> static bool is_valid_ptr( pmm::pptr<T, ManagerT> p ) noexcept
    {
        return resolve_checked<T>( p ) != nullptr;
    }
};
}
