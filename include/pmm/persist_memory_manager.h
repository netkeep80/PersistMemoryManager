/**
 * @file pmm/persist_memory_manager.h
 * @brief PersistMemoryManager — unified static persistent memory manager.
 *
 * All-static class with multiton support via InstanceId. Configuration via ConfigT:
 * address_traits, storage_backend, free_block_tree, lock_policy.
 *
 * @see manager_configs.h, pmm_presets.h, pptr.h
 */

#pragma once

// Require C++20 — this library uses concepts, std::atomic, and other C++20 features.
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
#include "pmm/diagnostics.h"
#include "pmm/forest_registry.h"
#include "pmm/layout.h"
#include "pmm/logging_policy.h"
#include "pmm/manager_configs.h"
#include "pmm/pallocator.h"
#include "pmm/parray.h"
#include "pmm/pmap.h"
#include "pmm/ppool.h"
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

/**
 * @brief Унифицированный статический менеджер персистентной памяти.
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
// ─── detect logging_policy in config ──────────────────
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

template <typename ConfigT = CacheManagerConfig, std::size_t InstanceId = 0>
class PersistMemoryManager : public detail::PersistMemoryTypedApi<PersistMemoryManager<ConfigT, InstanceId>>
{
  public:
    // ─── Типы ─────────────────────────────────────────────────────────────────

    using address_traits  = typename ConfigT::address_traits;
    using storage_backend = typename ConfigT::storage_backend;
    using free_block_tree = typename ConfigT::free_block_tree;
    using thread_policy   = typename ConfigT::lock_policy;
    using logging_policy  = typename detail::config_logging_policy<ConfigT>::type;
    using allocator       = AllocatorPolicy<free_block_tree, address_traits>;
    using index_type      = typename address_traits::index_type;
    using forest_registry = detail::ForestDomainRegistry<address_traits>;
    using forest_domain   = detail::ForestDomainRecord<address_traits>;

    /// @brief Тип самого менеджера.
    using manager_type = PersistMemoryManager<ConfigT, InstanceId>;

    template <typename> friend struct pstringview;
    template <typename, typename, typename> friend struct pmap;
    friend class detail::PersistMemoryTypedApi<manager_type>;
    template <typename> friend bool save_manager( const char* );

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
     * @brief Алиас для персистентного массива с O(1) индексацией.
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
     * @brief Алиас для STL-совместимого аллокатора.
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
     * @brief Алиас для персистентного пула объектов.
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

    // ─── Error code API ───────────────────────────────

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
        // Use address_traits::granule_size instead of hardcoded kGranuleSize.
        // Guard against overflow when initial_size is close to size_t max.
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

    /**
     * @brief Load existing state from backend with structured diagnostics.
     *
     * Performs verify-then-repair: first detects all violations, then applies
     * documented fixes. The VerifyResult records every repair action taken.
     * Header corruption (magic, image version, size, granule) is non-recoverable and aborts load.
     *
     * @param result  VerifyResult populated with detected violations and repair actions.
     * @return true on successful load (repairs applied), false on non-recoverable corruption.
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
        if ( !detail::is_supported_image_version( hdr->image_version ) )
        {
            _last_error = PmmError::UnsupportedImageVersion;
            logging_policy::on_corruption_detected( PmmError::UnsupportedImageVersion );
            result.add( ViolationType::HeaderCorruption, DiagnosticAction::Aborted, 0, detail::kCurrentImageVersion,
                        static_cast<std::uint64_t>( hdr->image_version ) );
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
        // Detect violations in the raw image, then mark with repair actions.
        auto mark_entries = []( VerifyResult& r, std::size_t from, DiagnosticAction act )
        {
            for ( std::size_t i = from; i < r.entry_count; ++i )
                r.entries[i].action = act;
        };
        std::size_t pre = result.entry_count;
        allocator::verify_block_states( base, hdr, result ); // Phase 1: Repaired
        mark_entries( result, pre, DiagnosticAction::Repaired );
        pre = result.entry_count;
        allocator::verify_linked_list( base, hdr, result ); // Phase 2: Repaired
        mark_entries( result, pre, DiagnosticAction::Repaired );
        pre = result.entry_count;
        allocator::verify_counters( base, hdr, result ); // Phase 3: Rebuilt
        mark_entries( result, pre, DiagnosticAction::Rebuilt );
        pre = result.entry_count;
        allocator::verify_free_tree( base, hdr, result ); // Phase 4: Rebuilt
        mark_entries( result, pre, DiagnosticAction::Rebuilt );
        // Repair phase: apply all fixes.
        if ( detail::image_version_requires_migration( hdr->image_version ) )
            hdr->image_version = detail::kCurrentImageVersion;
        hdr->owns_memory     = false;
        hdr->prev_total_size = 0;
        allocator::repair_linked_list( base, hdr );
        allocator::recompute_counters( base, hdr );
        allocator::rebuild_free_tree( base, hdr );
        _initialized = true;
        // Phase 5: forest registry diagnostics.
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

    /**
     * @brief Сбросить runtime-состояние менеджера (не освобождает бэкенд).
     *
     * Обнуляет флаг инициализации. Необходим для изоляции тестов и нормального
     * shutdown. Не изменяет persisted image: корректный backend-образ можно
     * снова загрузить через load().
     */
    static void destroy() noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return;
        _initialized = false;
        logging_policy::on_destroy();
    }

    /**
     * @brief Явно инвалидировать persisted image и сбросить runtime-состояние.
     *
     * Destructive helper for tests and corruption simulation. Normal shutdown
     * must use destroy(), which leaves the backend image loadable.
     */
    static void destroy_image() noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        std::uint8_t*                            base = _backend.base_ptr();
        if ( base != nullptr && _backend.total_size() >= detail::kMinMemorySize )
            get_header( base )->magic = 0;
        _initialized = false;
        logging_policy::on_destroy();
    }

    /// @brief Проверить, инициализирован ли менеджер.
    /// _initialized is std::atomic<bool> — lock-free fast path.
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
        return allocate_unlocked( user_size );
    }

    /**
     * @brief Освободить блок по указателю на пользовательские данные.
     *
     * @note Если блок заблокирован навечно (lock_block_permanent), освобождение не выполняется.
     * @note Raw-pointer reconstruction checks block-chain links; this entry point holds the manager lock so canonical
     *       validation observes stable prev/next relationships.
     */
    static void deallocate( void* ptr ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        deallocate_unlocked( ptr );
    }

    /**
     * @brief Заблокировать блок навечно — сделать его невозможным для освобождения.
     *
     * После вызова этого метода блок не может быть освобождён через deallocate().
     * Предназначено для блоков, содержащих постоянные данные (например, словарь stringview).
     *
     * @param ptr Указатель на пользовательские данные (тот же, что возвращает allocate()).
     * @return true если блок успешно заблокирован, false если блок не найден или уже свободен.
     *
     * @note Raw-pointer reconstruction checks block-chain links; this entry point holds the manager lock so canonical
     *       validation observes stable prev/next relationships.
     */
    static bool lock_block_permanent( void* ptr ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        return lock_block_permanent_unlocked( ptr );
    }

    /**
     * @brief Проверить, заблокирован ли блок навечно.
     *
     * @param ptr Указатель на пользовательские данные.
     * @return true если блок заблокирован навечно (node_type == kNodeReadOnly).
     *
     * @note Raw-pointer reconstruction checks block-chain links; this entry point holds a shared lock so no writer
     *       mutates prev/next relationships during validation.
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

    // ─── Root object API ──────────────────────────────

    /// @brief Compatibility shim for the legacy root object API.
    /// Stores the root in the canonical `service/legacy_root` domain record.
    template <typename T> static void set_root( pptr<T> p ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized )
            return;
        set_forest_domain_root_index_unlocked( find_domain_by_name_unlocked( detail::kServiceNameLegacyRoot ),
                                               p.is_null() ? static_cast<index_type>( 0 ) : p.offset() );
    }

    /**
     * @brief Compatibility shim for the legacy root object API.
     *
     * @tparam T Тип объекта (должен совпадать с типом, переданным в set_root).
     * @return pptr<T> — корневой указатель или пустой pptr, если корень не установлен.
     */
    template <typename T> static pptr<T> get_root() noexcept
    {
        typename thread_policy::shared_lock_type lock( _mutex );
        if ( !_initialized )
            return pptr<T>();
        index_type legacy_root =
            forest_domain_root_index_unlocked( find_domain_by_name_unlocked( detail::kServiceNameLegacyRoot ) );
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

    /// @brief Verify that all bootstrap invariants hold.
    /// Returns true iff the image is a valid, self-described persistent environment.
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

    // ─── Методы доступа к полям AVL-узла блока ─────────────
    // Safe-wrappers over BlockStateBase get_*/set_* with manager-level guards.

  private:
    /// @brief Read an index_type AVL field from pptr's block (returns 0 for null/no_block).
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
    /// @brief Write an index_type AVL field into pptr's block (0 maps to no_block).
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
    /// @brief Get left/right/parent AVL offset for pptr's block (0 if null/no_block).
    /// @{
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
    /// @}
    /// @brief Set left/right/parent AVL offset for pptr's block (0 maps to no_block).
    /// @{
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
    /// @}
    /// @brief Get/set weight (data granule count) of pptr's block.
    /// @warning set_tree_weight: use only for permanently locked blocks.
    /// @{
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
    /// @}

    /// @brief Get/set AVL subtree height of pptr's block (0 = not in tree).
    /// @{
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
    /// @}

    /// @brief Get TreeNode reference for direct AVL manipulation via pptr.
    /// @code auto& tn = MyMgr::tree_node(p); tn.get_left(); tn.set_left(idx); @endcode
    /// @warning Reference valid only while manager initialized and block not freed.
    template <typename T> static TreeNode<address_traits>& tree_node( pptr<T> p ) noexcept
    {
        assert( !p.is_null() && "tree_node: pptr must not be null" );
        assert( _initialized && "tree_node: manager must be initialized before calling tree_node" );
        void* blk = block_raw_mut_ptr_from_pptr( p );
        if ( blk == nullptr )
        {
            _last_error = PmmError::InvalidPointer;
            // Return a reference to a thread-local sentinel to avoid UB.
            // Callers must check last_error() when operating on untrusted pptrs.
            // Re-initialize each time so prior mutations don't leak across calls.
            static thread_local TreeNode<address_traits> sentinel{};
            sentinel = {};
            return sentinel;
        }
        return *reinterpret_cast<TreeNode<address_traits>*>( blk );
    }

    // ─── Статистика ────────────────────────────────────────────────────────────
    // All read-only methods use read_stat() to eliminate repeated
    // double-check-initialized + shared_lock boilerplate.

  private:
    /// @brief Shared-lock read with double-check-initialized guard.
    /// Returns fn(hdr) if initialized, else 0.
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
    /// @brief Returns the backend's total managed memory size.
    /// Special-cased to read from _backend (authoritative source of truth for
    /// physical size) rather than the header, so callers always see the real
    /// backend size even if the header is stale or corrupted.
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

    // ─── Verify / Repair ───────────────────────────────────────────

    /// @brief Read-only structural diagnostics. Returns violations without modifying image.
    /// @return VerifyResult with ok=true if no violations, false otherwise.
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
        // Use address_traits::granule_size for correct byte offset computations.
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
    /// Std::atomic<bool> allows lock-free is_initialized() fast path
    /// while remaining safe when racing against destroy()/load()/create().
    static inline std::atomic<bool> _initialized{ false };

    /// @brief Мьютекс для потокобезопасности.
    static inline typename thread_policy::mutex_type _mutex{};

    /// @brief Last error code.
    /// Thread_local to prevent data races in multi-threaded configurations.
    static inline thread_local PmmError _last_error{ PmmError::Ok };

    // ─── Вспомогательные методы ────────────────────────────────────────────────

    // ─── find_block helpers ───────────────────────────────────────

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

    /// @pre Caller must guarantee stable block links, normally by holding the manager lock.
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

    /// @pre Caller must guarantee stable block links, normally by holding the manager lock.
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

    // Forest/domain registry private methods — extracted to forest_domain_mixin.inc
    // to keep this file under the 1500-line CI limit.
#include "pmm/forest_domain_mixin.inc"
    // Verify/repair methods — extracted to verify_repair_mixin.inc.
#include "pmm/verify_repair_mixin.inc"

    // ─── Address-traits-specific layout constants ──────────────────
    // These compute the correct granule indices based on the actual address_traits
    // granule size, rather than using the hardcoded DefaultAddressTraits constants.

    /// @brief Byte offset of ManagerHeader from base: rounds sizeof(Block<A>) up to granule boundary.
    /// For DefaultAddressTraits: 32 bytes. For SmallAddressTraits: roundup(18,16) = 32. For Large: 64.
    static constexpr std::size_t kBlockHdrByteSize = detail::manager_header_offset_bytes_v<address_traits>;

    static constexpr index_type kBlockHdrGranules =
        static_cast<index_type>( kBlockHdrByteSize / address_traits::granule_size );

    static constexpr index_type kMgrHdrGranules = detail::kManagerHeaderGranules_t<address_traits>;

    /// @brief Granule index of first free block (Block_1 = after Block_0 + ManagerHeader).
    static constexpr index_type kFreeBlkIdxLayout = kBlockHdrGranules + kMgrHdrGranules;

    static detail::ManagerHeader<address_traits>* get_header( std::uint8_t* base ) noexcept
    {
        // Place ManagerHeader at a granule-aligned offset after Block_0.
        return detail::manager_header_at<address_traits>( base );
    }

    static const detail::ManagerHeader<address_traits>* get_header_c( const std::uint8_t* base ) noexcept
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

} // namespace pmm
