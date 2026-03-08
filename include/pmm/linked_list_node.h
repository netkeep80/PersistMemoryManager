/**
 * @file pmm/linked_list_node.h
 * @brief LinkedListNode<AddressTraits> — узел двухсвязного списка для ПАП (Issue #87 Phase 2).
 *
 * Параметрический узел двухсвязного списка, где тип индексных полей определяется
 * через `AddressTraits::index_type`.
 *
 * Issue #136: prev_offset вынесен из заголовка блока в область данных свободного блока
 * (см. FreeBlockData<A> в free_block_data.h). В заголовке Block<A> остаётся только
 * next_offset. Это позволяет уменьшить заголовок блока с 32 до 16 байт.
 *
 * Поля:
 *   `next_offset` — гранульный индекс следующего блока.
 *   Совместимость с Block<DefaultAddressTraits> подтверждена через
 *   `static_assert` в `types.h`.
 *
 * @see plan_issue87.md §5 «Фаза 2: LinkedListNode и TreeNode»
 * @see free_block_data.h — FreeBlockData (Issue #136: prev_offset и AVL-ссылки в области данных)
 * @version 0.2 (Issue #136 — prev_offset removed from header, moved to FreeBlockData)
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
 *                         Определяет тип индексного поля `next_offset`.
 *
 * Issue #136: поле `prev_offset` перемещено в `FreeBlockData<A>` (область данных свободного блока).
 * В заголовке хранится только `next_offset` — индекс следующего блока,
 * необходимый для вычисления размера блока у ВСЕХ блоков (свободных и занятых).
 *
 * `prev_offset` не персистируется (восстанавливается при load() через repair_linked_list()).
 * Для свободных блоков он доступен через FreeBlockData<A>.
 * Sentinel «нет соседа» = `AddressTraitsT::no_block`.
 */
template <typename AddressTraitsT> struct LinkedListNode
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

  protected:
    /// Гранульный индекс следующего блока (или no_block).
    /// Issue #136: prev_offset перемещён в FreeBlockData<A>.
    index_type next_offset;
};

// Layout: LinkedListNode is a standard-layout struct with exactly 1 index_type field.
// next_offset at byte 0.
static_assert( std::is_standard_layout<pmm::LinkedListNode<pmm::DefaultAddressTraits>>::value,
               "LinkedListNode must be standard-layout (Issue #87)" );

} // namespace pmm
