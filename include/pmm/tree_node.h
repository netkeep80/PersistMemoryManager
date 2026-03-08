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
 * Issue #136: поля `left_offset`, `right_offset`, `parent_offset` перемещены из
 * заголовка блока в область данных свободного блока (FreeBlockData<A>).
 * Это позволяет уменьшить заголовок Block<A> с 32 до 16 байт (с 2 до 1 гранулы).
 *
 * В заголовке остаются только поля `weight`, `root_offset`, `avl_height`, `node_type`,
 * нужные для ВСЕХ блоков (свободных и занятых).
 *
 * Поля в заголовке (Issue #136 — новый состав):
 *   `weight`, `root_offset`, `avl_height`, `node_type`
 *
 * Поля, перемещённые в FreeBlockData<A> (Issue #136):
 *   `left_offset`, `right_offset`, `parent_offset`
 *
 * Issue #126: Поля `avl_height` и `node_type` (бывший `_pad`) перемещены в конец.
 *             Два типа узлов: kNodeReadWrite (0) и kNodeReadOnly (1).
 *
 * @see plan_issue87.md §5 «Фаза 2: LinkedListNode и TreeNode»
 * @see free_block_data.h — FreeBlockData (Issue #136: left/right/parent в области данных)
 * @version 0.4 (Issue #136 — left/right/parent moved to FreeBlockData, header reduced to 12 bytes)
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
 * Sentinel «нет узла» = `AddressTraitsT::no_block`.
 *
 * `weight`     — ключ балансировки дерева (для дерева свободных блоков: количество
 *               свободных гранул за узлом; для выделенных блоков: размер данных в гранулах).
 * `root_offset`— идентификатор состояния блока: 0 = свободный блок,
 *               own_idx = занятый блок (значение равно гранульному индексу самого блока).
 * `avl_height` — высота AVL-поддерева (0 = узел не в дереве или занятый блок).
 * `node_type`  — тип узла (Issue #126): kNodeReadWrite (0) = доступен на чтение/запись,
 *               kNodeReadOnly (1) = заблокирован навечно (только чтение, нельзя освободить).
 *
 * Issue #136: left_offset, right_offset, parent_offset перемещены в FreeBlockData<A>.
 *
 * Размеры `TreeNode<DefaultAddressTraits>` (uint32_t, 16):
 *   weight        (4) +
 *   root_offset   (4) +
 *   avl_height    (2) + node_type (2) = 12 bytes
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
    /// Ключ балансировки дерева / размер данных в гранулах для занятых блоков.
    /// Для свободных блоков (root_offset == 0): всегда 0.
    index_type weight;
    /// Идентификатор состояния блока.
    /// 0 = свободный блок (дерево свободных блоков).
    /// own_idx = занятый блок (значение равно гранульному индексу самого блока).
    index_type root_offset;
    /// Высота AVL-поддерева (0 = узел не в дереве или занятый блок).
    std::int16_t avl_height;
    /// Тип узла (Issue #126): kNodeReadWrite (0) = чтение/запись, kNodeReadOnly (1) = только чтение.
    std::uint16_t node_type;
    // Issue #136: left_offset, right_offset, parent_offset перемещены в FreeBlockData<A>
    //             (область данных свободного блока, по адресу base + blk_idx*gran + sizeof(Block<A>)).
};

// Layout: TreeNode is a standard-layout struct.
static_assert( std::is_standard_layout<pmm::TreeNode<pmm::DefaultAddressTraits>>::value,
               "TreeNode must be standard-layout (Issue #87)" );

} // namespace pmm
