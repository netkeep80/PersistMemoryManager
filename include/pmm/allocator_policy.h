/**
 * @file pmm/allocator_policy.h
 * @brief AllocatorPolicy — политика выделения/освобождения памяти.
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
 * в терминах автомата состояний из `block_state.h`.
 *
 * блоков выполняются через методы `block_state.h`, прямые присвоения к полям
 * `size` и `root_offset` заменены на типобезопасные переходы состояний.
 * Используется Block<A> layout вместо legacy BlockHeader layout.
 *
 * обращается напрямую к полям Block<A>. В split-пути allocate_from_block()
 * используются методы SplittingBlock (initialize_new_block, link_new_block,
 * finalize_split). В coalesce() соседние блоки проверяются через BlockStateBase.
 * В repair-методах load() (rebuild_free_tree, repair_linked_list, recompute_counters)
 * напрямую используются статические методы BlockStateBase<AT>:
 *   - reset_avl_fields_of()  — вместо удалённой reset_block_avl_fields()
 *   - repair_prev_offset()   — вместо удалённой repair_block_prev_offset()
 *   - get_next_offset()      — вместо удалённой read_block_next_offset()
 *   - get_weight()           — вместо удалённой read_block_weight()
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
 * @see block_state.h — автомат состояний блока
 * @see free_block_tree.h — концепт FreeBlockTree
 * @version 0.6
 */

#pragma once

#include "pmm/block_state.h"
#include "pmm/diagnostics.h"
#include "pmm/free_block_tree.h"
#include "pmm/types.h"
#include "pmm/validation.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

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
    // Check against the specific AddressTraitsT, not hardcoded DefaultAddressTraits.
    static_assert( FreeBlockTreePolicyForTraitsConcept<FreeBlockTreeT, AddressTraitsT>,
                   "AllocatorPolicy: FreeBlockTreeT must satisfy FreeBlockTreePolicy for AddressTraitsT" );

  public:
    using address_traits  = AddressTraitsT;
    using free_block_tree = FreeBlockTreeT;
    using index_type      = typename AddressTraitsT::index_type;
    using BlockT          = Block<AddressTraitsT>;
    using BlockState      = BlockStateBase<AddressTraitsT>;

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
     * Граф состояний:
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
    static void* allocate_from_block( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr,
                                      index_type blk_idx, std::size_t user_size )
    {
        // State: FreeBlock → FreeBlockRemovedAVL [remove_from_avl]
        FreeBlockTreeT::remove( base, hdr, blk_idx );
        FreeBlock<AddressTraitsT>* fb =
            FreeBlock<AddressTraitsT>::cast_from_raw( detail::block_at<AddressTraitsT>( base, blk_idx ) );
        FreeBlockRemovedAVL<AddressTraitsT>* removed = fb->remove_from_avl();

        // Use AddressTraitsT-specific granule computations.
        static constexpr index_type kBlkHdrGran =
            detail::kBlockHeaderGranules_t<AddressTraitsT>; ///< Block header granules for this AT.

        index_type blk_total_gran =
            detail::block_total_granules( base, hdr, detail::block_at<AddressTraitsT>( base, blk_idx ) );
        index_type data_gran = detail::bytes_to_granules_t<AddressTraitsT>( user_size );

        // Overflow protection for needed_gran computation.
        if ( data_gran > std::numeric_limits<index_type>::max() - kBlkHdrGran )
            return nullptr; // overflow: request too large

        index_type needed_gran  = kBlkHdrGran + data_gran;
        index_type min_rem_gran = kBlkHdrGran + 1;

        // Overflow protection for split check (needed_gran + min_rem_gran).
        bool can_split = false;
        if ( needed_gran <= std::numeric_limits<index_type>::max() - min_rem_gran )
            can_split = ( blk_total_gran >= needed_gran + min_rem_gran );

        if ( can_split )
        {
            // State: FreeBlockRemovedAVL → SplittingBlock [begin_splitting]
            SplittingBlock<AddressTraitsT>* splitting = removed->begin_splitting();

            index_type new_idx     = blk_idx + needed_gran;
            void*      new_blk_ptr = detail::block_at<AddressTraitsT>( base, new_idx );

            // Capture old_next before initialize_new_block modifies splitting->next_offset()
            // Compare against AddressTraitsT::no_block (correct sentinel for index_type).
            index_type curr_next = splitting->next_offset();
            BlockT*    old_next  = ( curr_next != AddressTraitsT::no_block )
                                       ? detail::block_at<AddressTraitsT>( base, curr_next )
                                       : nullptr;

            // SplittingBlock::initialize_new_block — инициализировать новый (remainder) блок
            splitting->initialize_new_block( new_blk_ptr, new_idx, blk_idx );

            // SplittingBlock::link_new_block — обновить связный список
            splitting->link_new_block( old_next, new_idx );
            if ( old_next == nullptr )
                hdr->last_block_offset = new_idx;

            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += kBlkHdrGran;
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

        // Return user data pointer (after block header) via canonical detail::user_ptr
        return detail::user_ptr<AddressTraitsT>( detail::block_at<AddressTraitsT>( base, blk_idx ) );
    }

    // ─── Освобождение и слияние ────────────────────────────────────────────────

    /**
     * @brief Слить освобождённый блок с соседними свободными блоками.
     *
     * Если следующий или предыдущий блок свободен, объединяет их.
     * Добавляет результирующий свободный блок в дерево.
     *
     * Граф состояний:
     *   FreeBlockNotInAVL → CoalescingBlock [begin_coalescing]
     *     → [правый сосед free] coalesce_with_next
     *     → [левый сосед free]  coalesce_with_prev → CoalescingBlock(prv) → finalize_coalesce → FreeBlock
     *     → [нет соседей]       finalize_coalesce → FreeBlock [insert в AVL]
     *
     * @param base     Базовый указатель управляемой области.
     * @param hdr      Заголовок менеджера.
     * @param blk_idx  Гранульный индекс только что освобождённого блока (weight == 0).
     */
    static void coalesce( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type blk_idx )
    {
        // State: FreeBlockNotInAVL → CoalescingBlock [begin_coalescing]
        FreeBlockNotInAVL<AddressTraitsT>* not_avl =
            FreeBlockNotInAVL<AddressTraitsT>::cast_from_raw( detail::block_at<AddressTraitsT>( base, blk_idx ) );
        CoalescingBlock<AddressTraitsT>* coalescing = not_avl->begin_coalescing();

        // Use AddressTraitsT-specific block header granule count.
        static constexpr index_type kBlkHdrGran = detail::kBlockHeaderGranules_t<AddressTraitsT>;

        index_type b_idx = blk_idx;

        // Слияние с правым соседом
        // State: CoalescingBlock::coalesce_with_next
        // Compare against AddressTraitsT::no_block for correct sentinel check.
        index_type curr_next = coalescing->next_offset();
        if ( curr_next != AddressTraitsT::no_block )
        {
            void* nxt_raw = detail::block_at<AddressTraitsT>( base, curr_next );
            if ( BlockState::get_weight( nxt_raw ) == 0 ) // free block
            {
                index_type nxt_idx     = curr_next;
                index_type nxt_next    = BlockState::get_next_offset( nxt_raw );
                BlockT*    nxt_nxt_blk = ( nxt_next != AddressTraitsT::no_block )
                                             ? detail::block_at<AddressTraitsT>( base, nxt_next )
                                             : nullptr;

                FreeBlockTreeT::remove( base, hdr, nxt_idx );

                // CoalescingBlock::coalesce_with_next (also updates nxt_nxt_blk->prev_offset internally)
                coalescing->coalesce_with_next( detail::block_at<AddressTraitsT>( base, nxt_idx ), nxt_nxt_blk, b_idx );

                if ( nxt_nxt_blk == nullptr )
                    hdr->last_block_offset = b_idx;

                hdr->block_count--;
                hdr->free_count--;
                if ( hdr->used_size >= kBlkHdrGran )
                    hdr->used_size -= kBlkHdrGran;
            }
        }

        // Слияние с левым соседом
        // State: CoalescingBlock::coalesce_with_prev → результат CoalescingBlock(prv)
        index_type curr_prev = coalescing->prev_offset();
        if ( curr_prev != AddressTraitsT::no_block )
        {
            void* prv_raw = detail::block_at<AddressTraitsT>( base, curr_prev );
            if ( BlockState::get_weight( prv_raw ) == 0 ) // free block
            {
                index_type prv_idx  = curr_prev;
                index_type blk_next = coalescing->next_offset();
                BlockT*    next_blk = ( blk_next != AddressTraitsT::no_block )
                                          ? detail::block_at<AddressTraitsT>( base, blk_next )
                                          : nullptr;

                FreeBlockTreeT::remove( base, hdr, prv_idx );

                // CoalescingBlock::coalesce_with_prev — current block (blk) is absorbed into prv
                CoalescingBlock<AddressTraitsT>* result_coalescing =
                    coalescing->coalesce_with_prev( prv_raw, next_blk, prv_idx );

                if ( next_blk == nullptr )
                    hdr->last_block_offset = prv_idx;

                hdr->block_count--;
                hdr->free_count--;
                if ( hdr->used_size >= kBlkHdrGran )
                    hdr->used_size -= kBlkHdrGran;

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

    // ─── Repair phase of load() — structural reconstruction ────────────────────

    /**
     * @brief Перестроить дерево свободных блоков.
     *
     * Сбрасывает все AVL-ссылки в блоках и заново вставляет свободные блоки.
     * Вызывается при `load()` после восстановления менеджера из файла.
     * Также вызывает BlockState::recover_state для каждого блока.
     *
     * @param base  Базовый указатель управляемой области.
     * @param hdr   Заголовок менеджера.
     */
    static void rebuild_free_tree( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr )
    {
        hdr->free_tree_root    = AddressTraitsT::no_block;
        hdr->last_block_offset = AddressTraitsT::no_block;
        index_type idx         = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );

            // Repair: fix incorrect transitional states (weight/root_offset mismatch)
            BlockState::recover_state( blk_ptr, idx );

            if ( BlockState::get_weight( blk_ptr ) == 0 ) // free block
            {
                // Reset AVL fields only for free blocks — allocated blocks may use
                // TreeNode AVL fields for user-level trees (symbol tree, pmap, etc.)
                // so resetting them would corrupt those data structures.
                BlockState::reset_avl_fields_of( blk_ptr );
                FreeBlockTreeT::insert( base, hdr, idx );
            }
            // Use AddressTraitsT::no_block for correct sentinel check.
            index_type next_idx = BlockState::get_next_offset( blk_ptr );
            if ( next_idx == AddressTraitsT::no_block )
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
    static void repair_linked_list( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr )
    {
        index_type idx  = hdr->first_block_offset;
        index_type prev = AddressTraitsT::no_block;
        while ( idx != AddressTraitsT::no_block )
        {
            // Use AddressTraitsT::granule_size for correct size check.
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            BlockState::repair_prev_offset( blk_ptr, prev );
            prev                   = idx;
            index_type next_offset = BlockState::get_next_offset( blk_ptr );
            idx                    = next_offset;
        }
    }

    /**
     * @brief Пересчитать счётчики блоков и использованного размера.
     *
     * Проходит по всем блокам и обновляет `block_count`, `free_count`,
     * `alloc_count`, `used_size` в заголовке менеджера.
     * Использует Block<A> layout: `weight` вместо `size`.
     *
     * @param base  Базовый указатель управляемой области.
     * @param hdr   Заголовок менеджера.
     */
    static void recompute_counters( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr )
    {
        // Use AddressTraitsT-specific block header granule count.
        static constexpr index_type kBlkHdrGran = detail::kBlockHeaderGranules_t<AddressTraitsT>;

        index_type block_count = 0, free_count = 0, alloc_count = 0;
        index_type used_gran = 0;
        index_type idx       = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            // Use AddressTraitsT::granule_size for correct size check.
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            block_count++;
            used_gran += kBlkHdrGran;
            index_type w = BlockState::get_weight( blk_ptr );
            if ( w > 0 ) // allocated block
            {
                alloc_count++;
                used_gran += w;
            }
            else
            {
                free_count++;
            }
            idx = BlockState::get_next_offset( blk_ptr );
        }
        hdr->block_count = block_count;
        hdr->free_count  = free_count;
        hdr->alloc_count = alloc_count;
        hdr->used_size   = used_gran;
    }

    // ─── Verify-only diagnostics ──────────────────────────────────

    /**
     * @brief Verify linked list prev_offset consistency without modifying the image.
     *
     * Walks the block chain via next_offset and checks that each block's prev_offset
     * matches the index of the preceding block.
     *
     * @param base    Base pointer of the managed area.
     * @param hdr     Manager header (read-only).
     * @param result  Diagnostic result to append violations to.
     */
    static void verify_linked_list( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                    VerifyResult& result ) noexcept
    {
        index_type idx  = hdr->first_block_offset;
        index_type prev = AddressTraitsT::no_block;
        while ( idx != AddressTraitsT::no_block )
        {
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr     = detail::block_at<AddressTraitsT>( base, idx );
            index_type  stored_prev = BlockState::get_prev_offset( blk_ptr );
            if ( stored_prev != prev )
            {
                result.add( ViolationType::PrevOffsetMismatch, DiagnosticAction::NoAction,
                            static_cast<std::uint64_t>( idx ), static_cast<std::uint64_t>( prev ),
                            static_cast<std::uint64_t>( stored_prev ) );
            }
            prev                   = idx;
            index_type next_offset = BlockState::get_next_offset( blk_ptr );
            idx                    = next_offset;
        }
    }

    /**
     * @brief Verify block counters consistency without modifying the image.
     *
     * Recomputes block_count, free_count, alloc_count, used_size by walking the
     * linked list and compares against stored header values.
     *
     * @param base    Base pointer of the managed area.
     * @param hdr     Manager header (read-only).
     * @param result  Diagnostic result to append violations to.
     */
    static void verify_counters( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                 VerifyResult& result ) noexcept
    {
        static constexpr index_type kBlkHdrGran = detail::kBlockHeaderGranules_t<AddressTraitsT>;

        index_type block_count = 0, free_count = 0, alloc_count = 0;
        index_type used_gran = 0;
        index_type idx       = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            block_count++;
            used_gran += kBlkHdrGran;
            index_type w = BlockState::get_weight( blk_ptr );
            if ( w > 0 )
            {
                alloc_count++;
                used_gran += w;
            }
            else
            {
                free_count++;
            }
            idx = BlockState::get_next_offset( blk_ptr );
        }
        if ( hdr->block_count != block_count || hdr->free_count != free_count || hdr->alloc_count != alloc_count ||
             hdr->used_size != used_gran )
        {
            result.add( ViolationType::CounterMismatch, DiagnosticAction::NoAction, 0,
                        static_cast<std::uint64_t>( block_count ), static_cast<std::uint64_t>( hdr->block_count ) );
        }
    }

    /**
     * @brief Verify block state consistency for all blocks without modification.
     *
     * Walks the linked list and calls verify_state() for each block, detecting
     * any transitional (inconsistent) states.
     *
     * @param base    Base pointer of the managed area.
     * @param hdr     Manager header (read-only).
     * @param result  Diagnostic result to append violations to.
     */
    static void verify_block_states( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                     VerifyResult& result ) noexcept
    {
        index_type idx = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            BlockState::verify_state( blk_ptr, idx, result );
            idx = BlockState::get_next_offset( blk_ptr );
        }
    }

    /**
     * @brief Verify free tree structure without modifying the image.
     *
     * Checks root validity, AVL parent/child links, strict ordering, height/balance,
     * duplicate visits, and membership equality with the linked-list free blocks.
     */
    static void verify_free_tree( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                  VerifyResult& result ) noexcept
    {
        std::size_t expected_count = 0;
        index_type  idx            = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            if ( BlockState::get_weight( blk_ptr ) == 0 )
                ++expected_count;
            idx = BlockState::get_next_offset( blk_ptr );
        }

        const bool root_present = ( hdr->free_tree_root != AddressTraitsT::no_block );
        if ( expected_count == 0 )
        {
            if ( root_present )
            {
                result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction, 0, 0,
                            static_cast<std::uint64_t>( hdr->free_tree_root ) );
            }
            return;
        }
        if ( !root_present )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction, 0,
                        static_cast<std::uint64_t>( expected_count ),
                        static_cast<std::uint64_t>( hdr->free_tree_root ) );
            return;
        }
        if ( !detail::validate_block_index<AddressTraitsT>( hdr->total_size, hdr->free_tree_root ) )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( hdr->free_tree_root ), 1, 0 );
            return;
        }
        const void* root = detail::block_at<AddressTraitsT>( base, hdr->free_tree_root );
        if ( BlockState::get_weight( root ) != 0 || BlockState::get_parent_offset( root ) != AddressTraitsT::no_block )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( hdr->free_tree_root ), 0,
                        static_cast<std::uint64_t>( BlockState::get_parent_offset( root ) ) );
        }

        std::size_t visited_count = 0;
        verify_free_tree_node( base, hdr, hdr->free_tree_root, AddressTraitsT::no_block, {}, false, {}, false,
                               expected_count, visited_count, result );

        idx = hdr->first_block_offset;
        while ( idx != AddressTraitsT::no_block )
        {
            if ( static_cast<std::size_t>( idx ) * AddressTraitsT::granule_size + sizeof( BlockT ) > hdr->total_size )
                break;
            const void* blk_ptr = detail::block_at<AddressTraitsT>( base, idx );
            if ( BlockState::get_weight( blk_ptr ) == 0 &&
                 !free_tree_contains( base, hdr, hdr->free_tree_root, idx, expected_count ) )
            {
                result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction, static_cast<std::uint64_t>( idx ),
                            1, 0 );
            }
            idx = BlockState::get_next_offset( blk_ptr );
        }
        if ( visited_count != expected_count )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction, 0,
                        static_cast<std::uint64_t>( expected_count ), static_cast<std::uint64_t>( visited_count ) );
        }
    }

    static index_type free_tree_block_granules( const std::uint8_t*                          base,
                                                const detail::ManagerHeader<AddressTraitsT>* hdr,
                                                index_type                                   block_idx ) noexcept
    {
        const void* n      = detail::block_at<AddressTraitsT>( base, block_idx );
        index_type  n_next = BlockState::get_next_offset( n );
        index_type  total  = detail::byte_off_to_idx_t<AddressTraitsT>( hdr->total_size );
        return ( n_next != AddressTraitsT::no_block ) ? static_cast<index_type>( n_next - block_idx )
                                                      : static_cast<index_type>( total - block_idx );
    }

    static bool free_tree_less_key( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                    index_type a, index_type b ) noexcept
    {
        index_type a_gran = free_tree_block_granules( base, hdr, a );
        index_type b_gran = free_tree_block_granules( base, hdr, b );
        return ( a_gran < b_gran ) || ( a_gran == b_gran && a < b );
    }

    static bool free_tree_contains( const std::uint8_t* base, const detail::ManagerHeader<AddressTraitsT>* hdr,
                                    index_type node_idx, index_type target, std::size_t step_limit ) noexcept
    {
        while ( node_idx != AddressTraitsT::no_block && step_limit-- > 0 )
        {
            if ( !detail::validate_block_index<AddressTraitsT>( hdr->total_size, node_idx ) )
                return false;
            const void* node = detail::block_at<AddressTraitsT>( base, node_idx );
            if ( BlockState::get_weight( node ) != 0 )
                return false;
            if ( node_idx == target )
                return true;
            node_idx = free_tree_less_key( base, hdr, target, node_idx ) ? BlockState::get_left_offset( node )
                                                                         : BlockState::get_right_offset( node );
        }
        return false;
    }

    static std::int16_t verify_free_tree_node( const std::uint8_t*                          base,
                                               const detail::ManagerHeader<AddressTraitsT>* hdr, index_type node_idx,
                                               index_type parent, index_type lower, bool has_lower, index_type upper,
                                               bool has_upper, std::size_t expected_count, std::size_t& visited_count,
                                               VerifyResult& result ) noexcept
    {
        if ( node_idx == AddressTraitsT::no_block )
            return 0;
        if ( visited_count >= expected_count )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), 1, 2 );
            return 0;
        }
        ++visited_count;

        if ( !detail::validate_block_index<AddressTraitsT>( hdr->total_size, node_idx ) )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction, static_cast<std::uint64_t>( parent ),
                        0, static_cast<std::uint64_t>( node_idx ) );
            return 0;
        }

        const void* node = detail::block_at<AddressTraitsT>( base, node_idx );
        if ( BlockState::get_weight( node ) != 0 )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), 0,
                        static_cast<std::uint64_t>( BlockState::get_weight( node ) ) );
            return 0;
        }
        if ( BlockState::get_parent_offset( node ) != parent )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), static_cast<std::uint64_t>( parent ),
                        static_cast<std::uint64_t>( BlockState::get_parent_offset( node ) ) );
        }
        if ( has_lower && !free_tree_less_key( base, hdr, lower, node_idx ) )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), static_cast<std::uint64_t>( lower ),
                        static_cast<std::uint64_t>( node_idx ) );
        }
        if ( has_upper && !free_tree_less_key( base, hdr, node_idx, upper ) )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), static_cast<std::uint64_t>( node_idx ),
                        static_cast<std::uint64_t>( upper ) );
        }

        index_type left  = BlockState::get_left_offset( node );
        index_type right = BlockState::get_right_offset( node );

        std::int16_t left_h     = verify_free_tree_node( base, hdr, left, node_idx, lower, has_lower, node_idx, true,
                                                         expected_count, visited_count, result );
        std::int16_t right_h    = verify_free_tree_node( base, hdr, right, node_idx, node_idx, true, upper, has_upper,
                                                         expected_count, visited_count, result );
        std::int16_t expected_h = static_cast<std::int16_t>( 1 + ( left_h > right_h ? left_h : right_h ) );
        std::int16_t stored_h   = BlockState::get_avl_height( node );
        if ( stored_h != expected_h || left_h - right_h > 1 || right_h - left_h > 1 )
        {
            result.add( ViolationType::FreeTreeStale, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( node_idx ), static_cast<std::uint64_t>( expected_h ),
                        static_cast<std::uint64_t>( stored_h ) );
        }
        return expected_h;
    }

    // ─── reallocate_typed helpers ─────────────────────────────────

    /// @brief In-place shrink: update weight, split remainder into free block + coalesce.
    static void realloc_shrink( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type blk_idx,
                                void* blk_raw, index_type old_data_gran, index_type new_data_gran ) noexcept
    {
        static constexpr index_type kBlkHdrGran = detail::kBlockHeaderGranules_t<AddressTraitsT>;
        index_type                  remainder   = old_data_gran - new_data_gran;
        if ( remainder >= kBlkHdrGran + 1 )
        {
            index_type new_free_idx = blk_idx + kBlkHdrGran + new_data_gran;
            void*      new_free_blk = detail::block_at<AddressTraitsT>( base, new_free_idx );
            index_type old_next     = BlockState::get_next_offset( blk_raw );
            auto*      old_next_blk =
                ( old_next != AddressTraitsT::no_block ) ? detail::block_at<AddressTraitsT>( base, old_next ) : nullptr;
            std::memset( new_free_blk, 0, sizeof( BlockT ) );
            BlockState::init_fields( new_free_blk, blk_idx, old_next, 1, 0, 0 );
            BlockState::set_next_offset_of( blk_raw, new_free_idx );
            if ( old_next_blk != nullptr )
                BlockState::set_prev_offset_of( old_next_blk, new_free_idx );
            else
                hdr->last_block_offset = new_free_idx;
            BlockState::set_weight_of( blk_raw, new_data_gran );
            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += kBlkHdrGran;
            hdr->used_size -= ( old_data_gran - new_data_gran );
            coalesce( base, hdr, new_free_idx );
        }
        else
        {
            BlockState::set_weight_of( blk_raw, new_data_gran );
            hdr->used_size -= ( old_data_gran - new_data_gran );
        }
    }

    /// @brief Try in-place grow by absorbing adjacent free block. Returns true on success.
    static bool realloc_grow( std::uint8_t* base, detail::ManagerHeader<AddressTraitsT>* hdr, index_type blk_idx,
                              void* blk_raw, index_type old_data_gran, index_type new_data_gran ) noexcept
    {
        static constexpr index_type kBlkHdrGran = detail::kBlockHeaderGranules_t<AddressTraitsT>;
        index_type                  next_idx    = BlockState::get_next_offset( blk_raw );
        if ( next_idx == AddressTraitsT::no_block )
            return false;
        void* next_blk = detail::block_at<AddressTraitsT>( base, next_idx );
        if ( BlockState::get_weight( next_blk ) != 0 )
            return false;
        index_type next_total =
            detail::block_total_granules( base, hdr, detail::block_at<AddressTraitsT>( base, next_idx ) );
        index_type available = old_data_gran + next_total;
        if ( available < new_data_gran )
            return false;
        FreeBlockTreeT::remove( base, hdr, next_idx );
        index_type next_next = BlockState::get_next_offset( next_blk );
        BlockState::set_next_offset_of( blk_raw, next_next );
        if ( next_next != AddressTraitsT::no_block )
            BlockState::set_prev_offset_of( detail::block_at<AddressTraitsT>( base, next_next ), blk_idx );
        else
            hdr->last_block_offset = blk_idx;
        std::memset( next_blk, 0, sizeof( BlockT ) );
        hdr->block_count--;
        hdr->free_count--;
        if ( hdr->used_size >= kBlkHdrGran )
            hdr->used_size -= kBlkHdrGran;
        index_type rem = available - new_data_gran;
        if ( rem >= kBlkHdrGran + 1 )
        {
            index_type rem_idx      = blk_idx + kBlkHdrGran + new_data_gran;
            void*      rem_blk      = detail::block_at<AddressTraitsT>( base, rem_idx );
            index_type blk_new_next = BlockState::get_next_offset( blk_raw );
            std::memset( rem_blk, 0, sizeof( BlockT ) );
            BlockState::init_fields( rem_blk, blk_idx, blk_new_next, 1, 0, 0 );
            BlockState::set_next_offset_of( blk_raw, rem_idx );
            if ( blk_new_next != AddressTraitsT::no_block )
                BlockState::set_prev_offset_of( detail::block_at<AddressTraitsT>( base, blk_new_next ), rem_idx );
            else
                hdr->last_block_offset = rem_idx;
            hdr->block_count++;
            hdr->free_count++;
            hdr->used_size += kBlkHdrGran;
            FreeBlockTreeT::insert( base, hdr, rem_idx );
        }
        BlockState::set_weight_of( blk_raw, new_data_gran );
        hdr->used_size += ( new_data_gran - old_data_gran );
        return true;
    }
};

/// @brief Псевдоним AllocatorPolicy с настройками по умолчанию.
using DefaultAllocatorPolicy = AllocatorPolicy<AvlFreeTree<DefaultAddressTraits>, DefaultAddressTraits>;

} // namespace pmm
