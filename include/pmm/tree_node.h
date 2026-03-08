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
 * Поля (Issue #126 — новый порядок):
 *   `weight`, `left_offset`, `right_offset`, `parent_offset`, `root_offset`,
 *   `avl_height`, `node_type` соответствуют полям Block<DefaultAddressTraits>
 *   после LinkedListNode.  Layout подтверждён через `static_assert` в `types.h`.
 *
 * Issue #126: Поле `weight` перемещено в начало для ускорения доступа.
 *             Поля `avl_height` и `node_type` (бывший `_pad`) перемещены в конец.
 *             Два типа узлов: kNodeReadWrite (0) и kNodeReadOnly (1).
 *
 * @see plan_issue87.md §5 «Фаза 2: LinkedListNode и TreeNode»
 * @version 0.3 (Issue #126 — reordered fields, renamed _pad to node_type)
 */

#pragma once

#include "pmm/address_traits.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
 * `weight`     — ключ балансировки дерева (для дерева свободных блоков: количество
 *               свободных гранул за узлом; семантика произвольна для других деревьев).
 *               Первое поле для ускорения доступа (Issue #126).
 * `root_offset`— идентификатор дерева: 0 = свободный блок (дерево свободных блоков),
 *               own_idx = занятый блок (дерево с корнем own_idx).
 * `avl_height` — высота AVL-поддерева (0 = узел не в дереве).
 * `node_type`  — тип узла (Issue #126): kNodeReadWrite (0) = доступен на чтение/запись,
 *               kNodeReadOnly (1) = заблокирован навечно (только чтение, нельзя освободить).
 *
 * Размеры `TreeNode<DefaultAddressTraits>` (uint32_t, 16):
 *   weight        (4) +
 *   left_offset   (4) + right_offset (4) + parent_offset (4) +
 *   root_offset   (4) +
 *   avl_height    (2) + node_type    (2) = 24 bytes
 */

/// @brief Тип узла (Issue #126): значения для поля `TreeNode::node_type`.
///
/// kNodeReadWrite (0) — блок доступен на чтение и запись, может быть освобождён.
/// kNodeReadOnly  (1) — блок заблокирован навечно: доступен только на чтение,
///                      не может быть освобождён через deallocate().
enum : std::uint16_t
{
    kNodeReadWrite = 0,
    kNodeReadOnly  = 1,
};

template <typename AddressTraitsT> struct TreeNode
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

  protected:
    /// Ключ балансировки дерева. Первое поле для ускорения доступа (Issue #126).
    /// Для дерева свободных блоков (root_offset == 0): количество свободных гранул за блоком.
    /// Для других деревьев: произвольный вес, определяемый деревом-владельцем.
    index_type weight;
    /// Гранульный индекс левого дочернего узла AVL-дерева (или no_block).
    index_type left_offset;
    /// Гранульный индекс правого дочернего узла AVL-дерева (или no_block).
    index_type right_offset;
    /// Гранульный индекс родительского узла AVL-дерева (или no_block).
    index_type parent_offset;
    /// Идентификатор дерева-владельца.
    /// 0 = узел принадлежит дереву свободных блоков (ПАП-менеджер).
    /// own_idx = узел занят; значение равно гранульному индексу самого блока.
    index_type root_offset;
    /// Высота AVL-поддерева (0 = узел не в дереве).
    std::int16_t avl_height;
    /// Тип узла (Issue #126): kNodeReadWrite (0) = чтение/запись, kNodeReadOnly (1) = только чтение.
    std::uint16_t node_type;
};

// Layout: TreeNode is a standard-layout struct.
static_assert( std::is_standard_layout<pmm::TreeNode<pmm::DefaultAddressTraits>>::value,
               "TreeNode must be standard-layout (Issue #87)" );

} // namespace pmm
