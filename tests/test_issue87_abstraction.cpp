/**
 * @file test_issue87_abstraction.cpp
 * @brief Тесты-маяки для Issue #87: Повышение уровня абстракции библиотеки менеджера ПАП.
 *
 * Фаза 0 рефакторинга (plan_issue87.md).
 *
 * Структура тестов:
 *  PART A — Code Review текущей архитектуры: проверяем явные факты о текущем состоянии кода.
 *            Эти тесты проходят сейчас и должны оставаться зелёными на всех фазах.
 *
 *  PART B — Маяки будущих абстракций: описывают ожидаемые интерфейсы после рефакторинга.
 *            Закомментированы до реализации соответствующей фазы.
 *            Каждый блок помечен фазой: [Phase N].
 *
 * @see plan_issue87.md — полный план рефакторинга
 * @version 0.1 (Issue #87 Phase 0)
 */

#include "persist_memory_manager.h"
#include "persist_memory_types.h"
#include "persist_avl_tree.h"
#include "pmm_config.h"
#include "pmm/address_traits.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

#define PMM_TEST( expr )                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( expr ) )                                                                                               \
        {                                                                                                              \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " << #expr << "\n";                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while ( false )

#define PMM_RUN( name, fn )                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        std::cout << "  " << name << " ... ";                                                                          \
        if ( fn() )                                                                                                    \
        {                                                                                                              \
            std::cout << "PASS\n";                                                                                     \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::cout << "FAIL\n";                                                                                     \
            all_passed = false;                                                                                        \
        }                                                                                                              \
    } while ( false )

// =============================================================================
// PART A: Code Review — текущая архитектура
// =============================================================================

// ─── A1: Жёстко заданные константы (кандидаты к параметризации) ─────────────

/// @brief Проверяем, что kGranuleSize зафиксирован в compile-time и равен 16.
/// [Issue #87 Code Review] После рефакторинга это должно стать параметром AddressTraits.
static bool test_cr_granule_size_is_hardcoded()
{
    // kGranuleSize — compile-time константа, не параметр шаблона
    static_assert( pmm::kGranuleSize == 16, "kGranuleSize must be 16 (current hardcoded value)" );
    // Функции работают только с kGranuleSize == 16, не с произвольным размером
    PMM_TEST( pmm::detail::bytes_to_granules( 16 ) == 1 );
    PMM_TEST( pmm::detail::bytes_to_granules( 17 ) == 2 ); // округление вверх
    PMM_TEST( pmm::detail::granules_to_bytes( 1 ) == 16 );
    return true;
}

/// @brief Проверяем, что kNoBlock — это max uint32_t (зависит от 32-bit шины адреса).
/// [Issue #87 Code Review] После рефакторинга kNoBlock = max(index_type).
static bool test_cr_no_block_is_max_uint32()
{
    static_assert( pmm::detail::kNoBlock == 0xFFFFFFFFU, "kNoBlock is max uint32_t — depends on 32-bit address bus" );
    PMM_TEST( pmm::detail::kNoBlock == std::numeric_limits<std::uint32_t>::max() );
    return true;
}

// ─── A2: BlockHeader — совмещает узел списка и AVL-дерева ───────────────────

/// @brief Проверяем, что BlockHeader содержит поля и списка, и AVL-дерева в одной структуре.
/// [Issue #87 Code Review] После рефакторинга это должны быть отдельные компоненты:
/// LinkedListNode<A> и TreeNode<A>.
static bool test_cr_block_header_combines_list_and_tree()
{
    // Поля двухсвязного списка
    static_assert( offsetof( pmm::detail::BlockHeader, prev_offset ) == 4 );
    static_assert( offsetof( pmm::detail::BlockHeader, next_offset ) == 8 );

    // Поля AVL-дерева
    static_assert( offsetof( pmm::detail::BlockHeader, left_offset ) == 12 );
    static_assert( offsetof( pmm::detail::BlockHeader, right_offset ) == 16 );
    static_assert( offsetof( pmm::detail::BlockHeader, parent_offset ) == 20 );
    static_assert( offsetof( pmm::detail::BlockHeader, avl_height ) == 24 );

    // Поля блока
    static_assert( offsetof( pmm::detail::BlockHeader, size ) == 0 );
    static_assert( offsetof( pmm::detail::BlockHeader, root_offset ) == 28 );

    // Всё в одной структуре — ключевая точка для рефакторинга
    static_assert( sizeof( pmm::detail::BlockHeader ) == 32 );
    return true;
}

/// @brief Проверяем, что все индексные поля BlockHeader — uint32_t (32-bit шина адреса).
/// [Issue #87 Code Review] После рефакторинга тип должен стать параметром AddressTraits.
static bool test_cr_block_header_uses_uint32_indices()
{
    static_assert( std::is_same<decltype( pmm::detail::BlockHeader::prev_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( pmm::detail::BlockHeader::next_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( pmm::detail::BlockHeader::left_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( pmm::detail::BlockHeader::right_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( pmm::detail::BlockHeader::parent_offset ), std::uint32_t>::value );
    static_assert( std::is_same<decltype( pmm::detail::BlockHeader::size ), std::uint32_t>::value );
    return true;
}

// ─── A3: pptr<T> привязан к конкретному синглтону PersistMemoryManager<> ─────

/// @brief Проверяем, что pptr<T>::get() резолвит через PersistMemoryManager<> (дефолтный конфиг).
/// [Issue #87 Code Review] После рефакторинга pptr<T, Manager> должен принимать тип менеджера.
static bool test_cr_pptr_resolves_via_default_singleton()
{
    // pptr<T> — 4 байта, uint32_t гранульный индекс
    static_assert( sizeof( pmm::pptr<int> ) == 4 );
    static_assert( sizeof( pmm::pptr<double> ) == 4 );

    // Нулевой pptr
    pmm::pptr<int> null_ptr;
    PMM_TEST( null_ptr.is_null() );
    PMM_TEST( !null_ptr );
    PMM_TEST( null_ptr.get() == nullptr );

    // Проверяем что pptr работает с дефолтным менеджером
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::pptr<int> p = pmm::PersistMemoryManager<>::allocate_typed<int>( 1 );
    PMM_TEST( !p.is_null() );
    PMM_TEST( p.get() != nullptr );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── A4: PMMConfig — текущие параметры конфигурации ──────────────────────────

/// @brief Проверяем, что PMMConfig параметризует только часть зависимостей.
/// [Issue #87 Code Review] Не параметризует: тип индекса, гранулу (не runtime, а только проверяет),
/// бэкенд хранилища, структуру данных свободных блоков.
static bool test_cr_pmm_config_partial_parametrization()
{
    // GranuleSize в Config — только число, не влияет на тип индексов в BlockHeader
    static_assert( pmm::config::PMMConfig<16, 64, pmm::config::SharedMutexLock>::granule_size == 16 );
    static_assert( pmm::config::PMMConfig<16, 64, pmm::config::NoLock>::granule_size == 16 );

    // Config не содержит: тип индекса, бэкенд, алгоритм free-блоков
    // Это то, что Issue #87 просит добавить

    // Grow ratio параметризован (Issue #83)
    static_assert( pmm::config::PMMConfig<16, 64, pmm::config::SharedMutexLock, 5, 4>::grow_numerator == 5 );
    static_assert( pmm::config::PMMConfig<16, 64, pmm::config::SharedMutexLock, 5, 4>::grow_denominator == 4 );

    return true;
}

// ─── A5: PersistentAvlTree не зависит от PMM (уже выделено в #73) ───────────

/// @brief Проверяем, что PersistentAvlTree — all-static, не зависит от синглтона.
/// [Issue #87 Code Review] Это хорошая точка — AVL уже изолирован. Нужно только
/// параметризовать через AddressTraits (тип индекса, гранула).
static bool test_cr_avl_tree_is_standalone()
{
    static_assert( !std::is_constructible<pmm::PersistentAvlTree>::value,
                   "PersistentAvlTree must not be constructible (all-static)" );

    // Вызов напрямую (без синглтона PMM)
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    auto* base = static_cast<std::uint8_t*>( mem );
    auto* hdr  = reinterpret_cast<pmm::detail::ManagerHeader*>( base + sizeof( pmm::detail::BlockHeader ) );

    std::uint32_t needed = pmm::detail::required_block_granules( 64 );
    std::uint32_t found  = pmm::PersistentAvlTree::find_best_fit( base, hdr, needed );
    PMM_TEST( found != pmm::detail::kNoBlock );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── A6: Три типа менеджера — статический, динамический, файловый ────────────

/// @brief Проверяем, что текущий менеджер поддерживает только динамический (heap) бэкенд.
/// [Issue #87 Code Review] Фиксируем: expand() всегда использует std::malloc.
/// После рефакторинга должны быть: StaticStorage, HeapStorage, MMapStorage.
static bool test_cr_only_heap_backend_exists()
{
    // Текущий PersistMemoryManager требует внешний буфер для create()/load()
    // и внутренне использует std::malloc для expand()
    // Нет compile-time статического хранилища
    // Нет mmap бэкенда

    // Проверяем, что менеджер работает с любым буфером (heap, stack, static)
    static std::uint8_t static_buf[64 * 1024];
    PMM_TEST( pmm::PersistMemoryManager<>::create( static_buf, sizeof( static_buf ) ) );
    PMM_TEST( pmm::PersistMemoryManager<>::is_initialized() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );
    pmm::PersistMemoryManager<>::destroy();

    // Нет авто-расширения для статического буфера — это ожидаемо
    // После рефакторинга StaticStorage будет явно запрещать expand()

    return true;
}

// ─── A7: CRTP-цепочка миксинов (уже реализовано в #73) ───────────────────────

/// @brief Проверяем текущую CRTP-цепочку: ValidationMixin<StatsMixin<PmmCore<PMM>>>.
/// [Issue #87 Code Review] Механизм миксинов уже есть. Нужно обобщить для произвольных
/// Extensions как variadic template параметры.
static bool test_cr_crtp_mixin_chain()
{
    static_assert( !std::is_polymorphic<pmm::PersistMemoryManager<>>::value,
                   "PersistMemoryManager must not be polymorphic (no virtual functions, Issue #73 AR-02)" );

    // StatsMixin::get_stats() доступен через PMM
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::MemoryStats stats = pmm::PersistMemoryManager<>::get_stats();
    PMM_TEST( stats.total_blocks >= 2 ); // hdr_blk + free_blk

    // ValidationMixin::validate() доступен через PMM
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// ─── A8: ThreadPolicy работает как политика шаблона ──────────────────────────

/// @brief Проверяем, что SharedMutexLock и NoLock работают как независимые политики.
/// [Issue #87 Code Review] Уже реализовано. Нужно инжектировать глубже — в AllocatorPolicy.
static bool test_cr_thread_policy_injection()
{
    using NoLockPMM = pmm::PersistMemoryManager<pmm::config::PMMConfig<16, 64, pmm::config::NoLock>>;

    static_assert( !std::is_same<pmm::PersistMemoryManager<>, NoLockPMM>::value,
                   "Different lock policies must be different types" );

    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( NoLockPMM::create( mem, size ) );
    PMM_TEST( NoLockPMM::is_initialized() );
    PMM_TEST( NoLockPMM::validate() );

    NoLockPMM::destroy();
    std::free( mem );
    return true;
}

// =============================================================================
// PART B: Маяки будущих абстракций (закомментированы до реализации фаз)
// =============================================================================

// [Phase 1] AddressTraits — РЕАЛИЗОВАНО в include/pmm/address_traits.h
// ────────────────────────────────────────────────────────────────────────────

static bool test_phase1_address_traits()
{
    // 8-bit адресация, 8-байтная гранула (для tiny embedded)
    using A8 = pmm::AddressTraits<std::uint8_t, 8>;
    static_assert( A8::granule_size == 8 );
    static_assert( A8::no_block == 0xFFU );
    static_assert( A8::bytes_to_granules( 8 ) == 1 );
    static_assert( A8::bytes_to_granules( 9 ) == 2 );

    // 16-bit адресация, 16-байтная гранула
    using A16 = pmm::AddressTraits<std::uint16_t, 16>;
    static_assert( A16::no_block == 0xFFFFU );

    // 32-bit адресация, 16-байтная гранула (текущий дефолт)
    using A32 = pmm::AddressTraits<std::uint32_t, 16>;
    static_assert( A32::no_block == pmm::detail::kNoBlock );
    static_assert( A32::granule_size == pmm::kGranuleSize );

    // DefaultAddressTraits — алиас для A32
    static_assert( std::is_same<pmm::DefaultAddressTraits, A32>::value );

    return true;
}

// [Phase 2] LinkedListNode + TreeNode — РЕАЛИЗОВАНО в include/pmm/linked_list_node.h и tree_node.h
// ────────────────────────────────────────────────────────────────────────────
#include "pmm/linked_list_node.h"
#include "pmm/tree_node.h"

static bool test_phase2_list_and_tree_nodes()
{
    using A = pmm::DefaultAddressTraits;

    // LinkedListNode<A> содержит prev_offset и next_offset типа A::index_type
    static_assert( std::is_same<decltype( pmm::LinkedListNode<A>::prev_offset ), typename A::index_type>::value,
                   "prev_offset must be index_type" );
    static_assert( std::is_same<decltype( pmm::LinkedListNode<A>::next_offset ), typename A::index_type>::value,
                   "next_offset must be index_type" );

    // TreeNode<A> содержит left, right, parent, avl_height
    static_assert( std::is_same<decltype( pmm::TreeNode<A>::left_offset ), typename A::index_type>::value,
                   "left_offset must be index_type" );
    static_assert( std::is_same<decltype( pmm::TreeNode<A>::right_offset ), typename A::index_type>::value,
                   "right_offset must be index_type" );
    static_assert( std::is_same<decltype( pmm::TreeNode<A>::parent_offset ), typename A::index_type>::value,
                   "parent_offset must be index_type" );

    // 8-bit адресация: поля LinkedListNode и TreeNode используют uint8_t
    using A8 = pmm::TinyAddressTraits;
    static_assert( std::is_same<decltype( pmm::LinkedListNode<A8>::prev_offset ), std::uint8_t>::value );
    static_assert( std::is_same<decltype( pmm::TreeNode<A8>::left_offset ), std::uint8_t>::value );

    return true;
}

// [Phase 3] Block — РЕАЛИЗОВАНО в include/pmm/block.h
// ────────────────────────────────────────────────────────────────────────────
#include "pmm/block.h"

static bool test_phase3_block_layout()
{
    using A = pmm::DefaultAddressTraits;

    // Block<DefaultAddressTraits> имеет тот же размер что и BlockHeader (32 байта)
    static_assert( sizeof( pmm::Block<A> ) == sizeof( pmm::detail::BlockHeader ) );

    // Block наследует LinkedListNode и TreeNode
    static_assert( std::is_base_of<pmm::LinkedListNode<A>, pmm::Block<A>>::value );
    static_assert( std::is_base_of<pmm::TreeNode<A>, pmm::Block<A>>::value );

    return true;
}

// [Phase 5] StorageBackend
// ────────────────────────────────────────────────────────────────────────────
// #include "pmm/static_storage.h"
// #include "pmm/heap_storage.h"
//
// static bool test_phase5_static_storage()
// {
//     // StaticStorage<1024> — compile-time буфер 1KB
//     pmm::StaticStorage<1024> storage;
//     PMM_TEST(storage.base_ptr() != nullptr);
//     PMM_TEST(storage.total_size() == 1024);
//     PMM_TEST(!storage.expand(2048)); // нельзя расширить статический буфер
//     PMM_TEST(!storage.owns_memory()); // буфер на стеке
//     return true;
// }
//
// static bool test_phase5_heap_storage()
// {
//     pmm::HeapStorage<> storage;
//     PMM_TEST(storage.init(64 * 1024));
//     PMM_TEST(storage.total_size() == 64 * 1024);
//     PMM_TEST(storage.expand(128 * 1024));
//     PMM_TEST(storage.total_size() == 128 * 1024);
//     return true;
// }

// [Phase 7] AbstractPersistMemoryManager
// ────────────────────────────────────────────────────────────────────────────
// #include "pmm/abstract_pmm.h"
// #include "pmm/pmm_presets.h"
//
// static bool test_phase7_abstract_pmm_default()
// {
//     // AbstractPMM с дефолтными параметрами ≡ текущий PersistMemoryManager<>
//     using AbstractDefault = pmm::AbstractPersistMemoryManager<>;
//     const std::size_t size = 64 * 1024;
//     void* mem = std::malloc(size);
//     PMM_TEST(mem != nullptr);
//     PMM_TEST(AbstractDefault::create(mem, size));
//     PMM_TEST(AbstractDefault::validate());
//     AbstractDefault::destroy();
//     std::free(mem);
//     return true;
// }
//
// static bool test_phase7_presets_compile()
// {
//     // Все пресеты должны компилироваться
//     static_assert(std::is_class<pmm::presets::SingleThreadedHeap>::value);
//     static_assert(std::is_class<pmm::presets::MultiThreadedHeap>::value);
//     static_assert(std::is_class<pmm::presets::PersistentFileMapped>::value);
//     return true;
// }
//
// static bool test_phase7_preset_static_1k()
// {
//     // Статический менеджер для embedded, 1KB
//     // pmm::presets::EmbeddedStatic1K::create() — без параметра memory (буфер внутри)
//     PMM_TEST(pmm::presets::EmbeddedStatic1K::create());
//     PMM_TEST(pmm::presets::EmbeddedStatic1K::is_initialized());
//     auto p = pmm::presets::EmbeddedStatic1K::allocate_typed<std::uint8_t>(16);
//     PMM_TEST(!p.is_null());
//     pmm::presets::EmbeddedStatic1K::deallocate_typed(p);
//     pmm::presets::EmbeddedStatic1K::destroy();
//     return true;
// }

// =============================================================================
// PART C: Интеграционные тесты — текущий API должен работать после рефакторинга
// =============================================================================

/// @brief Полный сценарий allocate/deallocate/validate должен работать на всех фазах.
static bool test_integration_full_lifecycle()
{
    const std::size_t size = 128 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::is_initialized() );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    // Выделяем блоки разных размеров
    auto p1 = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 16 );
    auto p2 = pmm::PersistMemoryManager<>::allocate_typed<std::uint32_t>( 32 );
    auto p3 = pmm::PersistMemoryManager<>::allocate_typed<double>( 8 );
    PMM_TEST( !p1.is_null() && !p2.is_null() && !p3.is_null() );

    // Освобождаем в разном порядке (проверка coalesce)
    pmm::PersistMemoryManager<>::deallocate_typed( p2 );
    pmm::PersistMemoryManager<>::deallocate_typed( p1 );
    pmm::PersistMemoryManager<>::deallocate_typed( p3 );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/// @brief Проверка сохранения/загрузки (persistence) между сессиями менеджера.
/// Примечание: destroy() обнуляет magic, поэтому для имитации persistence
/// используем второй буфер с копией данных (как при реальном save/load).
static bool test_integration_persistence()
{
    const std::size_t size  = 64 * 1024;
    void*             mem   = std::malloc( size );
    void*             saved = std::malloc( size ); // буфер для "сохранённого образа"
    PMM_TEST( mem != nullptr && saved != nullptr );

    // Создаём, записываем данные, копируем образ (до destroy)
    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );
    auto p = pmm::PersistMemoryManager<>::allocate_typed<std::uint64_t>( 1 );
    PMM_TEST( !p.is_null() );
    std::uint32_t saved_offset = p.offset();
    *p.get()                   = 0xDEADBEEFCAFEBABEULL;

    // Копируем образ ПАМ до вызова destroy() (имитация file save)
    std::memcpy( saved, mem, size );
    pmm::PersistMemoryManager<>::destroy();

    // Загружаем из сохранённого образа
    PMM_TEST( pmm::PersistMemoryManager<>::load( saved, size ) );
    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    // Данные сохранились
    auto p2 = pmm::pptr<std::uint64_t>( saved_offset );
    PMM_TEST( p2.get() != nullptr );
    PMM_TEST( *p2.get() == 0xDEADBEEFCAFEBABEULL );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    std::free( saved );
    return true;
}

/// @brief Проверка NoLock конфига — работает как отдельная инстанция.
static bool test_integration_nolock_config()
{
    using NoLockPMM = pmm::PersistMemoryManager<pmm::config::PMMConfig<16, 64, pmm::config::NoLock>>;

    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( NoLockPMM::create( mem, size ) );
    PMM_TEST( NoLockPMM::validate() );

    auto p = NoLockPMM::allocate_typed<std::uint32_t>( 10 );
    PMM_TEST( !p.is_null() );

    NoLockPMM::deallocate_typed( p );
    NoLockPMM::destroy();
    std::free( mem );
    return true;
}

/// @brief Проверка статистики через StatsMixin (CRTP).
static bool test_integration_stats()
{
    const std::size_t size = 64 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    pmm::MemoryStats before = pmm::PersistMemoryManager<>::get_stats();
    PMM_TEST( before.free_blocks == 1 );
    PMM_TEST( before.allocated_blocks == 1 ); // BlockHeader_0 (ManagerHeader)

    auto p = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( 128 );
    PMM_TEST( !p.is_null() );

    pmm::MemoryStats after = pmm::PersistMemoryManager<>::get_stats();
    PMM_TEST( after.allocated_blocks == 2 );

    pmm::PersistMemoryManager<>::deallocate_typed( p );
    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

/// @brief Проверка AVL-дерева при множественных аллокациях/деаллокациях.
static bool test_integration_avl_tree_invariants()
{
    const std::size_t size = 256 * 1024;
    void*             mem  = std::malloc( size );
    PMM_TEST( mem != nullptr );

    PMM_TEST( pmm::PersistMemoryManager<>::create( mem, size ) );

    // Создаём фрагментацию
    static const int        N = 20;
    pmm::pptr<std::uint8_t> ptrs[N]{};
    for ( int i = 0; i < N; ++i )
        ptrs[i] = pmm::PersistMemoryManager<>::allocate_typed<std::uint8_t>( ( i + 1 ) * 64 );

    // Освобождаем чётные индексы (фрагментация)
    for ( int i = 0; i < N; i += 2 )
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    // Освобождаем нечётные (coalesce)
    for ( int i = 1; i < N; i += 2 )
        pmm::PersistMemoryManager<>::deallocate_typed( ptrs[i] );

    PMM_TEST( pmm::PersistMemoryManager<>::validate() );

    pmm::PersistMemoryManager<>::destroy();
    std::free( mem );
    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::cout << "=== test_issue87_abstraction (Phase 0: Code Review + Lighthouses) ===\n\n";
    bool all_passed = true;

    std::cout << "--- Part A: Code Review (current architecture) ---\n";
    PMM_RUN( "A1: granule_size is hardcoded (candidate for AddressTraits)", test_cr_granule_size_is_hardcoded );
    PMM_RUN( "A2: kNoBlock is max uint32 (32-bit address bus)", test_cr_no_block_is_max_uint32 );
    PMM_RUN( "A3: BlockHeader combines list+tree (to be split)", test_cr_block_header_combines_list_and_tree );
    PMM_RUN( "A4: BlockHeader uses uint32 indices (hardcoded bus width)", test_cr_block_header_uses_uint32_indices );
    PMM_RUN( "A5: pptr resolves via default singleton", test_cr_pptr_resolves_via_default_singleton );
    PMM_RUN( "A6: PMMConfig partially parametrizes", test_cr_pmm_config_partial_parametrization );
    PMM_RUN( "A7: PersistentAvlTree is standalone (good)", test_cr_avl_tree_is_standalone );
    PMM_RUN( "A8: only heap backend exists (static/mmap missing)", test_cr_only_heap_backend_exists );
    PMM_RUN( "A9: CRTP mixin chain works (good)", test_cr_crtp_mixin_chain );
    PMM_RUN( "A10: ThreadPolicy injection works (good)", test_cr_thread_policy_injection );

    std::cout << "\n--- Part B: Phase beacons (implemented abstractions) ---\n";
    PMM_RUN( "B1: AddressTraits<> — 8/16/32-bit address buses and no_block", test_phase1_address_traits );
    PMM_RUN( "B2: LinkedListNode<A> + TreeNode<A> — parametric node types", test_phase2_list_and_tree_nodes );
    PMM_RUN( "B3: Block<A> — composite type inheriting LinkedListNode + TreeNode", test_phase3_block_layout );

    std::cout << "\n--- Part C: Integration (must pass on all phases) ---\n";
    PMM_RUN( "C1: full lifecycle allocate/deallocate/validate", test_integration_full_lifecycle );
    PMM_RUN( "C2: persistence save/load", test_integration_persistence );
    PMM_RUN( "C3: NoLock config lifecycle", test_integration_nolock_config );
    PMM_RUN( "C4: stats via StatsMixin", test_integration_stats );
    PMM_RUN( "C5: AVL tree invariants under fragmentation", test_integration_avl_tree_invariants );

    std::cout << "\n" << ( all_passed ? "All tests PASSED\n" : "Some tests FAILED\n" );
    return all_passed ? 0 : 1;
}
