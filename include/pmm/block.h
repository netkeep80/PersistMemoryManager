/**
 * @file pmm/block.h
 * @brief Block<AddressTraits> — блок памяти как составной тип (Issue #87 Phase 3).
 *
 * Block<AddressTraits> является составным типом, объединяющим:
 *   - LinkedListNode<AddressTraits> — узел двухсвязного списка всех блоков
 *   - TreeNode<AddressTraits>       — узел AVL-дерева свободных блоков
 *   - Собственные поля: size, root_offset
 *
 * Бинарная совместимость:
 *   sizeof(Block<DefaultAddressTraits>) == sizeof(BlockHeader) == 32 байта.
 *   Совместимость подтверждена через static_assert в persist_memory_types.h.
 *
 * @note Block<A> задаёт НОВУЮ компонентную раскладку полей:
 *         [0..7]   LinkedListNode<A>: prev_offset, next_offset
 *         [8..23]  TreeNode<A>: left_offset, right_offset, parent_offset, avl_height, _pad
 *         [24..27] size       (0 = свободный блок, Issue #75)
 *         [28..31] root_offset (0 = свободный блок; own_idx = занятый, Issue #75)
 *       Существующий BlockHeader имеет иной порядок полей (size первый).
 *       Обратная совместимость обеспечивается на уровне sizeof == 32.
 *
 * @see plan_issue87.md §5 «Фаза 3: Block — блок как составной тип»
 * @version 0.1 (Issue #87 Phase 3)
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/linked_list_node.h"
#include "pmm/tree_node.h"

#include <cstdint>

namespace pmm
{

/**
 * @brief Блок памяти ПАП — составной тип.
 *
 * @tparam AddressTraitsT  Traits адресного пространства (из address_traits.h).
 *                         Определяет тип индексных полей.
 *
 * Наследует LinkedListNode<AddressTraitsT> и TreeNode<AddressTraitsT>.
 * Собственные поля:
 *   - `size`        — занятый размер в гранулах (0 = свободный блок, Issue #75)
 *   - `root_offset` — 0 = свободный блок; собственный индекс = занятый (Issue #75)
 *
 * При AddressTraitsT = DefaultAddressTraits (uint32_t, 16):
 *   sizeof(Block<DefaultAddressTraits>) == 32 == sizeof(BlockHeader)
 */
template <typename AddressTraitsT> struct Block : LinkedListNode<AddressTraitsT>, TreeNode<AddressTraitsT>
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

    /// Занятый размер в гранулах (0 = свободный блок, Issue #75).
    index_type size;
    /// 0 = свободный блок (участвует в дереве свободных блоков);
    /// собственный гранульный индекс = занятый блок (Issue #75).
    index_type root_offset;
};

} // namespace pmm
