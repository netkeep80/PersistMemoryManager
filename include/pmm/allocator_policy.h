/**
 * @file pmm/allocator_policy.h
 * @brief AllocatorPolicy — политика выделения/освобождения памяти (Issue #87 Phase 6).
 *
 * Параметрически объединяет:
 *   - `FreeBlockTreeT` — политику дерева свободных блоков (insert/remove/find_best_fit)
 *   - `AddressTraitsT` — traits адресного пространства
 *
 * Предоставляет все-статические методы:
 *   - `allocate_from_block()` — выделение из найденного свободного блока
 *   - `coalesce()`            — слияние соседних свободных блоков
 *   - `rebuild_free_tree()`   — перестройка дерева свободных блоков (после load())
 *   - `repair_linked_list()`  — восстановление двухсвязного списка (после load())
 *   - `recompute_counters()`  — пересчёт счётчиков (после load())
 *
 * Issue #97: Методы `allocate_from_block()` и `coalesce()` документированы
 * в терминах автомата состояний из `block_state.h`.
 *
 * Issue #106: Полная интеграция с BlockState machine — все изменения состояния
 * блоков выполняются через методы `block_state.h`, прямые присвоения к полям
 * `size` и `root_offset` заменены на типобезопасные переходы состояний.
 * Используется Block<A> layout вместо legacy BlockHeader layout.
 *
 * Issue #114: Устранение нарушений инкапсуляции — AllocatorPolicy больше не
 * обращается напрямую к полям Block<A>. В split-пути allocate_from_block()
 * используются методы SplittingBlock (initialize_new_block, link_new_block,
 * finalize_split). В coalesce() соседние блоки проверяются через BlockStateBase.
 * В recovery-методах (rebuild_free_tree, repair_linked_list, recompute_counters)
 * используются утилиты из block_state.h (reset_block_avl_fields,
 * repair_block_prev_offset, read_block_next_offset, read_block_weight).
 *
 * Граф состояний блока во время allocate_from_block():
 *   FreeBlock → remove_from_avl → FreeBlockRemovedAVL
 *     → [если split] begin_splitting → SplittingBlock → finalize_split → AllocatedBlock
 *     → [без split]  mark_as_allocated → AllocatedBlock
 *
 * Граф состояний блока во время coalesce():
 *   FreeBlockNotInAVL → begin_coalescing → CoalescingBlock
 *     → [правый сосед свободен] coalesce_with_next
 *     → [левый сосед свободен]  coalesce_with_prev → CoalescingBlock(prv)
 *     → finalize_coalesce → FreeBlock (вставка в AVL)
 *
 * @see plan_issue87.md §5 «Фаза 6: AllocatorPolicy»
 * @see block_state.h — автомат состояний блока (Issue #93, #106, #114)
 * @see free_block_tree.h — концепт FreeBlockTree
 * @version 0.4 (Issue #114 — устранение нарушений инкапсуляции Block<A>)
 */

#pragma once

#include "pmm/block_state.h"
#include "pmm/free_block_tree.h"
#include "pmm/types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pmm
{

/**
 * @brief Политика выделения и освобождения памяти для ПАП-менеджера.
 *
 * Все методы статические и принимают `base_ptr` и `header*` как контекст.
 *
 * @tparam FreeBlockTreeT   Политика дерева свободных блоков (AvlFreeTree или другая).
 * @tparam AddressTraitsT   Traits адресного пространства.
 */
template <typename FreeBlockTreeT = AvlFreeTree<DefaultAddressTraits>, typename AddressTraitsT = DefaultAddressTraits>
class AllocatorPolicy
{
    static_assert( is_free_block_tree_policy_v<FreeBlockTreeT>,
                   "AllocatorPolicy: FreeBlockTreeT must satisfy FreeBlockTreePolicy" );

  public:
    using address_traits  = AddressTraitsT;
    using free_block_tree = FreeBlockTreeT;
    using index_type      = typename AddressTraitsT::index_type;
    using BlockT          = Block<AddressTraitsT>;

    AllocatorPolicy()                                    = delete;
    AllocatorPolicy( const AllocatorPolicy& )            = delete;
    AllocatorPolicy& operator=( const AllocatorPolicy& ) = delete;

    // ─── Выделение ─────────────────────────────────────────────────────────────

    /**
     * @brief Выделить память из найденного свободного блока.
     *
     * Убирает блок из дерева свободных блоков, при необходимости разбивает его
     * на два: один — под запрошенные данные, второй — остаток (снова свободный).
     *
     * Граф состояний (Issue #97/#106, см. block_state.h):
     *   FreeBlock → FreeBlockRemovedAVL [remove_from_avl]
     *     → [split] SplittingBlock [begin_splitting]
     *               → initialize_new_block + link_new_block + AVL insert
     *               → AllocatedBlock [finalize_split]
     *     → [no split] AllocatedBlock [mark_as_allocated]
     *
     * @param base      Базовый указатель управляемой области.
     * @param hdr       Заголовок менеджера.
     * @param blk_idx   Гранульный индекс свободного блока, из которого выделяется память.
     * @param user_size Размер пользовательских данных в байтах.
     * @return Указатель на пользовательские данные или nullptr.
     */
    static void* allocate_from_block( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t blk_idx,
                                      std::size_t user_size )
    {
        // State: FreeBlock → FreeBlockRemovedAVL [remove_from_avl]
        FreeBlockTreeT::remove( base, hdr, blk_idx );
        FreeBlock<AddressTraitsT>* fb =
            FreeBlock<AddressTraitsT>::cast_from_raw( detail::block_at<AddressTraitsT>( base, blk_idx ) );
        FreeBlockRemovedAVL<AddressTraitsT>* removed = fb->remove_from_avl();

        std::uint32_t blk_total_gran =
            detail::block_total_granules( base, hdr, detail::block_at<AddressTraitsT>( base, blk_idx ) );
        std::uint32_t data_gran    = detail::bytes_to_granules( user_size );
        std::uint32_t needed_gran  = detail::kBlockHeaderGranules + data_gran;
        std::uint32_t min_rem_gran = detail::kBlockHeaderGranules + 1;
        bool          can_split    = ( blk_total_gran >= needed_gran + min_rem_gran );

        if ( can_split )
        {
            // State: FreeBlockRemovedAVL → SplittingBlock [begin_splitting]
            SplittingBlock<AddressTraitsT>* splitting = removed->begin_splitting();

            std::uint32_t new_idx     = blk_idx + needed_gran;
            void*         new_blk_ptr = detail::block_at<AddressTraitsT>( base, new_idx );

            // Capture old_next before initialize_new_block modifies splitting->next_offset()
            index_type curr_next = splitting->next_offset();
            BlockT*    old_next =
                ( curr_next != detail::kNoBlock ) ? detail::block_at<AddressTraitsT>( base, curr_next ) : nullptr;

            // SplittingBlock::initialize_new_block — инициализировать новый (remainder) блок
            splitting->initialize_new_block( new_blk_ptr, new_idx, blk_idx );

            // SplittingBlock::link_new_block — обновить связный список
            splitting->link_new_block( old_next, new_idx );
            if ( old_next == nullptr )
                hdr->last_block_offset = new_idx;

            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += detail::kBlockHeaderGranules;
            FreeBlockTreeT::insert( base, hdr, new_idx );

            // State: SplittingBlock → AllocatedBlock [finalize_split]
            AllocatedBlock<AddressTraitsT>* alloc = splitting->finalize_split( data_gran, blk_idx );
            (void)alloc; // allocated block pointer obtained via state machine
        }
        else
        {
            // State: FreeBlockRemovedAVL → AllocatedBlock [mark_as_allocated]
            AllocatedBlock<AddressTraitsT>* alloc = removed->mark_as_allocated( data_gran, blk_idx );
            (void)alloc; // allocated block pointer obtained via state machine
        }

        hdr->alloc_count++;
        hdr->free_count--;
        hdr->used_size += data_gran;

        // Return user data pointer (after block header) via canonical detail::user_ptr (Issue #141)
        return detail::user_ptr<AddressTraitsT>( detail::block_at<AddressTraitsT>( base, blk_idx ) );
    }

    // ─── Освобождение и слияние ────────────────────────────────────────────────

    /**
     * @brief Слить освобождённый блок с соседними свободными блоками.
     *
     * Если следующий или предыдущий блок свободен, объединяет их.
     * Добавляет результирующий свободный блок в дерево.
     *
     * Граф состояний (Issue #97/#106, см. block_state.h):
     *   FreeBlockNotInAVL → CoalescingBlock [begin_coalescing]
     *     → [правый сосед free] coalesce_with_next
     *     → [левый сосед free]  coalesce_with_prev → CoalescingBlock(prv) → finalize_coalesce → FreeBlock
     *     → [нет соседей]       finalize_coalesce → FreeBlock [insert в AVL]
     *
     * @param base     Базовый указатель управляемой области.
     * @param hdr      Заголовок менеджера.
     * @param blk_idx  Гранульный индекс только что освобождённого блока (weight == 0).
     */
    static void coalesce( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t blk_idx )
    {
        // State: FreeBlockNotInAVL → CoalescingBlock [begin_coalescing]
        FreeBlockNotInAVL<AddressTraitsT>* not_avl =
            FreeBlockNotInAVL<AddressTraitsT>::cast_from_raw( detail::block_at<AddressTraitsT>( base, blk_idx ) );
        CoalescingBlock<AddressTraitsT>* coalescing = not_avl->begin_coalescing();

        std::uint32_t b_idx = blk_idx;

        // Слияние с правым соседом
        // State: CoalescingBlock::coalesce_with_next
        index_type curr_next = coalescing->next_offset();
        if ( curr_next != detail::kNoBlock )
        {
            const BlockStateBase<AddressTraitsT>* nxt_state = reinterpret_cast<const BlockStateBase<AddressTraitsT>*>(
                detail::block_at<AddressTraitsT>( base, curr_next ) );
            if ( nxt_state->weight() == 0 ) // free block
            {
                std::uint32_t nxt_idx  = curr_next;
                index_type    nxt_next = nxt_state->next_offset();
                BlockT*       nxt_nxt_blk =
                    ( nxt_next != detail::kNoBlock ) ? detail::block_at<AddressTraitsT>( base, nxt_next ) : nullptr;

                FreeBlockTreeT::remove( base, hdr, nxt_idx );

                // CoalescingBlock::coalesce_with_next (also updates nxt_nxt_blk->prev_offset internally)
                coalescing->coalesce_with_next( detail::block_at<AddressTraitsT>( base, nxt_idx ), nxt_nxt_blk, b_idx );

                if ( nxt_nxt_blk == nullptr )
                    hdr->last_block_offset = b_idx;

                hdr->block_count--;
                hdr->free_count--;
                if ( hdr->used_size >= detail::kBlockHeaderGranules )
                    hdr->used_size -= detail::kBlockHeaderGranules;
            }
        }

        // Слияние с левым соседом
        // State: CoalescingBlock::coalesce_with_prev → результат CoalescingBlock(prv)
        index_type curr_prev = coalescing->prev_offset();
        if ( curr_prev != detail::kNoBlock )
        {
            const BlockStateBase<AddressTraitsT>* prv_state = reinterpret_cast<const BlockStateBase<AddressTraitsT>*>(
                detail::block_at<AddressTraitsT>( base, curr_prev ) );
            if ( prv_state->weight() == 0 ) // free block
            {
                std::uint32_t prv_idx  = curr_prev;
                index_type    blk_next = coalescing->next_offset();
                BlockT*       next_blk =
                    ( blk_next != detail::kNoBlock ) ? detail::block_at<AddressTraitsT>( base, blk_next ) : nullptr;

                FreeBlockTreeT::remove( base, hdr, prv_idx );

                // CoalescingBlock::coalesce_with_prev — current block (blk) is absorbed into prv
                CoalescingBlock<AddressTraitsT>* result_coalescing = coalescing->coalesce_with_prev(
                    detail::block_at<AddressTraitsT>( base, prv_idx ), next_blk, prv_idx );

                if ( next_blk == nullptr )
                    hdr->last_block_offset = prv_idx;

                hdr->block_count--;
                hdr->free_count--;
                if ( hdr->used_size >= detail::kBlockHeaderGranules )
                    hdr->used_size -= detail::kBlockHeaderGranules;

                // State: CoalescingBlock::finalize_coalesce → FreeBlock; вставка в AVL
                FreeBlock<AddressTraitsT>* fb = result_coalescing->finalize_coalesce();
                (void)fb;
                FreeBlockTreeT::insert( base, hdr, prv_idx );
                return;
            }
        }

        // State: CoalescingBlock::finalize_coalesce → FreeBlock; вставка в AVL
        FreeBlock<AddressTraitsT>* fb = coalescing->finalize_coalesce();
        (void)fb;
        FreeBlockTreeT::insert( base, hdr, b_idx );
    }

    // ─── Восстановление состояния (после load()) ───────────────────────────────

    /**
     * @brief Перестроить дерево свободных блоков.
     *
     * Сбрасывает все AVL-ссылки в блоках и заново вставляет свободные блоки.
     * Вызывается при `load()` после восстановления менеджера из файла.
     * Также вызывает recover_block_state для каждого блока (Issue #106).
     *
     * @param base  Базовый указатель управляемой области.
     * @param hdr   Заголовок менеджера.
     */
    static void rebuild_free_tree( std::uint8_t* base, detail::ManagerHeader* hdr )
    {
        hdr->free_tree_root    = detail::kNoBlock;
        hdr->last_block_offset = detail::kNoBlock;
        std::uint32_t idx      = hdr->first_block_offset;
        while ( idx != detail::kNoBlock )
        {
            void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );

            // Reset AVL fields via state machine utility (Issue #114)
            reset_block_avl_fields<AddressTraitsT>( blk_ptr );

            // Issue #106: recover_block_state — исправить некорректные переходные состояния
            recover_block_state<AddressTraitsT>( blk_ptr, idx );

            if ( read_block_weight<AddressTraitsT>( blk_ptr ) == 0 ) // free block
                FreeBlockTreeT::insert( base, hdr, idx );
            index_type next_idx = read_block_next_offset<AddressTraitsT>( blk_ptr );
            if ( next_idx == detail::kNoBlock )
                hdr->last_block_offset = idx;
            idx = next_idx;
        }
    }

    /**
     * @brief Восстановить двухсвязный список блоков.
     *
     * Проходит по списку блоков и восстанавливает `prev_offset` у каждого.
     * Вызывается при `load()` (данное поле не персистируется).
     *
     * @param base  Базовый указатель управляемой области.
     * @param hdr   Заголовок менеджера.
     */
    static void repair_linked_list( std::uint8_t* base, detail::ManagerHeader* hdr )
    {
        std::uint32_t idx  = hdr->first_block_offset;
        index_type    prev = detail::kNoBlock;
        while ( idx != detail::kNoBlock )
        {
            if ( detail::idx_to_byte_off( idx ) + sizeof( BlockT ) > hdr->total_size )
                break;
            void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            repair_block_prev_offset<AddressTraitsT>( blk_ptr, prev ); // Issue #114
            prev                   = idx;
            index_type next_offset = read_block_next_offset<AddressTraitsT>( blk_ptr );
            idx                    = next_offset;
        }
    }

    /**
     * @brief Пересчитать счётчики блоков и использованного размера.
     *
     * Проходит по всем блокам и обновляет `block_count`, `free_count`,
     * `alloc_count`, `used_size` в заголовке менеджера.
     * Использует Block<A> layout: `weight` вместо `size` (Issue #106).
     *
     * @param base  Базовый указатель управляемой области.
     * @param hdr   Заголовок менеджера.
     */
    static void recompute_counters( std::uint8_t* base, detail::ManagerHeader* hdr )
    {
        std::uint32_t block_count = 0, free_count = 0, alloc_count = 0;
        std::uint32_t used_gran = 0;
        std::uint32_t idx       = hdr->first_block_offset;
        while ( idx != detail::kNoBlock )
        {
            if ( detail::idx_to_byte_off( idx ) + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            block_count++;
            used_gran += detail::kBlockHeaderGranules;
            index_type w = read_block_weight<AddressTraitsT>( blk_ptr ); // Issue #114
            if ( w > 0 )                                                 // allocated block
            {
                alloc_count++;
                used_gran += w;
            }
            else
            {
                free_count++;
            }
            idx = read_block_next_offset<AddressTraitsT>( blk_ptr ); // Issue #114
        }
        hdr->block_count = block_count;
        hdr->free_count  = free_count;
        hdr->alloc_count = alloc_count;
        hdr->used_size   = used_gran;
    }
};

/// @brief Псевдоним AllocatorPolicy с настройками по умолчанию.
using DefaultAllocatorPolicy = AllocatorPolicy<AvlFreeTree<DefaultAddressTraits>, DefaultAddressTraits>;

} // namespace pmm
