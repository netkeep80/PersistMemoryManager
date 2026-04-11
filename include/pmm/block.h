/**
 * @file pmm/block.h
 * @brief Block<AddressTraits> — блок памяти как составной тип (: phase 3, #138).
 *
 * Block<AddressTraits> является составным типом, объединяющим:
 *   - поля связного списка (prev_offset, next_offset) — хранятся прямо в Block
 *   - TreeNode<AddressTraits>  — узел AVL-дерева (включает weight и root_offset)
 *
 * Концептуальная модель:
 *   ПАП-менеджер организует память как лес AVL-деревьев.  Каждый блок является
 *   одновременно узлом двухсвязного списка (охватывает всё адресное пространство)
 *   и узлом одного из AVL-деревьев леса.  Поля `weight` и `root_offset` принадлежат
 *   узлу дерева (TreeNode), а не блоку напрямую.
 *
 * Раскладка полей при AddressTraitsT = DefaultAddressTraits (uint32_t, 16):
 *   [0..23]  TreeNode<A>: weight (4), left_offset (4), right_offset (4),
 *                         parent_offset (4), root_offset (4),
 *                         avl_height (2), node_type (2)
 *   [24..31] Block:       prev_offset (4), next_offset (4)
 *
 *             avl_height и node_type (бывший _pad) перемещены в конец TreeNode.
 *             и next_offset перемещены прямо в Block.
 *
 * Размер и выравнивание:
 *   sizeof(Block<DefaultAddressTraits>) == 32 байта (2 гранулы по 16 байт).
 *   Подтверждено через static_assert в types.h. Block<A> — единственный тип блока.
 *
 * @version 0.5
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/tree_node.h"

#include <cstdint>
#include <type_traits>

namespace pmm
{

/**
 * @brief Блок памяти ПАП — составной тип.
 *
 * @tparam AddressTraitsT  Traits адресного пространства (из address_traits.h).
 *                         Определяет тип индексных полей.
 *
 * Наследует TreeNode<AddressTraitsT>.
 * Поля связного списка (prev_offset, next_offset) хранятся прямо в Block
 *.
 *
 * Доступ к полям:
 *   - prev_offset, next_offset          — прямые поля Block<A>
 *   - weight, left_offset, right_offset,
 *     parent_offset, root_offset,
 *     avl_height, node_type             — через TreeNode<A>
 *
 * При AddressTraitsT = DefaultAddressTraits (uint32_t, 16):
 *   sizeof(Block<DefaultAddressTraits>) == 32 байта
 *   Layout: [0..23] TreeNode fields, [24..31] prev_offset, next_offset
 */
template <typename AddressTraitsT> struct Block : TreeNode<AddressTraitsT>
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

  protected:
    /// Гранульный индекс предыдущего блока (или no_block).
    index_type prev_offset;
    /// Гранульный индекс следующего блока (или no_block).
    index_type next_offset;
};

// Block inherits TreeNode and adds prev_offset/next_offset as own members.
// Note: Block is NOT standard-layout (both base and derived have data members), but
// the memory layout is still predictable: TreeNode fields first, then Block own fields.
// The layout is validated via kOffset* constants in BlockStateBase (block_state.h).
static_assert( sizeof( pmm::Block<pmm::DefaultAddressTraits> ) == 32, "Block<DefaultAddressTraits> must be 32 bytes " );

} // namespace pmm
