/**
 * @file pmm/persist_memory_manager.h
 * @brief PersistMemoryManager — unified static persistent memory manager (Issue #110).
 *
 * All-static class with multiton support via InstanceId. Configuration via ConfigT:
 * address_traits, storage_backend, free_block_tree, lock_policy.
 *
 * @see manager_configs.h, pmm_presets.h, pptr.h
 */

#pragma once

// Issue #172: require C++20 — this library uses concepts, std::atomic, and other C++20 features.
// Note: On MSVC, __cplusplus is always 199711L unless /Zc:__cplusplus is set; use _MSVC_LANG instead.
#if defined( _MSVC_LANG )
#if _MSVC_LANG < 202002L
#error "pmm.h requires C++20 or later. Please compile with /std:c++20 on MSVC."
#endif
#elif __cplusplus < 202002L
#error "pmm.h requires C++20 or later. Please compile with -std=c++20."
#endif

#include "pmm/allocator_policy.h"
#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/logging_policy.h"
#include "pmm/manager_configs.h"
#include "pmm/pallocator.h"
#include "pmm/parray.h"
#include "pmm/pmap.h"
#include "pmm/ppool.h"
#include "pmm/pptr.h"
#include "pmm/pstring.h"
#include "pmm/pvector.h"
#include "pmm/pstringview.h"
#include "pmm/typed_guard.h"
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

/**
 * @brief Унифицированный статический менеджер персистентной памяти (Issue #110).
 *
 * Все состояние и методы статические — нет необходимости создавать экземпляры.
 * Параметр `InstanceId` обеспечивает уникальность типа для каждого логического
 * «экземпляра» при одинаковой конфигурации, что реализует паттерн мультитон.
 *
 * @tparam ConfigT     Конфигурация менеджера (например pmm::CacheManagerConfig).
 *                     Должна предоставлять:
 *                       - `address_traits`  — тип адресных traits
 *                       - `storage_backend` — тип бэкенда хранилища
 *                       - `free_block_tree` — тип политики дерева свободных блоков
 *                       - `lock_policy`     — политика блокировок
 * @tparam InstanceId  Идентификатор экземпляра (по умолчанию 0). Позволяет создать
 *                     несколько независимых менеджеров одной конфигурации.
 *
 * @note Состояние хранится в статических переменных класса. Каждая специализация
 *       `PersistMemoryManager<ConfigT, InstanceId>` имеет собственный независимый
 *       статический бэкенд, мьютекс и флаг инициализации.
 *
 * @note Используйте `destroy()` перед повторной инициализацией и между тестами.
 */
// ─── Issue #202, Phase 4.2: detect logging_policy in config ──────────────────
namespace detail
{
template <typename C, typename = void> struct config_logging_policy
{
    using type = logging::NoLogging; ///< Default: no logging (backward compatible).
};
template <typename C> struct config_logging_policy<C, std::void_t<typename C::logging_policy>>
{
    using type = typename C::logging_policy;
};
} // namespace detail

template <typename ConfigT = CacheManagerConfig, std::size_t InstanceId = 0> class PersistMemoryManager
{
  public:
    // ─── Типы ─────────────────────────────────────────────────────────────────

    using address_traits  = typename ConfigT::address_traits;
    using storage_backend = typename ConfigT::storage_backend;
    using free_block_tree = typename ConfigT::free_block_tree;
    using thread_policy   = typename ConfigT::lock_policy;
    using logging_policy  = typename detail::config_logging_policy<ConfigT>::type; ///< Issue #202, Phase 4.2
    using allocator       = AllocatorPolicy<free_block_tree, address_traits>;
    using index_type      = typename address_traits::index_type;

    /// @brief Тип самого менеджера.
    using manager_type = PersistMemoryManager<ConfigT, InstanceId>;

    /**
     * @brief Вложенный псевдоним персистентного указателя, привязанного к данному менеджеру.
     *
     * `PersistMemoryManager<ConfigT, 0>::pptr<T>` и
     * `PersistMemoryManager<ConfigT, 1>::pptr<T>` — разные типы.
     *
     * @tparam T Тип данных, на который указывает pptr.
     */
    template <typename T> using pptr = pmm::pptr<T, manager_type>;

    /**
     * @brief Псевдоним для персистентной интернированной строки, привязанной к данному менеджеру.
     *
     * Позволяет использовать краткий синтаксис:
     * @code
     *   Mgr::pptr<Mgr::pstringview> p = Mgr::pstringview("hello");
     * @endcode
     * вместо `Mgr::pptr<pmm::pstringview<Mgr>> p = pmm::pstringview<Mgr>("hello");`
     */
    using pstringview = pmm::pstringview<manager_type>;

    /**
     * @brief Псевдоним для мутабельной персистентной строки, привязанной к данному менеджеру.
     *
     * Позволяет использовать краткий синтаксис:
     * @code
     *   Mgr::pptr<Mgr::pstring> p = Mgr::create_typed<Mgr::pstring>();
     *   p->assign("hello");
     * @endcode
     * вместо `Mgr::pptr<pmm::pstring<Mgr>> p = ...;`
     */
    using pstring = pmm::pstring<manager_type>;

    /**
     * @brief Псевдоним для персистентного словаря (AVL-дерева), привязанного к данному менеджеру.
     *
     * Позволяет использовать краткий синтаксис:
     * @code
     *   Mgr::pmap<int, int> map;
     *   map.insert(42, 100);
     *   auto p = map.find(42);
     * @endcode
     * вместо `pmm::pmap<int, int, Mgr> map;`
     *
     * @tparam _K Тип ключа. Должен поддерживать operator< и operator==.
     * @tparam _V Тип значения.
     */
    template <typename _K, typename _V> using pmap = pmm::pmap<_K, _V, manager_type>;

    /**
     * @brief Алиас для персистентного вектора, привязанного к данному менеджеру (Issue #186).
     *
     * Позволяет писать:
     * @code
     *   Mgr::pvector<int> vec;
     *   vec.push_back(42);
     *   auto p = vec.at(0);
     * @endcode
     * вместо `pmm::pvector<int, Mgr> vec;`
     *
     * @tparam T Тип элемента.
     */
    template <typename T> using pvector = pmm::pvector<T, manager_type>;

    /**
     * @brief Алиас для персистентного массива с O(1) индексацией (Issue #195, Phase 3.2).
     *
     * Позволяет писать:
     * @code
     *   Mgr::parray<int> arr;
     *   arr.push_back(42);
     *   int* elem = arr.at(0);
     * @endcode
     * вместо `pmm::parray<int, Mgr> arr;`
     *
     * @tparam T Тип элемента. Должен быть trivially copyable.
     */
    template <typename T> using parray = pmm::parray<T, manager_type>;

    /**
     * @brief Алиас для STL-совместимого аллокатора (Issue #198, Phase 3.5).
     *
     * Позволяет писать:
     * @code
     *   std::vector<int, Mgr::pallocator<int>> vec;
     *   vec.push_back(42);
     * @endcode
     * вместо `std::vector<int, pmm::pallocator<int, Mgr>> vec;`
     *
     * @tparam T Тип элемента.
     */
    template <typename T> using pallocator = pmm::pallocator<T, manager_type>;

    /**
     * @brief Алиас для персистентного пула объектов (Issue #199, Phase 3.6).
     *
     * Позволяет писать:
     * @code
     *   Mgr::pptr<Mgr::ppool<int>> pool = Mgr::create_typed<Mgr::ppool<int>>();
     *   int* obj = pool->allocate();
     * @endcode
     * вместо `Mgr::pptr<pmm::ppool<int, Mgr>> pool = ...;`
     *
     * @tparam T Тип объекта. Должен быть trivially copyable.
     */
    template <typename T> using ppool = pmm::ppool<T, manager_type>;

    // ─── Error code API (Issue #201, Phase 4.1) ───────────────────────────────

    /// @brief Return the error code from the last operation (thread-local per manager specialization).
    static PmmError last_error() noexcept { return _last_error; }

    /// @brief Clear the last error code to PmmError::Ok.
    static void clear_error() noexcept { _last_error = PmmError::Ok; }

    /// @brief Set the last error code (for use by utility functions like io.h).
    static void set_last_error( PmmError err ) noexcept { _last_error = err; }

    // ─── Статические методы управления жизненным циклом ──────────────────────

    /**
     * @brief Инициализировать менеджер с заданным начальным размером.
     *
     * Если бэкенд уже содержит буфер меньшего размера (например, после предыдущего
     * destroy()), он будет расширен до требуемого размера.
     *
     * @param initial_size Начальный размер в байтах (>= kMinMemorySize).
     * @return true при успехе.
     */
    static bool create( std::size_t initial_size ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( initial_size < detail::kMinMemorySize )
        {
            _last_error = PmmError::InvalidSize;
            return false;
        }
        // Issue #146: use address_traits::granule_size instead of hardcoded kGranuleSize.
        // Issue #172: guard against overflow when initial_size is close to size_t max.
        static constexpr std::size_t kGranSzCreate = address_traits::granule_size;
        if ( initial_size > std::numeric_limits<std::size_t>::max() - ( kGranSzCreate - 1 ) )
        {
            _last_error = PmmError::Overflow;
            return false;
        }
        std::size_t aligned = ( ( initial_size + kGranSzCreate - 1 ) / kGranSzCreate ) * kGranSzCreate;
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < aligned )
        {
            // Либо буфера нет, либо он меньше требуемого — расширяем
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
        {
            _last_error = PmmError::Ok;
            logging_policy::on_create( _backend.total_size() );
        }
        return ok;
    }

    /**
     * @brief Инициализировать поверх готового бэкенда (бэкенд уже содержит буфер).
     *
     * @return true при успехе.
     */
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
        {
            _last_error = PmmError::Ok;
            logging_policy::on_create( _backend.total_size() );
        }
        return ok;
    }

    /**
     * @brief Загрузить существующее состояние из бэкенда.
     *
     * @return true при успехе.
     */
    static bool load() noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < detail::kMinMemorySize )
        {
            _last_error = ( _backend.base_ptr() == nullptr ) ? PmmError::BackendError : PmmError::InvalidSize;
            return false;
        }
        std::uint8_t*                          base = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr  = get_header( base );
        if ( hdr->magic != kMagic )
        {
            _last_error = PmmError::InvalidMagic;
            logging_policy::on_corruption_detected( PmmError::InvalidMagic );
            return false;
        }
        if ( hdr->total_size != _backend.total_size() )
        {
            _last_error = PmmError::SizeMismatch;
            logging_policy::on_corruption_detected( PmmError::SizeMismatch );
            return false;
        }
        // Issue #146: compare stored granule size against address_traits::granule_size.
        if ( hdr->granule_size != static_cast<std::uint16_t>( address_traits::granule_size ) )
        {
            _last_error = PmmError::GranuleMismatch;
            logging_policy::on_corruption_detected( PmmError::GranuleMismatch );
            return false;
        }
        hdr->owns_memory     = false;
        hdr->prev_total_size = 0;
        allocator::repair_linked_list( base, hdr );
        allocator::recompute_counters( base, hdr );
        allocator::rebuild_free_tree( base, hdr );
        _initialized = true;
        _last_error  = PmmError::Ok;
        logging_policy::on_load();
        return true;
    }

    /**
     * @brief Сбросить состояние менеджера (не освобождает бэкенд).
     *
     * Обнуляет флаг инициализации. Необходим для изоляции тестов.
     */
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

    /// @brief Проверить, инициализирован ли менеджер.
    /// Issue #172: _initialized is std::atomic<bool> — lock-free fast path.
    static bool is_initialized() noexcept { return _initialized.load( std::memory_order_acquire ); }

    // ─── Статические методы выделения и освобождения ─────────────────────────

    /**
     * @brief Выделить `user_size` байт в управляемой области.
     *
     * @return Указатель на пользовательские данные или nullptr.
     */
    static void* allocate( std::size_t user_size ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
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

        std::uint8_t*                          base = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr  = get_header( base );
        // Issue #146: use AddressTraits-specific granule size for required granule computation.
        index_type data_gran = detail::bytes_to_granules_t<address_traits>( user_size );
        if ( data_gran == 0 )
            data_gran = 1;
        // Issue #43 Phase 1.3: Overflow protection — check before adding header granules.
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

        // Попытка расширить (если бэкенд поддерживает)
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

    /**
     * @brief Освободить блок по указателю на пользовательские данные.
     *
     * @note Если блок заблокирован навечно (lock_block_permanent), освобождение не выполняется.
     */
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

        // Issue #126: Permanently locked blocks cannot be freed.
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

    /**
     * @brief Заблокировать блок навечно — сделать его невозможным для освобождения (Issue #126).
     *
     * После вызова этого метода блок не может быть освобождён через deallocate().
     * Предназначено для блоков, содержащих постоянные данные (например, словарь stringview).
     *
     * @param ptr Указатель на пользовательские данные (тот же, что возвращает allocate()).
     * @return true если блок успешно заблокирован, false если блок не найден или уже свободен.
     */
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
            return false; // Свободный блок нельзя блокировать
        BlockStateBase<address_traits>::set_node_type_of( blk, pmm::kNodeReadOnly );
        return true;
    }

    /**
     * @brief Проверить, заблокирован ли блок навечно (Issue #126).
     *
     * @param ptr Указатель на пользовательские данные.
     * @return true если блок заблокирован навечно (node_type == kNodeReadOnly).
     */
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

    // ─── Статический типизированный API с pptr<T> ─────────────────────────────

    /**
     * @brief Выделить один объект типа T и вернуть персистентный указатель pptr<T>.
     *
     * @tparam T Тип выделяемого объекта.
     * @return pptr<T> — персистентный указатель или pptr<T>() при ошибке.
     */
    template <typename T> static pptr<T> allocate_typed() noexcept
    {
        void* raw = allocate( sizeof( T ) );
        if ( raw == nullptr )
            return pptr<T>();
        return make_pptr_from_raw<T>( raw );
    }

    /**
     * @brief Выделить массив из count объектов типа T и вернуть pptr<T>.
     *
     * @tparam T Тип элемента массива.
     * @param count Количество элементов (должно быть > 0).
     * @return pptr<T> — персистентный указатель или pptr<T>() при ошибке.
     */
    template <typename T> static pptr<T> allocate_typed( std::size_t count ) noexcept
    {
        if ( count == 0 )
            return pptr<T>();
        // Use (max)() to prevent MSVC macro expansion of max
        if ( sizeof( T ) > 0 && count > ( std::numeric_limits<std::size_t>::max )() / sizeof( T ) )
            return pptr<T>();
        void* raw = allocate( sizeof( T ) * count );
        if ( raw == nullptr )
            return pptr<T>();
        return make_pptr_from_raw<T>( raw );
    }

    /**
     * @brief Освободить блок по персистентному указателю.
     *
     * @note Конструктор и деструктор T не вызываются — только освобождает сырую память.
     *       Для типов с нетривиальными деструкторами используйте destroy_typed<T>().
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель на блок.
     */
    template <typename T> static void deallocate_typed( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        std::uint8_t* base = _backend.base_ptr();
        void*         raw  = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        deallocate( raw );
    }

    // ─── Нативное перераспределение (Issue #210, Phase 4.3) ────────────────────

    /// @brief Перераспределить массив из old_count объектов T до new_count.
    /// T должен быть trivially copyable. При неудаче старый блок сохраняется.
    /// @see docs/phase4_api.md §4.3
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
        // blk_idx = pptr.offset - floor(sizeof(Block) / granule)
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
        // Skip in-place paths when resolve() overlaps block header (SmallAT).
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
        // Fallback: allocate new + memmove + free old (under same lock).
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
        // Free old block
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

    // ─── Типизированный API с вызовом конструктора/деструктора (Issue #172) ───

    /**
     * @brief Выделить память и создать объект типа T с помощью placement new.
     *
     * В отличие от allocate_typed<T>(), этот метод конструирует объект T,
     * передавая аргументы в его конструктор. Это делает поведение аналогичным
     * оператору new T(args...).
     *
     * @tparam T    Тип создаваемого объекта.
     * @tparam Args Типы аргументов конструктора T.
     * @param args  Аргументы, передаваемые в конструктор T.
     * @return pptr<T> — персистентный указатель или pptr<T>() при ошибке.
     *
     * @note Для освобождения используйте destroy_typed<T>(p).
     *
     * @see destroy_typed
     */
    template <typename T, typename... Args> static pptr<T> create_typed( Args&&... args ) noexcept
    {
        // Issue #43 Phase 1.1: Enforce noexcept constructibility at compile time.
        // create_typed is noexcept, so the constructor must not throw — otherwise
        // an exception would leak the allocated memory block.
        static_assert( std::is_nothrow_constructible_v<T, Args...>,
                       "create_typed<T>: T must be nothrow-constructible from Args. "
                       "Use allocate_typed<T>() + manual placement new for throwing constructors." );

        void* raw = allocate( sizeof( T ) );
        if ( raw == nullptr )
            return pptr<T>();
        ::new ( raw ) T( static_cast<Args&&>( args )... );
        return make_pptr_from_raw<T>( raw );
    }

    /**
     * @brief Разрушить объект типа T (вызвать деструктор) и освободить память.
     *
     * В отличие от deallocate_typed<T>(), этот метод явно вызывает деструктор T
     * перед освобождением блока. Это необходимо для типов с нетривиальными
     * деструкторами (RAII, строки, контейнеры и т.д.).
     *
     * @tparam T Тип уничтожаемого объекта.
     * @param p  Персистентный указатель на объект (должен быть создан через create_typed).
     *
     * @see create_typed
     */
    template <typename T> static void destroy_typed( pptr<T> p ) noexcept
    {
        // Issue #43 Phase 1.1: Enforce noexcept destructibility at compile time.
        static_assert( std::is_nothrow_destructible_v<T>, "destroy_typed<T>: T must be nothrow-destructible." );

        if ( p.is_null() || !_initialized )
            return;
        std::uint8_t* base = _backend.base_ptr();
        void*         raw  = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        reinterpret_cast<T*>( raw )->~T();
        deallocate( raw );
    }

    /// @brief Create a typed object and wrap it in a RAII scope-guard (Issue #235).
    /// The guard calls free_data()/free_all() + destroy_typed() on scope exit.
    /// @see typed_guard
    template <typename T, typename... Args> static typed_guard<T, PersistMemoryManager> make_guard( Args&&... args )
    {
        return typed_guard<T, PersistMemoryManager>( create_typed<T>( static_cast<Args&&>( args )... ) );
    }

    /**
     * @brief Разыменовать pptr<T> — получить сырой указатель T*.
     *
     * Этот статический метод вызывается из `pptr<T, PersistMemoryManager>::resolve()`.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель.
     * @return T* — указатель на данные или nullptr при ошибке.
     */
    template <typename T> static T* resolve( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return nullptr;
        std::uint8_t* base = _backend.base_ptr();
        // Issue #43 Phase 1.2: Bounds check — verify offset is within the managed region.
        std::size_t byte_off = static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        assert( byte_off + sizeof( T ) <= _backend.total_size() && "resolve(): pptr offset out of bounds" );
        return reinterpret_cast<T*>( base + byte_off );
    }

    /**
     * @brief Разыменовать pptr<T> и получить указатель на i-й элемент массива.
     *
     * @tparam T Тип элемента.
     * @param p Персистентный указатель на массив.
     * @param i Индекс элемента.
     * @return T* — указатель на i-й элемент или nullptr при ошибке.
     */
    template <typename T> static T* resolve_at( pptr<T> p, std::size_t i ) noexcept
    {
        T* base_elem = resolve( p );
        return ( base_elem == nullptr ) ? nullptr : base_elem + i;
    }

    /// @brief Создать pptr<T> из байтового смещения (Issue #211, Phase 4.4).
    /// Обратная операция к pptr::byte_offset(). Смещение должно быть кратно granule_size.
    /// @tparam T Тип данных.  @param byte_off Байтовое смещение.
    /// @return pptr<T> или пустой pptr при ошибке (InvalidPointer / Overflow).
    template <typename T> static pptr<T> pptr_from_byte_offset( std::size_t byte_off ) noexcept
    {
        if ( byte_off == 0 )
            return pptr<T>(); // 0 byte offset → null pptr
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

    /**
     * @brief Проверить, что pptr указывает на валидную область внутри кучи (Issue #43 Phase 1.2).
     *
     * Выполняет runtime-проверку: смещение не выходит за границы управляемой области
     * и достаточно места для sizeof(T). Не проверяет, что блок действительно выделен.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель.
     * @return true если pptr валиден (в пределах кучи), false если null или вне границ.
     */
    template <typename T> static bool is_valid_ptr( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return false;
        std::size_t byte_off = static_cast<std::size_t>( p.offset() ) * address_traits::granule_size;
        return byte_off + sizeof( T ) <= _backend.total_size();
    }

    // ─── Root object API (Issue #200, Phase 3.7) ──────────────────────────────

    /// @brief Установить корневой объект в ManagerHeader (Issue #200, Phase 3.7).
    /// @tparam T Тип объекта.  @param p Персистентный указатель; пустой pptr сбрасывает корень.
    template <typename T> static void set_root( pptr<T> p ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return;
        detail::ManagerHeader<address_traits>* hdr = get_header( _backend.base_ptr() );
        hdr->root_offset                           = p.is_null() ? address_traits::no_block : p.offset();
    }

    /**
     * @brief Получить корневой объект из ManagerHeader.
     *
     * @tparam T Тип объекта (должен совпадать с типом, переданным в set_root).
     * @return pptr<T> — корневой указатель или пустой pptr, если корень не установлен.
     */
    template <typename T> static pptr<T> get_root() noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return pptr<T>();
        const detail::ManagerHeader<address_traits>* hdr = get_header_c( _backend.base_ptr() );
        if ( hdr->root_offset == address_traits::no_block )
            return pptr<T>();
        return pptr<T>( hdr->root_offset );
    }

    // ─── Методы доступа к полям AVL-узла блока (Issue #125, #235) ──────────
    // Safe-wrappers over BlockStateBase get_*/set_* with manager-level guards.
    // Issue #235: condensed Doxygen to reduce file size (was near 1500-line CI limit).

    /// @brief Get left/right/parent AVL offset for pptr's block (0 if null/no_block).
    /// @{
    template <typename T> static index_type get_tree_left_offset( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        index_type v = BlockStateBase<address_traits>::get_left_offset( block_raw_ptr_from_pptr( p ) );
        return ( v == address_traits::no_block ) ? static_cast<index_type>( 0 ) : v;
    }
    template <typename T> static index_type get_tree_right_offset( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        index_type v = BlockStateBase<address_traits>::get_right_offset( block_raw_ptr_from_pptr( p ) );
        return ( v == address_traits::no_block ) ? static_cast<index_type>( 0 ) : v;
    }
    template <typename T> static index_type get_tree_parent_offset( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        index_type v = BlockStateBase<address_traits>::get_parent_offset( block_raw_ptr_from_pptr( p ) );
        return ( v == address_traits::no_block ) ? static_cast<index_type>( 0 ) : v;
    }
    /// @}

    /// @brief Set left/right/parent AVL offset for pptr's block (0 maps to no_block).
    /// @{
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
    /// @}

    /// @brief Get/set weight (data granule count) of pptr's block.
    /// @warning set_tree_weight: use only for permanently locked blocks.
    /// @{
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
    /// @}

    /// @brief Get/set AVL subtree height of pptr's block (0 = not in tree).
    /// @{
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
    /// @}

    /// @brief Get TreeNode reference for direct AVL manipulation via pptr.
    /// @code auto& tn = MyMgr::tree_node(p); tn.get_left(); tn.set_left(idx); @endcode
    /// @warning Reference valid only while manager initialized and block not freed.
    template <typename T> static TreeNode<address_traits>& tree_node( pptr<T> p ) noexcept
    {
        assert( !p.is_null() && "tree_node: pptr must not be null" );
        assert( _initialized && "tree_node: manager must be initialized before calling tree_node" );
        return *reinterpret_cast<TreeNode<address_traits>*>( block_raw_mut_ptr_from_pptr( p ) );
    }

    // ─── Статистика ────────────────────────────────────────────────────────────
    // Issue #172: all read-only methods take shared_lock to prevent data races in
    // multi-threaded configurations (e.g. SharedMutexLock). _initialized is
    // std::atomic<bool> — we do a fast load first to avoid contention when not initialized.

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
        // Issue #166: use address_traits::granules_to_bytes() instead of deprecated detail::granules_to_bytes().
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
        // Issue #166: use address_traits::granules_to_bytes() instead of deprecated detail::granules_to_bytes().
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

    // ─── Итерация по блокам ────────────────────────────────────────────────────

    /**
     * @brief Обойти все блоки в управляемой области и вызвать callback для каждого.
     *
     * Callback принимает `BlockView` — описание блока (смещение, размеры, занятость).
     * Блоки итерируются в порядке адресного пространства (от меньшего к большему).
     *
     * @tparam Callback  Тип callable: `void(const pmm::BlockView&)`.
     * @param callback   Функция, вызываемая для каждого блока.
     * @return false если менеджер не инициализирован, true иначе.
     *
     * @note Метод выполняется под блокировкой — не вызывайте allocate/deallocate
     *       из callback во избежание дедлока.
     */
    template <typename Callback> static bool for_each_block( Callback&& callback ) noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return false;
        const std::uint8_t* base                         = _backend.base_ptr();
        using BlockState                                 = BlockStateBase<address_traits>;
        const detail::ManagerHeader<address_traits>* hdr = get_header_c( base );
        index_type                                   idx = hdr->first_block_offset;
        // Issue #146: use address_traits::granule_size for correct byte offset computations.
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

    /**
     * @brief Обойти только свободные блоки в AVL-дереве и вызвать callback для каждого.
     *
     * Callback принимает `FreeBlockView` — описание свободного блока
     * (смещение, размер, AVL-ссылки, высота).
     * Итерация выполняется in-order (по возрастанию размера блока).
     *
     * @tparam Callback  Тип callable: `void(const pmm::FreeBlockView&)`.
     * @param callback   Функция, вызываемая для каждого свободного блока.
     * @return false если менеджер не инициализирован, true иначе.
     *
     * @note Метод выполняется под блокировкой — не вызывайте allocate/deallocate
     *       из callback во избежание дедлока.
     */
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

    /// @brief Доступ к статическому бэкенду (для продвинутых сценариев).
    static storage_backend& backend() noexcept { return _backend; }

  private:
    // ─── Статические данные (уникальны для каждой специализации шаблона) ─────

    /// @brief Бэкенд хранилища (static inline — C++17, нет ошибок линковщика).
    static inline storage_backend _backend{};

    /// @brief Флаг инициализации.
    /// Issue #172: std::atomic<bool> allows lock-free is_initialized() fast path
    /// while remaining safe when racing against destroy()/load()/create().
    static inline std::atomic<bool> _initialized{ false };

    /// @brief Мьютекс для потокобезопасности.
    static inline typename thread_policy::mutex_type _mutex{};

    /// @brief Last error code (Issue #201, Phase 4.1).
    /// Issue #235: thread_local to prevent data races in multi-threaded configurations.
    static inline thread_local PmmError _last_error{ PmmError::Ok };

    // ─── Вспомогательные методы ────────────────────────────────────────────────

    // ─── Issue #179: find_block helpers ───────────────────────────────────────

    /// @brief Find the mutable block header for a user-data pointer (or nullptr).
    static pmm::Block<address_traits>* find_block_from_user_ptr( void* ptr ) noexcept
    {
        std::uint8_t*                          base = _backend.base_ptr();
        detail::ManagerHeader<address_traits>* hdr  = get_header( base );
        return detail::header_from_ptr_t<address_traits>( base, ptr, static_cast<std::size_t>( hdr->total_size ) );
    }

    /// @brief Find the const block header for a user-data pointer.
    /// Returns nullptr if ptr is out of range or the block header is invalid.
    static const pmm::Block<address_traits>* find_block_from_user_ptr( const void* ptr ) noexcept
    {
        const std::uint8_t* base = _backend.base_ptr();
        return detail::header_from_ptr_t<address_traits>(
            const_cast<std::uint8_t*>( base ), const_cast<void*>( ptr ),
            static_cast<std::size_t>( get_header_c( base )->total_size ) );
    }

    // ─── Issue #179: raw ↔ pptr helpers ───────────────────────────────────────

    /// @brief Convert a raw user-data pointer returned by allocate() into a pptr<T>.
    /// Caller must ensure raw != nullptr and _initialized before calling.
    template <typename T> static pptr<T> make_pptr_from_raw( void* raw ) noexcept
    {
        std::uint8_t* base     = _backend.base_ptr();
        std::size_t   byte_off = static_cast<std::uint8_t*>( raw ) - base;
        return pptr<T>( static_cast<index_type>( byte_off / address_traits::granule_size ) );
    }

    // ─── Issue #179: blk_raw helpers ──────────────────────────────────────────
    // base + offset * granule_size - sizeof(Block<AT>) → block header before user data.

    /// @brief Return a const pointer to the block header for the given pptr.
    template <typename T> static const void* block_raw_ptr_from_pptr( pptr<T> p ) noexcept
    {
        const std::uint8_t* base = _backend.base_ptr();
        return base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
               sizeof( Block<address_traits> );
    }

    /// @brief Return a mutable pointer to the block header for the given pptr.
    template <typename T> static void* block_raw_mut_ptr_from_pptr( pptr<T> p ) noexcept
    {
        std::uint8_t* base = _backend.base_ptr();
        return base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
               sizeof( Block<address_traits> );
    }

    /// @brief Recursive in-order traversal of the AVL free block tree.
    template <typename Callback>
    static void for_each_free_block_inorder( const std::uint8_t* base, const detail::ManagerHeader<address_traits>* hdr,
                                             index_type node_idx, int depth, Callback&& callback ) noexcept
    {
        using BlockState = BlockStateBase<address_traits>;
        // Issue #146: use address_traits::granule_size for correct byte offset computations.
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

        // Visit left subtree first (smaller blocks)
        for_each_free_block_inorder( base, hdr, left_off, depth + 1, callback );

        // Visit current node
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

        // Visit right subtree (larger blocks)
        for_each_free_block_inorder( base, hdr, right_off, depth + 1, callback );
    }

    // ─── Address-traits-specific layout constants (Issue #146) ──────────────────
    // These compute the correct granule indices based on the actual address_traits
    // granule size, rather than using the hardcoded DefaultAddressTraits constants.

    /// @brief Byte offset of ManagerHeader from base: rounds sizeof(Block<A>) up to granule boundary.
    /// For DefaultAddressTraits: 32 bytes. For SmallAddressTraits: roundup(18,16) = 32. For Large: 64.
    static constexpr std::size_t kBlockHdrByteSize =
        ( ( sizeof( Block<address_traits> ) + address_traits::granule_size - 1 ) / address_traits::granule_size ) *
        address_traits::granule_size;

    /// @brief Number of granules occupied by Block_0 (includes alignment padding).
    static constexpr index_type kBlockHdrGranules =
        static_cast<index_type>( kBlockHdrByteSize / address_traits::granule_size );

    /// @brief Number of granules occupied by ManagerHeader<address_traits> (Issue #175).
    /// Uses ceiling division: ceil(sizeof(ManagerHeader<address_traits>) / granule_size).
    static constexpr index_type kMgrHdrGranules = detail::kManagerHeaderGranules_t<address_traits>;

    /// @brief Granule index of first free block (Block_1 = after Block_0 + ManagerHeader).
    static constexpr index_type kFreeBlkIdxLayout = kBlockHdrGranules + kMgrHdrGranules;

    static detail::ManagerHeader<address_traits>* get_header( std::uint8_t* base ) noexcept
    {
        // Place ManagerHeader at a granule-aligned offset after Block_0 (Issue #146).
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

        // Minimum size check: Block_0 + ManagerHeader + Block_1 + at least 1 data granule
        static constexpr std::size_t kMinBlockDataSize = kGranSz; // 1 data granule
        if ( static_cast<std::size_t>( kFreeBlkIdx ) * kGranSz + sizeof( Block<address_traits> ) + kMinBlockDataSize >
             size )
            return false;

        // Инициализация блока-заголовка (Block_0) через state machine утилиты
        void* hdr_blk = base;
        std::memset( hdr_blk, 0, kBlockHdrByteSize ); // zero entire aligned region (including padding)
        BlockState::init_fields( hdr_blk,
                                 /*prev*/ address_traits::no_block,
                                 /*next*/ kFreeBlkIdx,
                                 /*avl_height*/ 0,
                                 /*weight*/ kMgrHdrGranules,
                                 /*root_offset*/ kHdrBlkIdx );

        detail::ManagerHeader<address_traits>* hdr = get_header( base );
        std::memset( hdr, 0, sizeof( detail::ManagerHeader<address_traits> ) );
        hdr->magic              = kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = kHdrBlkIdx;
        hdr->last_block_offset  = address_traits::no_block;
        hdr->free_tree_root     = address_traits::no_block;
        hdr->granule_size       = static_cast<std::uint16_t>( kGranSz );
        hdr->root_offset        = address_traits::no_block; // Issue #200: no root object by default

        // Инициализация первого свободного блока через state machine утилиты
        void* blk = base + static_cast<std::size_t>( kFreeBlkIdx ) * kGranSz;
        std::memset( blk, 0, sizeof( Block<address_traits> ) );
        BlockState::init_fields( blk,
                                 /*prev*/ kHdrBlkIdx,
                                 /*next*/ address_traits::no_block,
                                 /*avl_height*/ 1,
                                 /*weight*/ 0,
                                 /*root_offset*/ 0 );

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

        // Issue #146: use AddressTraitsT-specific granule size for all computations.
        static constexpr std::size_t kGranSz        = address_traits::granule_size;
        index_type                   data_gran_need = detail::bytes_to_granules_t<address_traits>( user_size );
        if ( data_gran_need == 0 )
            data_gran_need = 1;
        // min_need = block_header + data + another block_header (for the new expand block)
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

        logging_policy::on_expand( old_size, new_size );
        hdr = get_header( new_base );

        // Issue #146: compute extra_idx using address_traits::granule_size.
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
            // Issue #146: minimum extra size check uses address_traits granule size.
            if ( extra_size < sizeof( Block<address_traits> ) + kGranSz )
                return false;
            void* nb_blk = new_base + static_cast<std::size_t>( extra_idx ) * kGranSz;
            std::memset( nb_blk, 0, sizeof( Block<address_traits> ) );
            if ( last_blk_raw != nullptr )
            {
                Block<address_traits>* last_blk = reinterpret_cast<Block<address_traits>*>( last_blk_raw );
                index_type             loff     = detail::block_idx_t<address_traits>( new_base, last_blk );
                BlockState::init_fields( nb_blk,
                                         /*prev*/ loff,
                                         /*next*/ address_traits::no_block,
                                         /*avl_height*/ 1,
                                         /*weight*/ 0,
                                         /*root_offset*/ 0 );
                BlockState::set_next_offset_of( last_blk_raw, static_cast<index_type>( extra_idx ) );
            }
            else
            {
                BlockState::init_fields( nb_blk,
                                         /*prev*/ address_traits::no_block,
                                         /*next*/ address_traits::no_block,
                                         /*avl_height*/ 1,
                                         /*weight*/ 0,
                                         /*root_offset*/ 0 );
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

} // namespace pmm
