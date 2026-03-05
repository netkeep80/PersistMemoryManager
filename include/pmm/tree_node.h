/**
 * @file pmm/tree_node.h
 * @brief TreeNode<AddressTraits> — узел AVL-дерева для ПАП (Issue #87 Phase 2).
 *
 * Параметрический узел AVL-дерева, где тип индексных полей определяется
 * через `AddressTraits::index_type`.
 *
 * Обратная совместимость:
 *   `pmm::detail::BlockHeader` содержит поля `left_offset`, `right_offset`,
 *   `parent_offset`, `avl_height`, `_pad`, бинарно совместимые с
 *   `TreeNode<DefaultAddressTraits>`.
 *   Совместимость подтверждена через `static_assert` в `persist_memory_types.h`.
 *
 * @see plan_issue87.md §5 «Фаза 2: LinkedListNode и TreeNode»
 * @version 0.1 (Issue #87 Phase 2)
 */

#pragma once

#include "pmm/address_traits.h"

#include <cstdint>

namespace pmm
{

/**
 * @brief Узел AVL-дерева для адресного пространства ПАП.
 *
 * @tparam AddressTraitsT  Traits адресного пространства (из address_traits.h).
 *                         Определяет тип индексных полей ссылок на дочерние узлы и родителя.
 *
 * Поля хранят гранульные индексы узлов-потомков / родителя в ПАП.
 * Sentinel «нет узла» = `AddressTraitsT::no_block`.
 *
 * Поле `avl_height` хранит высоту AVL-поддерева (0 = узел не в дереве).
 * Поле `_pad` — выравнивающий паддинг; зарезервировано.
 *
 * Бинарная совместимость с `BlockHeader`:
 *   При `AddressTraitsT = DefaultAddressTraits` (uint32_t, 16):
 *     offsetof(left_offset)   == 0   (+12 в составе BlockHeader)
 *     offsetof(right_offset)  == 4   (+16 в составе BlockHeader)
 *     offsetof(parent_offset) == 8   (+20 в составе BlockHeader)
 *     offsetof(avl_height)    == 12  (+24 в составе BlockHeader)
 *     offsetof(_pad)          == 14  (+26 в составе BlockHeader)
 *     sizeof(TreeNode<DefaultAddressTraits>) == 16 bytes
 */
template <typename AddressTraitsT> struct TreeNode
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

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
};

} // namespace pmm
