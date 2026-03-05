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

#include "pmm/block_state.h"
#include "pmm/types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
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
// Phase 9 tests: BlockState machine (Issue #93)
// =============================================================================

// ─── P9-A: Бинарная совместимость BlockStateBase ──────────────────────────────

/// @brief sizeof(BlockStateBase<DefaultAddressTraits>) == sizeof(Block<A>) == 32.
static bool test_p9_block_state_base_size()
{
    using A = pmm::DefaultAddressTraits;

    static_assert( sizeof( pmm::BlockStateBase<A> ) == 32, "BlockStateBase<DefaultAddressTraits> must be 32 bytes" );
    static_assert( sizeof( pmm::BlockStateBase<A> ) == sizeof( pmm::Block<A> ),
                   "BlockStateBase must match Block size" );

    return true;
}

/// @brief Все состояния блока имеют одинаковый размер (бинарная совместимость).
static bool test_p9_all_states_same_size()
{
    using A = pmm::DefaultAddressTraits;

    static_assert( sizeof( pmm::FreeBlock<A> ) == 32, "FreeBlock must be 32 bytes" );
    static_assert( sizeof( pmm::AllocatedBlock<A> ) == 32, "AllocatedBlock must be 32 bytes" );
    static_assert( sizeof( pmm::FreeBlockRemovedAVL<A> ) == 32, "FreeBlockRemovedAVL must be 32 bytes" );
    static_assert( sizeof( pmm::FreeBlockNotInAVL<A> ) == 32, "FreeBlockNotInAVL must be 32 bytes" );
    static_assert( sizeof( pmm::SplittingBlock<A> ) == 32, "SplittingBlock must be 32 bytes" );
    static_assert( sizeof( pmm::CoalescingBlock<A> ) == 32, "CoalescingBlock must be 32 bytes" );

    return true;
}

// ─── P9-B: Read-only accessors ────────────────────────────────────────────────

/// @brief Проверка read-only accessors через cast_from_raw.
static bool test_p9_accessors()
{
    using A = pmm::DefaultAddressTraits;

    // Создаём блок вручную через Block<A> layout
    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* blk        = reinterpret_cast<pmm::Block<A>*>( buffer );
    blk->prev_offset = 10;
    blk->next_offset = 20;
    blk->weight      = 5;
    blk->root_offset = 6;

    // Интерпретируем как BlockStateBase
    auto* state = reinterpret_cast<pmm::BlockStateBase<A>*>( buffer );

    PMM_TEST( state->prev_offset() == 10 );
    PMM_TEST( state->next_offset() == 20 );
    PMM_TEST( state->weight() == 5 );
    PMM_TEST( state->root_offset() == 6 );

    return true;
}

/// @brief Проверка is_free() и is_allocated().
static bool test_p9_is_free_is_allocated()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];

    // Свободный блок: weight=0, root_offset=0
    std::memset( buffer, 0, sizeof( buffer ) );
    auto* state = reinterpret_cast<pmm::BlockStateBase<A>*>( buffer );
    PMM_TEST( state->is_free() == true );
    PMM_TEST( state->is_allocated( 0 ) == false );
    PMM_TEST( state->is_allocated( 6 ) == false );

    // Занятый блок: weight>0, root_offset=idx
    auto* blk        = reinterpret_cast<pmm::Block<A>*>( buffer );
    blk->weight      = 5;
    blk->root_offset = 6; // idx=6
    PMM_TEST( state->is_free() == false );
    PMM_TEST( state->is_allocated( 6 ) == true );
    PMM_TEST( state->is_allocated( 7 ) == false ); // Другой idx

    return true;
}

// ─── P9-C: FreeBlock state ────────────────────────────────────────────────────

/// @brief FreeBlock::cast_from_raw и verify_invariants.
static bool test_p9_free_block_cast_and_verify()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* fb = pmm::FreeBlock<A>::cast_from_raw( buffer );
    PMM_TEST( fb != nullptr );
    PMM_TEST( fb->verify_invariants() == true );

    // Портим инварианты
    auto* blk   = reinterpret_cast<pmm::Block<A>*>( buffer );
    blk->weight = 5; // Не 0
    PMM_TEST( fb->verify_invariants() == false );

    return true;
}

/// @brief FreeBlock::remove_from_avl() → FreeBlockRemovedAVL.
static bool test_p9_free_block_remove_from_avl()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* fb      = pmm::FreeBlock<A>::cast_from_raw( buffer );
    auto* removed = fb->remove_from_avl();

    // Должен вернуть тот же адрес (reinterpret)
    PMM_TEST( reinterpret_cast<void*>( removed ) == reinterpret_cast<void*>( fb ) );

    // Инварианты должны сохраниться (weight=0, root_offset=0)
    PMM_TEST( removed->weight() == 0 );
    PMM_TEST( removed->root_offset() == 0 );

    return true;
}

// ─── P9-D: FreeBlockRemovedAVL state ──────────────────────────────────────────

/// @brief FreeBlockRemovedAVL::mark_as_allocated() → AllocatedBlock.
static bool test_p9_removed_avl_mark_allocated()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* fb      = pmm::FreeBlock<A>::cast_from_raw( buffer );
    auto* removed = fb->remove_from_avl();
    auto* alloc   = removed->mark_as_allocated( 5, 6 ); // weight=5, idx=6

    PMM_TEST( reinterpret_cast<void*>( alloc ) == reinterpret_cast<void*>( fb ) );
    PMM_TEST( alloc->weight() == 5 );
    PMM_TEST( alloc->root_offset() == 6 );
    PMM_TEST( alloc->verify_invariants( 6 ) == true );
    PMM_TEST( alloc->verify_invariants( 7 ) == false ); // Неверный idx

    return true;
}

/// @brief FreeBlockRemovedAVL::begin_splitting() → SplittingBlock.
static bool test_p9_removed_avl_begin_splitting()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* removed   = pmm::FreeBlockRemovedAVL<A>::cast_from_raw( buffer );
    auto* splitting = removed->begin_splitting();

    PMM_TEST( reinterpret_cast<void*>( splitting ) == reinterpret_cast<void*>( removed ) );

    return true;
}

/// @brief FreeBlockRemovedAVL::insert_to_avl() → FreeBlock (откат).
static bool test_p9_removed_avl_insert_to_avl()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* removed = pmm::FreeBlockRemovedAVL<A>::cast_from_raw( buffer );
    auto* fb      = removed->insert_to_avl();

    PMM_TEST( reinterpret_cast<void*>( fb ) == reinterpret_cast<void*>( removed ) );
    PMM_TEST( fb->verify_invariants() == true );

    return true;
}

// ─── P9-E: SplittingBlock state ───────────────────────────────────────────────

/// @brief SplittingBlock::initialize_new_block + link_new_block + finalize_split.
static bool test_p9_splitting_full_flow()
{
    using A = pmm::DefaultAddressTraits;

    // Два блока: текущий и новый (результат split)
    alignas( 16 ) std::uint8_t buffer_curr[32];
    alignas( 16 ) std::uint8_t buffer_new[32];
    alignas( 16 ) std::uint8_t buffer_old_next[32];

    // Инициализация текущего блока
    std::memset( buffer_curr, 0, sizeof( buffer_curr ) );
    auto* curr_blk        = reinterpret_cast<pmm::Block<A>*>( buffer_curr );
    curr_blk->next_offset = 100; // Указывает на старый следующий

    // Инициализация старого следующего блока
    std::memset( buffer_old_next, 0, sizeof( buffer_old_next ) );
    auto* old_next_blk        = reinterpret_cast<pmm::Block<A>*>( buffer_old_next );
    old_next_blk->prev_offset = 6; // Указывает на текущий (idx=6)

    // Начинаем splitting
    auto* splitting = pmm::SplittingBlock<A>::cast_from_raw( buffer_curr );

    // 1. Инициализируем новый блок (idx=8)
    splitting->initialize_new_block( buffer_new, 8, 6 );

    // Проверяем новый блок
    auto* new_state = reinterpret_cast<pmm::BlockStateBase<A>*>( buffer_new );
    PMM_TEST( new_state->prev_offset() == 6 );   // Указывает на текущий
    PMM_TEST( new_state->next_offset() == 100 ); // Указывает на старый следующий
    PMM_TEST( new_state->avl_height() == 1 );    // Готов к AVL

    // 2. Связываем новый блок
    splitting->link_new_block( buffer_old_next, 8 );

    PMM_TEST( splitting->next_offset() == 8 );  // Текущий → новый
    PMM_TEST( old_next_blk->prev_offset == 8 ); // Старый следующий ← новый

    // 3. Финализируем split
    auto* alloc = splitting->finalize_split( 5, 6 );

    PMM_TEST( alloc->weight() == 5 );
    PMM_TEST( alloc->root_offset() == 6 );
    PMM_TEST( alloc->verify_invariants( 6 ) == true );

    return true;
}

// ─── P9-F: AllocatedBlock state ───────────────────────────────────────────────

/// @brief AllocatedBlock::cast_from_raw, verify_invariants, user_ptr.
static bool test_p9_allocated_block_cast_and_verify()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[64]; // 32 header + 32 data
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* blk        = reinterpret_cast<pmm::Block<A>*>( buffer );
    blk->weight      = 2; // 2 гранулы данных
    blk->root_offset = 0; // idx=0

    auto* alloc = pmm::AllocatedBlock<A>::cast_from_raw( buffer );
    PMM_TEST( alloc != nullptr );
    PMM_TEST( alloc->verify_invariants( 0 ) == true );
    PMM_TEST( alloc->verify_invariants( 1 ) == false ); // Неверный idx

    // user_ptr = header + sizeof(Block<A>)
    void* uptr = alloc->user_ptr();
    PMM_TEST( uptr == buffer + 32 );

    return true;
}

/// @brief AllocatedBlock::mark_as_free() → FreeBlockNotInAVL.
static bool test_p9_allocated_mark_as_free()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // Создаём занятый блок
    auto* blk        = reinterpret_cast<pmm::Block<A>*>( buffer );
    blk->weight      = 5;
    blk->root_offset = 6;

    auto* alloc   = pmm::AllocatedBlock<A>::cast_from_raw( buffer );
    auto* not_avl = alloc->mark_as_free();

    PMM_TEST( reinterpret_cast<void*>( not_avl ) == reinterpret_cast<void*>( alloc ) );
    PMM_TEST( not_avl->weight() == 0 );
    PMM_TEST( not_avl->root_offset() == 0 );

    return true;
}

// ─── P9-G: FreeBlockNotInAVL state ────────────────────────────────────────────

/// @brief FreeBlockNotInAVL::insert_to_avl() → FreeBlock.
static bool test_p9_not_avl_insert_to_avl()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* not_avl = pmm::FreeBlockNotInAVL<A>::cast_from_raw( buffer );
    auto* fb      = not_avl->insert_to_avl();

    PMM_TEST( reinterpret_cast<void*>( fb ) == reinterpret_cast<void*>( not_avl ) );
    PMM_TEST( fb->avl_height() == 1 ); // Готов к вставке
    PMM_TEST( fb->verify_invariants() == true );

    return true;
}

/// @brief FreeBlockNotInAVL::begin_coalescing() → CoalescingBlock.
static bool test_p9_not_avl_begin_coalescing()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* not_avl    = pmm::FreeBlockNotInAVL<A>::cast_from_raw( buffer );
    auto* coalescing = not_avl->begin_coalescing();

    PMM_TEST( reinterpret_cast<void*>( coalescing ) == reinterpret_cast<void*>( not_avl ) );

    return true;
}

// ─── P9-H: CoalescingBlock state ──────────────────────────────────────────────

/// @brief CoalescingBlock::coalesce_with_next — слияние с правым соседом.
static bool test_p9_coalesce_with_next()
{
    using A = pmm::DefaultAddressTraits;

    // Три блока: текущий (idx=6), следующий (idx=10), следующий следующего (idx=20)
    alignas( 16 ) std::uint8_t buffer_curr[32];
    alignas( 16 ) std::uint8_t buffer_next[32];
    alignas( 16 ) std::uint8_t buffer_nxt_nxt[32];

    // Инициализация текущего блока
    std::memset( buffer_curr, 0, sizeof( buffer_curr ) );
    auto* curr_blk        = reinterpret_cast<pmm::Block<A>*>( buffer_curr );
    curr_blk->next_offset = 10; // → следующий

    // Инициализация следующего блока (будет поглощён)
    std::memset( buffer_next, 0, sizeof( buffer_next ) );
    auto* next_blk        = reinterpret_cast<pmm::Block<A>*>( buffer_next );
    next_blk->prev_offset = 6;  // ← текущий
    next_blk->next_offset = 20; // → следующий следующего

    // Инициализация следующего следующего
    std::memset( buffer_nxt_nxt, 0, sizeof( buffer_nxt_nxt ) );
    auto* nxt_nxt_blk        = reinterpret_cast<pmm::Block<A>*>( buffer_nxt_nxt );
    nxt_nxt_blk->prev_offset = 10; // ← следующий

    // Выполняем слияние
    auto* coalescing = pmm::CoalescingBlock<A>::cast_from_raw( buffer_curr );
    coalescing->coalesce_with_next( buffer_next, buffer_nxt_nxt, 6 );

    // Проверяем результат
    PMM_TEST( coalescing->next_offset() == 20 ); // Текущий → следующий следующего
    PMM_TEST( nxt_nxt_blk->prev_offset == 6 );   // Следующий следующего ← текущий

    // Следующий блок должен быть обнулён
    for ( size_t i = 0; i < sizeof( buffer_next ); ++i )
    {
        PMM_TEST( buffer_next[i] == 0 );
    }

    return true;
}

/// @brief CoalescingBlock::coalesce_with_prev — слияние с левым соседом.
static bool test_p9_coalesce_with_prev()
{
    using A = pmm::DefaultAddressTraits;

    // Три блока: предыдущий (idx=4), текущий (idx=10), следующий (idx=20)
    alignas( 16 ) std::uint8_t buffer_prev[32];
    alignas( 16 ) std::uint8_t buffer_curr[32];
    alignas( 16 ) std::uint8_t buffer_next[32];

    // Инициализация предыдущего блока
    std::memset( buffer_prev, 0, sizeof( buffer_prev ) );
    auto* prev_blk        = reinterpret_cast<pmm::Block<A>*>( buffer_prev );
    prev_blk->next_offset = 10; // → текущий

    // Инициализация текущего блока (будет поглощён)
    std::memset( buffer_curr, 0, sizeof( buffer_curr ) );
    auto* curr_blk        = reinterpret_cast<pmm::Block<A>*>( buffer_curr );
    curr_blk->prev_offset = 4;  // ← предыдущий
    curr_blk->next_offset = 20; // → следующий

    // Инициализация следующего блока
    std::memset( buffer_next, 0, sizeof( buffer_next ) );
    auto* next_blk        = reinterpret_cast<pmm::Block<A>*>( buffer_next );
    next_blk->prev_offset = 10; // ← текущий

    // Выполняем слияние
    auto* coalescing = pmm::CoalescingBlock<A>::cast_from_raw( buffer_curr );
    auto* result     = coalescing->coalesce_with_prev( buffer_prev, buffer_next, 4 );

    // Результат — предыдущий блок
    PMM_TEST( reinterpret_cast<void*>( result ) == reinterpret_cast<void*>( buffer_prev ) );
    PMM_TEST( result->next_offset() == 20 ); // Предыдущий → следующий
    PMM_TEST( next_blk->prev_offset == 4 );  // Следующий ← предыдущий

    // Текущий блок должен быть обнулён
    for ( size_t i = 0; i < sizeof( buffer_curr ); ++i )
    {
        PMM_TEST( buffer_curr[i] == 0 );
    }

    return true;
}

/// @brief CoalescingBlock::finalize_coalesce() → FreeBlock.
static bool test_p9_coalesce_finalize()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    auto* coalescing = pmm::CoalescingBlock<A>::cast_from_raw( buffer );
    auto* fb         = coalescing->finalize_coalesce();

    PMM_TEST( reinterpret_cast<void*>( fb ) == reinterpret_cast<void*>( coalescing ) );
    PMM_TEST( fb->avl_height() == 1 ); // Готов к вставке в AVL
    PMM_TEST( fb->verify_invariants() == true );

    return true;
}

// ─── P9-I: Utility functions ──────────────────────────────────────────────────

/// @brief detect_block_state — определение состояния блока.
static bool test_p9_detect_block_state()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];

    // Свободный блок
    std::memset( buffer, 0, sizeof( buffer ) );
    PMM_TEST( pmm::detect_block_state<A>( buffer, 6 ) == 0 ); // FreeBlock

    // Занятый блок (idx=6)
    auto* blk        = reinterpret_cast<pmm::Block<A>*>( buffer );
    blk->weight      = 5;
    blk->root_offset = 6;
    PMM_TEST( pmm::detect_block_state<A>( buffer, 6 ) == 1 );  // AllocatedBlock
    PMM_TEST( pmm::detect_block_state<A>( buffer, 7 ) == -1 ); // Неопределённое (неверный idx)

    // Некорректное состояние: weight=0, но root_offset != 0
    blk->weight      = 0;
    blk->root_offset = 6;
    PMM_TEST( pmm::detect_block_state<A>( buffer, 6 ) == -1 ); // Неопределённое

    return true;
}

/// @brief recover_block_state — восстановление состояния блока.
static bool test_p9_recover_block_state()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];

    // Случай 1: weight>0, но root_offset неверен
    std::memset( buffer, 0, sizeof( buffer ) );
    auto* blk        = reinterpret_cast<pmm::Block<A>*>( buffer );
    blk->weight      = 5;
    blk->root_offset = 10; // Должен быть 6

    pmm::recover_block_state<A>( buffer, 6 );
    PMM_TEST( blk->root_offset == 6 ); // Исправлено

    // Случай 2: weight==0, но root_offset != 0
    blk->weight      = 0;
    blk->root_offset = 6;

    pmm::recover_block_state<A>( buffer, 6 );
    PMM_TEST( blk->root_offset == 0 ); // Исправлено

    // Случай 3: корректное состояние — без изменений
    blk->weight      = 5;
    blk->root_offset = 6;

    pmm::recover_block_state<A>( buffer, 6 );
    PMM_TEST( blk->weight == 5 );
    PMM_TEST( blk->root_offset == 6 ); // Без изменений

    return true;
}

// ─── P9-J: Full allocate flow simulation ──────────────────────────────────────

/// @brief Симуляция полного цикла allocate без split.
static bool test_p9_allocate_flow_no_split()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // 1. Начинаем с FreeBlock
    auto* fb = pmm::FreeBlock<A>::cast_from_raw( buffer );
    PMM_TEST( fb->verify_invariants() == true );

    // 2. Удаляем из AVL (внешняя операция)
    auto* removed = fb->remove_from_avl();
    PMM_TEST( removed->weight() == 0 );
    PMM_TEST( removed->root_offset() == 0 );

    // 3. Помечаем как занятый
    auto* alloc = removed->mark_as_allocated( 5, 6 );
    PMM_TEST( alloc->verify_invariants( 6 ) == true );
    PMM_TEST( alloc->weight() == 5 );

    return true;
}

/// @brief Симуляция полного цикла deallocate без coalesce.
static bool test_p9_deallocate_flow_no_coalesce()
{
    using A = pmm::DefaultAddressTraits;

    alignas( 16 ) std::uint8_t buffer[32];
    std::memset( buffer, 0, sizeof( buffer ) );

    // 1. Создаём занятый блок
    auto* blk        = reinterpret_cast<pmm::Block<A>*>( buffer );
    blk->weight      = 5;
    blk->root_offset = 6;

    auto* alloc = pmm::AllocatedBlock<A>::cast_from_raw( buffer );
    PMM_TEST( alloc->verify_invariants( 6 ) == true );

    // 2. Освобождаем
    auto* not_avl = alloc->mark_as_free();
    PMM_TEST( not_avl->weight() == 0 );
    PMM_TEST( not_avl->root_offset() == 0 );

    // 3. Добавляем в AVL (внешняя операция)
    auto* fb = not_avl->insert_to_avl();
    PMM_TEST( fb->verify_invariants() == true );
    PMM_TEST( fb->avl_height() == 1 );

    return true;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    bool all_passed = true;

    std::cout << "[Phase 9: BlockState machine (Issue #93)]\n";

    std::cout << "  P9-A: Binary compatibility\n";
    PMM_RUN( "    BlockStateBase size", test_p9_block_state_base_size );
    PMM_RUN( "    All states same size", test_p9_all_states_same_size );

    std::cout << "  P9-B: Read-only accessors\n";
    PMM_RUN( "    Accessors", test_p9_accessors );
    PMM_RUN( "    is_free / is_allocated", test_p9_is_free_is_allocated );

    std::cout << "  P9-C: FreeBlock state\n";
    PMM_RUN( "    cast_from_raw and verify_invariants", test_p9_free_block_cast_and_verify );
    PMM_RUN( "    remove_from_avl", test_p9_free_block_remove_from_avl );

    std::cout << "  P9-D: FreeBlockRemovedAVL state\n";
    PMM_RUN( "    mark_as_allocated", test_p9_removed_avl_mark_allocated );
    PMM_RUN( "    begin_splitting", test_p9_removed_avl_begin_splitting );
    PMM_RUN( "    insert_to_avl (rollback)", test_p9_removed_avl_insert_to_avl );

    std::cout << "  P9-E: SplittingBlock state\n";
    PMM_RUN( "    Full splitting flow", test_p9_splitting_full_flow );

    std::cout << "  P9-F: AllocatedBlock state\n";
    PMM_RUN( "    cast_from_raw and verify_invariants", test_p9_allocated_block_cast_and_verify );
    PMM_RUN( "    mark_as_free", test_p9_allocated_mark_as_free );

    std::cout << "  P9-G: FreeBlockNotInAVL state\n";
    PMM_RUN( "    insert_to_avl", test_p9_not_avl_insert_to_avl );
    PMM_RUN( "    begin_coalescing", test_p9_not_avl_begin_coalescing );

    std::cout << "  P9-H: CoalescingBlock state\n";
    PMM_RUN( "    coalesce_with_next", test_p9_coalesce_with_next );
    PMM_RUN( "    coalesce_with_prev", test_p9_coalesce_with_prev );
    PMM_RUN( "    finalize_coalesce", test_p9_coalesce_finalize );

    std::cout << "  P9-I: Utility functions\n";
    PMM_RUN( "    detect_block_state", test_p9_detect_block_state );
    PMM_RUN( "    recover_block_state", test_p9_recover_block_state );

    std::cout << "  P9-J: Full flow simulation\n";
    PMM_RUN( "    Allocate flow (no split)", test_p9_allocate_flow_no_split );
    PMM_RUN( "    Deallocate flow (no coalesce)", test_p9_deallocate_flow_no_coalesce );

    std::cout << "\n";
    if ( all_passed )
    {
        std::cout << "All Phase 9 tests PASSED.\n";
        return EXIT_SUCCESS;
    }
    else
    {
        std::cout << "Some Phase 9 tests FAILED.\n";
        return EXIT_FAILURE;
    }
}
