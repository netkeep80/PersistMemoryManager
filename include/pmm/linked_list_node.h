/**
 * @file pmm/linked_list_node.h
 * @brief LinkedListNode<AddressTraits> — узел двухсвязного списка для ПАП (Issue #87 Phase 2).
 *
 * Параметрический узел двухсвязного списка, где тип индексных полей определяется
 * через `AddressTraits::index_type`.
 *
 * Обратная совместимость:
 *   `pmm::detail::BlockHeader` содержит поля `prev_offset` и `next_offset`,
 *   бинарно совместимые с `LinkedListNode<DefaultAddressTraits>`.
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
 * @brief Узел двухсвязного списка для адресного пространства ПАП.
 *
 * @tparam AddressTraitsT  Traits адресного пространства (из address_traits.h).
 *                         Определяет тип индексных полей `prev_offset` / `next_offset`.
 *
 * Поля хранят гранульные индексы блоков-соседей в ПАП.
 * Sentinel «нет соседа» = `AddressTraitsT::no_block`.
 */
template <typename AddressTraitsT>
struct LinkedListNode
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

    /// Гранульный индекс предыдущего блока (или no_block).
    index_type prev_offset;
    /// Гранульный индекс следующего блока (или no_block).
    index_type next_offset;
};

} // namespace pmm
