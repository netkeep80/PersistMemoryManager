/**
 * @file pmm/tree_node.h
 * @brief TreeNode<AddressTraits> — узел AVL-дерева для ПАП (Issue #87 Phase 2, #138).
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
 *   `avl_height`, `node_type`.  Layout подтверждён через `static_assert` в `types.h`.
 *
 * Issue #126: Поле `weight` перемещено в начало для ускорения доступа.
 *             Поля `avl_height` и `node_type` (бывший `_pad`) перемещены в конец.
 *             Два типа узлов: kNodeReadWrite (0) и kNodeReadOnly (1).
 * Issue #138: Добавлены публичные методы доступа к полям TreeNode, чтобы операции
 *             над узлом дерева можно было выполнять через ссылку, полученную из pptr.
 *
 * @see plan_issue87.md §5 «Фаза 2: LinkedListNode и TreeNode»
 * @version 0.4 (Issue #138 — public accessors added for use via pptr::tree_node())
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
 *
 * Публичные методы (Issue #138): доступ к полям узла дерева для использования
 * через ссылку, полученную из pptr<T, ManagerT>::tree_node().
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

    // ─── Публичные методы доступа к полям узла дерева (Issue #138) ────────────
    // Используются через ссылку, полученную из pptr<T, ManagerT>::tree_node().

    /// @brief Получить гранульный индекс левого дочернего узла AVL-дерева.
    /// @return Гранульный индекс или no_block если нет левого потомка.
    index_type get_left() const noexcept { return left_offset; }

    /// @brief Получить гранульный индекс правого дочернего узла AVL-дерева.
    /// @return Гранульный индекс или no_block если нет правого потомка.
    index_type get_right() const noexcept { return right_offset; }

    /// @brief Получить гранульный индекс родительского узла AVL-дерева.
    /// @return Гранульный индекс или no_block если нет родителя (корень).
    index_type get_parent() const noexcept { return parent_offset; }

    /// @brief Получить идентификатор дерева-владельца (root_offset).
    /// @return 0 = свободный блок; own_idx = занятый блок.
    index_type get_root() const noexcept { return root_offset; }

    /// @brief Получить ключ балансировки (weight).
    /// @return Вес узла (для дерева свободных блоков: число свободных гранул).
    index_type get_weight() const noexcept { return weight; }

    /// @brief Получить высоту AVL-поддерева.
    /// @return Высота (0 = узел не в дереве).
    std::int16_t get_height() const noexcept { return avl_height; }

    /// @brief Получить тип узла.
    /// @return kNodeReadWrite (0) или kNodeReadOnly (1).
    std::uint16_t get_node_type() const noexcept { return node_type; }

    /// @brief Установить гранульный индекс левого дочернего узла AVL-дерева.
    /// @param v Гранульный индекс или no_block.
    void set_left( index_type v ) noexcept { left_offset = v; }

    /// @brief Установить гранульный индекс правого дочернего узла AVL-дерева.
    /// @param v Гранульный индекс или no_block.
    void set_right( index_type v ) noexcept { right_offset = v; }

    /// @brief Установить гранульный индекс родительского узла AVL-дерева.
    /// @param v Гранульный индекс или no_block.
    void set_parent( index_type v ) noexcept { parent_offset = v; }

    /// @brief Установить идентификатор дерева-владельца (root_offset).
    /// @param v 0 = свободный блок; own_idx = занятый блок.
    void set_root( index_type v ) noexcept { root_offset = v; }

    /// @brief Установить ключ балансировки (weight).
    /// @param v Новый вес узла.
    void set_weight( index_type v ) noexcept { weight = v; }

    /// @brief Установить высоту AVL-поддерева.
    /// @param v Новая высота (0 = не в дереве).
    void set_height( std::int16_t v ) noexcept { avl_height = v; }

    /// @brief Установить тип узла.
    /// @param v kNodeReadWrite (0) или kNodeReadOnly (1).
    void set_node_type( std::uint16_t v ) noexcept { node_type = v; }

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
