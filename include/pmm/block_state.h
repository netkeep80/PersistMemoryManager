/**
 * @file pmm/block_state.h
 * @brief BlockState machine — автомат состояний блока для атомарных операций (Issue #93).
 *
 * Реализует state machine через наследование состояний, где каждое состояние — это
 * наследник Block<A> с доступными методами работы. Каждый метод
 * выполняет один атомарный шаг критической операции и возвращает следующего
 * наследника, соответствующего новому состоянию блока.
 *
 * Корректные состояния:
 *   - FreeBlock        — свободный блок (weight=0, root_offset=0, в AVL-дереве)
 *   - AllocatedBlock   — занятый блок (weight>0, root_offset=idx, не в AVL)
 *
 * Переходные состояния (только во время операций):
 *   - FreeBlockRemovedAVL    — свободный, удалённый из AVL (перед allocate)
 *   - FreeBlockNotInAVL      — свободный, не в AVL (после deallocate, перед coalesce)
 *   - SplittingBlock         — блок в процессе разбиения
 *   - CoalescingBlock        — блок в процессе слияния
 *
 * Гарантии:
 *   1. Типобезопасность: компилятор запрещает вызов недоступных методов
 *   2. Восстановимость: переходные состояния детектируются при load()
 *   3. Атомарность: каждый метод выполняет один атомарный шаг
 *   4. Завершаемость: цепочка вызовов приводит к корректному состоянию
 *
 * Issue #114: Добавлены статические методы в BlockStateBase для recovery-операций:
 *   - reset_avl_fields_of()     — сброс AVL-полей перед rebuild_free_tree
 *   - repair_prev_offset()      — восстановление prev_offset при repair_linked_list
 *   - get_next_offset()         — чтение next_offset в recovery-методах
 *   - get_weight()              — чтение weight в recovery-методах
 *
 * Issue #168: Удалены избыточные функции-обёртки reset_block_avl_fields(),
 *   repair_block_prev_offset(), read_block_next_offset(), read_block_weight() —
 *   AllocatorPolicy вызывает BlockStateBase<AT>::* напрямую.
 *
 * @see docs/atomic_writes.md «Граф состояний блока»
 * @see plan_issue87.md §5 «Фаза 9: BlockState machine»
 * @version 0.4 (Issue #168 — удалены дублирующие функции-обёртки)
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/block.h"
#include "pmm/tree_node.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm
{

// Forward declarations
template <typename AddressTraitsT> class FreeBlock;
template <typename AddressTraitsT> class AllocatedBlock;
template <typename AddressTraitsT> class FreeBlockRemovedAVL;
template <typename AddressTraitsT> class FreeBlockNotInAVL;
template <typename AddressTraitsT> class SplittingBlock;
template <typename AddressTraitsT> class CoalescingBlock;

/**
 * @brief Базовый класс блока для state machine.
 *
 * Наследует Block<A> (с полями prev_offset/next_offset і TreeNode<A>).
 * Все поля приватные для потомков. Доступ только через методы состояний.
 *
 * Layout Block<A> (32 bytes при DefaultAddressTraits):
 *   [0..23]  TreeNode<A>: weight (4), left_offset (4), right_offset (4),
 *                         parent_offset (4), root_offset (4),
 *                         avl_height (2), node_type (2)
 *   [24..31] Block<A>:    prev_offset (4), next_offset (4)
 *
 * @tparam AddressTraitsT  Traits адресного пространства.
 */
template <typename AddressTraitsT> class BlockStateBase : private Block<AddressTraitsT>
{
  private:
    using TNode = TreeNode<AddressTraitsT>;

  public:
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;
    using BaseBlock      = Block<AddressTraitsT>;

    // ─── Compile-time layout offsets (Issue #120: derived from sizes + struct layout) ──
    // Note: offsetof cannot be used on protected members from outside the class body.
    // These offsets are derived from sizeof base types and the assumption of standard layout.
    // The struct layout is verified by static_assert in block.h and tree_node.h.

    /// Byte offset of prev_offset within Block<A> layout (first direct field of Block, after TreeNode).
    static constexpr std::size_t kOffsetPrevOffset = sizeof( TNode );
    /// Byte offset of next_offset within Block<A> layout (second direct field of Block, after prev_offset).
    static constexpr std::size_t kOffsetNextOffset = sizeof( TNode ) + sizeof( index_type );
    /// Byte offset of weight within Block<A> layout (first field of TreeNode, Issue #126, #138).
    static constexpr std::size_t kOffsetWeight = 0;
    /// Byte offset of left_offset within Block<A> layout (second field of TreeNode, follows weight).
    static constexpr std::size_t kOffsetLeftOffset = sizeof( index_type );
    /// Byte offset of right_offset within Block<A> layout.
    static constexpr std::size_t kOffsetRightOffset = 2 * sizeof( index_type );
    /// Byte offset of parent_offset within Block<A> layout.
    static constexpr std::size_t kOffsetParentOffset = 3 * sizeof( index_type );
    /// Byte offset of root_offset within Block<A> layout.
    static constexpr std::size_t kOffsetRootOffset = 4 * sizeof( index_type );
    /// Byte offset of avl_height within Block<A> layout (after weight+left+right+parent+root = 5 index_type fields).
    static constexpr std::size_t kOffsetAvlHeight = 5 * sizeof( index_type );
    /// Byte offset of node_type within Block<A> layout (after avl_height(2) = 2 bytes, Issue #126, #138).
    static constexpr std::size_t kOffsetNodeType = 5 * sizeof( index_type ) + 2;

    // Прямое создание запрещено — используйте cast_from_raw()
    BlockStateBase() = delete;

    // Read-only доступ к weight (определяет состояние: 0 = свободный, >0 = занятый)
    index_type weight() const noexcept { return TNode::weight; }

    // Read-only доступ к полям связного списка (не критичны для состояния)
    index_type prev_offset() const noexcept { return Block<AddressTraitsT>::prev_offset; }
    index_type next_offset() const noexcept { return Block<AddressTraitsT>::next_offset; }

    // Read-only доступ к AVL-полям (для диагностики)
    index_type   left_offset() const noexcept { return TNode::left_offset; }
    index_type   right_offset() const noexcept { return TNode::right_offset; }
    index_type   parent_offset() const noexcept { return TNode::parent_offset; }
    std::int16_t avl_height() const noexcept { return TNode::avl_height; }

    // Read-only доступ к root_offset (определяет состояние)
    index_type root_offset() const noexcept { return TNode::root_offset; }

    // Read-only доступ к node_type (Issue #126: тип узла)
    std::uint16_t node_type() const noexcept { return TNode::node_type; }

    /**
     * @brief Определить, является ли блок свободным (по структурным признакам).
     * @return true если weight == 0 и root_offset == 0.
     */
    bool is_free() const noexcept { return weight() == 0 && root_offset() == 0; }

    /**
     * @brief Определить, является ли блок занятым (по структурным признакам).
     * @param own_idx Гранульный индекс данного блока.
     * @return true если weight > 0 и root_offset == own_idx.
     */
    bool is_allocated( index_type own_idx ) const noexcept { return weight() > 0 && root_offset() == own_idx; }

    /**
     * @brief Определить, заблокирован ли блок навечно (Issue #126).
     * @return true если node_type == kNodeReadOnly.
     */
    bool is_permanently_locked() const noexcept { return node_type() == pmm::kNodeReadOnly; }

    // ─── Статические утилиты для recovery-операций ──────────────────────────

    /**
     * @brief Восстановить блок в корректное состояние (при load()).
     *
     * Используется для восстановления после crash — приводит блок к корректному
     * состоянию (FreeBlock или AllocatedBlock) в зависимости от weight и root_offset.
     *
     * @param raw_blk   Указатель на блок.
     * @param own_idx   Гранульный индекс данного блока.
     */
    static void recover_state( void* raw_blk, index_type own_idx ) noexcept
    {
        auto* blk = reinterpret_cast<BlockStateBase*>( raw_blk );
        // Если weight > 0, но root_offset неверен — исправляем
        if ( blk->weight() > 0 && blk->root_offset() != own_idx )
            blk->set_root_offset( own_idx );
        // Если weight == 0, но root_offset != 0 — исправляем
        if ( blk->weight() == 0 && blk->root_offset() != 0 )
            blk->set_root_offset( 0 );
    }

    /**
     * @brief Сбросить AVL-поля блока перед перестройкой дерева (при rebuild_free_tree).
     *
     * Устанавливает left_offset, right_offset, parent_offset в no_block, avl_height в 0.
     *
     * @param raw_blk  Указатель на блок.
     */
    static void reset_avl_fields_of( void* raw_blk ) noexcept
    {
        auto* blk = reinterpret_cast<BlockStateBase*>( raw_blk );
        blk->set_left_offset( AddressTraitsT::no_block );
        blk->set_right_offset( AddressTraitsT::no_block );
        blk->set_parent_offset( AddressTraitsT::no_block );
        blk->set_avl_height( 0 );
    }

    /**
     * @brief Восстановить prev_offset блока (при repair_linked_list).
     *
     * @param raw_blk   Указатель на блок.
     * @param prev_idx  Гранульный индекс предыдущего блока (или no_block).
     */
    static void repair_prev_offset( void* raw_blk, index_type prev_idx ) noexcept
    {
        auto* blk = reinterpret_cast<BlockStateBase*>( raw_blk );
        blk->set_prev_offset( prev_idx );
    }

    /**
     * @brief Прочитать prev_offset блока (read-only, без перехода состояний).
     *
     * @param raw_blk  Указатель на блок.
     * @return Гранульный индекс предыдущего блока.
     */
    static index_type get_prev_offset( const void* raw_blk ) noexcept
    {
        const auto* blk = reinterpret_cast<const BlockStateBase*>( raw_blk );
        return blk->prev_offset();
    }

    /**
     * @brief Прочитать next_offset блока (read-only, без перехода состояний).
     *
     * @param raw_blk  Указатель на блок.
     * @return Гранульный индекс следующего блока.
     */
    static index_type get_next_offset( const void* raw_blk ) noexcept
    {
        const auto* blk = reinterpret_cast<const BlockStateBase*>( raw_blk );
        return blk->next_offset();
    }

    /**
     * @brief Прочитать weight блока (read-only, без перехода состояний).
     *
     * @param raw_blk  Указатель на блок.
     * @return Значение поля weight (0 = свободный, >0 = занятый).
     */
    static index_type get_weight( const void* raw_blk ) noexcept
    {
        const auto* blk = reinterpret_cast<const BlockStateBase*>( raw_blk );
        return blk->weight();
    }

    /**
     * @brief Инициализировать поля нового блока (для AVL tree insert при expand/init).
     *
     * @param raw_blk          Указатель на блок (уже обнулённый memset).
     * @param prev_idx         Гранульный индекс предыдущего блока.
     * @param next_idx         Гранульный индекс следующего блока (или no_block).
     * @param avl_height_val   Начальная высота AVL (1 = новый свободный узел, 0 = занятый).
     * @param weight_val       Начальный вес (0 = свободный).
     * @param root_offset_val  Начальный root_offset (0 = свободный, own_idx = занятый).
     */
    static void init_fields( void* raw_blk, index_type prev_idx, index_type next_idx, std::int16_t avl_height_val,
                             index_type weight_val, index_type root_offset_val ) noexcept
    {
        auto* blk = reinterpret_cast<BlockStateBase*>( raw_blk );
        blk->set_prev_offset( prev_idx );
        blk->set_next_offset( next_idx );
        blk->set_left_offset( AddressTraitsT::no_block );
        blk->set_right_offset( AddressTraitsT::no_block );
        blk->set_parent_offset( AddressTraitsT::no_block );
        blk->set_avl_height( avl_height_val );
        blk->set_weight( weight_val );
        blk->set_root_offset( root_offset_val );
    }

    /**
     * @brief Обновить next_offset соседнего блока (для операций со связным списком).
     *
     * @param raw_blk   Указатель на блок.
     * @param next_idx  Новый гранульный индекс следующего блока.
     */
    static void set_next_offset_of( void* raw_blk, index_type next_idx ) noexcept
    {
        auto* blk = reinterpret_cast<BlockStateBase*>( raw_blk );
        blk->set_next_offset( next_idx );
    }

    // ─── Статические утилиты для AVL-дерева ────────────────────────────────

    /**
     * @brief Прочитать left_offset блока.
     */
    static index_type get_left_offset( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->left_offset();
    }
    /**
     * @brief Прочитать right_offset блока.
     */
    static index_type get_right_offset( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->right_offset();
    }
    /**
     * @brief Прочитать parent_offset блока.
     */
    static index_type get_parent_offset( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->parent_offset();
    }
    /**
     * @brief Прочитать avl_height блока.
     */
    static std::int16_t get_avl_height( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->avl_height();
    }
    /**
     * @brief Установить left_offset блока.
     */
    static void set_left_offset_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_left_offset( v );
    }
    /**
     * @brief Установить right_offset блока.
     */
    static void set_right_offset_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_right_offset( v );
    }
    /**
     * @brief Установить parent_offset блока.
     */
    static void set_parent_offset_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_parent_offset( v );
    }
    /**
     * @brief Установить avl_height блока.
     */
    static void set_avl_height_of( void* raw_blk, std::int16_t v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_avl_height( v );
    }
    /**
     * @brief Прочитать root_offset блока.
     */
    static index_type get_root_offset( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->root_offset();
    }
    /**
     * @brief Установить prev_offset блока.
     */
    static void set_prev_offset_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_prev_offset( v );
    }
    /**
     * @brief Установить weight блока.
     */
    static void set_weight_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_weight( v );
    }
    /**
     * @brief Установить root_offset блока.
     */
    static void set_root_offset_of( void* raw_blk, index_type v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_root_offset( v );
    }
    /**
     * @brief Прочитать node_type блока (Issue #126).
     */
    static std::uint16_t get_node_type( const void* raw_blk ) noexcept
    {
        return reinterpret_cast<const BlockStateBase*>( raw_blk )->node_type();
    }
    /**
     * @brief Установить node_type блока (Issue #126).
     */
    static void set_node_type_of( void* raw_blk, std::uint16_t v ) noexcept
    {
        reinterpret_cast<BlockStateBase*>( raw_blk )->set_node_type( v );
    }

  protected:
    // Внутренние сеттеры для наследников
    void set_weight( index_type v ) noexcept { TNode::weight = v; }
    void set_prev_offset( index_type v ) noexcept { Block<AddressTraitsT>::prev_offset = v; }
    void set_next_offset( index_type v ) noexcept { Block<AddressTraitsT>::next_offset = v; }
    void set_left_offset( index_type v ) noexcept { TNode::left_offset = v; }
    void set_right_offset( index_type v ) noexcept { TNode::right_offset = v; }
    void set_parent_offset( index_type v ) noexcept { TNode::parent_offset = v; }
    void set_avl_height( std::int16_t v ) noexcept { TNode::avl_height = v; }
    void set_root_offset( index_type v ) noexcept { TNode::root_offset = v; }
    void set_node_type( std::uint16_t v ) noexcept { TNode::node_type = v; }

    // Reset AVL fields to "not in tree" state
    void reset_avl_fields() noexcept
    {
        set_left_offset( AddressTraitsT::no_block );
        set_right_offset( AddressTraitsT::no_block );
        set_parent_offset( AddressTraitsT::no_block );
        set_avl_height( 0 );
    }
};

// Проверка бинарной совместимости с Block<A>
static_assert( sizeof( BlockStateBase<DefaultAddressTraits> ) == sizeof( Block<DefaultAddressTraits> ),
               "BlockStateBase<A> must have same size as Block<A> (Issue #93)" );
static_assert( sizeof( BlockStateBase<DefaultAddressTraits> ) == 32,
               "BlockStateBase<DefaultAddressTraits> must be 32 bytes (Issue #93)" );

/**
 * @brief FreeBlock — свободный блок в корректном состоянии.
 *
 * Инварианты:
 *   - weight == 0
 *   - root_offset == 0
 *   - Блок находится в AVL-дереве свободных блоков
 *
 * Допустимые операции:
 *   - remove_from_avl() → FreeBlockRemovedAVL (начало allocate)
 */
template <typename AddressTraitsT> class FreeBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    /**
     * @brief Интерпретировать сырые байты как FreeBlock.
     *
     * @param raw Указатель на Block<A>.
     * @return Указатель на FreeBlock.
     *
     * @warning Вызывающий код должен гарантировать, что блок действительно свободен.
     *
     * @note В debug-режиме (NDEBUG не определён) проверяет инварианты FreeBlock
     *       через assert: weight==0 и root_offset==0.
     */
    /**
     * @brief Интерпретировать сырые байты как FreeBlock.
     *
     * Issue #43 Phase 1.4: runtime check — returns nullptr in Release builds
     * if block is not in FreeBlock state, instead of relying on assert only.
     */
    static FreeBlock* cast_from_raw( void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( !reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->is_free() )
        {
            assert( false && "cast_from_raw<FreeBlock>: block is not in FreeBlock state" );
            return nullptr;
        }
        return reinterpret_cast<FreeBlock*>( raw );
    }

    static const FreeBlock* cast_from_raw( const void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( !reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->is_free() )
        {
            assert( false && "cast_from_raw<FreeBlock>: block is not in FreeBlock state" );
            return nullptr;
        }
        return reinterpret_cast<const FreeBlock*>( raw );
    }

    /**
     * @brief Проверить инварианты свободного блока.
     * @return true если блок в корректном состоянии FreeBlock.
     */
    bool verify_invariants() const noexcept { return Base::is_free(); }

    /**
     * @brief Удалить блок из AVL-дерева (первый шаг allocate).
     *
     * @note AVL-операция выполняется вызывающим кодом отдельно.
     * @return Указатель на блок в состоянии FreeBlockRemovedAVL.
     */
    FreeBlockRemovedAVL<AddressTraitsT>* remove_from_avl() noexcept
    {
        // AVL-удаление выполняется внешне; здесь только reinterpret
        // (инварианты сохраняются: weight=0, root_offset=0)
        return reinterpret_cast<FreeBlockRemovedAVL<AddressTraitsT>*>( this );
    }
};

/**
 * @brief FreeBlockRemovedAVL — свободный блок, удалённый из AVL-дерева.
 *
 * Переходное состояние во время операции allocate.
 *
 * Инварианты:
 *   - weight == 0
 *   - root_offset == 0
 *   - Блок НЕ находится в AVL-дереве (удалён)
 *
 * Допустимые операции:
 *   - mark_as_allocated() → AllocatedBlock (завершение allocate)
 *   - begin_splitting()   → SplittingBlock (если нужно разбить блок)
 */
template <typename AddressTraitsT> class FreeBlockRemovedAVL : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    /**
     * @brief Интерпретировать сырые байты как FreeBlockRemovedAVL.
     */
    static FreeBlockRemovedAVL* cast_from_raw( void* raw ) noexcept
    {
        return reinterpret_cast<FreeBlockRemovedAVL*>( raw );
    }

    /**
     * @brief Пометить блок как занятый (финализация allocate без split).
     *
     * @param data_granules Размер данных в гранулах.
     * @param own_idx       Гранульный индекс данного блока.
     * @return Указатель на блок в состоянии AllocatedBlock.
     */
    AllocatedBlock<AddressTraitsT>* mark_as_allocated( index_type data_granules, index_type own_idx ) noexcept
    {
        Base::set_weight( data_granules );
        Base::set_root_offset( own_idx );
        Base::reset_avl_fields();
        return reinterpret_cast<AllocatedBlock<AddressTraitsT>*>( this );
    }

    /**
     * @brief Начать операцию разбиения блока.
     *
     * @return Указатель на блок в состоянии SplittingBlock.
     */
    SplittingBlock<AddressTraitsT>* begin_splitting() noexcept
    {
        return reinterpret_cast<SplittingBlock<AddressTraitsT>*>( this );
    }

    /**
     * @brief Восстановить блок обратно в AVL-дерево (откат allocate).
     *
     * @note AVL-операция выполняется вызывающим кодом отдельно.
     * @return Указатель на блок в состоянии FreeBlock.
     */
    FreeBlock<AddressTraitsT>* insert_to_avl() noexcept { return reinterpret_cast<FreeBlock<AddressTraitsT>*>( this ); }
};

/**
 * @brief SplittingBlock — блок в процессе разбиения на два.
 *
 * Переходное состояние во время операции allocate с split.
 *
 * Допустимые операции:
 *   - initialize_new_block() — инициализировать новый блок (memset)
 *   - link_new_block()       — обновить связный список
 *   - finalize_split()       — завершить split и вернуться к mark_as_allocated
 */
template <typename AddressTraitsT> class SplittingBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    /**
     * @brief Интерпретировать сырые байты как SplittingBlock.
     */
    static SplittingBlock* cast_from_raw( void* raw ) noexcept { return reinterpret_cast<SplittingBlock*>( raw ); }

    /**
     * @brief Инициализировать новый блок (результат split).
     *
     * @param new_blk_ptr Указатель на новый блок (memset + инициализация полей).
     * @param new_idx     Гранульный индекс нового блока (unused, for API clarity).
     * @param own_idx     Гранульный индекс текущего блока.
     */
    void initialize_new_block( void* new_blk_ptr, [[maybe_unused]] index_type new_idx, index_type own_idx ) noexcept
    {
        std::memset( new_blk_ptr, 0, sizeof( Block<AddressTraitsT> ) );
        // Инициализация через SplittingBlock<A> — все поля доступны через state machine методы
        auto* new_blk = reinterpret_cast<SplittingBlock<AddressTraitsT>*>( new_blk_ptr );
        new_blk->set_prev_offset( own_idx );
        new_blk->set_next_offset( Base::next_offset() );
        new_blk->set_left_offset( AddressTraitsT::no_block );
        new_blk->set_right_offset( AddressTraitsT::no_block );
        new_blk->set_parent_offset( AddressTraitsT::no_block );
        new_blk->set_avl_height( 1 ); // Будет вставлен в AVL
        new_blk->set_weight( 0 );
        new_blk->set_root_offset( 0 );
    }

    /**
     * @brief Обновить связный список для включения нового блока.
     *
     * @param old_next_blk Указатель на старый следующий блок (может быть nullptr).
     * @param new_idx      Гранульный индекс нового блока.
     */
    void link_new_block( void* old_next_blk, index_type new_idx ) noexcept
    {
        if ( old_next_blk != nullptr )
        {
            auto* old_next_blk_state = reinterpret_cast<SplittingBlock<AddressTraitsT>*>( old_next_blk );
            old_next_blk_state->set_prev_offset( new_idx );
        }
        Base::set_next_offset( new_idx );
    }

    /**
     * @brief Завершить операцию split и пометить текущий блок как занятый.
     *
     * @param data_granules Размер данных в гранулах.
     * @param own_idx       Гранульный индекс текущего блока.
     * @return Указатель на блок в состоянии AllocatedBlock.
     */
    AllocatedBlock<AddressTraitsT>* finalize_split( index_type data_granules, index_type own_idx ) noexcept
    {
        Base::set_weight( data_granules );
        Base::set_root_offset( own_idx );
        Base::reset_avl_fields();
        return reinterpret_cast<AllocatedBlock<AddressTraitsT>*>( this );
    }
};

/**
 * @brief AllocatedBlock — занятый блок в корректном состоянии.
 *
 * Инварианты:
 *   - weight > 0
 *   - root_offset == собственный гранульный индекс
 *   - Блок НЕ находится в AVL-дереве свободных блоков
 *
 * Допустимые операции:
 *   - mark_as_free() → FreeBlockNotInAVL (начало deallocate)
 *   - user_ptr()     — получить указатель на пользовательские данные
 */
template <typename AddressTraitsT> class AllocatedBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    /**
     * @brief Интерпретировать сырые байты как AllocatedBlock.
     *
     * @note В debug-режиме (NDEBUG не определён) проверяет, что weight > 0
     *       (минимальное условие AllocatedBlock). Полная проверка (root_offset == own_idx)
     *       доступна через verify_invariants(own_idx).
     */
    /**
     * @brief Интерпретировать сырые байты как AllocatedBlock.
     *
     * Issue #43 Phase 1.4: runtime check — returns nullptr in Release builds
     * if block is not allocated, instead of relying on assert only.
     */
    static AllocatedBlock* cast_from_raw( void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->weight() == 0 )
        {
            assert( false && "cast_from_raw<AllocatedBlock>: block is not allocated (weight==0)" );
            return nullptr;
        }
        return reinterpret_cast<AllocatedBlock*>( raw );
    }

    static const AllocatedBlock* cast_from_raw( const void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw )->weight() == 0 )
        {
            assert( false && "cast_from_raw<AllocatedBlock>: block is not allocated (weight==0)" );
            return nullptr;
        }
        return reinterpret_cast<const AllocatedBlock*>( raw );
    }

    /**
     * @brief Проверить инварианты занятого блока.
     * @param own_idx Гранульный индекс данного блока.
     * @return true если блок в корректном состоянии AllocatedBlock.
     */
    bool verify_invariants( index_type own_idx ) const noexcept { return Base::is_allocated( own_idx ); }

    /**
     * @brief Получить указатель на пользовательские данные.
     * @return Указатель на данные (после заголовка блока).
     */
    void* user_ptr() noexcept { return reinterpret_cast<std::uint8_t*>( this ) + sizeof( Block<AddressTraitsT> ); }

    const void* user_ptr() const noexcept
    {
        return reinterpret_cast<const std::uint8_t*>( this ) + sizeof( Block<AddressTraitsT> );
    }

    /**
     * @brief Пометить блок как свободный (первый шаг deallocate).
     *
     * @return Указатель на блок в состоянии FreeBlockNotInAVL.
     */
    FreeBlockNotInAVL<AddressTraitsT>* mark_as_free() noexcept
    {
        Base::set_weight( 0 );
        Base::set_root_offset( 0 );
        return reinterpret_cast<FreeBlockNotInAVL<AddressTraitsT>*>( this );
    }
};

/**
 * @brief FreeBlockNotInAVL — только что освобождённый блок, ещё не в AVL.
 *
 * Переходное состояние во время операции deallocate, перед coalesce.
 *
 * Инварианты:
 *   - weight == 0
 *   - root_offset == 0
 *   - Блок НЕ находится в AVL-дереве (ещё не добавлен)
 *
 * Допустимые операции:
 *   - begin_coalescing() → CoalescingBlock (если есть соседи для слияния)
 *   - insert_to_avl()    → FreeBlock (завершение deallocate)
 */
template <typename AddressTraitsT> class FreeBlockNotInAVL : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    /**
     * @brief Интерпретировать сырые байты как FreeBlockNotInAVL.
     */
    static FreeBlockNotInAVL* cast_from_raw( void* raw ) noexcept
    {
        return reinterpret_cast<FreeBlockNotInAVL*>( raw );
    }

    /**
     * @brief Начать операцию слияния с соседними блоками.
     *
     * @return Указатель на блок в состоянии CoalescingBlock.
     */
    CoalescingBlock<AddressTraitsT>* begin_coalescing() noexcept
    {
        return reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( this );
    }

    /**
     * @brief Добавить блок в AVL-дерево (завершение deallocate).
     *
     * @note AVL-операция выполняется вызывающим кодом отдельно.
     * @return Указатель на блок в состоянии FreeBlock.
     */
    FreeBlock<AddressTraitsT>* insert_to_avl() noexcept
    {
        Base::set_avl_height( 1 ); // Готов к вставке в AVL
        return reinterpret_cast<FreeBlock<AddressTraitsT>*>( this );
    }
};

/**
 * @brief CoalescingBlock — блок в процессе слияния с соседями.
 *
 * Переходное состояние во время операции coalesce.
 *
 * Допустимые операции:
 *   - coalesce_with_next() — слить с правым соседом
 *   - coalesce_with_prev() — слить с левым соседом (this будет уничтожен)
 *   - finalize_coalesce()  — завершить слияние и добавить в AVL
 */
template <typename AddressTraitsT> class CoalescingBlock : public BlockStateBase<AddressTraitsT>
{
  public:
    using Base       = BlockStateBase<AddressTraitsT>;
    using index_type = typename AddressTraitsT::index_type;

    /**
     * @brief Интерпретировать сырые байты как CoalescingBlock.
     */
    static CoalescingBlock* cast_from_raw( void* raw ) noexcept { return reinterpret_cast<CoalescingBlock*>( raw ); }

    /**
     * @brief Слить текущий блок с правым соседом.
     *
     * @param next_blk       Указатель на правый соседний блок (будет поглощён).
     * @param next_next_blk  Указатель на блок после соседа (может быть nullptr).
     * @param own_idx        Гранульный индекс текущего блока.
     *
     * @note AVL-удаление соседа выполняется вызывающим кодом отдельно.
     */
    void coalesce_with_next( void* next_blk, void* next_next_blk, index_type own_idx ) noexcept
    {
        auto* nxt = reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( next_blk );

        // Обновляем связный список
        Base::set_next_offset( nxt->next_offset() );
        if ( next_next_blk != nullptr )
        {
            auto* nxt_nxt = reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( next_next_blk );
            nxt_nxt->set_prev_offset( own_idx );
        }

        // Обнуляем поглощённый блок
        std::memset( next_blk, 0, sizeof( Block<AddressTraitsT> ) );
    }

    /**
     * @brief Слить текущий блок с левым соседом (текущий будет поглощён).
     *
     * @param prev_blk       Указатель на левый соседний блок (станет результатом).
     * @param next_blk       Указатель на следующий блок за текущим (может быть nullptr).
     * @param prev_idx       Гранульный индекс левого соседа.
     *
     * @note AVL-удаление соседа выполняется вызывающим кодом отдельно.
     * @return Указатель на результирующий блок (prev_blk) в состоянии CoalescingBlock.
     */
    CoalescingBlock<AddressTraitsT>* coalesce_with_prev( void* prev_blk, void* next_blk, index_type prev_idx ) noexcept
    {
        auto* prv = reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( prev_blk );
        prv->set_next_offset( Base::next_offset() );

        if ( next_blk != nullptr )
        {
            auto* nxt = reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( next_blk );
            nxt->set_prev_offset( prev_idx );
        }

        // Обнуляем текущий блок (поглощён)
        std::memset( this, 0, sizeof( Block<AddressTraitsT> ) );

        // Возвращаем левый сосед как результирующий блок
        return reinterpret_cast<CoalescingBlock<AddressTraitsT>*>( prev_blk );
    }

    /**
     * @brief Завершить операцию coalesce и добавить блок в AVL-дерево.
     *
     * @note AVL-операция выполняется вызывающим кодом отдельно.
     * @return Указатель на блок в состоянии FreeBlock.
     */
    FreeBlock<AddressTraitsT>* finalize_coalesce() noexcept
    {
        Base::set_avl_height( 1 ); // Готов к вставке в AVL
        return reinterpret_cast<FreeBlock<AddressTraitsT>*>( this );
    }
};

// ─── Утилиты для работы со state machine ───────────────────────────────────────

/**
 * @brief Определить состояние блока по сырым данным.
 *
 * @tparam AddressTraitsT Traits адресного пространства.
 * @param raw_blk   Указатель на Block<A>.
 * @param own_idx   Гранульный индекс данного блока.
 * @return 0 = FreeBlock, 1 = AllocatedBlock, -1 = неопределённое/переходное.
 */
template <typename AddressTraitsT>
int detect_block_state( const void* raw_blk, typename AddressTraitsT::index_type own_idx ) noexcept
{
    const auto* base = reinterpret_cast<const BlockStateBase<AddressTraitsT>*>( raw_blk );
    if ( base->is_free() )
        return 0; // FreeBlock (или переходное — требует проверки AVL)
    if ( base->is_allocated( own_idx ) )
        return 1; // AllocatedBlock
    return -1;    // Неопределённое состояние (ошибка или переходное)
}

/**
 * @brief Восстановить блок в корректное состояние (при load()).
 *
 * Используется для восстановления после crash — приводит блок к корректному
 * состоянию (FreeBlock или AllocatedBlock) в зависимости от weight и root_offset.
 *
 * @tparam AddressTraitsT Traits адресного пространства.
 * @param raw_blk   Указатель на блок.
 * @param own_idx   Гранульный индекс данного блока.
 */
template <typename AddressTraitsT>
void recover_block_state( void* raw_blk, typename AddressTraitsT::index_type own_idx ) noexcept
{
    BlockStateBase<AddressTraitsT>::recover_state( raw_blk, own_idx );
}

} // namespace pmm
