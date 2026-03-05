/**
 * @file pmm/abstract_pmm.h
 * @brief AbstractPersistMemoryManager — параметрический конструктор ПАП-менеджера (Issue #87 Phase 7).
 *
 * Объединяет все отдельные абстракции в один параметризованный менеджер:
 *   - AddressTraitsT   — адресное пространство
 *   - StorageBackendT  — бэкенд хранилища (StaticStorage / HeapStorage / MMapStorage)
 *   - FreeBlockTreeT   — политика дерева свободных блоков (AvlFreeTree / ...)
 *   - ThreadPolicyT    — политика многопоточности (SharedMutexLock / NoLock)
 *
 * Интерфейс:
 *   - `create(backend)`  — инициализировать уже готовый бэкенд
 *   - `load(backend)`    — загрузить существующее состояние из бэкенда
 *   - `destroy()`        — сбросить состояние
 *   - `allocate(size)`   — выделить блок
 *   - `deallocate(ptr)`  — освободить блок
 *
 * Обратная совместимость:
 *   `PersistMemoryManager<DefaultConfig>` продолжает работать без изменений.
 *   `AbstractPersistMemoryManager` — новая параметрическая альтернатива.
 *
 * @see plan_issue87.md §5 «Фаза 7: AbstractPersistMemoryManager»
 * @version 0.1 (Issue #87 Phase 7)
 */

#pragma once

#include "persist_memory_types.h"
#include "pmm/address_traits.h"
#include "pmm/allocator_policy.h"
#include "pmm/free_block_tree.h"
#include "pmm/heap_storage.h"
#include "pmm/storage_backend.h"
#include "pmm_config.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace pmm
{

/**
 * @brief Параметрический ПАП-менеджер.
 *
 * @tparam AddressTraitsT   Traits адресного пространства (из address_traits.h).
 * @tparam StorageBackendT  Бэкенд хранилища (HeapStorage, StaticStorage, MMapStorage).
 * @tparam FreeBlockTreeT   Политика дерева свободных блоков.
 * @tparam ThreadPolicyT    Политика многопоточности.
 */
template <typename AddressTraitsT = DefaultAddressTraits, typename StorageBackendT = HeapStorage<AddressTraitsT>,
          typename FreeBlockTreeT = AvlFreeTree<AddressTraitsT>, typename ThreadPolicyT = config::SharedMutexLock>
class AbstractPersistMemoryManager
{
    static_assert( is_storage_backend_v<StorageBackendT>,
                   "AbstractPersistMemoryManager: StorageBackendT must satisfy StorageBackendConcept" );
    static_assert( is_free_block_tree_policy_v<FreeBlockTreeT>,
                   "AbstractPersistMemoryManager: FreeBlockTreeT must satisfy FreeBlockTreePolicy" );

  public:
    using address_traits  = AddressTraitsT;
    using storage_backend = StorageBackendT;
    using free_block_tree = FreeBlockTreeT;
    using thread_policy   = ThreadPolicyT;
    using allocator       = AllocatorPolicy<FreeBlockTreeT, AddressTraitsT>;

    // ─── Управление жизненным циклом ─────────────────────────────────────────

    /**
     * @brief Инициализировать ПАП-менеджер поверх готового бэкенда.
     *
     * Бэкенд должен уже содержать валидный буфер (base_ptr() != nullptr,
     * total_size() >= kMinMemorySize). Записывает структуры BlockHeader_0,
     * ManagerHeader и первый свободный блок.
     *
     * @return true при успехе.
     */
    bool create() noexcept
    {
        typename ThreadPolicyT::unique_lock_type lock( _mutex );
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < detail::kMinMemorySize )
            return false;
        return init_layout( _backend.base_ptr(), _backend.total_size() );
    }

    /**
     * @brief Инициализировать ПАП-менеджер с указанием начального размера.
     *
     * Вызывает `_backend.expand(initial_size)` для выделения памяти,
     * затем инициализирует структуры данных.
     * Подходит для `HeapStorage`, где буфер изначально пуст.
     *
     * @param initial_size Начальный размер в байтах (>= kMinMemorySize).
     * @return true при успехе.
     */
    bool create( std::size_t initial_size ) noexcept
    {
        typename ThreadPolicyT::unique_lock_type lock( _mutex );
        if ( initial_size < detail::kMinMemorySize )
            return false;
        // Align to granule_size
        std::size_t aligned = ( ( initial_size + kGranuleSize - 1 ) / kGranuleSize ) * kGranuleSize;
        if ( _backend.base_ptr() == nullptr )
        {
            // Backend has no buffer yet — ask it to expand from zero
            if ( !_backend.expand( aligned ) )
                return false;
        }
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < aligned )
            return false;
        return init_layout( _backend.base_ptr(), _backend.total_size() );
    }

    /**
     * @brief Загрузить существующее состояние из бэкенда.
     *
     * Проверяет magic/granule_size, восстанавливает структуры данных.
     *
     * @return true при успехе.
     */
    bool load() noexcept
    {
        typename ThreadPolicyT::unique_lock_type lock( _mutex );
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < detail::kMinMemorySize )
            return false;
        std::uint8_t*          base = _backend.base_ptr();
        detail::ManagerHeader* hdr  = get_header( base );
        if ( hdr->magic != kMagic || hdr->total_size != _backend.total_size() )
            return false;
        if ( hdr->granule_size != static_cast<std::uint16_t>( kGranuleSize ) )
            return false;
        hdr->owns_memory = hdr->prev_owns_memory = false;
        hdr->prev_total_size                     = 0;
        hdr->prev_base_ptr                       = nullptr;
        allocator::repair_linked_list( base, hdr );
        allocator::recompute_counters( base, hdr );
        allocator::rebuild_free_tree( base, hdr );
        _initialized = true;
        return true;
    }

    /// @brief Сбросить состояние менеджера (не освобождает бэкенд).
    void destroy() noexcept
    {
        typename ThreadPolicyT::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return;
        std::uint8_t*          base = _backend.base_ptr();
        detail::ManagerHeader* hdr  = ( base != nullptr ) ? get_header( base ) : nullptr;
        if ( hdr != nullptr )
            hdr->magic = 0;
        _initialized = false;
    }

    bool is_initialized() const noexcept { return _initialized; }

    // ─── Выделение и освобождение ─────────────────────────────────────────────

    /**
     * @brief Выделить `user_size` байт в управляемой области.
     *
     * @return Указатель на пользовательские данные или nullptr.
     */
    void* allocate( std::size_t user_size ) noexcept
    {
        typename ThreadPolicyT::unique_lock_type lock( _mutex );
        if ( !_initialized || user_size == 0 )
            return nullptr;

        std::uint8_t*          base   = _backend.base_ptr();
        detail::ManagerHeader* hdr    = get_header( base );
        std::uint32_t          needed = detail::required_block_granules( user_size );
        std::uint32_t          idx    = FreeBlockTreeT::find_best_fit( base, hdr, needed );

        if ( idx != detail::kNoBlock )
            return allocator::allocate_from_block( base, hdr, detail::block_at( base, idx ), user_size );

        // Попытка расширить (если бэкенд поддерживает)
        if ( !do_expand( user_size ) )
            return nullptr;

        base = _backend.base_ptr();
        hdr  = get_header( base );
        idx  = FreeBlockTreeT::find_best_fit( base, hdr, needed );
        if ( idx != detail::kNoBlock )
            return allocator::allocate_from_block( base, hdr, detail::block_at( base, idx ), user_size );
        return nullptr;
    }

    /**
     * @brief Освободить блок по указателю на пользовательские данные.
     */
    void deallocate( void* ptr ) noexcept
    {
        typename ThreadPolicyT::unique_lock_type lock( _mutex );
        if ( !_initialized || ptr == nullptr )
            return;
        std::uint8_t*          base = _backend.base_ptr();
        detail::ManagerHeader* hdr  = get_header( base );
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
        allocator::coalesce( base, hdr, blk );
    }

    // ─── Статистика ────────────────────────────────────────────────────────────

    std::size_t total_size() const noexcept { return _initialized ? _backend.total_size() : 0; }

    std::size_t used_size() const noexcept
    {
        if ( !_initialized )
            return 0;
        const detail::ManagerHeader* hdr = get_header_c( _backend.base_ptr() );
        return detail::granules_to_bytes( hdr->used_size );
    }

    std::size_t free_size() const noexcept
    {
        if ( !_initialized )
            return 0;
        const detail::ManagerHeader* hdr  = get_header_c( _backend.base_ptr() );
        std::size_t                  used = detail::granules_to_bytes( hdr->used_size );
        return ( hdr->total_size > used ) ? ( hdr->total_size - used ) : 0;
    }

    std::size_t block_count() const noexcept
    {
        return _initialized ? get_header_c( _backend.base_ptr() )->block_count : 0;
    }

    std::size_t free_block_count() const noexcept
    {
        return _initialized ? get_header_c( _backend.base_ptr() )->free_count : 0;
    }

    std::size_t alloc_block_count() const noexcept
    {
        return _initialized ? get_header_c( _backend.base_ptr() )->alloc_count : 0;
    }

    /// @brief Доступ к бэкенду (для продвинутых сценариев).
    StorageBackendT&       backend() noexcept { return _backend; }
    const StorageBackendT& backend() const noexcept { return _backend; }

  private:
    StorageBackendT                    _backend{};
    bool                               _initialized = false;
    typename ThreadPolicyT::mutex_type _mutex{};

    // ─── Вспомогательные методы ────────────────────────────────────────────────

    static detail::ManagerHeader* get_header( std::uint8_t* base ) noexcept
    {
        return reinterpret_cast<detail::ManagerHeader*>( base + sizeof( detail::BlockHeader ) );
    }

    static const detail::ManagerHeader* get_header_c( const std::uint8_t* base ) noexcept
    {
        return reinterpret_cast<const detail::ManagerHeader*>( base + sizeof( detail::BlockHeader ) );
    }

    bool init_layout( std::uint8_t* base, std::size_t size ) noexcept
    {
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
        hdr_blk->root_offset   = kHdrBlkIdx;

        detail::ManagerHeader* hdr = get_header( base );
        std::memset( hdr, 0, sizeof( detail::ManagerHeader ) );
        hdr->magic              = kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = kHdrBlkIdx;
        hdr->last_block_offset  = detail::kNoBlock;
        hdr->free_tree_root     = detail::kNoBlock;
        hdr->granule_size       = static_cast<std::uint16_t>( kGranuleSize );

        detail::BlockHeader* blk = detail::block_at( base, kFreeBlkIdx );
        std::memset( blk, 0, sizeof( detail::BlockHeader ) );
        blk->prev_offset   = kHdrBlkIdx;
        blk->next_offset   = detail::kNoBlock;
        blk->left_offset   = detail::kNoBlock;
        blk->right_offset  = detail::kNoBlock;
        blk->parent_offset = detail::kNoBlock;
        blk->avl_height    = 1;

        hdr->last_block_offset = kFreeBlkIdx;
        hdr->free_tree_root    = kFreeBlkIdx;
        hdr->block_count       = 2;
        hdr->free_count        = 1;
        hdr->alloc_count       = 1;
        hdr->used_size         = kFreeBlkIdx + detail::kBlockHeaderGranules;

        _initialized = true;
        return true;
    }

    bool do_expand( std::size_t user_size ) noexcept
    {
        if ( !_initialized )
            return false;
        std::uint8_t*          base     = _backend.base_ptr();
        detail::ManagerHeader* hdr      = get_header( base );
        std::size_t            old_size = hdr->total_size;

        // Вычисляем минимально необходимый прирост
        std::size_t min_need =
            detail::granules_to_bytes( detail::required_block_granules( user_size ) + detail::kBlockHeaderGranules );
        std::size_t growth = old_size / 4; // 25% рост
        if ( growth < min_need )
            growth = min_need;

        if ( !_backend.expand( growth ) )
            return false;

        std::uint8_t* new_base = _backend.base_ptr();
        std::size_t   new_size = _backend.total_size();
        if ( new_base == nullptr || new_size <= old_size )
            return false;

        // Обновляем total_size в заголовке и добавляем новый свободный блок
        hdr = get_header( new_base );

        std::uint32_t extra_idx  = detail::byte_off_to_idx( old_size );
        std::size_t   extra_size = new_size - old_size;

        detail::BlockHeader* last_blk = ( hdr->last_block_offset != detail::kNoBlock )
                                            ? detail::block_at( new_base, hdr->last_block_offset )
                                            : nullptr;

        if ( last_blk != nullptr && last_blk->size == 0 )
        {
            // Расширяем последний свободный блок
            std::uint32_t loff = detail::block_idx( new_base, last_blk );
            FreeBlockTreeT::remove( new_base, hdr, loff );
            hdr->total_size = new_size;
            FreeBlockTreeT::insert( new_base, hdr, loff );
        }
        else
        {
            if ( extra_size < sizeof( detail::BlockHeader ) + detail::kMinBlockSize )
                return false;
            detail::BlockHeader* nb_blk = detail::block_at( new_base, extra_idx );
            std::memset( nb_blk, 0, sizeof( detail::BlockHeader ) );
            nb_blk->left_offset   = detail::kNoBlock;
            nb_blk->right_offset  = detail::kNoBlock;
            nb_blk->parent_offset = detail::kNoBlock;
            nb_blk->avl_height    = 1;
            if ( last_blk != nullptr )
            {
                std::uint32_t loff    = detail::block_idx( new_base, last_blk );
                nb_blk->prev_offset   = loff;
                nb_blk->next_offset   = detail::kNoBlock;
                last_blk->next_offset = extra_idx;
            }
            else
            {
                nb_blk->prev_offset     = detail::kNoBlock;
                nb_blk->next_offset     = detail::kNoBlock;
                hdr->first_block_offset = extra_idx;
            }
            hdr->last_block_offset = extra_idx;
            hdr->block_count++;
            hdr->free_count++;
            hdr->total_size = new_size;
            FreeBlockTreeT::insert( new_base, hdr, extra_idx );
        }
        return true;
    }
};

/// @brief Псевдоним с настройками по умолчанию (HeapStorage + AvlFreeTree + SharedMutexLock).
using DefaultAbstractPMM = AbstractPersistMemoryManager<>;

/// @brief Однопоточный вариант с HeapStorage.
using SingleThreadedAbstractPMM = AbstractPersistMemoryManager<DefaultAddressTraits, HeapStorage<DefaultAddressTraits>,
                                                               AvlFreeTree<DefaultAddressTraits>, config::NoLock>;

} // namespace pmm
