/**
 * @file test_block_state.cpp
 * @brief Тесты Phase 9: BlockState machine для атомарных операций (Issue #93).
 *
 * Проверяет:
 *  - Бинарная совместимость BlockStateBase с Block<A>.
 *  - Корректность переходов между состояниями.
 *  - Методы состояний (remove_from_avl, mark_as_allocated, mark_as_free, etc.).
 *  - Детекция и восстановление состояний.
 *  - Инварианты всех состояний.
 *
 * @see include/pmm/block_state.h
 * @see docs/atomic_writes.md «Граф состояний блока»
 * @version 0.2 (Issue #93 — based on Block<A>)
 */

#include "pmm_single_threaded_heap.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <type_traits>

// ─── Макросы тестирования ─────────────────────────────────────────────────────

// =============================================================================
// Phase 9 tests: BlockState machine (Issue #93)
// =============================================================================

// ─── P9-A: Бинарная совместимость BlockStateBase ──────────────────────────────

/// @brief sizeof(BlockStateBase<DefaultAddressTraits>) == sizeof(Block<A>) == 32.
TEST_CASE( "    BlockStateBase size", "[test_block_state]" )
{
    using A = pmm::DefaultAddressTraits;

    static_assert( sizeof( pmm::BlockStateBase<A> ) == 32, "BlockStateBase<DefaultAddressTraits> must be 32 bytes" );
    static_assert( sizeof( pmm::BlockStateBase<A> ) == sizeof( pmm::Block<A> ),
                   "BlockStateBase must match Block size" );
}

/// @brief Все состояния блока имеют одинаковый размер (бинарная совместимость).
TEST_CASE( "    All states same size", "[test_block_state]" )
{
    using A = pmm::DefaultAddressTraits;

    static_assert( sizeof( pmm::FreeBlock<A> ) == 32, "FreeBlock must be 32 bytes" );
    static_assert( sizeof( pmm::AllocatedBlock<A> ) == 32, "AllocatedBlock must be 32 bytes" );
    static_assert( sizeof( pmm::FreeBlockRemovedAVL<A> ) == 32, "FreeBlockRemovedAVL must be 32 bytes" );
    static_assert( sizeof( pmm::FreeBlockNotInAVL<A> ) == 32, "FreeBlockNotInAVL must be 32 bytes" );
    static_assert( sizeof( pmm::SplittingBlock<A> ) == 32, "SplittingBlock must be 32 bytes" );
    static_assert( sizeof( pmm::CoalescingBlock<A> ) == 32, "CoalescingBlock must be 32 bytes" );
}

// ─── P9-B: Read-only accessors ────────────────────────────────────────────────

/// @brief Проверка read-only accessors через cast_from_raw.
TEST_CASE( "    Accessors", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Создаём блок через BlockStateBase::init_fields + static setters (Issue #120)
    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    BlockState::init_fields( buffer, 10u, 20u, 0, 5u, 6u );

    // Интерпретируем как BlockStateBase
    auto* state = reinterpret_cast<pmm::BlockStateBase<A>*>( buffer );

    REQUIRE( state->prev_offset() == 10 );
    REQUIRE( state->next_offset() == 20 );
    REQUIRE( state->weight() == 5 );
    REQUIRE( state->root_offset() == 6 );
}

/// @brief Проверка is_free() и is_allocated().
TEST_CASE( "    is_free / is_allocated", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];

    // Свободный блок: weight=0, root_offset=0
    std::memset( buffer, 0, sizeof( buffer ) );
    auto* state = reinterpret_cast<BlockState*>( buffer );
    REQUIRE( state->is_free() == true );
    REQUIRE( state->is_allocated( 0 ) == false );
    REQUIRE( state->is_allocated( 6 ) == false );

    // Занятый блок: weight>0, root_offset=idx (via static setters, Issue #120)
    BlockState::set_weight_of( buffer, 5u );
    BlockState::set_root_offset_of( buffer, 6u ); // idx=6
    REQUIRE( state->is_free() == false );
    REQUIRE( state->is_allocated( 6 ) == true );
    REQUIRE( state->is_allocated( 7 ) == false ); // Другой idx
}

// ─── P9-C: FreeBlock state ────────────────────────────────────────────────────

/// @brief FreeBlock::cast_from_raw и verify_invariants.
TEST_CASE( "FreeBlock cast_from_raw and verify_invariants", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* fb = pmm::FreeBlock<A>::cast_from_raw( buffer );
    REQUIRE( fb != nullptr );
    REQUIRE( fb->verify_invariants() == true );

    // Портим инварианты (via static setter, Issue #120)
    BlockState::set_weight_of( buffer, 5u ); // Не 0
    REQUIRE( fb->verify_invariants() == false );
}

/// @brief FreeBlock::remove_from_avl() → FreeBlockRemovedAVL.
TEST_CASE( "    remove_from_avl", "[test_block_state]" )
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* fb      = pmm::FreeBlock<A>::cast_from_raw( buffer );
    auto* removed = fb->remove_from_avl();

    // Должен вернуть тот же адрес (reinterpret)
    REQUIRE( reinterpret_cast<void*>( removed ) == reinterpret_cast<void*>( fb ) );

    // Инварианты должны сохраниться (weight=0, root_offset=0)
    REQUIRE( removed->weight() == 0 );
    REQUIRE( removed->root_offset() == 0 );
}

// ─── P9-D: FreeBlockRemovedAVL state ──────────────────────────────────────────

/// @brief FreeBlockRemovedAVL::mark_as_allocated() → AllocatedBlock.
TEST_CASE( "    mark_as_allocated", "[test_block_state]" )
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* fb      = pmm::FreeBlock<A>::cast_from_raw( buffer );
    auto* removed = fb->remove_from_avl();
    auto* alloc   = removed->mark_as_allocated( 5, 6 ); // weight=5, idx=6

    REQUIRE( reinterpret_cast<void*>( alloc ) == reinterpret_cast<void*>( fb ) );
    REQUIRE( alloc->weight() == 5 );
    REQUIRE( alloc->root_offset() == 6 );
    REQUIRE( alloc->verify_invariants( 6 ) == true );
    REQUIRE( alloc->verify_invariants( 7 ) == false ); // Неверный idx
}

/// @brief FreeBlockRemovedAVL::begin_splitting() → SplittingBlock.
TEST_CASE( "    begin_splitting", "[test_block_state]" )
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* removed   = pmm::FreeBlockRemovedAVL<A>::cast_from_raw( buffer );
    auto* splitting = removed->begin_splitting();

    REQUIRE( reinterpret_cast<void*>( splitting ) == reinterpret_cast<void*>( removed ) );
}

/// @brief FreeBlockRemovedAVL::insert_to_avl() → FreeBlock (откат).
TEST_CASE( "    insert_to_avl (rollback)", "[test_block_state]" )
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* removed = pmm::FreeBlockRemovedAVL<A>::cast_from_raw( buffer );
    auto* fb      = removed->insert_to_avl();

    REQUIRE( reinterpret_cast<void*>( fb ) == reinterpret_cast<void*>( removed ) );
    REQUIRE( fb->verify_invariants() == true );
}

// ─── P9-E: SplittingBlock state ───────────────────────────────────────────────

/// @brief SplittingBlock::initialize_new_block + link_new_block + finalize_split.
TEST_CASE( "    Full splitting flow", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Два блока: текущий и новый (результат split)
    alignas( 16 ) std::uint8_t buffer_curr[32];
    alignas( 16 ) std::uint8_t buffer_new[32];
    alignas( 16 ) std::uint8_t buffer_old_next[32];

    // Инициализация текущего блока (via BlockStateBase static API, Issue #120)
    std::memset( buffer_curr, 0, sizeof( buffer_curr ) );
    BlockState::set_next_offset_of( buffer_curr, 100u ); // Указывает на старый следующий

    // Инициализация старого следующего блока (via BlockStateBase static API, Issue #120)
    std::memset( buffer_old_next, 0, sizeof( buffer_old_next ) );
    BlockState::set_prev_offset_of( buffer_old_next, 6u ); // Указывает на текущий (idx=6)

    // Начинаем splitting
    auto* splitting = pmm::SplittingBlock<A>::cast_from_raw( buffer_curr );

    // 1. Инициализируем новый блок (idx=8)
    splitting->initialize_new_block( buffer_new, 8, 6 );

    // Проверяем новый блок
    auto* new_state = reinterpret_cast<pmm::BlockStateBase<A>*>( buffer_new );
    REQUIRE( new_state->prev_offset() == 6 );   // Указывает на текущий
    REQUIRE( new_state->next_offset() == 100 ); // Указывает на старый следующий
    REQUIRE( new_state->avl_height() == 1 );    // Готов к AVL

    // 2. Связываем новый блок
    splitting->link_new_block( buffer_old_next, 8 );

    REQUIRE( splitting->next_offset() == 8 );                       // Текущий → новый
    REQUIRE( BlockState::get_prev_offset( buffer_old_next ) == 8 ); // Старый следующий ← новый

    // 3. Финализируем split
    auto* alloc = splitting->finalize_split( 5, 6 );

    REQUIRE( alloc->weight() == 5 );
    REQUIRE( alloc->root_offset() == 6 );
    REQUIRE( alloc->verify_invariants( 6 ) == true );
}

// ─── P9-F: AllocatedBlock state ───────────────────────────────────────────────

/// @brief AllocatedBlock::cast_from_raw, verify_invariants, user_ptr.
TEST_CASE( "AllocatedBlock cast_from_raw and verify_invariants", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[64]; // 32 header + 32 data
    std::memset( buffer, 0, sizeof( buffer ) );

    // Initialize via BlockStateBase static API (Issue #120)
    BlockState::set_weight_of( buffer, 2u );      // 2 гранулы данных
    BlockState::set_root_offset_of( buffer, 0u ); // idx=0

    auto* alloc = pmm::AllocatedBlock<A>::cast_from_raw( buffer );
    REQUIRE( alloc != nullptr );
    REQUIRE( alloc->verify_invariants( 0 ) == true );
    REQUIRE( alloc->verify_invariants( 1 ) == false ); // Неверный idx

    // user_ptr = header + sizeof(Block<A>)
    void* uptr = alloc->user_ptr();
    REQUIRE( uptr == buffer + 32 );
}

/// @brief AllocatedBlock::mark_as_free() → FreeBlockNotInAVL.
TEST_CASE( "    mark_as_free", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // Создаём занятый блок (via BlockStateBase static API, Issue #120)
    BlockState::set_weight_of( buffer, 5u );
    BlockState::set_root_offset_of( buffer, 6u );

    auto* alloc   = pmm::AllocatedBlock<A>::cast_from_raw( buffer );
    auto* not_avl = alloc->mark_as_free();

    REQUIRE( reinterpret_cast<void*>( not_avl ) == reinterpret_cast<void*>( alloc ) );
    REQUIRE( not_avl->weight() == 0 );
    REQUIRE( not_avl->root_offset() == 0 );
}

// ─── P9-G: FreeBlockNotInAVL state ────────────────────────────────────────────

/// @brief FreeBlockNotInAVL::insert_to_avl() → FreeBlock.
TEST_CASE( "    insert_to_avl", "[test_block_state]" )
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* not_avl = pmm::FreeBlockNotInAVL<A>::cast_from_raw( buffer );
    auto* fb      = not_avl->insert_to_avl();

    REQUIRE( reinterpret_cast<void*>( fb ) == reinterpret_cast<void*>( not_avl ) );
    REQUIRE( fb->avl_height() == 1 ); // Готов к вставке
    REQUIRE( fb->verify_invariants() == true );
}

/// @brief FreeBlockNotInAVL::begin_coalescing() → CoalescingBlock.
TEST_CASE( "    begin_coalescing", "[test_block_state]" )
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* not_avl    = pmm::FreeBlockNotInAVL<A>::cast_from_raw( buffer );
    auto* coalescing = not_avl->begin_coalescing();

    REQUIRE( reinterpret_cast<void*>( coalescing ) == reinterpret_cast<void*>( not_avl ) );
}

// ─── P9-H: CoalescingBlock state ──────────────────────────────────────────────

/// @brief CoalescingBlock::coalesce_with_next — слияние с правым соседом.
TEST_CASE( "    coalesce_with_next", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Три блока: текущий (idx=6), следующий (idx=10), следующий следующего (idx=20)
    alignas( 16 ) std::uint8_t buffer_curr[32];
    alignas( 16 ) std::uint8_t buffer_next[32];
    alignas( 16 ) std::uint8_t buffer_nxt_nxt[32];

    // Инициализация текущего блока (via BlockStateBase static API, Issue #120)
    std::memset( buffer_curr, 0, sizeof( buffer_curr ) );
    BlockState::set_next_offset_of( buffer_curr, 10u ); // → следующий

    // Инициализация следующего блока (будет поглощён)
    std::memset( buffer_next, 0, sizeof( buffer_next ) );
    BlockState::set_prev_offset_of( buffer_next, 6u );  // ← текущий
    BlockState::set_next_offset_of( buffer_next, 20u ); // → следующий следующего

    // Инициализация следующего следующего
    std::memset( buffer_nxt_nxt, 0, sizeof( buffer_nxt_nxt ) );
    BlockState::set_prev_offset_of( buffer_nxt_nxt, 10u ); // ← следующий

    // Выполняем слияние
    auto* coalescing = pmm::CoalescingBlock<A>::cast_from_raw( buffer_curr );
    coalescing->coalesce_with_next( buffer_next, buffer_nxt_nxt, 6 );

    // Проверяем результат
    REQUIRE( coalescing->next_offset() == 20 ); // Текущий → следующий следующего
    REQUIRE( BlockState::get_prev_offset( buffer_nxt_nxt ) == 6 ); // Следующий следующего ← текущий

    // Следующий блок должен быть обнулён
    for ( size_t i = 0; i < sizeof( buffer_next ); ++i )
    {
        REQUIRE( buffer_next[i] == 0 );
    }
}

/// @brief CoalescingBlock::coalesce_with_prev — слияние с левым соседом.
TEST_CASE( "    coalesce_with_prev", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    // Три блока: предыдущий (idx=4), текущий (idx=10), следующий (idx=20)
    alignas( 16 ) std::uint8_t buffer_prev[32];
    alignas( 16 ) std::uint8_t buffer_curr[32];
    alignas( 16 ) std::uint8_t buffer_next[32];

    // Инициализация предыдущего блока (via BlockStateBase static API, Issue #120)
    std::memset( buffer_prev, 0, sizeof( buffer_prev ) );
    BlockState::set_next_offset_of( buffer_prev, 10u ); // → текущий

    // Инициализация текущего блока (будет поглощён)
    std::memset( buffer_curr, 0, sizeof( buffer_curr ) );
    BlockState::set_prev_offset_of( buffer_curr, 4u );  // ← предыдущий
    BlockState::set_next_offset_of( buffer_curr, 20u ); // → следующий

    // Инициализация следующего блока
    std::memset( buffer_next, 0, sizeof( buffer_next ) );
    BlockState::set_prev_offset_of( buffer_next, 10u ); // ← текущий

    // Выполняем слияние
    auto* coalescing = pmm::CoalescingBlock<A>::cast_from_raw( buffer_curr );
    auto* result     = coalescing->coalesce_with_prev( buffer_prev, buffer_next, 4 );

    // Результат — предыдущий блок
    REQUIRE( reinterpret_cast<void*>( result ) == reinterpret_cast<void*>( buffer_prev ) );
    REQUIRE( result->next_offset() == 20 );                     // Предыдущий → следующий
    REQUIRE( BlockState::get_prev_offset( buffer_next ) == 4 ); // Следующий ← предыдущий

    // Текущий блок должен быть обнулён
    for ( size_t i = 0; i < sizeof( buffer_curr ); ++i )
    {
        REQUIRE( buffer_curr[i] == 0 );
    }
}

/// @brief CoalescingBlock::finalize_coalesce() → FreeBlock.
TEST_CASE( "    finalize_coalesce", "[test_block_state]" )
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* coalescing = pmm::CoalescingBlock<A>::cast_from_raw( buffer );
    auto* fb         = coalescing->finalize_coalesce();

    REQUIRE( reinterpret_cast<void*>( fb ) == reinterpret_cast<void*>( coalescing ) );
    REQUIRE( fb->avl_height() == 1 ); // Готов к вставке в AVL
    REQUIRE( fb->verify_invariants() == true );
}

// ─── P9-I: Utility functions ──────────────────────────────────────────────────

/// @brief detect_block_state — определение состояния блока.
TEST_CASE( "    detect_block_state", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];

    // Свободный блок
    std::memset( buffer, 0, sizeof( buffer ) );
    REQUIRE( pmm::detect_block_state<A>( buffer, 6 ) == 0 ); // FreeBlock

    // Занятый блок (idx=6) (via BlockStateBase static API, Issue #120)
    BlockState::set_weight_of( buffer, 5u );
    BlockState::set_root_offset_of( buffer, 6u );
    REQUIRE( pmm::detect_block_state<A>( buffer, 6 ) == 1 );  // AllocatedBlock
    REQUIRE( pmm::detect_block_state<A>( buffer, 7 ) == -1 ); // Неопределённое (неверный idx)

    // Некорректное состояние: weight=0, но root_offset != 0
    BlockState::set_weight_of( buffer, 0u );
    BlockState::set_root_offset_of( buffer, 6u );
    REQUIRE( pmm::detect_block_state<A>( buffer, 6 ) == -1 ); // Неопределённое
}

/// @brief recover_block_state — восстановление состояния блока.
TEST_CASE( "    recover_block_state", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];

    // Случай 1: weight>0, но root_offset неверен (via BlockStateBase static API, Issue #120)
    std::memset( buffer, 0, sizeof( buffer ) );
    BlockState::set_weight_of( buffer, 5u );
    BlockState::set_root_offset_of( buffer, 10u ); // Должен быть 6

    pmm::recover_block_state<A>( buffer, 6 );
    REQUIRE( BlockState::get_root_offset( buffer ) == 6 ); // Исправлено

    // Случай 2: weight==0, но root_offset != 0
    BlockState::set_weight_of( buffer, 0u );
    BlockState::set_root_offset_of( buffer, 6u );

    pmm::recover_block_state<A>( buffer, 6 );
    REQUIRE( BlockState::get_root_offset( buffer ) == 0 ); // Исправлено

    // Случай 3: корректное состояние — без изменений
    BlockState::set_weight_of( buffer, 5u );
    BlockState::set_root_offset_of( buffer, 6u );

    pmm::recover_block_state<A>( buffer, 6 );
    REQUIRE( BlockState::get_weight( buffer ) == 5 );
    REQUIRE( BlockState::get_root_offset( buffer ) == 6 ); // Без изменений
}

// ─── P9-J: Full allocate flow simulation ──────────────────────────────────────

/// @brief Симуляция полного цикла allocate без split.
TEST_CASE( "    Allocate flow (no split)", "[test_block_state]" )
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // 1. Начинаем с FreeBlock
    auto* fb = pmm::FreeBlock<A>::cast_from_raw( buffer );
    REQUIRE( fb->verify_invariants() == true );

    // 2. Удаляем из AVL (внешняя операция)
    auto* removed = fb->remove_from_avl();
    REQUIRE( removed->weight() == 0 );
    REQUIRE( removed->root_offset() == 0 );

    // 3. Помечаем как занятый
    auto* alloc = removed->mark_as_allocated( 5, 6 );
    REQUIRE( alloc->verify_invariants( 6 ) == true );
    REQUIRE( alloc->weight() == 5 );
}

/// @brief Симуляция полного цикла deallocate без coalesce.
TEST_CASE( "    Deallocate flow (no coalesce)", "[test_block_state]" )
{
    using A          = pmm::DefaultAddressTraits;
    using BlockState = pmm::BlockStateBase<A>;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // 1. Создаём занятый блок (via BlockStateBase static API, Issue #120)
    BlockState::set_weight_of( buffer, 5u );
    BlockState::set_root_offset_of( buffer, 6u );

    auto* alloc = pmm::AllocatedBlock<A>::cast_from_raw( buffer );
    REQUIRE( alloc->verify_invariants( 6 ) == true );

    // 2. Освобождаем
    auto* not_avl = alloc->mark_as_free();
    REQUIRE( not_avl->weight() == 0 );
    REQUIRE( not_avl->root_offset() == 0 );

    // 3. Добавляем в AVL (внешняя операция)
    auto* fb = not_avl->insert_to_avl();
    REQUIRE( fb->verify_invariants() == true );
    REQUIRE( fb->avl_height() == 1 );
}

// =============================================================================
// main
// =============================================================================
