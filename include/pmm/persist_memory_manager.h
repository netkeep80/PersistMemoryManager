#pragma once
#if defined( _MSVC_LANG )
#if _MSVC_LANG < 202002L
#error "pmm.h requires C++20 or later. Please compile with /std:c++20 on MSVC."
#endif
#elif __cplusplus < 202002L
#error "pmm.h requires C++20 or later. Please compile with -std=c++20."
#endif
#include "pmm/allocator_policy.h"
#include "pmm/arena_internals.h"
#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/diagnostics.h"
#include "pmm/forest_registry.h"
#include "pmm/layout.h"
#include "pmm/logging_policy.h"
#include "pmm/manager_configs.h"
#include "pmm/pallocator.h"
#include "pmm/parray.h"
#include "pmm/pmap.h"
#include "pmm/pptr.h"
#include "pmm/pstring.h"
#include "pmm/pstringview.h"
#include "pmm/typed_manager_api.h"
#include "pmm/types.h"
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
template <typename ConfigT = CacheManagerConfig, size_t InstanceId = 0>
/*
## pmm-persistmemorymanager
req: feat-001, if-008, con-004, con-005, if-009, con-009, dr-002, dr-008, dr-018, fr-004, fr-007, fr-008, fr-009,
fr-010, fr-011, fr-015, fr-021, fr-022, fr-032, qa-compat-001, qa-perf-002, qa-rec-001, qa-rel-002, qa-thread-001,
qa-thread-002, rule-006, sys-001, sys-005
*/
class PersistMemoryManager : public detail::PersistMemoryTypedApi<PersistMemoryManager<ConfigT, InstanceId>>
{
  public:
    using address_traits  = typename ConfigT::address_traits;
    using storage_backend = typename ConfigT::storage_backend;
    using free_block_tree = typename ConfigT::free_block_tree;
    using thread_policy   = typename ConfigT::lock_policy;
    using logging_policy  = typename detail::config_logging_policy<ConfigT>::type;
    static_assert( ConfigT::grow_numerator >= 1, "ConfigT must define grow_numerator >= 1" );
    static_assert( ConfigT::grow_denominator >= 1, "ConfigT must define grow_denominator >= 1" );
    static_assert( ConfigT::grow_numerator >= ConfigT::grow_denominator,
                   "ConfigT::grow_numerator must be >= grow_denominator" );
    using allocator       = AllocatorPolicy<free_block_tree, address_traits>;
    using index_type      = typename address_traits::index_type;
    using forest_registry = detail::ForestDomainRegistry<address_traits>;
    using forest_domain   = detail::ForestDomainRecord<address_traits>;
    using manager_type    = PersistMemoryManager<ConfigT, InstanceId>;
    template <typename> friend struct pstringview;
    template <typename, typename, typename> friend struct pmap;
    friend class detail::PersistMemoryTypedApi<manager_type>;
    template <typename> friend bool save_manager( const char* );
    template <typename T> using pptr               = pmm::pptr<T, manager_type>;
    using pstringview                              = pmm::pstringview<manager_type>;
    using pstring                                  = pmm::pstring<manager_type>;
    template <typename _K, typename _V> using pmap = pmm::pmap<_K, _V, manager_type>;
    template <typename T> using parray             = pmm::parray<T, manager_type>;
    template <typename T> using pallocator         = pmm::pallocator<T, manager_type>;
    static PmmError last_error() noexcept { return _last_error; }
    static void     clear_error() noexcept { _last_error = PmmError::Ok; }
    static void     set_last_error( PmmError err ) noexcept { _last_error = err; }
    /*
    ### pmm-persistmemorymanager-create
    req: fr-001, fr-026, ur-001, feat-001
    */
    static bool create( size_t initial_size ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( initial_size < detail::kMinMemorySize )
        {
            _last_error = PmmError::InvalidSize;
            return false;
        }
        static constexpr size_t kGranSzCreate = address_traits::granule_size;
        auto                    aligned_opt   = detail::round_up_checked( initial_size, kGranSzCreate );
        if ( !aligned_opt.has_value() )
        {
            _last_error = PmmError::Overflow;
            return false;
        }
        size_t aligned = *aligned_opt;
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < aligned )
        {
            if ( !_backend.resize_to( aligned ) )
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
        detail::InitGuard guard( _initialized );
        if ( !init_layout( _backend.base_ptr(), _backend.total_size() ) )
        {
            _last_error = PmmError::BackendError;
            return false;
        }
        if ( !bootstrap_forest_registry_unlocked() )
        {
            _last_error = PmmError::BackendError;
            return false;
        }
        if ( !validate_bootstrap_invariants_unlocked() )
        {
            _last_error = PmmError::BackendError;
            return false;
        }
        _last_error = PmmError::Ok;
        logging_policy::on_create( _backend.total_size() );
        guard.commit();
        return true;
    }
    static bool create() noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < detail::kMinMemorySize )
        {
            _last_error = ( _backend.base_ptr() == nullptr ) ? PmmError::BackendError : PmmError::InvalidSize;
            return false;
        }
        detail::InitGuard guard( _initialized );
        if ( !init_layout( _backend.base_ptr(), _backend.total_size() ) )
        {
            _last_error = PmmError::BackendError;
            return false;
        }
        if ( !bootstrap_forest_registry_unlocked() )
        {
            _last_error = PmmError::BackendError;
            return false;
        }
        if ( !validate_bootstrap_invariants_unlocked() )
        {
            _last_error = PmmError::BackendError;
            return false;
        }
        _last_error = PmmError::Ok;
        logging_policy::on_create( _backend.total_size() );
        guard.commit();
        return true;
    }
    /*
    ### pmm-persistmemorymanager-load
    req: fr-002, fr-014, ur-005, feat-001, feat-004, qa-rec-001, qa-compat-001, ur-001
    */
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
        uint8_t*                               base = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr  = get_header( base );
        if ( hdr->magic != kMagic )
        {
            _last_error = PmmError::InvalidMagic;
            logging_policy::on_corruption_detected( PmmError::InvalidMagic );
            result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, static_cast<uint64_t>( kMagic ),
                        static_cast<uint64_t>( hdr->magic ) );
            return false;
        }
        if ( !detail::is_supported_image_version( hdr->image_version ) )
        {
            _last_error = PmmError::UnsupportedImageVersion;
            logging_policy::on_corruption_detected( PmmError::UnsupportedImageVersion );
            result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, detail::kCurrentImageVersion,
                        static_cast<uint64_t>( hdr->image_version ) );
            return false;
        }
        if ( hdr->total_size != _backend.total_size() )
        {
            _last_error = PmmError::SizeMismatch;
            logging_policy::on_corruption_detected( PmmError::SizeMismatch );
            result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, _backend.total_size(),
                        static_cast<uint64_t>( hdr->total_size ) );
            return false;
        }
        if ( hdr->granule_size != static_cast<uint16_t>( address_traits::granule_size ) )
        {
            _last_error = PmmError::GranuleMismatch;
            logging_policy::on_corruption_detected( PmmError::GranuleMismatch );
            result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, address_traits::granule_size,
                        static_cast<uint64_t>( hdr->granule_size ) );
            return false;
        }
        auto mark_entries = []( VerifyResult& r, size_t from, DiagnosticAction act )
        {
            for ( size_t i = from; i < r.entry_count; ++i )
                r.entries[i].action = act;
        };
        detail::ConstArenaView<address_traits> cview{ base, hdr };
        size_t                                 pre = result.entry_count;
        allocator::verify_block_states( cview, result );
        mark_entries( result, pre, DiagnosticAction::Repaired );
        pre = result.entry_count;
        allocator::verify_linked_list( cview, result );
        mark_entries( result, pre, DiagnosticAction::Repaired );
        pre = result.entry_count;
        allocator::verify_counters( cview, result );
        mark_entries( result, pre, DiagnosticAction::Rebuilt );
        pre = result.entry_count;
        allocator::verify_free_tree( cview, result );
        mark_entries( result, pre, DiagnosticAction::Rebuilt );
        if ( detail::image_version_requires_migration( hdr->image_version ) )
            hdr->image_version = detail::kCurrentImageVersion;
        hdr->owns_memory     = false;
        hdr->prev_total_size = 0;
        detail::ArenaView<address_traits> arena_mut{ base, hdr };
        allocator::repair_linked_list( arena_mut );
        allocator::recompute_counters( arena_mut );
        allocator::rebuild_free_tree( arena_mut );
        _initialized = true;
        {
            VerifyResult forest_verify;
            verify_forest_registry_unlocked( forest_verify );
            for ( size_t i = 0; i < forest_verify.entry_count; ++i )
            {
                const auto& e = forest_verify.entries[i];
                result.add( e.type, DiagnosticAction::Repaired, e.block_index, e.expected, e.actual );
            }
        }
        if ( !validate_or_bootstrap_forest_registry_unlocked() )
        {
            for ( size_t i = 0; i < result.entry_count; ++i )
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
    /*
    ### pmm-persistmemorymanager-destroy
    req: fr-003, ur-001
    */
    static void destroy() noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return;
        _initialized = false;
        logging_policy::on_destroy();
    }
    static void destroy_image() noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        uint8_t*                                 base = _backend.base_ptr();
        if ( base != nullptr && _backend.total_size() >= detail::kMinMemorySize )
            get_header( base )->magic = 0;
        _initialized = false;
        logging_policy::on_destroy();
    }
    static bool is_initialized() noexcept { return _initialized.load( std::memory_order_acquire ); }
    /*
    ### pmm-persistmemorymanager-allocate
    req: fr-004, fr-021, fr-022, ur-002, feat-002
    */
    static void* allocate( size_t user_size ) noexcept
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
        return BlockStateBase<address_traits>::get_node_type( blk ) == pmm::NodeType::ReadOnlyLocked;
    }
    template <typename T> static void set_root( pptr<T> p ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return;
        set_forest_domain_root_index_unlocked( find_domain_by_name_unlocked( detail::kServiceNameDomainRoot ),
                                               p.is_null() ? static_cast<index_type>( 0 ) : p.offset() );
    }
    template <typename T> static pptr<T> get_root() noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return pptr<T>();
        index_type root =
            forest_domain_root_index_unlocked( find_domain_by_name_unlocked( detail::kServiceNameDomainRoot ) );
        if ( root == static_cast<index_type>( 0 ) )
            return pptr<T>();
        return pptr<T>( root );
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
        return forest_domain_root_index_unlocked( rec );
    }
    static index_type get_domain_root_offset( index_type binding_id ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return 0;
        const forest_domain* rec = find_domain_by_binding_unlocked( binding_id );
        return forest_domain_root_index_unlocked( rec );
    }
    static index_type get_domain_root_offset( pptr<pstringview> symbol ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return 0;
        const forest_domain* rec = find_domain_by_symbol_unlocked( symbol );
        return forest_domain_root_index_unlocked( rec );
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
        return set_forest_domain_root_index_unlocked( rec,
                                                      root.is_null() ? static_cast<index_type>( 0 ) : root.offset() );
    }

  private:
    template <typename T> static void* try_checked_block_from_pptr( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return nullptr;
        void* blk = block_raw_mut_ptr_from_pptr( p );
        if ( blk == nullptr )
        {
            _last_error = PmmError::InvalidPointer;
            return nullptr;
        }
        const index_type    blk_idx = block_idx_from_pptr( p );
        const pmm::NodeType nt      = BlockStateBase<address_traits>::get_node_type( blk );
        if ( !pmm::is_known_node_type( static_cast<std::uint8_t>( nt ) ) || !pmm::is_allocated( nt ) ||
             BlockStateBase<address_traits>::get_root_offset( blk ) != blk_idx )
        {
            _last_error = PmmError::InvalidPointer;
            return nullptr;
        }
        return blk;
    }
    template <typename T, typename ValueT>
    static ValueT get_tree_field( pptr<T> p, ValueT ( *getter )( const void* ) ) noexcept
    {
        const void* blk = try_checked_block_from_pptr( p );
        if ( blk == nullptr )
            return ValueT{};
        return getter( blk );
    }
    template <typename T, typename ValueT>
    static void set_tree_field( pptr<T> p, void ( *setter )( void*, ValueT ), ValueT value ) noexcept
    {
        void* blk = try_checked_block_from_pptr( p );
        if ( blk == nullptr )
            return;
        setter( blk, value );
    }
    template <typename T>
    static index_type get_tree_idx_field( pptr<T> p, index_type ( *getter )( const void* ) ) noexcept
    {
        index_type v = get_tree_field( p, getter );
        return ( v == address_traits::no_block ) ? static_cast<index_type>( 0 ) : v;
    }
    template <typename T>
    static void set_tree_idx_field( pptr<T> p, void ( *setter )( void*, index_type ), index_type val ) noexcept
    {
        set_tree_field( p, setter, ( val == 0 ) ? address_traits::no_block : val );
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
        return get_tree_field( p, &BlockStateBase<address_traits>::get_weight );
    }
    template <typename T> static void set_tree_weight( pptr<T> p, index_type w ) noexcept
    {
        set_tree_field( p, &BlockStateBase<address_traits>::set_weight_of, w );
    }
    template <typename T> static std::uint8_t get_tree_height( pptr<T> p ) noexcept
    {
        return get_tree_field( p, &BlockStateBase<address_traits>::get_avl_height );
    }
    template <typename T> static void set_tree_height( pptr<T> p, std::uint8_t h ) noexcept
    {
        set_tree_field( p, &BlockStateBase<address_traits>::set_avl_height_of, h );
    }
    template <typename T> static BlockHeader<address_traits>* try_tree_node( pptr<T> p ) noexcept
    {
        void* blk = try_checked_block_from_pptr( p );
        if ( blk == nullptr )
            return nullptr;
        return detail::block_header_at<address_traits>( blk );
    }
    template <typename T> static BlockHeader<address_traits>& tree_node_unchecked( pptr<T> p ) noexcept
    {
        assert( !p.is_null() && "tree_node_unchecked: pptr must not be null" );
        assert( _initialized && "tree_node_unchecked: manager must be initialized" );
        void* blk = block_raw_mut_ptr_from_pptr( p );
        assert( blk != nullptr && "tree_node_unchecked: pptr must resolve to a valid block" );
        return *detail::block_header_at<address_traits>( blk );
    }

  private:
    template <typename Fn> static size_t read_stat( Fn fn ) noexcept
    {
        if ( !_initialized.load( std::memory_order_acquire ) )
            return 0;
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized.load( std::memory_order_relaxed ) )
            return 0;
        return fn( get_header_c( _backend.base_ptr() ) );
    }

  public:
    static size_t total_size() noexcept
    {
        if ( !_initialized.load( std::memory_order_acquire ) )
            return 0;
        typename thread_policy::shared_lock_type lock( _mutex );
        return _initialized.load( std::memory_order_relaxed ) ? _backend.total_size() : 0;
    }
    static size_t used_size() noexcept
    {
        return read_stat( []( const auto* h ) { return address_traits::granules_to_bytes( h->used_size ); } );
    }
    static size_t free_size() noexcept
    {
        return read_stat(
            []( const auto* h )
            {
                size_t used = address_traits::granules_to_bytes( h->used_size );
                return ( h->total_size > used ) ? ( h->total_size - used ) : size_t( 0 );
            } );
    }
    static size_t block_count() noexcept
    {
        return read_stat( []( const auto* h ) { return static_cast<size_t>( h->block_count ); } );
    }
    static size_t free_block_count() noexcept
    {
        return read_stat( []( const auto* h ) { return static_cast<size_t>( h->free_count ); } );
    }
    static size_t alloc_block_count() noexcept
    {
        return read_stat( []( const auto* h ) { return static_cast<size_t>( h->alloc_count ); } );
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
        const uint8_t* base                                  = _backend.base_ptr();
        using BlockState                                     = BlockStateBase<address_traits>;
        const detail::ManagerHeader<address_traits>* hdr     = get_header_c( base );
        static constexpr size_t                      kGranSz = address_traits::granule_size;
        detail::ConstArenaView<address_traits>       cview{ base, hdr };
        return detail::for_each_physical_block<address_traits>(
            cview,
            [&]( index_type idx, const void* blk_raw ) noexcept
            {
                const Block<address_traits>* blk        = reinterpret_cast<const Block<address_traits>*>( blk_raw );
                index_type                   total_gran = detail::block_total_granules( base, hdr, blk );
                auto                         w          = BlockState::get_weight( blk_raw );
                bool                         is_used    = pmm::is_allocated( BlockState::get_node_type( blk_raw ) );
                size_t                       hdr_bytes  = sizeof( Block<address_traits> );
                size_t                       data_bytes = is_used ? static_cast<size_t>( w ) * kGranSz : 0;
                BlockView                    view;
                view.index       = idx;
                view.offset      = static_cast<std::ptrdiff_t>( static_cast<size_t>( idx ) * kGranSz );
                view.total_size  = static_cast<size_t>( total_gran ) * kGranSz;
                view.header_size = hdr_bytes;
                view.user_size   = data_bytes;
                view.alignment   = kGranSz;
                view.used        = is_used;
                callback( view );
                return true;
            } );
    }
    template <typename Callback> static bool for_each_free_block( Callback&& callback ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return false;
        const uint8_t*                               base = _backend.base_ptr();
        const detail::ManagerHeader<address_traits>* hdr  = get_header_c( base );
        for_each_free_block_inorder( base, hdr, hdr->free_tree_root, 0, callback );
        return true;
    }
    static storage_backend& backend() noexcept { return _backend; }

  private:
    static inline storage_backend                    _backend{};
    static inline std::atomic<bool>                  _initialized{ false };
    static inline typename thread_policy::mutex_type _mutex{};
    static inline thread_local PmmError              _last_error{ PmmError::Ok };
    static bool is_valid_user_offset_unlocked( index_type off, size_t size_bytes ) noexcept
    {
        if ( off == 0 || _backend.base_ptr() == nullptr || _backend.total_size() == 0 )
            return false;
        auto byte_off_opt = detail::checked_granule_offset<address_traits>( off );
        if ( !byte_off_opt.has_value() )
            return false;
        return detail::fits_range( *byte_off_opt, size_bytes, _backend.total_size() );
    }
    static void* allocate_unlocked( size_t user_size ) noexcept
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
        auto checked = detail::bytes_to_granules_checked<address_traits>( user_size );
        if ( !checked.has_value() )
        {
            _last_error = PmmError::Overflow;
            logging_policy::on_allocation_failure( user_size, PmmError::Overflow );
            return nullptr;
        }
        index_type data_gran = checked->value;
        if ( data_gran == 0 )
        {
            _last_error = PmmError::InvalidSize;
            logging_policy::on_allocation_failure( user_size, PmmError::InvalidSize );
            return nullptr;
        }
        if ( data_gran > std::numeric_limits<index_type>::max() - kBlockHdrGranules )
        {
            _last_error = PmmError::Overflow;
            logging_policy::on_allocation_failure( user_size, PmmError::Overflow );
            return nullptr;
        }
        uint8_t*                               base   = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr    = get_header( base );
        index_type                             needed = kBlockHdrGranules + data_gran;
        index_type                             idx    = free_block_tree::find_best_fit( base, hdr, needed );
        if ( idx != address_traits::no_block )
        {
            _last_error = PmmError::Ok;
            return allocator::allocate_from_block( detail::ArenaView<address_traits>{ base, hdr }, idx, data_gran );
        }
        if ( !do_expand( data_gran ) )
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
            return allocator::allocate_from_block( detail::ArenaView<address_traits>{ base, hdr }, idx, data_gran );
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
        const pmm::NodeType nt = BlockStateBase<address_traits>::get_node_type( blk );
        if ( !pmm::is_allocated( nt ) || !pmm::can_be_deleted_from_pap( nt ) )
            return;
        index_type freed = BlockStateBase<address_traits>::get_weight( blk );
        if ( freed == 0 )
            return;
        uint8_t*                               base       = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr        = get_header( base );
        index_type                             blk_idx    = detail::block_idx_t<address_traits>( base, blk );
        index_type                             total_gran = detail::physical_block_total_granules<address_traits>(
            base, hdr, detail::block_at<address_traits>( base, blk_idx ) );
        AllocatedBlock<address_traits> alloc = AllocatedBlock<address_traits>::cast_from_raw( blk );
        alloc.mark_as_free( total_gran );
        hdr->alloc_count--;
        hdr->free_count++;
        if ( hdr->used_size >= freed )
            hdr->used_size -= freed;
        allocator::coalesce( detail::ArenaView<address_traits>{ base, hdr }, blk_idx );
    }
    static bool lock_block_permanent_unlocked( void* ptr ) noexcept
    {
        if ( !_initialized || ptr == nullptr )
            return false;
        pmm::Block<address_traits>* blk = find_block_from_user_ptr( ptr );
        if ( blk == nullptr )
            return false;
        const pmm::NodeType nt = BlockStateBase<address_traits>::get_node_type( blk );
        if ( !pmm::is_allocated( nt ) )
            return false;
        BlockStateBase<address_traits>::set_node_type_of( blk, pmm::NodeType::ReadOnlyLocked );
        return true;
    }
    template <typename T, typename... Args> static pptr<T> create_typed_unlocked( Args&&... args ) noexcept
    {
        static_assert( std::is_nothrow_constructible_v<T, Args...>, "" );
        void* raw = allocate_unlocked( sizeof( T ) );
        if ( raw == nullptr )
            return pptr<T>();
        manager_type::template assign_node_type_for<T>( raw );
        pptr<T> p   = make_pptr_from_raw<T>( raw );
        T*      obj = manager_type::template resolve_unchecked<T>( p );
        if ( obj == nullptr )
        {
            deallocate_unlocked( raw );
            return pptr<T>();
        }
        ::new ( obj ) T( static_cast<Args&&>( args )... );
        return p;
    }
    static forest_registry* forest_registry_root_unlocked() noexcept
    {
        if ( !_initialized || _backend.base_ptr() == nullptr )
            return nullptr;
        detail::ManagerHeader<address_traits>* hdr = get_header( _backend.base_ptr() );
        if ( hdr->root_offset == address_traits::no_block ||
             !is_valid_user_offset_unlocked( hdr->root_offset, sizeof( forest_registry ) ) )
            return nullptr;
        auto* reg = reinterpret_cast<forest_registry*>( _backend.base_ptr() + static_cast<size_t>( hdr->root_offset ) *
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
        for ( uint16_t i = 0; i < reg->domain_count; ++i )
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
        for ( uint16_t i = 0; i < reg->domain_count; ++i )
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
        const char* sym_str = pstringview_c_str_unlocked( symbol );
        if ( sym_str == nullptr )
            return nullptr;
        forest_registry* reg = forest_registry_root_unlocked();
        if ( reg == nullptr )
            return nullptr;
        for ( uint16_t i = 0; i < reg->domain_count; ++i )
        {
            if ( reg->domains[i].symbol_offset == symbol.offset() ||
                 std::strncmp( reg->domains[i].name, sym_str, detail::kForestDomainNameCapacity ) == 0 )
            {
                reg->domains[i].symbol_offset = symbol.offset();
                return &reg->domains[i];
            }
        }
        return nullptr;
    }
    static index_type forest_domain_root_index_unlocked( const forest_domain* rec ) noexcept
    {
        const detail::ManagerHeader<address_traits>* hdr =
            ( _backend.base_ptr() != nullptr ) ? get_header_c( _backend.base_ptr() ) : nullptr;
        if ( rec == nullptr || hdr == nullptr )
            return 0;
        if ( rec->binding_kind == detail::kForestBindingFreeTree )
            return ( hdr->free_tree_root == address_traits::no_block ) ? static_cast<index_type>( 0 )
                                                                       : hdr->free_tree_root;
        return rec->root_offset;
    }
    static index_type* forest_domain_root_index_ptr_unlocked( forest_domain* rec ) noexcept
    {
        if ( rec == nullptr || rec->binding_kind != detail::kForestBindingDirectRoot )
            return nullptr;
        return &rec->root_offset;
    }
    static bool set_forest_domain_root_index_unlocked( forest_domain* rec, index_type root ) noexcept
    {
        index_type* root_ptr = forest_domain_root_index_ptr_unlocked( rec );
        if ( root_ptr == nullptr )
            return false;
        *root_ptr = root;
        return true;
    }
    static forest_domain* symbol_domain_record_unlocked() noexcept
    {
        return find_domain_by_name_unlocked( detail::kSystemDomainSymbols );
    }
    static bool register_domain_unlocked( const char* name, uint8_t flags, uint8_t binding_kind,
                                          index_type initial_root ) noexcept
    {
        if ( !detail::forest_domain_name_fits( name ) )
            return false;
        detail::relocation_owner<const char, manager_type> name_owner( name );
        forest_registry*                                   reg = forest_registry_root_unlocked();
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
                name                     = name_owner.get();
                existing                 = name != nullptr ? find_domain_by_name_unlocked( name ) : nullptr;
                if ( existing != nullptr && !symbol.is_null() )
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
        rec.binding_kind         = binding_kind;
        rec.flags                = flags;
        rec.symbol_offset        = 0;
        pptr<pstringview> symbol = intern_symbol_unlocked( name );
        reg                      = forest_registry_root_unlocked();
        if ( reg == nullptr )
            return false;
        if ( reg->domain_count >= detail::kMaxForestDomains )
            return false;
        if ( !symbol.is_null() )
            rec.symbol_offset = symbol.offset();
        reg->domains[reg->domain_count++] = rec;
        return true;
    }
    static pptr<pstringview> intern_symbol_unlocked( const char* s ) noexcept
    {
        if ( s == nullptr )
            s = "";
        detail::relocation_owner<const char, manager_type> source( s );
        auto                                               symbol_policy = pstringview::forest_domain_ops();
        if ( symbol_policy.root_index_ptr() == nullptr )
            return pptr<pstringview>();
        pptr<pstringview> found = symbol_policy.find( s );
        if ( !found.is_null() )
            return found;
        uint32_t len        = static_cast<uint32_t>( std::strlen( s ) );
        size_t   alloc_size = offsetof( pstringview, str ) + static_cast<size_t>( len ) + 1;
        void*    raw        = allocate_unlocked( alloc_size );
        if ( raw == nullptr )
            return pptr<pstringview>();
        s = source.get();
        if ( s == nullptr )
        {
            deallocate_unlocked( raw );
            return pptr<pstringview>();
        }
        pptr<pstringview> new_node   = make_pptr_from_raw<pstringview>( raw );
        void*             public_raw = raw_user_ptr_from_pptr( new_node );
        if ( public_raw == nullptr )
        {
            deallocate_unlocked( raw );
            return pptr<pstringview>();
        }
        std::memcpy( public_raw, &len, sizeof( len ) );
        char* str_dst = static_cast<char*>( public_raw ) + offsetof( pstringview, str );
        std::memcpy( str_dst, s, static_cast<size_t>( len ) + 1 );
        detail::avl_init_node( new_node );
        if ( !lock_block_permanent_unlocked( public_raw ) )
            return pptr<pstringview>();
        symbol_policy.insert( new_node );
        return new_node;
    }
    static bool bootstrap_system_symbols_unlocked() noexcept
    {
        static constexpr const char* kBootstrapSymbols[] = {
            detail::kSystemDomainFreeTree,     detail::kSystemDomainSymbols,          detail::kSystemDomainRegistry,
            detail::kSystemTypeForestRegistry, detail::kSystemTypeForestDomainRecord, detail::kSystemTypePstringview,
            detail::kServiceNameDomainRoot,    detail::kServiceNameDomainSymbol,
        };
        for ( const char* sym : kBootstrapSymbols )
        {
            if ( intern_symbol_unlocked( sym ).is_null() )
                return false;
        }
        forest_registry* reg = forest_registry_root_unlocked();
        if ( reg == nullptr )
            return false;
        const uint16_t domain_count = reg->domain_count;
        for ( uint16_t i = 0; i < domain_count; ++i )
        {
            reg = forest_registry_root_unlocked();
            if ( reg == nullptr || i >= reg->domain_count )
                return false;
            if ( reg->domains[i].name[0] == '\0' || reg->domains[i].symbol_offset != 0 )
                continue;
            const index_type  binding_id = reg->domains[i].binding_id;
            pptr<pstringview> symbol     = intern_symbol_unlocked( reg->domains[i].name );
            if ( symbol.is_null() )
                return false;
            forest_domain* rec = find_domain_by_binding_unlocked( binding_id );
            if ( rec == nullptr )
                return false;
            rec->symbol_offset = symbol.offset();
        }
        return true;
    }
    static bool bootstrap_forest_registry_unlocked() noexcept
    {
        static constexpr size_t kGranSz = address_traits::granule_size;
        void*                   raw     = allocate_unlocked( sizeof( forest_registry ) + ( kGranSz - 1 ) );
        if ( raw == nullptr )
        {
            if ( _last_error == PmmError::Ok )
                _last_error = PmmError::OutOfMemory;
            return false;
        }
        uint8_t*         base        = _backend.base_ptr();
        size_t           raw_off     = static_cast<size_t>( static_cast<uint8_t*>( raw ) - base );
        size_t           aligned_off = ( raw_off + ( kGranSz - 1 ) ) & ~( kGranSz - 1 );
        forest_registry* reg         = reinterpret_cast<forest_registry*>( base + aligned_off );
        if ( reg == nullptr )
        {
            _last_error = PmmError::InvalidPointer;
            return false;
        }
        std::memset( reg, 0, sizeof( forest_registry ) );
        reg->magic           = detail::kForestRegistryMagic;
        reg->version         = detail::kForestRegistryVersion;
        reg->domain_count    = 0;
        reg->next_binding_id = 1;
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
                                        detail::kForestBindingDirectRoot,
                                        get_header( _backend.base_ptr() )->root_offset ) )
        {
            _last_error = PmmError::BackendError;
            return false;
        }
        if ( !register_domain_unlocked( detail::kServiceNameDomainRoot, detail::kForestDomainFlagSystem,
                                        detail::kForestBindingDirectRoot, 0 ) )
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
    static bool validate_bootstrap_invariants_unlocked() noexcept
    {
        if ( !_initialized )
            return false;
        uint8_t* base = _backend.base_ptr();
        if ( base == nullptr )
            return false;
        const auto* hdr = get_header_c( base );
        if ( hdr->magic != kMagic )
            return false;
        if ( hdr->image_version != detail::kCurrentImageVersion )
            return false;
        if ( hdr->total_size != _backend.total_size() )
            return false;
        if ( hdr->granule_size != static_cast<uint16_t>( address_traits::granule_size ) )
            return false;
        const forest_registry* reg = forest_registry_root_unlocked();
        if ( reg == nullptr )
            return false;
        if ( reg->magic != detail::kForestRegistryMagic )
            return false;
        if ( reg->version != detail::kForestRegistryVersion )
            return false;
        if ( reg->domain_count < 4 )
            return false;
        static constexpr const char* kRequired[] = { detail::kSystemDomainFreeTree, detail::kSystemDomainSymbols,
                                                     detail::kSystemDomainRegistry, detail::kServiceNameDomainRoot };
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
        if ( pstringview::forest_domain_ops().root_index() == 0 )
            return false;
        const forest_domain* reg_rec = find_domain_by_name_unlocked( detail::kSystemDomainRegistry );
        if ( reg_rec->root_offset != hdr->root_offset )
            return false;
        const forest_domain* root_rec = find_domain_by_name_unlocked( detail::kServiceNameDomainRoot );
        if ( root_rec->binding_kind != detail::kForestBindingDirectRoot )
            return false;
        return true;
    }
    static bool validate_or_bootstrap_forest_registry_unlocked() noexcept
    {
        detail::ManagerHeader<address_traits>* hdr           = get_header( _backend.base_ptr() );
        const index_type                       registry_root = hdr->root_offset;
        if ( forest_registry_root_unlocked() != nullptr )
        {
            if ( !register_domain_unlocked( detail::kSystemDomainFreeTree, detail::kForestDomainFlagSystem,
                                            detail::kForestBindingFreeTree, 0 ) )
                return false;
            if ( !register_domain_unlocked( detail::kSystemDomainSymbols, detail::kForestDomainFlagSystem,
                                            detail::kForestBindingDirectRoot,
                                            pstringview::forest_domain_ops().root_index() ) )
                return false;
            if ( !register_domain_unlocked( detail::kSystemDomainRegistry, detail::kForestDomainFlagSystem,
                                            detail::kForestBindingDirectRoot, registry_root ) )
                return false;
            if ( !register_domain_unlocked( detail::kServiceNameDomainRoot, detail::kForestDomainFlagSystem,
                                            detail::kForestBindingDirectRoot, 0 ) )
                return false;
            return bootstrap_system_symbols_unlocked();
        }
        hdr->root_offset = address_traits::no_block;
        return bootstrap_forest_registry_unlocked();
    }
    template <typename Callback>
    static void for_each_free_block_inorder( const uint8_t* base, const detail::ManagerHeader<address_traits>* hdr,
                                             index_type node_idx, int depth, Callback&& callback ) noexcept
    {
        using BlockState                = BlockStateBase<address_traits>;
        static constexpr size_t kGranSz = address_traits::granule_size;
        if ( node_idx == address_traits::no_block )
            return;
        if ( static_cast<size_t>( node_idx ) * kGranSz + sizeof( Block<address_traits> ) > hdr->total_size )
            return;
        const void*                  blk_raw    = base + static_cast<size_t>( node_idx ) * kGranSz;
        const Block<address_traits>* blk        = reinterpret_cast<const Block<address_traits>*>( blk_raw );
        index_type                   left_off   = BlockState::get_left_offset( blk_raw );
        index_type                   right_off  = BlockState::get_right_offset( blk_raw );
        index_type                   parent_off = BlockState::get_parent_offset( blk_raw );
        for_each_free_block_inorder( base, hdr, left_off, depth + 1, callback );
        index_type    total_gran = detail::block_total_granules( base, hdr, blk );
        FreeBlockView view;
        view.offset        = static_cast<std::ptrdiff_t>( static_cast<size_t>( node_idx ) * kGranSz );
        view.total_size    = static_cast<size_t>( total_gran ) * kGranSz;
        view.free_size     = static_cast<size_t>( total_gran - kBlockHdrGranules ) * kGranSz;
        view.left_offset   = ( left_off != address_traits::no_block )
                                 ? static_cast<std::ptrdiff_t>( static_cast<size_t>( left_off ) * kGranSz )
                                 : -1;
        view.right_offset  = ( right_off != address_traits::no_block )
                                 ? static_cast<std::ptrdiff_t>( static_cast<size_t>( right_off ) * kGranSz )
                                 : -1;
        view.parent_offset = ( parent_off != address_traits::no_block )
                                 ? static_cast<std::ptrdiff_t>( static_cast<size_t>( parent_off ) * kGranSz )
                                 : -1;
        view.avl_height    = BlockState::get_avl_height( blk_raw );
        view.avl_depth     = depth;
        callback( view );
        for_each_free_block_inorder( base, hdr, right_off, depth + 1, callback );
    }
    static pmm::Block<address_traits>* find_block_from_user_ptr( void* ptr ) noexcept
    {
        return const_cast<pmm::Block<address_traits>*>( find_block_from_user_ptr( static_cast<const void*>( ptr ) ) );
    }
    static const pmm::Block<address_traits>* find_block_from_user_ptr( const void* ptr ) noexcept
    {
        const uint8_t* base = _backend.base_ptr();
        const auto*    hdr  = get_header_c( base );
        return detail::header_from_ptr_t<address_traits>( const_cast<uint8_t*>( base ), const_cast<void*>( ptr ),
                                                          static_cast<size_t>( hdr->total_size ) );
    }
    template <typename T> static pptr<T> make_pptr_from_raw( void* raw ) noexcept
    {
        if ( raw == nullptr || !_initialized )
            return pptr<T>();
        uint8_t* base = _backend.base_ptr();
        if ( base == nullptr )
            return pptr<T>();
        detail::ArenaAddress<address_traits> addr{ base, _backend.total_size() };
        auto                                 user_idx = addr.try_user_idx_from_raw( raw );
        if ( !user_idx.has_value() )
            return pptr<T>();
        auto blk_idx_opt = addr.try_block_idx_from_user_idx( *user_idx );
        if ( !blk_idx_opt.has_value() )
            return pptr<T>();
        const void*         blk       = addr.block_unchecked( *blk_idx_opt );
        const pmm::NodeType node_type = BlockStateBase<address_traits>::get_node_type( blk );
        if ( !pmm::is_allocated( node_type ) || BlockStateBase<address_traits>::get_root_offset( blk ) != *blk_idx_opt )
            return pptr<T>();
        if ( !pmm::is_known_node_type( static_cast<std::uint8_t>( node_type ) ) )
            return pptr<T>();
        return pptr<T>( *user_idx );
    }
    template <typename T> static const void* block_raw_ptr_from_pptr( pptr<T> p ) noexcept
    {
        detail::ConstArenaAddress<address_traits> addr{ _backend.base_ptr(), _backend.total_size() };
        auto                                      blk_idx = addr.try_block_idx_from_user_idx( p.offset() );
        return blk_idx.has_value() ? addr.block( *blk_idx ) : nullptr;
    }
    template <typename T> static void* block_raw_mut_ptr_from_pptr( pptr<T> p ) noexcept
    {
        detail::ArenaAddress<address_traits> addr{ _backend.base_ptr(), _backend.total_size() };
        auto                                 blk_idx = addr.try_block_idx_from_user_idx( p.offset() );
        return blk_idx.has_value() ? static_cast<void*>( addr.block( *blk_idx ) ) : nullptr;
    }
    template <typename T> static constexpr index_type block_idx_from_pptr( pptr<T> p ) noexcept
    {
        return static_cast<index_type>( p.offset() - kBlockHdrGranules );
    }
    template <typename T> static void* raw_user_ptr_from_pptr( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return nullptr;
        detail::ArenaAddress<address_traits> addr{ _backend.base_ptr(), _backend.total_size() };
        return addr.try_user_ptr( p.offset(), sizeof( T ) );
    }
    template <typename T> static void* raw_block_user_ptr_from_pptr( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return nullptr;
        if constexpr ( sizeof( Block<address_traits> ) % address_traits::granule_size == 0 )
            return raw_user_ptr_from_pptr( p );
        else
        {
            detail::ArenaAddress<address_traits> addr{ _backend.base_ptr(), _backend.total_size() };
            auto                                 blk_idx = addr.try_block_idx_from_user_idx( p.offset() );
            if ( !blk_idx.has_value() )
                return nullptr;
            Block<address_traits>* blk = addr.block( *blk_idx );
            return blk ? reinterpret_cast<uint8_t*>( blk ) + sizeof( Block<address_traits> ) : nullptr;
        }
    }
    static const char* pstringview_c_str_unlocked( pptr<pstringview> p ) noexcept
    {
        const void* raw = raw_user_ptr_from_pptr( p );
        if ( raw == nullptr )
            return nullptr;
        return static_cast<const char*>( raw ) + offsetof( pstringview, str );
    }
#include "pmm/verify_repair_mixin.inc"
    static constexpr size_t     kBlockHdrByteSize = detail::manager_header_offset_bytes_v<address_traits>;
    static constexpr index_type kBlockHdrGranules =
        static_cast<index_type>( kBlockHdrByteSize / address_traits::granule_size );
    static constexpr index_type                   kMgrHdrGranules   = detail::kManagerHeaderGranules_t<address_traits>;
    static constexpr index_type                   kFreeBlkIdxLayout = kBlockHdrGranules + kMgrHdrGranules;
    static detail::ManagerHeader<address_traits>* get_header( uint8_t* base ) noexcept
    {
        return detail::manager_header_at<address_traits>( base );
    }
    static const detail::ManagerHeader<address_traits>* get_header_c( const uint8_t* base ) noexcept
    {
        return detail::manager_header_at<address_traits>( base );
    }
    struct layout_access
    {
        using address_traits                                            = manager_type::address_traits;
        using free_block_tree                                           = manager_type::free_block_tree;
        using logging_policy                                            = manager_type::logging_policy;
        using storage_backend                                           = manager_type::storage_backend;
        using index_type                                                = manager_type::index_type;
        static constexpr uint64_t                     kMagic            = pmm::kMagic;
        static constexpr size_t                       kBlockHdrByteSize = manager_type::kBlockHdrByteSize;
        static constexpr index_type                   kBlockHdrGranules = manager_type::kBlockHdrGranules;
        static constexpr index_type                   kMgrHdrGranules   = manager_type::kMgrHdrGranules;
        static constexpr index_type                   kFreeBlkIdxLayout = manager_type::kFreeBlkIdxLayout;
        static constexpr size_t                       kGrowNumerator    = ConfigT::grow_numerator;
        static constexpr size_t                       kGrowDenominator  = ConfigT::grow_denominator;
        static constexpr size_t                       kMaxMemoryGB      = ConfigT::max_memory_gb;
        static detail::ManagerHeader<address_traits>* get_header( uint8_t* base ) noexcept
        {
            return manager_type::get_header( base );
        }
        static void set_initialized() noexcept { manager_type::_initialized = true; }
    };
    static bool init_layout( uint8_t* base, size_t size ) noexcept
    {
        return detail::ManagerLayoutOps<layout_access>::init_layout( _backend, base, size );
    }
    static bool do_expand( index_type data_gran ) noexcept
    {
        return detail::ManagerLayoutOps<layout_access>::do_expand( _backend, _initialized, data_gran );
    }
};
}
