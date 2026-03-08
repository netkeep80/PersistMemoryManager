/**
 * @file pmm/tree_node.h
 * @brief TreeNode<AddressTraits> — узел AVL-дерева для ПАП (Issue #87 Phase 2).
 *
 * Параметрический узел AVL-дерева, где тип индексных полей определяется
 * через `AddressTraits::index_type`.
 *
 * ПАП-менеджер организует блоки памяти как лес AVL-деревьев.  Каждый узел
 * принадлежит ровно одному дереву и может мигрировать из одного дерева в другое
 * (например, из дерева свободных блоков в пользовательское дерево).
 *
 * Поля `weight` и `root_offset` хранятся внутри узла дерева, поскольку они
 * являются атрибутами именно узла дерева:
 *   - `weight`      — ключ балансировки; семантика определяется деревом-владельцем
 *                     (для дерева свободных блоков — количество свободных гранул
 *                     за этим узлом; для других деревьев — произвольный вес).
 *   - `root_offset` — идентификатор дерева-владельца:
 *                       0         = узел принадлежит дереву свободных блоков (ПАП);
 *                       own_index = узел занят и является корнем своего дерева.
 *
 * Поля:
 *   `left_offset`, `right_offset`, `parent_offset`, `avl_height`, `_pad`,
 *   `weight`, `root_offset` соответствуют полям Block<DefaultAddressTraits>
 *   после LinkedListNode.  Layout подтверждён через `static_assert` в `types.h`.
 *
 * @see plan_issue87.md §5 «Фаза 2: LinkedListNode и TreeNode»
 * @version 0.2 (Issue #87 Phase 2 — weight+root_offset moved into TreeNode)
 */

#pragma once

#include "pmm/address_traits.h"

#include <cstdint>

namespace pmm
{

/**
 * @brief Узел AVL-дерева для адресного пространства ПАП, использование только через наследование.
 *
 * @tparam AddressTraitsT  Traits адресного пространства (из address_traits.h).
 *                         Определяет тип индексных полей.
 *
 * Поля хранят гранульные индексы узлов-потомков / родителя в ПАП.
 * Sentinel «нет узла» = `AddressTraitsT::no_block`.
 *
 * `avl_height` — высота AVL-поддерева (0 = узел не в дереве).
 * `_pad`       — выравнивающий паддинг; зарезервировано.
 * `weight`     — ключ балансировки дерева (для дерева свободных блоков: количество
 *               свободных гранул за узлом; семантика произвольна для других деревьев).
 * `root_offset`— идентификатор дерева: 0 = свободный блок (дерево свободных блоков),
 *               own_idx = занятый блок (дерево с корнем own_idx).
 *
 * Размеры `TreeNode<DefaultAddressTraits>` (uint32_t, 16):
 *   left_offset   (4) + right_offset (4) + parent_offset (4) +
 *   avl_height    (2) + _pad         (2) +
 *   weight        (4) + root_offset  (4) = 24 bytes
 */
template <typename AddressTraitsT> struct TreeNode
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

protected:
    /// Гранульный индекс левого дочернего узла AVL-дерева (или no_block).
    index_type left_offset;
    /// Гранульный индекс правого дочернего узла AVL-дерева (или no_block).
    index_type right_offset;
    /// Гранульный индекс родительского узла AVL-дерева (или no_block).
    index_type parent_offset;
    /// Высота AVL-поддерева (0 = узел не в дереве).
    std::int16_t avl_height;
    /// Зарезервировано (выравнивающий паддинг).
    std::uint16_t _pad;
    /// Ключ балансировки дерева.
    /// Для дерева свободных блоков (root_offset == 0): количество свободных гранул за блоком.
    /// Для других деревьев: произвольный вес, определяемый деревом-владельцем.
    index_type weight;
    /// Идентификатор дерева-владельца.
    /// 0 = узел принадлежит дереву свободных блоков (ПАП-менеджер).
    /// own_idx = узел занят; значение равно гранульному индексу самого блока.
    index_type root_offset;
};

} // namespace pmm
