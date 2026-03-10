/**
 * @file pmm/persist_memory_manager.h
 * @brief PersistMemoryManager — унифицированный статический менеджер ПАП (Issue #110).
 *
 * Заменяет пару AbstractPersistMemoryManager + StaticMemoryManager единым
 * полностью статическим классом, поддерживающим мультитон через InstanceId.
 *
 * Ключевые особенности:
 *   - Все члены (`_backend`, `_initialized`, `_mutex`) объявлены `static inline` (C++17)
 *   - Все методы объявлены `static` — не нужно создавать экземпляры
 *   - Конфигурация полностью задаётся через `ConfigT`:
 *       - `address_traits`  — тип адресного пространства
 *       - `storage_backend` — бэкенд хранилища (HeapStorage, StaticStorage, MMapStorage)
 *       - `free_block_tree` — политика дерева свободных блоков (AvlFreeTree)
 *       - `lock_policy`     — политика многопоточности (NoLock, SharedMutexLock)
 *   - Параметр `InstanceId` позволяет создавать несколько независимых
 *     экземпляров одной конфигурации (мультитон по ID)
 *   - `pptr<T, PersistMemoryManager<...>>::resolve()` не требует аргументов —
 *     он обращается к `PersistMemoryManager::resolve<T>(p)` напрямую
 *
 * Пример использования:
 * @code
 *   // Определяем два независимых менеджера одной конфигурации
 *   using Cache   = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 0>;
 *   using Buffer  = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 1>;
 *
 *   Cache::create(64 * 1024);
 *   Buffer::create(32 * 1024);
 *
 *   Cache::pptr<int> cp = Cache::allocate_typed<int>();
 *   Buffer::pptr<int> bp = Buffer::allocate_typed<int>();
 *
 *   // Разыменование без аргументов — используется operator* и operator->
 *   *cp = 42;
 *   *bp = 100;
 *
 *   Cache::deallocate_typed(cp);
 *   Buffer::deallocate_typed(bp);
 *   Cache::destroy();
 *   Buffer::destroy();
 * @endcode
 *
 * @see manager_configs.h — готовые конфигурации менеджеров
 * @see pmm_presets.h — готовые псевдонимы для типичных сценариев
 * @see pptr.h — pptr<T, ManagerT> (с поддержкой статического resolve())
 * @version 0.1 (Issue #110 — унификация архитектуры)
 */

#pragma once

#include "pmm/allocator_policy.h"
#include "pmm/block.h"
#include "pmm/block_state.h"
#include "pmm/manager_configs.h"
#include "pmm/pmap.h"
#include "pmm/pptr.h"
#include "pmm/pstringview.h"
#include "pmm/types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

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
template <typename ConfigT = CacheManagerConfig, std::size_t InstanceId = 0> class PersistMemoryManager
{
  public:
    // ─── Типы ─────────────────────────────────────────────────────────────────

    using address_traits  = typename ConfigT::address_traits;
    using storage_backend = typename ConfigT::storage_backend;
    using free_block_tree = typename ConfigT::free_block_tree;
    using thread_policy   = typename ConfigT::lock_policy;
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
            return false;
        // Issue #146: use address_traits::granule_size instead of hardcoded kGranuleSize.
        static constexpr std::size_t kGranSzCreate = address_traits::granule_size;
        std::size_t                  aligned = ( ( initial_size + kGranSzCreate - 1 ) / kGranSzCreate ) * kGranSzCreate;
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < aligned )
        {
            // Либо буфера нет, либо он меньше требуемого — расширяем
            std::size_t additional =
                ( _backend.total_size() < aligned ) ? ( aligned - _backend.total_size() ) : aligned;
            if ( !_backend.expand( additional ) )
                return false;
        }
        if ( _backend.base_ptr() == nullptr || _backend.total_size() < aligned )
            return false;
        return init_layout( _backend.base_ptr(), _backend.total_size() );
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
            return false;
        return init_layout( _backend.base_ptr(), _backend.total_size() );
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
            return false;
        std::uint8_t*          base = _backend.base_ptr();
        detail::ManagerHeader* hdr  = get_header( base );
        if ( hdr->magic != kMagic || hdr->total_size != _backend.total_size() )
            return false;
        // Issue #146: compare stored granule size against address_traits::granule_size.
        if ( hdr->granule_size != static_cast<std::uint16_t>( address_traits::granule_size ) )
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
        std::uint8_t*          base = _backend.base_ptr();
        detail::ManagerHeader* hdr  = ( base != nullptr ) ? get_header( base ) : nullptr;
        if ( hdr != nullptr )
            hdr->magic = 0;
        _initialized = false;
    }

    /// @brief Проверить, инициализирован ли менеджер.
    static bool is_initialized() noexcept { return _initialized; }

    // ─── Статические методы выделения и освобождения ─────────────────────────

    /**
     * @brief Выделить `user_size` байт в управляемой области.
     *
     * @return Указатель на пользовательские данные или nullptr.
     */
    static void* allocate( std::size_t user_size ) noexcept
    {
        typename thread_policy::unique_lock_type lock( _mutex );
        if ( !_initialized || user_size == 0 )
            return nullptr;

        std::uint8_t*          base = _backend.base_ptr();
        detail::ManagerHeader* hdr  = get_header( base );
        // Issue #146: use AddressTraits-specific granule size for required granule computation.
        std::uint32_t data_gran = detail::bytes_to_granules_t<address_traits>( user_size );
        if ( data_gran == 0 )
            data_gran = 1;
        std::uint32_t needed = kBlockHdrGranules + data_gran;
        std::uint32_t idx    = free_block_tree::find_best_fit( base, hdr, needed );

        if ( idx != detail::kNoBlock )
            return allocator::allocate_from_block( base, hdr, idx, user_size );

        // Попытка расширить (если бэкенд поддерживает)
        if ( !do_expand( user_size ) )
            return nullptr;

        base = _backend.base_ptr();
        hdr  = get_header( base );
        idx  = free_block_tree::find_best_fit( base, hdr, needed );
        if ( idx != detail::kNoBlock )
            return allocator::allocate_from_block( base, hdr, idx, user_size );
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
        std::uint8_t*               base = _backend.base_ptr();
        detail::ManagerHeader*      hdr  = get_header( base );
        pmm::Block<address_traits>* blk =
            detail::header_from_ptr_t<address_traits>( base, ptr, static_cast<std::size_t>( hdr->total_size ) );
        if ( blk == nullptr )
            return;
        std::uint32_t freed = BlockStateBase<address_traits>::get_weight( blk );
        if ( freed == 0 )
            return;

        // Issue #126: Permanently locked blocks cannot be freed.
        if ( BlockStateBase<address_traits>::get_node_type( blk ) == pmm::kNodeReadOnly )
            return;

        index_type blk_idx = detail::block_idx_t<address_traits>( base, blk );

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
        std::uint8_t*               base = _backend.base_ptr();
        detail::ManagerHeader*      hdr  = get_header( base );
        pmm::Block<address_traits>* blk =
            detail::header_from_ptr_t<address_traits>( base, ptr, static_cast<std::size_t>( hdr->total_size ) );
        if ( blk == nullptr )
            return false;
        std::uint32_t w = BlockStateBase<address_traits>::get_weight( blk );
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
        const std::uint8_t*               base = _backend.base_ptr();
        const detail::ManagerHeader*      hdr  = get_header_c( base );
        const pmm::Block<address_traits>* blk  = detail::header_from_ptr_t<address_traits>(
            const_cast<std::uint8_t*>( base ), const_cast<void*>( ptr ), static_cast<std::size_t>( hdr->total_size ) );
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
        std::uint8_t* base     = _backend.base_ptr();
        std::size_t   byte_off = static_cast<std::uint8_t*>( raw ) - base;
        return pptr<T>( static_cast<index_type>( byte_off / address_traits::granule_size ) );
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
        std::uint8_t* base     = _backend.base_ptr();
        std::size_t   byte_off = static_cast<std::uint8_t*>( raw ) - base;
        return pptr<T>( static_cast<index_type>( byte_off / address_traits::granule_size ) );
    }

    /**
     * @brief Освободить блок по персистентному указателю.
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
        return reinterpret_cast<T*>( base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size );
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

    // ─── Методы доступа к полям AVL-узла блока (Issue #125) ─────────────────
    //
    // Note (Issue #141): These 12 get_tree_*/set_tree_* methods are intentional
    // safe-wrappers over BlockStateBase<address_traits>::get_*/set_* utilities.
    // They add manager-level guards (null-check, _initialized-check) and translate
    // between the public pptr<T> API and the raw void* block interface used internally.
    // The delegation is not duplication — it is the adapter layer between the public
    // persistent-pointer API and the internal block-state machine.

    /**
     * @brief Получить смещение левого дочернего узла AVL-дерева для блока,
     *        на который указывает данный pptr.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель на блок.
     * @return index_type — гранульный индекс левого дочернего узла
     *                      или 0 (null pptr) если нет.
     */
    template <typename T> static index_type get_tree_left_offset( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        const std::uint8_t* base    = _backend.base_ptr();
        const void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                              sizeof( Block<address_traits> );
        index_type left = BlockStateBase<address_traits>::get_left_offset( blk_raw );
        return ( left == address_traits::no_block ) ? static_cast<index_type>( 0 ) : left;
    }

    /**
     * @brief Получить смещение правого дочернего узла AVL-дерева для блока,
     *        на который указывает данный pptr.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель на блок.
     * @return index_type — гранульный индекс правого дочернего узла
     *                      или 0 (null pptr) если нет.
     */
    template <typename T> static index_type get_tree_right_offset( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        const std::uint8_t* base    = _backend.base_ptr();
        const void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                              sizeof( Block<address_traits> );
        index_type right = BlockStateBase<address_traits>::get_right_offset( blk_raw );
        return ( right == address_traits::no_block ) ? static_cast<index_type>( 0 ) : right;
    }

    /**
     * @brief Получить смещение родительского узла AVL-дерева для блока,
     *        на который указывает данный pptr.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель на блок.
     * @return index_type — гранульный индекс родительского узла
     *                      или 0 (null pptr) если нет.
     */
    template <typename T> static index_type get_tree_parent_offset( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        const std::uint8_t* base    = _backend.base_ptr();
        const void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                              sizeof( Block<address_traits> );
        index_type parent = BlockStateBase<address_traits>::get_parent_offset( blk_raw );
        return ( parent == address_traits::no_block ) ? static_cast<index_type>( 0 ) : parent;
    }

    /**
     * @brief Установить левый дочерний узел AVL-дерева для блока,
     *        на который указывает данный pptr.
     *
     * Принимает только pptr того же менеджера (ManagerT).
     *
     * @tparam T Тип данных.
     * @param p    Персистентный указатель на блок.
     * @param left Гранульный индекс нового левого дочернего узла (0 = null).
     */
    template <typename T> static void set_tree_left_offset( pptr<T> p, index_type left ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        std::uint8_t* base    = _backend.base_ptr();
        void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                        sizeof( Block<address_traits> );
        index_type v = ( left == 0 ) ? address_traits::no_block : left;
        BlockStateBase<address_traits>::set_left_offset_of( blk_raw, v );
    }

    /**
     * @brief Установить правый дочерний узел AVL-дерева для блока,
     *        на который указывает данный pptr.
     *
     * Принимает только pptr того же менеджера (ManagerT).
     *
     * @tparam T Тип данных.
     * @param p     Персистентный указатель на блок.
     * @param right Гранульный индекс нового правого дочернего узла (0 = null).
     */
    template <typename T> static void set_tree_right_offset( pptr<T> p, index_type right ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        std::uint8_t* base    = _backend.base_ptr();
        void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                        sizeof( Block<address_traits> );
        index_type v = ( right == 0 ) ? address_traits::no_block : right;
        BlockStateBase<address_traits>::set_right_offset_of( blk_raw, v );
    }

    /**
     * @brief Установить родительский узел AVL-дерева для блока,
     *        на который указывает данный pptr.
     *
     * Принимает только pptr того же менеджера (ManagerT).
     *
     * @tparam T Тип данных.
     * @param p      Персистентный указатель на блок.
     * @param parent Гранульный индекс нового родительского узла (0 = null).
     */
    template <typename T> static void set_tree_parent_offset( pptr<T> p, index_type parent ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        std::uint8_t* base    = _backend.base_ptr();
        void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                        sizeof( Block<address_traits> );
        index_type v = ( parent == 0 ) ? address_traits::no_block : parent;
        BlockStateBase<address_traits>::set_parent_offset_of( blk_raw, v );
    }

    /**
     * @brief Получить вес (ключ балансировки) узла AVL-дерева для блока,
     *        на который указывает данный pptr.
     *
     * Для выделенных блоков поле weight хранит размер данных в гранулах.
     * Пользовательский ключ балансировки следует хранить в отдельном поле
     * пользовательских данных.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель на блок.
     * @return index_type — текущий вес узла.
     */
    template <typename T> static index_type get_tree_weight( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        const std::uint8_t* base    = _backend.base_ptr();
        const void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                              sizeof( Block<address_traits> );
        return BlockStateBase<address_traits>::get_weight( blk_raw );
    }

    /**
     * @brief Установить вес (ключ балансировки) узла AVL-дерева для блока,
     *        на который указывает данный pptr.
     *
     * Принимает только pptr того же менеджера (ManagerT).
     *
     * @warning Используйте только для блоков, заблокированных навечно через
     *          lock_block_permanent(). Изменение веса обычного выделенного блока
     *          может нарушить инварианты менеджера памяти.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель на блок.
     * @param w Новый вес узла.
     */
    template <typename T> static void set_tree_weight( pptr<T> p, index_type w ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        std::uint8_t* base    = _backend.base_ptr();
        void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                        sizeof( Block<address_traits> );
        BlockStateBase<address_traits>::set_weight_of( blk_raw, w );
    }

    /**
     * @brief Получить высоту AVL-поддерева для блока,
     *        на который указывает данный pptr.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель на блок.
     * @return std::int16_t — высота поддерева (0 = узел не в дереве).
     */
    template <typename T> static std::int16_t get_tree_height( pptr<T> p ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return 0;
        const std::uint8_t* base    = _backend.base_ptr();
        const void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                              sizeof( Block<address_traits> );
        return BlockStateBase<address_traits>::get_avl_height( blk_raw );
    }

    /**
     * @brief Установить высоту AVL-поддерева для блока,
     *        на который указывает данный pptr.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель на блок.
     * @param h Новая высота поддерева.
     */
    template <typename T> static void set_tree_height( pptr<T> p, std::int16_t h ) noexcept
    {
        if ( p.is_null() || !_initialized )
            return;
        std::uint8_t* base    = _backend.base_ptr();
        void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                        sizeof( Block<address_traits> );
        BlockStateBase<address_traits>::set_avl_height_of( blk_raw, h );
    }

    /**
     * @brief Получить ссылку на узел AVL-дерева для блока, на который указывает данный pptr.
     *
     * Позволяет работать с узлом дерева напрямую через методы TreeNode:
     * get_left(), set_left(), get_right(), set_right(), get_parent(), set_parent(),
     * get_weight(), set_weight(), get_height(), set_height(), etc.
     *
     * Использование:
     * @code
     *   auto& tn = MyMgr::tree_node(p);
     *   auto left_idx = tn.get_left();  // no_block если нет левого потомка
     *   tn.set_left(other_p.offset());
     * @endcode
     *
     * @warning Возвращаемая ссылка действительна только пока менеджер инициализирован
     *          и блок не освобождён. Не сохраняйте ссылку дольше операции.
     *
     * @tparam T Тип данных.
     * @param p Персистентный указатель на блок (должен быть ненулевым).
     * @return TreeNode<address_traits>& — ссылка на узел AVL-дерева в заголовке блока.
     */
    template <typename T> static TreeNode<address_traits>& tree_node( pptr<T> p ) noexcept
    {
        std::uint8_t* base    = _backend.base_ptr();
        void*         blk_raw = base + static_cast<std::size_t>( p.offset() ) * address_traits::granule_size -
                        sizeof( Block<address_traits> );
        return *reinterpret_cast<TreeNode<address_traits>*>( blk_raw );
    }

    // ─── Статистика ────────────────────────────────────────────────────────────

    static std::size_t total_size() noexcept { return _initialized ? _backend.total_size() : 0; }

    static std::size_t used_size() noexcept
    {
        if ( !_initialized )
            return 0;
        const detail::ManagerHeader* hdr = get_header_c( _backend.base_ptr() );
        // Issue #166: use address_traits::granules_to_bytes() instead of deprecated detail::granules_to_bytes().
        return address_traits::granules_to_bytes( hdr->used_size );
    }

    static std::size_t free_size() noexcept
    {
        if ( !_initialized )
            return 0;
        const detail::ManagerHeader* hdr = get_header_c( _backend.base_ptr() );
        // Issue #166: use address_traits::granules_to_bytes() instead of deprecated detail::granules_to_bytes().
        std::size_t used = address_traits::granules_to_bytes( hdr->used_size );
        return ( hdr->total_size > used ) ? ( hdr->total_size - used ) : 0;
    }

    static std::size_t block_count() noexcept
    {
        return _initialized ? get_header_c( _backend.base_ptr() )->block_count : 0;
    }

    static std::size_t free_block_count() noexcept
    {
        return _initialized ? get_header_c( _backend.base_ptr() )->free_count : 0;
    }

    static std::size_t alloc_block_count() noexcept
    {
        return _initialized ? get_header_c( _backend.base_ptr() )->alloc_count : 0;
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
        const std::uint8_t* base         = _backend.base_ptr();
        using BlockState                 = BlockStateBase<address_traits>;
        const detail::ManagerHeader* hdr = get_header_c( base );
        std::uint32_t                idx = hdr->first_block_offset;
        // Issue #146: use address_traits::granule_size for correct byte offset computations.
        static constexpr std::size_t kGranSz = address_traits::granule_size;
        while ( idx != detail::kNoBlock )
        {
            if ( static_cast<std::size_t>( idx ) * kGranSz + sizeof( Block<address_traits> ) > hdr->total_size )
                break;
            const void*                  blk_raw    = base + static_cast<std::size_t>( idx ) * kGranSz;
            const Block<address_traits>* blk        = reinterpret_cast<const Block<address_traits>*>( blk_raw );
            std::uint32_t                total_gran = detail::block_total_granules( base, hdr, blk );
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
            idx = detail::to_u32_idx<address_traits>( BlockState::get_next_offset( blk_raw ) );
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
        const std::uint8_t*          base = _backend.base_ptr();
        const detail::ManagerHeader* hdr  = get_header_c( base );
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
    static inline bool _initialized = false;

    /// @brief Мьютекс для потокобезопасности.
    static inline typename thread_policy::mutex_type _mutex{};

    // ─── Вспомогательные методы ────────────────────────────────────────────────

    /// @brief Recursive in-order traversal of the AVL free block tree.
    template <typename Callback>
    static void for_each_free_block_inorder( const std::uint8_t* base, const detail::ManagerHeader* hdr,
                                             std::uint32_t node_idx, int depth, Callback&& callback ) noexcept
    {
        using BlockState = BlockStateBase<address_traits>;
        // Issue #146: use address_traits::granule_size for correct byte offset computations.
        static constexpr std::size_t kGranSz = address_traits::granule_size;
        if ( node_idx == detail::kNoBlock )
            return;
        if ( static_cast<std::size_t>( node_idx ) * kGranSz + sizeof( Block<address_traits> ) > hdr->total_size )
            return;
        const void*                  blk_raw = base + static_cast<std::size_t>( node_idx ) * kGranSz;
        const Block<address_traits>* blk     = reinterpret_cast<const Block<address_traits>*>( blk_raw );

        auto          left_off_idx   = BlockState::get_left_offset( blk_raw );
        auto          right_off_idx  = BlockState::get_right_offset( blk_raw );
        auto          parent_off_idx = BlockState::get_parent_offset( blk_raw );
        std::uint32_t left_off       = detail::to_u32_idx<address_traits>( left_off_idx );
        std::uint32_t right_off      = detail::to_u32_idx<address_traits>( right_off_idx );
        std::uint32_t parent_off     = detail::to_u32_idx<address_traits>( parent_off_idx );

        // Visit left subtree first (smaller blocks)
        for_each_free_block_inorder( base, hdr, left_off, depth + 1, callback );

        // Visit current node
        std::uint32_t total_gran = detail::block_total_granules( base, hdr, blk );
        FreeBlockView view;
        view.offset        = static_cast<std::ptrdiff_t>( static_cast<std::size_t>( node_idx ) * kGranSz );
        view.total_size    = static_cast<std::size_t>( total_gran ) * kGranSz;
        view.free_size     = static_cast<std::size_t>( total_gran - kBlockHdrGranules ) * kGranSz;
        view.left_offset   = ( left_off != detail::kNoBlock )
                                 ? static_cast<std::ptrdiff_t>( static_cast<std::size_t>( left_off ) * kGranSz )
                                 : -1;
        view.right_offset  = ( right_off != detail::kNoBlock )
                                 ? static_cast<std::ptrdiff_t>( static_cast<std::size_t>( right_off ) * kGranSz )
                                 : -1;
        view.parent_offset = ( parent_off != detail::kNoBlock )
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
    static constexpr std::uint32_t kBlockHdrGranules =
        static_cast<std::uint32_t>( kBlockHdrByteSize / address_traits::granule_size );

    /// @brief Number of granules occupied by ManagerHeader.
    static constexpr std::uint32_t kMgrHdrGranules =
        static_cast<std::uint32_t>( sizeof( detail::ManagerHeader ) / address_traits::granule_size );

    /// @brief Granule index of first free block (Block_1 = after Block_0 + ManagerHeader).
    static constexpr std::uint32_t kFreeBlkIdxLayout = kBlockHdrGranules + kMgrHdrGranules;

    static detail::ManagerHeader* get_header( std::uint8_t* base ) noexcept
    {
        // Place ManagerHeader at a granule-aligned offset after Block_0 (Issue #146).
        return reinterpret_cast<detail::ManagerHeader*>( base + kBlockHdrByteSize );
    }

    static const detail::ManagerHeader* get_header_c( const std::uint8_t* base ) noexcept
    {
        return reinterpret_cast<const detail::ManagerHeader*>( base + kBlockHdrByteSize );
    }

    static bool init_layout( std::uint8_t* base, std::size_t size ) noexcept
    {
        using BlockState                           = BlockStateBase<address_traits>;
        static constexpr std::uint32_t kHdrBlkIdx  = 0;
        static constexpr std::uint32_t kFreeBlkIdx = kFreeBlkIdxLayout;
        static constexpr std::size_t   kGranSz     = address_traits::granule_size;

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
                                 /*next*/ static_cast<index_type>( kFreeBlkIdx ),
                                 /*avl_height*/ 0,
                                 /*weight*/ static_cast<index_type>( kMgrHdrGranules ),
                                 /*root_offset*/ static_cast<index_type>( kHdrBlkIdx ) );

        detail::ManagerHeader* hdr = get_header( base );
        std::memset( hdr, 0, sizeof( detail::ManagerHeader ) );
        hdr->magic              = kMagic;
        hdr->total_size         = size;
        hdr->first_block_offset = kHdrBlkIdx;
        hdr->last_block_offset  = detail::kNoBlock;
        hdr->free_tree_root     = detail::kNoBlock;
        hdr->granule_size       = static_cast<std::uint16_t>( kGranSz );

        // Инициализация первого свободного блока через state machine утилиты
        void* blk = base + static_cast<std::size_t>( kFreeBlkIdx ) * kGranSz;
        std::memset( blk, 0, sizeof( Block<address_traits> ) );
        BlockState::init_fields( blk,
                                 /*prev*/ static_cast<index_type>( kHdrBlkIdx ),
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
        std::uint8_t*          base     = _backend.base_ptr();
        detail::ManagerHeader* hdr      = get_header( base );
        std::size_t            old_size = hdr->total_size;

        // Issue #146: use AddressTraitsT-specific granule size for all computations.
        static constexpr std::size_t kGranSz        = address_traits::granule_size;
        std::uint32_t                data_gran_need = detail::bytes_to_granules_t<address_traits>( user_size );
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

        hdr = get_header( new_base );

        // Issue #146: compute extra_idx using address_traits::granule_size.
        std::uint32_t extra_idx  = detail::byte_off_to_idx_t<address_traits>( old_size );
        std::size_t   extra_size = new_size - old_size;

        void* last_blk_raw =
            ( hdr->last_block_offset != detail::kNoBlock )
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
