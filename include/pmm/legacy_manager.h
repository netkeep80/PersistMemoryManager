/**
 * @file persist_memory_manager.h
 * @brief Менеджер персистентной кучи памяти для C++17
 *
 * Multi-header библиотека управления персистентной памятью.
 * Предоставляет низкоуровневый менеджер памяти, хранящий все метаданные
 * в управляемой области памяти для возможности персистентности между запусками.
 *
 * Алгоритм (Issue #55): Каждый блок содержит 6 ключевых полей:
 *   1. size         — занятый размер данных в гранулах (0 = свободный блок, Issue #75)
 *   2. prev_offset  — индекс предыдущего блока (kNoBlock = нет)
 *   3. next_offset  — индекс следующего блока (kNoBlock = последний)
 *   4. left_offset  — левый дочерний узел AVL-дерева свободных блоков
 *   5. right_offset — правый дочерний узел AVL-дерева свободных блоков
 *   6. parent_offset — родительский узел AVL-дерева
 *
 * Поиск свободного блока: best-fit через AVL-дерево (O(log n)).
 * При освобождении: слияние с соседними свободными блоками.
 *
 * @version 6.1.0
 * Refactoring (Issue #73): template<Config> PersistMemoryManager with CRTP mixins.
 *   PmmCore<Derived> base, StatsMixin<Base>, ValidationMixin<Base>.
 *   Config: pmm::config::PMMConfig<GranuleSize, MaxMemoryGB, LockPolicy>.
 *   AVL tree extracted to PersistentAvlTree (persist_avl_tree.h).
 *   Types extracted to persist_memory_types.h.
 *   Backward compatible: pmm::PersistMemoryManager<> for default config.
 *
 * Block modernization (Issue #69): removed BlockHeader.magic; validity via is_valid_block()
 * Refactoring (Issue #75): PAP homogenization. kMagic updated to "PMM_V083" (Issue #83).
 * Optimizations (Issue #57, #59): O(1) ops, 16-byte granule, 32-bit indices.
 * Refactoring (Issue #61): fully static PersistMemoryManager; pptr<T>-only public API.
 * Bug fix (Issue #67): reallocate_typed re-derives old pointer after possible expand().
 *
 * @deprecated (Issue #97): This singleton-based manager is deprecated.
 *   Migrate to AbstractPersistMemoryManager and pmm_presets.h for RAII-style usage.
 *   Example migration:
 *     Old: auto ptr = PersistMemoryManager<>::allocate_typed<int>();
 *     New: pmm::presets::SingleThreadedHeap pmm; pmm.create(1024); auto ptr = pmm.allocate(sizeof(int));
 */

#pragma once

#include "pmm/types.h"
#include "pmm/free_block_tree.h"
#include "pmm/config.h"
#include "pmm/pptr.h"

#include <cstdlib>
#include <cstring>

namespace pmm
{

// Forward declaration for the primary template
template <typename Config = config::DefaultConfig> class PersistMemoryManager;

// Forward declarations for free functions
inline MemoryStats get_stats();
inline ManagerInfo get_manager_info();

// ─── CRTP базовый класс PmmCore ───────────────────────────────────────────────

/// @brief CRTP базовый класс, предоставляющий доступ к памяти (Issue #73 AR-01).
/// Derived must be the final PersistMemoryManager<Config> class.
template <typename Derived> class PmmCore
{
  public:
    /// @brief Tag typedef so CRTP mixins can access the final derived type.
    using derived_type = Derived;

  protected:
    std::uint8_t*       base_ptr() { return reinterpret_cast<std::uint8_t*>( this ); }
    const std::uint8_t* const_base_ptr() const { return reinterpret_cast<const std::uint8_t*>( this ); }

    // Issue #75: ManagerHeader lives inside BlockHeader_0 (after first BlockHeader)
    detail::ManagerHeader* header()
    {
        return reinterpret_cast<detail::ManagerHeader*>( this->base_ptr() + sizeof( detail::BlockHeader ) );
    }
    const detail::ManagerHeader* header() const
    {
        return reinterpret_cast<const detail::ManagerHeader*>( this->const_base_ptr() + sizeof( detail::BlockHeader ) );
    }
};

// ─── CRTP-миксин StatsMixin ───────────────────────────────────────────────────

/// @brief CRTP-миксин, добавляющий статический метод get_stats() (Issue #73 AR-01).
/// @tparam Base  PmmCore<Derived> or another mixin on top of PmmCore<Derived>.
template <typename Base> class StatsMixin : public Base
{
  public:
    static MemoryStats get_stats()
    {
        using Derived = typename Base::derived_type;
        MemoryStats stats{};
        Derived*    mgr = Derived::s_instance;
        if ( mgr == nullptr )
            return stats;
        const std::uint8_t*          base = mgr->const_base_ptr();
        const detail::ManagerHeader* hdr  = mgr->header();
        stats.total_blocks                = hdr->block_count;
        stats.free_blocks                 = hdr->free_count;
        stats.allocated_blocks            = hdr->alloc_count;
        bool          first_free          = true;
        std::uint32_t idx                 = hdr->first_block_offset;
        while ( idx != detail::kNoBlock )
        {
            const detail::BlockHeader* blk = detail::block_at( base, idx );
            if ( blk->size == 0 )
            {
                std::uint32_t gran     = ( blk->next_offset != detail::kNoBlock )
                                             ? ( blk->next_offset - idx )
                                             : ( detail::byte_off_to_idx( hdr->total_size ) - idx );
                std::size_t   blk_size = detail::granules_to_bytes( gran );
                if ( first_free )
                {
                    stats.largest_free  = blk_size;
                    stats.smallest_free = blk_size;
                    first_free          = false;
                }
                else
                {
                    stats.largest_free  = std::max( stats.largest_free, blk_size );
                    stats.smallest_free = std::min( stats.smallest_free, blk_size );
                }
                stats.total_fragmentation += blk_size;
            }
            idx = blk->next_offset;
        }
        return stats;
    }
};

// ─── CRTP-миксин ValidationMixin ─────────────────────────────────────────────

/// @brief CRTP-миксин, добавляющий статический метод validate() (Issue #73 AR-01).
template <typename Base> class ValidationMixin : public Base
{
  public:
    static bool validate()
    {
        using Derived    = typename Base::derived_type;
        using LockPolicy = typename Derived::config_type::lock_policy;
        Derived* mgr     = Derived::s_instance;
        if ( mgr == nullptr )
            return false;
        typename LockPolicy::shared_lock_type lock( Derived::s_mutex );
        const std::uint8_t*                   base = mgr->const_base_ptr();
        const detail::ManagerHeader*          hdr  = mgr->header();

        if ( hdr->magic != kMagic )
            return false;
        std::size_t   block_count = 0, free_count = 0, alloc_count = 0;
        std::uint32_t idx = hdr->first_block_offset;
        while ( idx != detail::kNoBlock )
        {
            if ( !detail::is_valid_block( base, hdr, idx ) )
                return false;
            const detail::BlockHeader* blk = detail::block_at( base, idx );
            block_count++;
            if ( blk->size > 0 )
                alloc_count++;
            else
                free_count++;
            if ( blk->next_offset != detail::kNoBlock )
            {
                const detail::BlockHeader* nxt = detail::block_at( base, blk->next_offset );
                if ( nxt->prev_offset != idx )
                    return false;
            }
            idx = blk->next_offset;
        }
        std::size_t tree_free = 0;
        if ( !validate_avl( base, hdr, hdr->free_tree_root, tree_free ) )
            return false;
        if ( tree_free != free_count )
            return false;
        return ( block_count == hdr->block_count && free_count == hdr->free_count && alloc_count == hdr->alloc_count );
    }

  private:
    static bool validate_avl( const std::uint8_t* base, const detail::ManagerHeader* hdr, std::uint32_t node_idx,
                              std::size_t& count )
    {
        if ( node_idx == detail::kNoBlock )
            return true;
        if ( detail::idx_to_byte_off( node_idx ) >= hdr->total_size )
            return false;
        if ( !detail::is_valid_block( base, hdr, node_idx ) )
            return false;
        const detail::BlockHeader* node = detail::block_at( base, node_idx );
        if ( node->size != 0 )
            return false;
        count++;
        if ( !validate_avl( base, hdr, node->left_offset, count ) )
            return false;
        if ( !validate_avl( base, hdr, node->right_offset, count ) )
            return false;
        if ( node->left_offset != detail::kNoBlock )
        {
            const detail::BlockHeader* lc = detail::block_at( base, node->left_offset );
            if ( lc->parent_offset != node_idx )
                return false;
        }
        if ( node->right_offset != detail::kNoBlock )
        {
            const detail::BlockHeader* rc = detail::block_at( base, node->right_offset );
            if ( rc->parent_offset != node_idx )
                return false;
        }
        return true;
    }
};

// ─── Основной класс ───────────────────────────────────────────────────────────

/// @brief Менеджер персистентной памяти — полностью статический класс-шаблон (Issue #73).
/// Public API: only pptr<T> and PersistMemoryManager<Config>::static_method().
/// @tparam Config  Конфигурация (pmm::config::PMMConfig<...>). По умолчанию DefaultConfig.
/// Architecture (Issue #73): ValidationMixin<StatsMixin<PmmCore<Derived>>> CRTP chain.
template <typename Config>
class PersistMemoryManager : public ValidationMixin<StatsMixin<PmmCore<PersistMemoryManager<Config>>>>
{
  public:
    using config_type = Config;
    using LockPolicy  = typename Config::lock_policy;

    static PersistMemoryManager*           s_instance;
    static typename LockPolicy::mutex_type s_mutex;

    // ─── Управление жизненным циклом ─────────────────────────────────────────

    /// @brief Создать менеджер на буфере memory (>= detail::kMinMemorySize, granule-aligned).
    static bool create( void* memory, std::size_t size )
    {
        typename LockPolicy::unique_lock_type lock( s_mutex );
        if ( memory == nullptr || size < detail::kMinMemorySize )
            return false;
        if ( size > static_cast<std::uint64_t>( std::numeric_limits<std::uint32_t>::max() ) * kGranuleSize )
            return false;
        if ( size % kGranuleSize != 0 )
            size -= ( size % kGranuleSize );

        std::uint8_t* base = static_cast<std::uint8_t*>( memory );
        // Issue #75: BlockHeader_0 at granule 0 holds ManagerHeader as user data.
        static constexpr std::uint32_t kHdrBlkIdx  = 0;
        static constexpr std::uint32_t kFreeBlkIdx = detail::kBlockHeaderGranules + detail::kManagerHeaderGranules;

        if ( detail::idx_to_byte_off( kFreeBlkIdx ) + sizeof( detail::BlockHeader ) + detail::kMinBlockSize > size )
            return false;

        detail::BlockHeader* hdr_blk = detail::block_at( base, kHdrBlkIdx );
        std::memset( hdr_blk, 0, sizeof( detail::BlockHeader ) );
        hdr_blk->size          = detail::kManagerHeaderGranules;
        hdr_blk->prev_offset   = detail::kNoBlock;
        hdr_blk->next_offset   = kFreeBlkIdx;
        hdr_blk->left_offset   = detail::kNoBlock;
        hdr_blk->right_offset  = detail::kNoBlock;
        hdr_blk->parent_offset = detail::kNoBlock;
        // hdr_blk->avl_height = 0; — already zero from memset
        hdr_blk->root_offset = kHdrBlkIdx;

        detail::ManagerHeader* hdr = reinterpret_cast<detail::ManagerHeader*>( base + sizeof( detail::BlockHeader ) );
        std::memset( hdr, 0, sizeof( detail::ManagerHeader ) );
        hdr->magic              = kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = kHdrBlkIdx;
        hdr->last_block_offset  = detail::kNoBlock;
        hdr->free_tree_root     = detail::kNoBlock;
        // hdr->owns_memory = false;      — already false from memset
        // hdr->prev_owns_memory = false; — already false from memset
        hdr->granule_size = static_cast<std::uint16_t>( kGranuleSize ); ///< Issue #83

        detail::BlockHeader* blk = detail::block_at( base, kFreeBlkIdx );
        std::memset( blk, 0, sizeof( detail::BlockHeader ) );
        // blk->size = 0;        — already zero from memset
        blk->prev_offset   = kHdrBlkIdx;
        blk->next_offset   = detail::kNoBlock;
        blk->left_offset   = detail::kNoBlock;
        blk->right_offset  = detail::kNoBlock;
        blk->parent_offset = detail::kNoBlock;
        blk->avl_height    = 1;
        // blk->root_offset = 0; — already zero from memset

        hdr->last_block_offset = kFreeBlkIdx;
        hdr->free_tree_root    = kFreeBlkIdx;
        hdr->block_count       = 2;
        hdr->free_count        = 1;
        hdr->alloc_count       = 1;
        hdr->used_size         = kFreeBlkIdx + detail::kBlockHeaderGranules;

        s_instance = reinterpret_cast<PersistMemoryManager*>( base );
        return true;
    }

    /// @brief Загрузить сохранённый менеджер из буфера.
    static bool load( void* memory, std::size_t size )
    {
        typename LockPolicy::unique_lock_type lock( s_mutex );
        if ( memory == nullptr || size < detail::kMinMemorySize )
            return false;
        std::uint8_t* base = static_cast<std::uint8_t*>( memory );
        auto*         hdr  = reinterpret_cast<detail::ManagerHeader*>( base + sizeof( detail::BlockHeader ) );
        if ( hdr->magic != kMagic || hdr->total_size != size )
            return false;
        if ( hdr->granule_size != static_cast<std::uint16_t>( kGranuleSize ) ) ///< Issue #83
            return false;
        hdr->owns_memory = hdr->prev_owns_memory = false;
        hdr->prev_total_size                     = 0;
        hdr->prev_base_ptr                       = nullptr;
        auto* mgr                                = reinterpret_cast<PersistMemoryManager*>( base );
        mgr->repair_linked_list();
        mgr->recompute_counters();
        mgr->rebuild_free_tree();
        s_instance = mgr;
        return true;
    }

    /// @brief Уничтожить менеджер; освободить expand() буферы.
    static void destroy()
    {
        typename LockPolicy::unique_lock_type lock( s_mutex );
        if ( s_instance == nullptr )
            return;
        detail::ManagerHeader* hdr = s_instance->header();
        hdr->magic                 = 0;
        bool  owns                 = hdr->owns_memory;
        void* buf                  = s_instance->base_ptr();
        void* prev                 = hdr->prev_base_ptr;
        bool  prev_owns            = hdr->prev_owns_memory;
        s_instance                 = nullptr;
        while ( prev != nullptr )
        {
            auto* ph        = reinterpret_cast<detail::ManagerHeader*>( static_cast<std::uint8_t*>( prev ) +
                                                                        sizeof( detail::BlockHeader ) );
            void* next_prev = ph->prev_base_ptr;
            bool  next_owns = ph->prev_owns_memory;
            if ( prev_owns )
                std::free( prev );
            prev      = next_prev;
            prev_owns = next_owns;
        }
        if ( owns )
            std::free( buf );
    }

    // ─── Типизированное выделение памяти (публичный API — Issue #61) ──────────

    template <class T> static pptr<T> allocate_typed()
    {
        void* raw = s_instance ? s_instance->allocate_raw( sizeof( T ) ) : nullptr;
        if ( raw == nullptr )
            return pptr<T>();
        std::uint8_t* base     = s_instance->base_ptr();
        std::size_t   byte_off = static_cast<std::uint8_t*>( raw ) - base;
        assert( byte_off % kGranuleSize == 0 );
        return pptr<T>( static_cast<std::uint32_t>( byte_off / kGranuleSize ) );
    }

    template <class T> static pptr<T> allocate_typed( std::size_t count )
    {
        if ( count == 0 )
            return pptr<T>();
        if ( sizeof( T ) > 0 && count > std::numeric_limits<std::size_t>::max() / sizeof( T ) )
            return pptr<T>();
        void* raw = s_instance ? s_instance->allocate_raw( sizeof( T ) * count ) : nullptr;
        if ( raw == nullptr )
            return pptr<T>();
        std::uint8_t* base     = s_instance->base_ptr();
        std::size_t   byte_off = static_cast<std::uint8_t*>( raw ) - base;
        assert( byte_off % kGranuleSize == 0 );
        return pptr<T>( static_cast<std::uint32_t>( byte_off / kGranuleSize ) );
    }

    template <class T> static void deallocate_typed( pptr<T> p )
    {
        if ( !p.is_null() && s_instance != nullptr )
        {
            void* raw = s_instance->base_ptr() + detail::idx_to_byte_off( p.offset() );
            s_instance->deallocate_raw( raw );
        }
    }

    template <class T> static pptr<T> reallocate_typed( pptr<T> p, std::size_t count )
    {
        if ( p.is_null() )
            return allocate_typed<T>( count );
        if ( count == 0 )
        {
            deallocate_typed( p );
            return pptr<T>();
        }
        if ( s_instance == nullptr )
            return pptr<T>();

        std::uint8_t*        base    = s_instance->base_ptr();
        void*                old_raw = base + detail::idx_to_byte_off( p.offset() );
        detail::BlockHeader* blk =
            detail::header_from_ptr( base, old_raw, static_cast<std::size_t>( s_instance->header()->total_size ) );
        if ( blk == nullptr || blk->size == 0 )
            return pptr<T>();

        if ( count > 0 && sizeof( T ) > std::numeric_limits<std::size_t>::max() / count )
            return pptr<T>();
        std::uint32_t new_granules = detail::bytes_to_granules( sizeof( T ) * count );
        if ( new_granules <= blk->size )
            return p;

        std::size_t   old_bytes  = detail::granules_to_bytes( blk->size );
        std::uint32_t old_offset = p.offset();
        pptr<T>       new_p      = allocate_typed<T>( count );
        if ( new_p.is_null() )
            return pptr<T>();

        // Re-derive after possible auto-expand (Issue #67).
        std::uint8_t* cur_base = s_instance->base_ptr();
        void*         new_raw  = cur_base + detail::idx_to_byte_off( new_p.offset() );
        void*         cur_old  = cur_base + detail::idx_to_byte_off( old_offset );
        std::memcpy( new_raw, cur_old, old_bytes );
        deallocate_typed( p );
        return new_p;
    }

    // ─── Статические методы доступа к состоянию (Issue #61) ──────────────────

    static void* offset_to_ptr( std::uint32_t idx ) noexcept
    {
        if ( s_instance == nullptr || idx == 0 )
            return nullptr;
        return s_instance->base_ptr() + detail::idx_to_byte_off( idx );
    }

    static std::size_t block_data_size_bytes( std::uint32_t data_idx ) noexcept
    {
        if ( s_instance == nullptr || data_idx == 0 )
            return 0;
        if ( data_idx < detail::kBlockHeaderGranules )
            return 0;
        std::uint8_t* base    = s_instance->base_ptr();
        std::uint32_t blk_idx = data_idx - detail::kBlockHeaderGranules;
        if ( blk_idx == 0 )
            return 0;
        const detail::ManagerHeader* hdr = s_instance->header();
        if ( !detail::is_valid_block( base, hdr, blk_idx ) )
            return 0;
        const detail::BlockHeader* blk = detail::block_at( base, blk_idx );
        if ( blk->size == 0 )
            return 0;
        return detail::granules_to_bytes( blk->size );
    }

    static std::size_t total_size() noexcept
    {
        return s_instance ? static_cast<std::size_t>( s_instance->header()->total_size ) : 0;
    }

    static std::size_t manager_header_size() noexcept
    {
        return sizeof( detail::BlockHeader ) + sizeof( detail::ManagerHeader );
    }

    static std::size_t used_size() noexcept
    {
        return s_instance ? detail::granules_to_bytes( s_instance->header()->used_size ) : 0;
    }

    static std::size_t free_size() noexcept
    {
        if ( s_instance == nullptr )
            return 0;
        const detail::ManagerHeader* hdr        = s_instance->header();
        std::size_t                  used_bytes = detail::granules_to_bytes( hdr->used_size );
        return ( hdr->total_size > used_bytes ) ? ( hdr->total_size - used_bytes ) : 0;
    }

    static std::size_t fragmentation() noexcept
    {
        if ( s_instance == nullptr )
            return 0;
        const detail::ManagerHeader* hdr = s_instance->header();
        return ( hdr->free_count > 1 ) ? ( hdr->free_count - 1 ) : 0;
    }

    static void dump_stats( std::ostream& os )
    {
        if ( s_instance == nullptr )
        {
            os << "=== PersistMemoryManager: no instance ===\n";
            return;
        }
        typename LockPolicy::shared_lock_type lock( s_mutex );
        const detail::ManagerHeader*          hdr = s_instance->header();
        os << "=== PersistMemoryManager stats ===\n"
           << "  total_size  : " << hdr->total_size << " bytes\n"
           << "  used_size   : " << detail::granules_to_bytes( hdr->used_size ) << " bytes\n"
           << "  free_size   : "
           << ( hdr->total_size > detail::granules_to_bytes( hdr->used_size )
                    ? hdr->total_size - detail::granules_to_bytes( hdr->used_size )
                    : 0ULL )
           << " bytes\n"
           << "  blocks      : " << hdr->block_count << " (free=" << hdr->free_count << ", alloc=" << hdr->alloc_count
           << ")\n"
           << "  fragmentation: " << ( ( hdr->free_count > 1 ) ? ( hdr->free_count - 1 ) : 0U )
           << " extra free segments\n"
           << "==================================\n";
    }

    static bool is_initialized() noexcept { return s_instance != nullptr; }

    friend MemoryStats                       get_stats();
    friend ManagerInfo                       get_manager_info();
    template <typename Callback> friend void for_each_block( Callback&& cb );
    template <typename Callback> friend void for_each_free_block_avl( Callback&& cb );

    static PersistMemoryManager* instance() noexcept { return s_instance; }

  private:
    void* allocate_raw( std::size_t user_size )
    {
        typename LockPolicy::unique_lock_type lock( s_mutex );
        if ( user_size == 0 )
            return nullptr;

        std::uint8_t*          base    = this->base_ptr();
        detail::ManagerHeader* hdr     = this->header();
        std::uint32_t          needed  = detail::required_block_granules( user_size );
        std::uint32_t          blk_idx = PersistentAvlTree::find_best_fit( base, hdr, needed );

        if ( blk_idx != detail::kNoBlock )
            return allocate_from_block( detail::block_at( base, blk_idx ), user_size );

        if ( !expand( user_size ) )
            return nullptr;

        PersistMemoryManager* new_mgr = s_instance;
        if ( new_mgr == nullptr )
            return nullptr;
        std::uint8_t*          nb      = new_mgr->base_ptr();
        detail::ManagerHeader* nh      = new_mgr->header();
        std::uint32_t          new_idx = PersistentAvlTree::find_best_fit( nb, nh, needed );
        if ( new_idx != detail::kNoBlock )
            return new_mgr->allocate_from_block( detail::block_at( nb, new_idx ), user_size );
        return nullptr;
    }

    void deallocate_raw( void* ptr )
    {
        typename LockPolicy::unique_lock_type lock( s_mutex );
        if ( ptr == nullptr )
            return;
        ptr                         = translate_ptr( ptr );
        std::uint8_t*          base = this->base_ptr();
        detail::ManagerHeader* hdr  = this->header();
        detail::BlockHeader*   blk  = detail::header_from_ptr( base, ptr, static_cast<std::size_t>( hdr->total_size ) );
        if ( blk == nullptr || blk->size == 0 )
            return;

        std::uint32_t freed = blk->size;
        blk->size           = 0;
        blk->root_offset    = 0;
        hdr->alloc_count--;
        hdr->free_count++;
        if ( hdr->used_size >= freed )
            hdr->used_size -= freed;
        coalesce( blk );
    }

    bool expand( std::size_t user_size )
    {
        detail::ManagerHeader* hdr      = this->header();
        std::size_t            old_size = hdr->total_size;
        std::uint32_t          needed   = detail::required_block_granules( user_size );

        std::size_t growth   = ( old_size / Config::grow_denominator ) * Config::grow_numerator; ///< Issue #83
        std::size_t new_size = ( ( growth + kGranuleSize - 1 ) / kGranuleSize ) * kGranuleSize;
        if ( new_size <= old_size )
            new_size = old_size + kGranuleSize;
        if ( new_size < old_size + detail::granules_to_bytes( needed ) )
        {
            std::size_t extra = detail::granules_to_bytes( needed + detail::kBlockHeaderGranules );
            if ( extra > std::numeric_limits<std::size_t>::max() - old_size - ( kGranuleSize - 1 ) )
                return false;
            new_size = ( ( old_size + extra + kGranuleSize - 1 ) / kGranuleSize ) * kGranuleSize;
        }

        if ( new_size > static_cast<std::uint64_t>( std::numeric_limits<std::uint32_t>::max() ) * kGranuleSize )
            return false;

        void* new_memory = std::malloc( new_size );
        if ( new_memory == nullptr )
            return false;

        std::memcpy( new_memory, this->base_ptr(), old_size );
        std::uint8_t*          nb      = static_cast<std::uint8_t*>( new_memory );
        detail::ManagerHeader* nh      = reinterpret_cast<detail::ManagerHeader*>( nb + sizeof( detail::BlockHeader ) );
        nh->owns_memory                = true;
        std::uint32_t        extra_idx = detail::byte_off_to_idx( old_size );
        std::size_t          extra_size = new_size - old_size;
        detail::BlockHeader* last_blk =
            ( nh->last_block_offset != detail::kNoBlock ) ? detail::block_at( nb, nh->last_block_offset ) : nullptr;

        if ( last_blk != nullptr && last_blk->size == 0 )
        {
            std::uint32_t loff = detail::block_idx( nb, last_blk );
            PersistentAvlTree::remove( nb, nh, loff );
            nh->total_size = new_size;
            PersistentAvlTree::insert( nb, nh, loff );
        }
        else
        {
            if ( extra_size < sizeof( detail::BlockHeader ) + detail::kMinBlockSize )
            {
                std::free( new_memory );
                return false;
            }
            detail::BlockHeader* nb_blk = detail::block_at( nb, extra_idx );
            std::memset( nb_blk, 0, sizeof( detail::BlockHeader ) );
            // nb_blk->size = 0;        — already zero from memset
            nb_blk->left_offset   = detail::kNoBlock;
            nb_blk->right_offset  = detail::kNoBlock;
            nb_blk->parent_offset = detail::kNoBlock;
            nb_blk->avl_height    = 1;
            // nb_blk->root_offset = 0; — already zero from memset
            if ( last_blk != nullptr )
            {
                std::uint32_t loff    = detail::block_idx( nb, last_blk );
                nb_blk->prev_offset   = loff;
                nb_blk->next_offset   = detail::kNoBlock;
                last_blk->next_offset = extra_idx;
            }
            else
            {
                nb_blk->prev_offset    = detail::kNoBlock;
                nb_blk->next_offset    = detail::kNoBlock;
                nh->first_block_offset = extra_idx;
            }
            nh->last_block_offset = extra_idx;
            nh->block_count++;
            nh->free_count++;
            nh->total_size = new_size;
            PersistentAvlTree::insert( nb, nh, extra_idx );
        }
        nh->prev_base_ptr    = this->base_ptr();
        nh->prev_total_size  = old_size;
        nh->prev_owns_memory = hdr->owns_memory;
        s_instance           = reinterpret_cast<PersistMemoryManager*>( new_memory );
        return true;
    }

    void rebuild_free_tree()
    {
        std::uint8_t*          base = this->base_ptr();
        detail::ManagerHeader* hdr  = this->header();
        hdr->free_tree_root         = detail::kNoBlock;
        hdr->last_block_offset      = detail::kNoBlock;
        std::uint32_t idx           = hdr->first_block_offset;
        while ( idx != detail::kNoBlock )
        {
            detail::BlockHeader* blk = detail::block_at( base, idx );
            blk->left_offset         = detail::kNoBlock;
            blk->right_offset        = detail::kNoBlock;
            blk->parent_offset       = detail::kNoBlock;
            blk->avl_height          = 0;
            if ( blk->size == 0 )
                PersistentAvlTree::insert( base, hdr, idx );
            if ( blk->next_offset == detail::kNoBlock )
                hdr->last_block_offset = idx;
            idx = blk->next_offset;
        }
    }

    void repair_linked_list()
    {
        std::uint8_t*          base = this->base_ptr();
        detail::ManagerHeader* hdr  = this->header();
        std::uint32_t          idx  = hdr->first_block_offset;
        std::uint32_t          prev = detail::kNoBlock;
        while ( idx != detail::kNoBlock )
        {
            if ( detail::idx_to_byte_off( idx ) + sizeof( detail::BlockHeader ) > hdr->total_size )
                break;
            detail::BlockHeader* blk = detail::block_at( base, idx );
            blk->prev_offset         = prev;
            prev                     = idx;
            idx                      = blk->next_offset;
        }
    }

    void recompute_counters()
    {
        std::uint8_t*          base        = this->base_ptr();
        detail::ManagerHeader* hdr         = this->header();
        std::uint32_t          block_count = 0, free_count = 0, alloc_count = 0;
        std::uint32_t          used_gran = 0;
        std::uint32_t          idx       = hdr->first_block_offset;
        while ( idx != detail::kNoBlock )
        {
            if ( detail::idx_to_byte_off( idx ) + sizeof( detail::BlockHeader ) > hdr->total_size )
                break;
            detail::BlockHeader* blk = detail::block_at( base, idx );
            block_count++;
            used_gran += detail::kBlockHeaderGranules;
            if ( blk->size > 0 )
            {
                alloc_count++;
                used_gran += blk->size;
            }
            else
            {
                free_count++;
            }
            idx = blk->next_offset;
        }
        hdr->block_count = block_count;
        hdr->free_count  = free_count;
        hdr->alloc_count = alloc_count;
        hdr->used_size   = used_gran;
    }

    void* translate_ptr( void* ptr ) const noexcept
    {
        std::uint8_t*                base     = const_cast<std::uint8_t*>( this->const_base_ptr() );
        const detail::ManagerHeader* hdr      = this->header();
        std::uint8_t*                raw      = static_cast<std::uint8_t*>( ptr );
        void*                        prev_buf = hdr->prev_base_ptr;
        std::size_t                  prev_sz  = hdr->prev_total_size;
        while ( prev_buf != nullptr && prev_sz > 0 )
        {
            auto* prev = static_cast<std::uint8_t*>( prev_buf );
            if ( raw >= prev && raw < prev + prev_sz )
                return base + ( raw - prev );
            auto* ph = reinterpret_cast<detail::ManagerHeader*>( prev + sizeof( detail::BlockHeader ) );
            prev_buf = ph->prev_base_ptr;
            prev_sz  = ph->prev_total_size;
        }
        return ptr;
    }

    void coalesce( detail::BlockHeader* blk )
    {
        std::uint8_t*          base  = this->base_ptr();
        detail::ManagerHeader* hdr   = this->header();
        std::uint32_t          b_idx = detail::block_idx( base, blk );

        if ( blk->next_offset != detail::kNoBlock )
        {
            detail::BlockHeader* nxt = detail::block_at( base, blk->next_offset );
            if ( nxt->size == 0 )
            {
                std::uint32_t nxt_idx = blk->next_offset;
                PersistentAvlTree::remove( base, hdr, nxt_idx );
                blk->next_offset = nxt->next_offset;
                if ( nxt->next_offset != detail::kNoBlock )
                    detail::block_at( base, nxt->next_offset )->prev_offset = b_idx;
                else
                    hdr->last_block_offset = b_idx;
                std::memset( nxt, 0, sizeof( detail::BlockHeader ) );
                hdr->block_count--;
                hdr->free_count--;
                if ( hdr->used_size >= detail::kBlockHeaderGranules )
                    hdr->used_size -= detail::kBlockHeaderGranules;
            }
        }

        if ( blk->prev_offset != detail::kNoBlock )
        {
            detail::BlockHeader* prv = detail::block_at( base, blk->prev_offset );
            if ( prv->size == 0 )
            {
                std::uint32_t prv_idx = blk->prev_offset;
                PersistentAvlTree::remove( base, hdr, prv_idx );
                prv->next_offset = blk->next_offset;
                if ( blk->next_offset != detail::kNoBlock )
                    detail::block_at( base, blk->next_offset )->prev_offset = prv_idx;
                else
                    hdr->last_block_offset = prv_idx;
                std::memset( blk, 0, sizeof( detail::BlockHeader ) );
                hdr->block_count--;
                hdr->free_count--;
                if ( hdr->used_size >= detail::kBlockHeaderGranules )
                    hdr->used_size -= detail::kBlockHeaderGranules;
                PersistentAvlTree::insert( base, hdr, prv_idx );
                return;
            }
        }

        PersistentAvlTree::insert( base, hdr, b_idx );
    }

    void* allocate_from_block( detail::BlockHeader* blk, std::size_t user_size )
    {
        std::uint8_t*          base    = this->base_ptr();
        detail::ManagerHeader* hdr     = this->header();
        std::uint32_t          blk_idx = detail::block_idx( base, blk );
        PersistentAvlTree::remove( base, hdr, blk_idx );

        std::uint32_t blk_total_gran = detail::block_total_granules( base, hdr, blk );
        std::uint32_t data_gran      = detail::bytes_to_granules( user_size );
        std::uint32_t needed_gran    = detail::kBlockHeaderGranules + data_gran;
        std::uint32_t min_rem_gran   = detail::kBlockHeaderGranules + 1;
        bool          can_split      = ( blk_total_gran >= needed_gran + min_rem_gran );

        if ( can_split )
        {
            std::uint32_t        new_idx = blk_idx + needed_gran;
            detail::BlockHeader* new_blk = detail::block_at( base, new_idx );
            std::memset( new_blk, 0, sizeof( detail::BlockHeader ) );
            // new_blk->size = 0;        — already zero from memset
            new_blk->prev_offset   = blk_idx;
            new_blk->next_offset   = blk->next_offset;
            new_blk->left_offset   = detail::kNoBlock;
            new_blk->right_offset  = detail::kNoBlock;
            new_blk->parent_offset = detail::kNoBlock;
            new_blk->avl_height    = 1;
            // new_blk->root_offset = 0; — already zero from memset
            if ( blk->next_offset != detail::kNoBlock )
                detail::block_at( base, blk->next_offset )->prev_offset = new_idx;
            else
                hdr->last_block_offset = new_idx;
            blk->next_offset = new_idx;
            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += detail::kBlockHeaderGranules;
            PersistentAvlTree::insert( base, hdr, new_idx );
        }

        blk->size          = data_gran;
        blk->root_offset   = blk_idx;
        blk->left_offset   = detail::kNoBlock;
        blk->right_offset  = detail::kNoBlock;
        blk->parent_offset = detail::kNoBlock;
        blk->avl_height    = 0;
        hdr->alloc_count++;
        hdr->free_count--;
        hdr->used_size += data_gran;
        return detail::user_ptr( blk );
    }
};

// ─── Static member definitions ────────────────────────────────────────────────

template <typename Config> PersistMemoryManager<Config>* PersistMemoryManager<Config>::s_instance = nullptr;

template <typename Config>
typename PersistMemoryManager<Config>::LockPolicy::mutex_type PersistMemoryManager<Config>::s_mutex;

// ─── Реализация методов pptr<T, ManagerT> ──────────────────────────────────────
// Эти методы реализуют устаревший синглтон-API для PersistMemoryManager<>.
// Для нового API используйте mgr.resolve<T>(p) с AbstractPersistMemoryManager.

template <class T, class ManagerT> inline T* pptr<T, ManagerT>::get() const noexcept
{
    if ( _idx == 0 )
        return nullptr;
    return static_cast<T*>( PersistMemoryManager<>::offset_to_ptr( _idx ) );
}

template <class T, class ManagerT> inline T* pptr<T, ManagerT>::get_at( std::size_t index ) const noexcept
{
    T* base_elem = get();
    return ( base_elem == nullptr ) ? nullptr : base_elem + index;
}

template <class T, class ManagerT> inline T* pptr<T, ManagerT>::operator[]( std::size_t i ) const noexcept
{
    if ( _idx == 0 )
        return nullptr;
    std::size_t block_bytes = PersistMemoryManager<>::block_data_size_bytes( _idx );
    std::size_t capacity    = ( sizeof( T ) > 0 ) ? ( block_bytes / sizeof( T ) ) : 0;
    if ( i >= capacity )
        return nullptr;
    T* base_elem = static_cast<T*>( PersistMemoryManager<>::offset_to_ptr( _idx ) );
    return base_elem + i;
}

// ─── Реализация свободных функций ─────────────────────────────────────────────

inline MemoryStats get_stats()
{
    return PersistMemoryManager<>::get_stats();
}

inline ManagerInfo get_manager_info()
{
    ManagerInfo             info{};
    PersistMemoryManager<>* mgr = PersistMemoryManager<>::instance();
    if ( mgr == nullptr )
        return info;
    const detail::ManagerHeader* hdr = mgr->header();
    info.magic                       = hdr->magic;
    info.total_size                  = hdr->total_size;
    info.used_size                   = detail::granules_to_bytes( hdr->used_size );
    info.block_count                 = hdr->block_count;
    info.free_count                  = hdr->free_count;
    info.alloc_count                 = hdr->alloc_count;
    info.first_block_offset          = ( hdr->first_block_offset != detail::kNoBlock )
                                           ? static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( hdr->first_block_offset ) )
                                           : -1;
    info.first_free_offset           = ( hdr->free_tree_root != detail::kNoBlock )
                                           ? static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( hdr->free_tree_root ) )
                                           : -1;
    info.manager_header_size         = sizeof( detail::BlockHeader ) + sizeof( detail::ManagerHeader );
    return info;
}

template <typename Callback> inline void for_each_block( Callback&& cb )
{
    PersistMemoryManager<>* mgr = PersistMemoryManager<>::instance();
    if ( mgr == nullptr )
        return;
    using LockPolicy = config::DefaultConfig::lock_policy;
    typename LockPolicy::shared_lock_type lock( PersistMemoryManager<>::s_mutex );
    const std::uint8_t*                   base  = mgr->const_base_ptr();
    const detail::ManagerHeader*          hdr   = mgr->header();
    std::uint32_t                         idx   = hdr->first_block_offset;
    std::size_t                           index = 0;
    while ( idx != detail::kNoBlock )
    {
        if ( detail::idx_to_byte_off( idx ) >= hdr->total_size )
            break;
        const detail::BlockHeader* blk  = detail::block_at( base, idx );
        std::uint32_t              gran = ( blk->next_offset != detail::kNoBlock )
                                              ? ( blk->next_offset - idx )
                                              : ( detail::byte_off_to_idx( hdr->total_size ) - idx );
        BlockView                  view;
        view.index       = index;
        view.offset      = static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( idx ) );
        view.total_size  = detail::granules_to_bytes( gran );
        view.header_size = sizeof( detail::BlockHeader );
        view.user_size   = detail::granules_to_bytes( blk->size );
        view.alignment   = kGranuleSize; ///< Issue #83: replaced kDefaultAlignment
        view.used        = ( blk->size > 0 );
        cb( view );
        ++index;
        idx = blk->next_offset;
    }
}

template <typename Callback> inline void for_each_free_block_avl( Callback&& cb )
{
    PersistMemoryManager<>* mgr = PersistMemoryManager<>::instance();
    if ( mgr == nullptr )
        return;
    using LockPolicy = config::DefaultConfig::lock_policy;
    typename LockPolicy::shared_lock_type lock( PersistMemoryManager<>::s_mutex );
    const std::uint8_t*                   base   = mgr->const_base_ptr();
    const detail::ManagerHeader*          hdr    = mgr->header();
    auto                                  to_off = []( std::uint32_t i ) -> std::ptrdiff_t
    { return ( i != detail::kNoBlock ) ? static_cast<std::ptrdiff_t>( detail::idx_to_byte_off( i ) ) : -1; };
    for ( std::uint32_t idx = hdr->first_block_offset; idx != detail::kNoBlock; )
    {
        if ( detail::idx_to_byte_off( idx ) >= hdr->total_size )
            break;
        const detail::BlockHeader* blk = detail::block_at( base, idx );
        if ( blk->size == 0 )
        {
            std::uint32_t gran = ( blk->next_offset != detail::kNoBlock )
                                     ? ( blk->next_offset - idx )
                                     : ( detail::byte_off_to_idx( hdr->total_size ) - idx );
            FreeBlockView view;
            view.offset        = to_off( idx );
            view.total_size    = detail::granules_to_bytes( gran );
            view.free_size     = ( gran > detail::kBlockHeaderGranules )
                                     ? detail::granules_to_bytes( gran - detail::kBlockHeaderGranules )
                                     : 0;
            view.left_offset   = to_off( blk->left_offset );
            view.right_offset  = to_off( blk->right_offset );
            view.parent_offset = to_off( blk->parent_offset );
            view.avl_height    = static_cast<int>( blk->avl_height );
            view.avl_depth     = 0;
            cb( view );
        }
        idx = blk->next_offset;
    }
}

} // namespace pmm
