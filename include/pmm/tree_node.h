/**
 * @file pmm/tree_node.h
 * @brief TreeNode<AddressTraits> — intrusive AVL-slot for the forest model.
 *
 * Parametric AVL tree node where index field types are defined by
 * `AddressTraits::index_type`.
 *
 * The PAP manager organizes blocks as an AVL-forest. Each node belongs to exactly
 * one forest domain and can migrate between domains (e.g., from the free-tree
 * to a user domain tree).
 *
 * ## Field semantics in the forest model
 *
 *   - `weight`      — universal granule-key / granule-scalar field.
 *                     Semantics are determined by the owning forest domain:
 *                       - free-tree domain: state discriminator (0 = free block);
 *                         ordering key is derived from linear PAP geometry, not weight.
 *                       - user domains (pstringview, pmap): domain-specific sort key.
 *   - `root_offset` — owner-domain / owner-tree marker:
 *                       0         = node belongs to the free-tree domain;
 *                       own_index = node is allocated (self-owned).
 *
 *
 * @see docs/free_tree_forest_policy.md — free-tree ordering policy
 * @see docs/block_and_treenode_semantics.md — canonical field semantics
 * @version 0.5
 */

#pragma once

#include "pmm/address_traits.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

/**
 * @brief Intrusive AVL-slot for the forest model — used only via inheritance.
 *
 * @tparam AddressTraitsT  Address space traits (from address_traits.h).
 *                         Defines index field types.
 *
 * Fields store granule indices of child/parent nodes in the PAP.
 * Sentinel "no node" = `AddressTraitsT::no_block`.
 *
 * `weight`     — universal granule-key whose semantics depend on the owning domain.
 *               Free-tree domain: state discriminator (0 = free block);
 *               ordering is derived from linear PAP geometry, not this field.
 *               User domains: domain-specific sort key (e.g., string index, entity ID).
 *               First field for cache-efficient access.
 * `root_offset`— owner-domain marker: 0 = free-tree domain,
 *               own_idx = allocated block (self-owned tree).
 * `avl_height` — AVL subtree height (0 = slot not in any tree).
 * `node_type`  — coarse-grained block type:
 *               kNodeReadWrite (0) = read/write, kNodeReadOnly (1) = read-only.
 *
 * Layout `TreeNode<DefaultAddressTraits>` (uint32_t, 16-byte granule):
 *   weight        (4) +
 *   left_offset   (4) + right_offset (4) + parent_offset (4) +
 *   root_offset   (4) +
 *   avl_height    (2) + node_type    (2) = 24 bytes
 *
 * Public accessors: for use via pptr<T, ManagerT>::tree_node().
 */

/// @brief Тип узла: значения для поля `TreeNode::node_type`.
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

    // ─── Публичные методы доступа к полям узла дерева ────────────
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

    /// @brief Get the owner-domain marker (root_offset).
    /// @return 0 = free-tree domain; own_idx = allocated block.
    index_type get_root() const noexcept { return root_offset; }

    /// @brief Get the universal granule-key (weight).
    /// @return Domain-specific scalar (free-tree: 0 = free; user domains: sort key).
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

    /// @brief Set the owner-domain marker (root_offset).
    /// @param v 0 = free-tree domain; own_idx = allocated block.
    void set_root( index_type v ) noexcept { root_offset = v; }

    /// @brief Set the universal granule-key (weight).
    /// @param v Domain-specific scalar value.
    void set_weight( index_type v ) noexcept { weight = v; }

    /// @brief Установить высоту AVL-поддерева.
    /// @param v Новая высота (0 = не в дереве).
    void set_height( std::int16_t v ) noexcept { avl_height = v; }

    /// @brief Установить тип узла.
    /// @param v kNodeReadWrite (0) или kNodeReadOnly (1).
    void set_node_type( std::uint16_t v ) noexcept { node_type = v; }

  protected:
    /// Universal granule-key / granule-scalar. First field for cache-efficient access.
    /// Semantics determined by the owning forest domain:
    ///   - free-tree: state discriminator (0 = free); sort key derived from PAP geometry.
    ///   - user domains: domain-specific sort key (e.g., granule count, string index).
    index_type weight;
    /// Гранульный индекс левого дочернего узла AVL-дерева (или no_block).
    index_type left_offset;
    /// Гранульный индекс правого дочернего узла AVL-дерева (или no_block).
    index_type right_offset;
    /// Гранульный индекс родительского узла AVL-дерева (или no_block).
    index_type parent_offset;
    /// Owner-domain / owner-tree marker.
    /// 0 = node belongs to the free-tree domain (system/free_tree).
    /// own_idx = node is allocated; value equals the block's own granule index.
    index_type root_offset;
    /// Высота AVL-поддерева (0 = узел не в дереве).
    std::int16_t avl_height;
    /// Тип узла: kNodeReadWrite (0) = чтение/запись, kNodeReadOnly (1) = только чтение.
    std::uint16_t node_type;
};

// Layout: TreeNode is a standard-layout struct.
static_assert( std::is_standard_layout<pmm::TreeNode<pmm::DefaultAddressTraits>>::value,
               "TreeNode must be standard-layout " );

} // namespace pmm
