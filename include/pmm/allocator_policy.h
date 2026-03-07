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
 * Переработка кода из `PersistMemoryManager` (Issue #73) в отдельный
 * параметризованный компонент.
 *
 * Issue #97: Методы `allocate_from_block()` и `coalesce()` документированы
 * в терминах автомата состояний из `block_state.h`. Полная интеграция с
 * Block<A> (замена BlockHeader) запланирована как отдельный этап миграции.
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
 * @see block_state.h — автомат состояний блока (Issue #93)
 * @see free_block_tree.h — концепт FreeBlockTree
 * @version 0.2 (Issue #97 — state machine documentation)
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
     * Граф состояний (Issue #97, см. block_state.h):
     *   FreeBlock → FreeBlockRemovedAVL [remove_from_avl]
     *     → [split] SplittingBlock [begin_splitting]
     *               → initialize_new_block + link_new_block + AVL insert
     *               → AllocatedBlock [finalize_split]
     *     → [no split] AllocatedBlock [mark_as_allocated]
     *
     * @param base      Базовый указатель управляемой области.
     * @param hdr       Заголовок менеджера.
     * @param blk       Свободный блок, из которого выделяется память.
     * @param user_size Размер пользовательских данных в байтах.
     * @return Указатель на пользовательские данные или nullptr.
     */
    static void* allocate_from_block( std::uint8_t* base, detail::ManagerHeader* hdr, detail::BlockHeader* blk,
                                      std::size_t user_size )
    {
        std::uint32_t blk_idx = detail::block_idx( base, blk );
        // State: FreeBlock → FreeBlockRemovedAVL
        FreeBlockTreeT::remove( base, hdr, blk_idx );

        std::uint32_t blk_total_gran = detail::block_total_granules( base, hdr, blk );
        std::uint32_t data_gran      = detail::bytes_to_granules( user_size );
        std::uint32_t needed_gran    = detail::kBlockHeaderGranules + data_gran;
        std::uint32_t min_rem_gran   = detail::kBlockHeaderGranules + 1;
        bool          can_split      = ( blk_total_gran >= needed_gran + min_rem_gran );

        if ( can_split )
        {
            // State: FreeBlockRemovedAVL → SplittingBlock [begin_splitting]
            std::uint32_t        new_idx = blk_idx + needed_gran;
            detail::BlockHeader* new_blk = detail::block_at( base, new_idx );
            // SplittingBlock::initialize_new_block
            std::memset( new_blk, 0, sizeof( detail::BlockHeader ) );
            new_blk->prev_offset   = blk_idx;
            new_blk->next_offset   = blk->next_offset;
            new_blk->left_offset   = detail::kNoBlock;
            new_blk->right_offset  = detail::kNoBlock;
            new_blk->parent_offset = detail::kNoBlock;
            new_blk->avl_height    = 1;
            // SplittingBlock::link_new_block
            if ( blk->next_offset != detail::kNoBlock )
                detail::block_at( base, blk->next_offset )->prev_offset = new_idx;
            else
                hdr->last_block_offset = new_idx;
            blk->next_offset = new_idx;
            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += detail::kBlockHeaderGranules;
            FreeBlockTreeT::insert( base, hdr, new_idx );
        }

        // State: FreeBlockRemovedAVL/SplittingBlock → AllocatedBlock
        // [mark_as_allocated / finalize_split: set size=data_gran, root_offset=blk_idx]
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

    // ─── Освобождение и слияние ────────────────────────────────────────────────

    /**
     * @brief Слить освобождённый блок с соседними свободными блоками.
     *
     * Если следующий или предыдущий блок свободен, объединяет их.
     * Добавляет результирующий свободный блок в дерево.
     *
     * Граф состояний (Issue #97, см. block_state.h):
     *   FreeBlockNotInAVL → CoalescingBlock [begin_coalescing]
     *     → [правый сосед free] coalesce_with_next
     *     → [левый сосед free]  coalesce_with_prev → CoalescingBlock(prv) → finalize_coalesce → FreeBlock
     *     → [нет соседей]       finalize_coalesce → FreeBlock [insert в AVL]
     *
     * @param base  Базовый указатель управляемой области.
     * @param hdr   Заголовок менеджера.
     * @param blk   Только что освобождённый блок (size == 0).
     */
    static void coalesce( std::uint8_t* base, detail::ManagerHeader* hdr, detail::BlockHeader* blk )
    {
        // State: FreeBlockNotInAVL → CoalescingBlock [begin_coalescing]
        std::uint32_t b_idx = detail::block_idx( base, blk );

        // Слияние с правым соседом
        // State: CoalescingBlock::coalesce_with_next
        if ( blk->next_offset != detail::kNoBlock )
        {
            detail::BlockHeader* nxt = detail::block_at( base, blk->next_offset );
            if ( nxt->size == 0 )
            {
                std::uint32_t nxt_idx = blk->next_offset;
                FreeBlockTreeT::remove( base, hdr, nxt_idx );
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

        // Слияние с левым соседом
        // State: CoalescingBlock::coalesce_with_prev → результат CoalescingBlock(prv)
        if ( blk->prev_offset != detail::kNoBlock )
        {
            detail::BlockHeader* prv = detail::block_at( base, blk->prev_offset );
            if ( prv->size == 0 )
            {
                std::uint32_t prv_idx = blk->prev_offset;
                FreeBlockTreeT::remove( base, hdr, prv_idx );
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
                // State: CoalescingBlock::finalize_coalesce → FreeBlock; вставка в AVL
                FreeBlockTreeT::insert( base, hdr, prv_idx );
                return;
            }
        }

        // State: CoalescingBlock::finalize_coalesce → FreeBlock; вставка в AVL
        FreeBlockTreeT::insert( base, hdr, b_idx );
    }

    // ─── Восстановление состояния (после load()) ───────────────────────────────

    /**
     * @brief Перестроить дерево свободных блоков.
     *
     * Сбрасывает все AVL-ссылки в блоках и заново вставляет свободные блоки.
     * Вызывается при `load()` после восстановления менеджера из файла.
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
            detail::BlockHeader* blk = detail::block_at( base, idx );
            blk->left_offset         = detail::kNoBlock;
            blk->right_offset        = detail::kNoBlock;
            blk->parent_offset       = detail::kNoBlock;
            blk->avl_height          = 0;
            if ( blk->size == 0 )
                FreeBlockTreeT::insert( base, hdr, idx );
            if ( blk->next_offset == detail::kNoBlock )
                hdr->last_block_offset = idx;
            idx = blk->next_offset;
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
        std::uint32_t prev = detail::kNoBlock;
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

    /**
     * @brief Пересчитать счётчики блоков и использованного размера.
     *
     * Проходит по всем блокам и обновляет `block_count`, `free_count`,
     * `alloc_count`, `used_size` в заголовке менеджера.
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
};

/// @brief Псевдоним AllocatorPolicy с настройками по умолчанию.
using DefaultAllocatorPolicy = AllocatorPolicy<AvlFreeTree<DefaultAddressTraits>, DefaultAddressTraits>;

} // namespace pmm
