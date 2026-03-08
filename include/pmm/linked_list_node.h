/**
 * @file pmm/linked_list_node.h
 * @brief LinkedListNode<AddressTraits> — узел двухсвязного списка для ПАП (Issue #87 Phase 2).
 *
 * Параметрический узел двухсвязного списка, где тип индексных полей определяется
 * через `AddressTraits::index_type`.
 *
 * Поля:
 *   `prev_offset` и `next_offset` — гранульные индексы соседних блоков.
 *   Совместимость с Block<DefaultAddressTraits> подтверждена через
 *   `static_assert` в `types.h`.
 *
 * @see plan_issue87.md §5 «Фаза 2: LinkedListNode и TreeNode»
 * @version 0.1 (Issue #87 Phase 2)
 */

#pragma once

#include "pmm/address_traits.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

/**
 * @brief Узел двухсвязного списка для адресного пространства ПАП, использование только через наследование.
 *
 * @tparam AddressTraitsT  Traits адресного пространства (из address_traits.h).
 *                         Определяет тип индексных полей `prev_offset` / `next_offset`.
 *
 * Поля хранят гранульные индексы блоков-соседей в ПАП.
 * Sentinel «нет соседа» = `AddressTraitsT::no_block`.
 */
template <typename AddressTraitsT> struct LinkedListNode
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

  protected:
    /// Гранульный индекс предыдущего блока (или no_block).
    index_type prev_offset;
    /// Гранульный индекс следующего блока (или no_block).
    index_type next_offset;
};

// Layout: LinkedListNode is a standard-layout struct with exactly 2 index_type fields.
// prev_offset at byte 0, next_offset at byte sizeof(index_type).
static_assert( std::is_standard_layout<pmm::LinkedListNode<pmm::DefaultAddressTraits>>::value,
               "LinkedListNode must be standard-layout (Issue #87)" );

} // namespace pmm
