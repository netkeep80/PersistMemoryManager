/**
 * @file pmm/block.h
 * @brief Block<AddressTraits> — блок памяти как составной тип (Issue #87 Phase 3).
 *
 * Block<AddressTraits> является составным типом, объединяющим:
 *   - LinkedListNode<AddressTraits> — узел двухсвязного списка всех блоков
 *   - TreeNode<AddressTraits>       — узел AVL-дерева (включает weight и root_offset)
 *
 * Концептуальная модель:
 *   ПАП-менеджер организует память как лес AVL-деревьев.  Каждый блок является
 *   одновременно узлом двухсвязного списка (охватывает всё адресное пространство)
 *   и узлом одного из AVL-деревьев леса.  Поля `weight` и `root_offset` принадлежат
 *   узлу дерева (TreeNode), а не блоку напрямую.
 *
 * Раскладка полей при AddressTraitsT = DefaultAddressTraits (uint32_t, 16):
 *   [0..7]   LinkedListNode<A>: prev_offset (4), next_offset (4)
 *   [8..31]  TreeNode<A>:       left_offset (4), right_offset (4), parent_offset (4),
 *                                avl_height (2), _pad (2), weight (4), root_offset (4)
 *
 * Бинарная совместимость:
 *   sizeof(Block<DefaultAddressTraits>) == sizeof(BlockHeader) == 32 байта.
 *   Совместимость подтверждена через static_assert в persist_memory_types.h.
 *
 * @see plan_issue87.md §5 «Фаза 3: Block — блок как составной тип»
 * @version 0.2 (Issue #87 Phase 3 — weight+root_offset moved to TreeNode)
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
 * Все поля (включая weight и root_offset) хранятся в базовых классах.
 *
 * Доступ к полям через наследование:
 *   - prev_offset, next_offset          — через LinkedListNode<A>
 *   - left_offset, right_offset,
 *     parent_offset, avl_height, _pad,
 *     weight, root_offset               — через TreeNode<A>
 *
 * При AddressTraitsT = DefaultAddressTraits (uint32_t, 16):
 *   sizeof(Block<DefaultAddressTraits>) == 32 == sizeof(BlockHeader)
 */
template <typename AddressTraitsT>
struct Block : LinkedListNode<AddressTraitsT>, TreeNode<AddressTraitsT>
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;
};

} // namespace pmm
