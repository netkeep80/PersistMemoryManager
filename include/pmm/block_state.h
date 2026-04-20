/**
 * @file pmm/block_state.h
 * @brief FSM allocator/free-tree domain: FreeBlock ↔ AllocatedBlock.
 *
 * Scope: автомат физической мутации блока (allocate/deallocate/split/coalesce).
 * `pmap`/`pstringview` работают с уже выделенными блоками и через FSM не проходят.
 * `BlockStateBase<AT>::*` — low-level helper layer для allocator/repair, не public API.
 *
 * Полный граф состояний и анализ восстановления — docs/atomic_writes.md.
 *
 * @version 0.5
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/block.h"
#include "pmm/block_field.h"
#include "pmm/diagnostics.h"
#include "pmm/tree_node.h"

#include <cassert>
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

    template <typename FieldTag> using field_value_type = detail::block_field_value_t<AddressTraitsT, FieldTag>;

    template <typename FieldTag>
    static constexpr std::size_t field_offset = detail::block_field_offset_v<AddressTraitsT, FieldTag>;

    template <typename FieldTag> static field_value_type<FieldTag> get_field_of( const void* raw_blk ) noexcept
    {
        return detail::read_block_field<AddressTraitsT, FieldTag>( raw_blk );
    }

    template <typename FieldTag> static void set_field_of( void* raw_blk, field_value_type<FieldTag> value ) noexcept
    {
        detail::write_block_field<AddressTraitsT, FieldTag>( raw_blk, value );
    }

    static bool is_free_raw( const void* raw_blk ) noexcept
    {
        return get_weight( raw_blk ) == 0 && get_root_offset( raw_blk ) == 0;
    }

    static bool is_allocated_raw( const void* raw_blk, index_type own_idx ) noexcept
    {
        return get_weight( raw_blk ) > 0 && get_root_offset( raw_blk ) == own_idx;
    }

    // ─── Compile-time layout offsets derived by block field descriptors ─────
    // The descriptor mirror is checked against the production TreeNode/Block
    // sizes so raw field access follows the compiler's padding decisions.

    /// Byte offset of prev_offset within Block<A> layout (first direct field of Block, after TreeNode).
    static constexpr std::size_t kOffsetPrevOffset = field_offset<detail::BlockPrevOffsetField>;
    /// Byte offset of next_offset within Block<A> layout (second direct field of Block, after prev_offset).
    static constexpr std::size_t kOffsetNextOffset = field_offset<detail::BlockNextOffsetField>;
    /// Byte offset of weight within Block<A> layout (first field of TreeNode).
    static constexpr std::size_t kOffsetWeight = field_offset<detail::BlockWeightField>;
    /// Byte offset of left_offset within Block<A> layout (second field of TreeNode, follows weight).
    static constexpr std::size_t kOffsetLeftOffset = field_offset<detail::BlockLeftOffsetField>;
    /// Byte offset of right_offset within Block<A> layout.
    static constexpr std::size_t kOffsetRightOffset = field_offset<detail::BlockRightOffsetField>;
    /// Byte offset of parent_offset within Block<A> layout.
    static constexpr std::size_t kOffsetParentOffset = field_offset<detail::BlockParentOffsetField>;
    /// Byte offset of root_offset within Block<A> layout.
    static constexpr std::size_t kOffsetRootOffset = field_offset<detail::BlockRootOffsetField>;
    /// Byte offset of avl_height within Block<A> layout.
    static constexpr std::size_t kOffsetAvlHeight = field_offset<detail::BlockAvlHeightField>;
    /// Byte offset of node_type within Block<A> layout.
    static constexpr std::size_t kOffsetNodeType = field_offset<detail::BlockNodeTypeField>;

    static_assert( detail::block_tree_slot_size_v<AddressTraitsT> == sizeof( TNode ),
                   "Block field descriptors must match TreeNode layout" );
    static_assert( detail::block_layout_size_v<AddressTraitsT> == sizeof( BaseBlock ),
                   "Block field descriptors must match Block layout" );

    // Прямое создание запрещено — используйте cast_from_raw()
    BlockStateBase() = delete;

    // Read-only доступ к weight (определяет состояние: 0 = свободный, >0 = занятый)
    index_type weight() const noexcept { return get_weight( this ); }

    // Read-only доступ к полям связного списка (не критичны для состояния)
    index_type prev_offset() const noexcept { return get_prev_offset( this ); }
    index_type next_offset() const noexcept { return get_next_offset( this ); }

    // Read-only доступ к AVL-полям (для диагностики)
    index_type   left_offset() const noexcept { return get_left_offset( this ); }
    index_type   right_offset() const noexcept { return get_right_offset( this ); }
    index_type   parent_offset() const noexcept { return get_parent_offset( this ); }
    std::int16_t avl_height() const noexcept { return get_avl_height( this ); }

    // Read-only доступ к root_offset (определяет состояние)
    index_type root_offset() const noexcept { return get_root_offset( this ); }

    // Read-only доступ к node_type
    std::uint16_t node_type() const noexcept { return get_node_type( this ); }

    /**
     * @brief Определить, является ли блок свободным (по структурным признакам).
     * @return true если weight == 0 и root_offset == 0.
     */
    bool is_free() const noexcept { return is_free_raw( this ); }

    /**
     * @brief Определить, является ли блок занятым (по структурным признакам).
     * @param own_idx Гранульный индекс данного блока.
     * @return true если weight > 0 и root_offset == own_idx.
     */
    bool is_allocated( index_type own_idx ) const noexcept { return is_allocated_raw( this, own_idx ); }

    /**
     * @brief Определить, заблокирован ли блок навечно.
     * @return true если node_type == kNodeReadOnly.
     */
    bool is_permanently_locked() const noexcept { return node_type() == pmm::kNodeReadOnly; }

    // ─── Статические утилиты для repair-операций (вызываются из load()) ─────

    /**
     * @brief Repair block state to a consistent value (called during load()).
     *
     * Part of the repair phase in load(): fixes transitional (inconsistent)
     * weight/root_offset states left by interrupted allocate/deallocate.
     * Deterministic: weight alone determines the correct root_offset.
     *
     * @param raw_blk   Указатель на блок.
     * @param own_idx   Гранульный индекс данного блока.
     */
    static void recover_state( void* raw_blk, index_type own_idx ) noexcept
    {
        const index_type weight_val = get_weight( raw_blk );
        const index_type root_val   = get_root_offset( raw_blk );
        // Если weight > 0, но root_offset неверен — исправляем
        if ( weight_val > 0 && root_val != own_idx )
            set_root_offset_of( raw_blk, own_idx );
        // Если weight == 0, но root_offset != 0 — исправляем
        if ( weight_val == 0 && root_val != 0 )
            set_root_offset_of( raw_blk, 0 );
    }

    /**
     * @brief Verify block state consistency without modifying the image.
     *
     * Read-only counterpart of recover_state(). Checks that weight and root_offset
     * are in a consistent (non-transitional) state. Reports violations into result.
     *
     * @param raw_blk   Pointer to the block (read-only).
     * @param own_idx   Granule index of this block.
     * @param result    Diagnostic result to append violations to.
     */
    static void verify_state( const void* raw_blk, index_type own_idx, VerifyResult& result ) noexcept
    {
        const index_type weight_val = get_weight( raw_blk );
        const index_type root_val   = get_root_offset( raw_blk );
        if ( weight_val > 0 && root_val != own_idx )
        {
            result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( own_idx ), static_cast<std::uint64_t>( own_idx ),
                        static_cast<std::uint64_t>( root_val ) );
        }
        if ( weight_val == 0 && root_val != 0 )
        {
            result.add( ViolationType::BlockStateInconsistent, DiagnosticAction::NoAction,
                        static_cast<std::uint64_t>( own_idx ), 0, static_cast<std::uint64_t>( root_val ) );
        }
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
        set_left_offset_of( raw_blk, AddressTraitsT::no_block );
        set_right_offset_of( raw_blk, AddressTraitsT::no_block );
        set_parent_offset_of( raw_blk, AddressTraitsT::no_block );
        set_avl_height_of( raw_blk, 0 );
    }

    /**
     * @brief Восстановить prev_offset блока (при repair_linked_list).
     *
     * @param raw_blk   Указатель на блок.
     * @param prev_idx  Гранульный индекс предыдущего блока (или no_block).
     */
    static void repair_prev_offset( void* raw_blk, index_type prev_idx ) noexcept
    {
        set_prev_offset_of( raw_blk, prev_idx );
    }

    /**
     * @brief Прочитать prev_offset блока (read-only, без перехода состояний).
     *
     * @param raw_blk  Указатель на блок.
     * @return Гранульный индекс предыдущего блока.
     */
    static index_type get_prev_offset( const void* raw_blk ) noexcept
    {
        return get_field_of<detail::BlockPrevOffsetField>( raw_blk );
    }

    /**
     * @brief Прочитать next_offset блока (read-only, без перехода состояний).
     *
     * @param raw_blk  Указатель на блок.
     * @return Гранульный индекс следующего блока.
     */
    static index_type get_next_offset( const void* raw_blk ) noexcept
    {
        return get_field_of<detail::BlockNextOffsetField>( raw_blk );
    }

    /**
     * @brief Прочитать weight блока (read-only, без перехода состояний).
     *
     * @param raw_blk  Указатель на блок.
     * @return Значение поля weight (0 = свободный, >0 = занятый).
     */
    static index_type get_weight( const void* raw_blk ) noexcept
    {
        return get_field_of<detail::BlockWeightField>( raw_blk );
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
        set_prev_offset_of( raw_blk, prev_idx );
        set_next_offset_of( raw_blk, next_idx );
        set_left_offset_of( raw_blk, AddressTraitsT::no_block );
        set_right_offset_of( raw_blk, AddressTraitsT::no_block );
        set_parent_offset_of( raw_blk, AddressTraitsT::no_block );
        set_avl_height_of( raw_blk, avl_height_val );
        set_weight_of( raw_blk, weight_val );
        set_root_offset_of( raw_blk, root_offset_val );
    }

    /**
     * @brief Обновить next_offset соседнего блока (для операций со связным списком).
     *
     * @param raw_blk   Указатель на блок.
     * @param next_idx  Новый гранульный индекс следующего блока.
     */
    static void set_next_offset_of( void* raw_blk, index_type next_idx ) noexcept
    {
        set_field_of<detail::BlockNextOffsetField>( raw_blk, next_idx );
    }

    // ─── Статические утилиты для AVL-дерева ────────────────────────────────

    static index_type get_left_offset( const void* b ) noexcept
    {
        return get_field_of<detail::BlockLeftOffsetField>( b );
    }
    static index_type get_right_offset( const void* b ) noexcept
    {
        return get_field_of<detail::BlockRightOffsetField>( b );
    }
    static index_type get_parent_offset( const void* b ) noexcept
    {
        return get_field_of<detail::BlockParentOffsetField>( b );
    }
    static index_type get_root_offset( const void* b ) noexcept
    {
        return get_field_of<detail::BlockRootOffsetField>( b );
    }
    static void set_left_offset_of( void* b, index_type v ) noexcept
    {
        set_field_of<detail::BlockLeftOffsetField>( b, v );
    }
    static void set_right_offset_of( void* b, index_type v ) noexcept
    {
        set_field_of<detail::BlockRightOffsetField>( b, v );
    }
    static void set_parent_offset_of( void* b, index_type v ) noexcept
    {
        set_field_of<detail::BlockParentOffsetField>( b, v );
    }
    static void set_prev_offset_of( void* b, index_type v ) noexcept
    {
        set_field_of<detail::BlockPrevOffsetField>( b, v );
    }
    static void set_weight_of( void* b, index_type v ) noexcept { set_field_of<detail::BlockWeightField>( b, v ); }
    static void set_root_offset_of( void* b, index_type v ) noexcept
    {
        set_field_of<detail::BlockRootOffsetField>( b, v );
    }

    static std::int16_t get_avl_height( const void* raw_blk ) noexcept
    {
        return get_field_of<detail::BlockAvlHeightField>( raw_blk );
    }
    static void set_avl_height_of( void* raw_blk, std::int16_t v ) noexcept
    {
        set_field_of<detail::BlockAvlHeightField>( raw_blk, v );
    }
    static std::uint16_t get_node_type( const void* raw_blk ) noexcept
    {
        return get_field_of<detail::BlockNodeTypeField>( raw_blk );
    }
    static void set_node_type_of( void* raw_blk, std::uint16_t v ) noexcept
    {
        set_field_of<detail::BlockNodeTypeField>( raw_blk, v );
    }

  protected:
    template <typename StateT> static StateT* state_from_raw( void* raw ) noexcept
    {
        return reinterpret_cast<StateT*>( raw );
    }

    template <typename StateT> static const StateT* state_from_raw( const void* raw ) noexcept
    {
        return reinterpret_cast<const StateT*>( raw );
    }

    template <typename StateT> StateT* state_as() noexcept { return reinterpret_cast<StateT*>( this ); }

    // Внутренние сеттеры для наследников
    void set_weight( index_type v ) noexcept { set_weight_of( this, v ); }
    void set_prev_offset( index_type v ) noexcept { set_prev_offset_of( this, v ); }
    void set_next_offset( index_type v ) noexcept { set_next_offset_of( this, v ); }
    void set_left_offset( index_type v ) noexcept { set_left_offset_of( this, v ); }
    void set_right_offset( index_type v ) noexcept { set_right_offset_of( this, v ); }
    void set_parent_offset( index_type v ) noexcept { set_parent_offset_of( this, v ); }
    void set_avl_height( std::int16_t v ) noexcept { set_avl_height_of( this, v ); }
    void set_root_offset( index_type v ) noexcept { set_root_offset_of( this, v ); }
    void set_node_type( std::uint16_t v ) noexcept { set_node_type_of( this, v ); }

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
               "BlockStateBase<A> must have same size as Block<A> " );
static_assert( sizeof( BlockStateBase<DefaultAddressTraits> ) == 32,
               "BlockStateBase<DefaultAddressTraits> must be 32 bytes " );

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
     * @return Указатель на FreeBlock, или nullptr если raw==nullptr или блок не свободен.
     *
     * if block is not in FreeBlock state, instead of relying on assert only.
     * В debug-режиме дополнительно срабатывает assert для диагностики.
     */
    static FreeBlock* cast_from_raw( void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( !Base::is_free_raw( raw ) )
        {
            assert( false && "cast_from_raw<FreeBlock>: block is not in FreeBlock state" );
            return nullptr;
        }
        return Base::template state_from_raw<FreeBlock<AddressTraitsT>>( raw );
    }

    static const FreeBlock* cast_from_raw( const void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( !Base::is_free_raw( raw ) )
        {
            assert( false && "cast_from_raw<FreeBlock>: block is not in FreeBlock state" );
            return nullptr;
        }
        return Base::template state_from_raw<FreeBlock<AddressTraitsT>>( raw );
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
        // AVL-удаление выполняется внешне; здесь только state overlay.
        // (инварианты сохраняются: weight=0, root_offset=0)
        return this->template state_as<FreeBlockRemovedAVL<AddressTraitsT>>();
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
        return Base::template state_from_raw<FreeBlockRemovedAVL<AddressTraitsT>>( raw );
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
        return this->template state_as<AllocatedBlock<AddressTraitsT>>();
    }

    /**
     * @brief Начать операцию разбиения блока.
     *
     * @return Указатель на блок в состоянии SplittingBlock.
     */
    SplittingBlock<AddressTraitsT>* begin_splitting() noexcept
    {
        return this->template state_as<SplittingBlock<AddressTraitsT>>();
    }

    /**
     * @brief Восстановить блок обратно в AVL-дерево (откат allocate).
     *
     * @note AVL-операция выполняется вызывающим кодом отдельно.
     * @return Указатель на блок в состоянии FreeBlock.
     */
    FreeBlock<AddressTraitsT>* insert_to_avl() noexcept { return this->template state_as<FreeBlock<AddressTraitsT>>(); }
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
    static SplittingBlock* cast_from_raw( void* raw ) noexcept
    {
        return Base::template state_from_raw<SplittingBlock<AddressTraitsT>>( raw );
    }

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
        Base::init_fields( new_blk_ptr, own_idx, this->next_offset(), 1, 0, 0 );
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
            Base::set_prev_offset_of( old_next_blk, new_idx );
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
        return this->template state_as<AllocatedBlock<AddressTraitsT>>();
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
     * @return Указатель на AllocatedBlock, или nullptr если raw==nullptr или weight==0.
     *
     * if block is not allocated, instead of relying on assert only.
     * В debug-режиме дополнительно срабатывает assert для диагностики.
     * Полная проверка (root_offset == own_idx) доступна через verify_invariants(own_idx).
     */
    static AllocatedBlock* cast_from_raw( void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( Base::get_weight( raw ) == 0 )
        {
            assert( false && "cast_from_raw<AllocatedBlock>: block is not allocated (weight==0)" );
            return nullptr;
        }
        return Base::template state_from_raw<AllocatedBlock<AddressTraitsT>>( raw );
    }

    static const AllocatedBlock* cast_from_raw( const void* raw ) noexcept
    {
        if ( raw == nullptr )
            return nullptr;
        if ( Base::get_weight( raw ) == 0 )
        {
            assert( false && "cast_from_raw<AllocatedBlock>: block is not allocated (weight==0)" );
            return nullptr;
        }
        return Base::template state_from_raw<AllocatedBlock<AddressTraitsT>>( raw );
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
        return this->template state_as<FreeBlockNotInAVL<AddressTraitsT>>();
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
        return Base::template state_from_raw<FreeBlockNotInAVL<AddressTraitsT>>( raw );
    }

    /**
     * @brief Начать операцию слияния с соседними блоками.
     *
     * @return Указатель на блок в состоянии CoalescingBlock.
     */
    CoalescingBlock<AddressTraitsT>* begin_coalescing() noexcept
    {
        return this->template state_as<CoalescingBlock<AddressTraitsT>>();
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
        return this->template state_as<FreeBlock<AddressTraitsT>>();
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
    static CoalescingBlock* cast_from_raw( void* raw ) noexcept
    {
        return Base::template state_from_raw<CoalescingBlock<AddressTraitsT>>( raw );
    }

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
        // Обновляем связный список
        Base::set_next_offset( Base::get_next_offset( next_blk ) );
        if ( next_next_blk != nullptr )
        {
            Base::set_prev_offset_of( next_next_blk, own_idx );
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
        Base::set_next_offset_of( prev_blk, Base::next_offset() );

        if ( next_blk != nullptr )
        {
            Base::set_prev_offset_of( next_blk, prev_idx );
        }

        // Обнуляем текущий блок (поглощён)
        std::memset( this, 0, sizeof( Block<AddressTraitsT> ) );

        // Возвращаем левый сосед как результирующий блок
        return Base::template state_from_raw<CoalescingBlock<AddressTraitsT>>( prev_blk );
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
        return this->template state_as<FreeBlock<AddressTraitsT>>();
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
    using BlockState = BlockStateBase<AddressTraitsT>;
    if ( BlockState::is_free_raw( raw_blk ) )
        return 0; // FreeBlock (или переходное — требует проверки AVL)
    if ( BlockState::is_allocated_raw( raw_blk, own_idx ) )
        return 1; // AllocatedBlock
    return -1;    // Неопределённое состояние (ошибка или переходное)
}

/// @brief Alias for BlockStateBase<AT>::recover_state().
template <typename AT> inline void recover_block_state( void* raw_blk, typename AT::index_type own_idx ) noexcept
{
    BlockStateBase<AT>::recover_state( raw_blk, own_idx );
}

/// @brief Alias for BlockStateBase<AT>::verify_state().
template <typename AT>
inline void verify_block_state( const void* raw_blk, typename AT::index_type own_idx, VerifyResult& result ) noexcept
{
    BlockStateBase<AT>::verify_state( raw_blk, own_idx, result );
}

} // namespace pmm
