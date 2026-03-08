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
 * Размер и выравнивание:
 *   sizeof(Block<DefaultAddressTraits>) == 32 байта (2 гранулы по 16 байт).
 *   Подтверждено через static_assert в types.h. Issue #112: Block<A> — единственный тип блока.
 *
 * @see plan_issue87.md §5 «Фаза 3: Block — блок как составной тип»
 * @version 0.3 (Issue #112 — BlockHeader removed, Block<A> is sole block type)
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
 *   sizeof(Block<DefaultAddressTraits>) == 32 байта
 *
 * Issue #118: re-expose protected fields from base classes as public so that
 * external code (PersistMemoryManager, utility functions in block_state.h, etc.)
 * can access them through a Block<A>* pointer.  The fields remain protected in
 * LinkedListNode / TreeNode — only Block<A> promotes them to public.
 */
template <typename AddressTraitsT> struct Block : LinkedListNode<AddressTraitsT>, TreeNode<AddressTraitsT>
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

    // Re-publish protected fields from LinkedListNode<A> as public.
    using LinkedListNode<AddressTraitsT>::prev_offset;
    using LinkedListNode<AddressTraitsT>::next_offset;

    // Re-publish protected fields from TreeNode<A> as public.
    using TreeNode<AddressTraitsT>::left_offset;
    using TreeNode<AddressTraitsT>::right_offset;
    using TreeNode<AddressTraitsT>::parent_offset;
    using TreeNode<AddressTraitsT>::avl_height;
    using TreeNode<AddressTraitsT>::weight;
    using TreeNode<AddressTraitsT>::root_offset;
};

} // namespace pmm
