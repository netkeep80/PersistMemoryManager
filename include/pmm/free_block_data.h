/**
 * @file pmm/free_block_data.h
 * @brief FreeBlockData<AddressTraits> — данные свободного блока, вложенные в область данных (Issue #136).
 *
 * Реализует идею переноса узла двухсвязного списка внутрь самого блока (Issue #136).
 *
 * Проблема:
 *   В предыдущей архитектуре Block<A> включал LinkedListNode<A> (8 байт: prev_offset + next_offset)
 *   как часть заголовка. Это означало, что каждый выделенный блок имел 32 байта накладных расходов
 *   (2 гранулы по 16 байт), хотя allocated-блоку нужны лишь: next_offset, weight, root_offset,
 *   avl_height и node_type (16 байт = 1 грануля).
 *
 * Решение (Issue #136):
 *   Перенести prev_offset, left_offset, right_offset, parent_offset из заголовка блока
 *   в область данных свободного блока. Это позволяет:
 *     - Уменьшить заголовок блока с 32 до 16 байт (с 2 до 1 гранулы)
 *     - Сохранить накладные расходы у свободных блоков (их область данных используется
 *       для хранения FreeBlockData)
 *     - Уменьшить накладные расходы на каждый выделенный блок на 16 байт (1 гранулу)
 *
 * Раскладка памяти (DefaultAddressTraits):
 *
 *   Выделенный блок:
 *     [0..15]  Block<A>: next_offset(4), weight(4), root_offset(4), avl_height(2), node_type(2)
 *     [16..]   Пользовательские данные
 *
 *   Свободный блок (weight==0, root_offset==0):
 *     [0..15]  Block<A>: next_offset(4), weight(4)=0, root_offset(4)=0, avl_height(2), node_type(2)
 *     [16..31] FreeBlockData<A>: prev_offset(4), left_offset(4), right_offset(4), parent_offset(4)
 *     [32..]   Не используется (или дополнительные пользовательские данные при освобождении)
 *
 * Гарантии безопасности:
 *   - FreeBlockData доступна только для блоков с weight==0 (свободных)
 *   - Для выделенных блоков область данных не затрагивается
 *   - prev_offset не персистируется (восстанавливается при load())
 *   - Минимальный размер блока: sizeof(Block<A>) + sizeof(FreeBlockData<A>) =
 *     16 + 16 = 32 байта (2 гранулы) — такой же, как и раньше
 *
 * @see plan_issue136.md «Перенос узла двухсвязного списка внутрь блока»
 * @version 0.1 (Issue #136 — embedded linked list node in free block data area)
 */

#pragma once

#include "pmm/address_traits.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pmm
{

/**
 * @brief Данные свободного блока, вложенные в его область данных (Issue #136).
 *
 * Эта структура хранится по адресу `base + blk_idx * granule_size + sizeof(Block<A>)`
 * только для свободных блоков (weight == 0, root_offset == 0).
 *
 * Поля:
 *   - `prev_offset`    — гранульный индекс предыдущего блока в двухсвязном списке
 *   - `left_offset`    — гранульный индекс левого потомка в AVL-дереве
 *   - `right_offset`   — гранульный индекс правого потомка в AVL-дереве
 *   - `parent_offset`  — гранульный индекс родителя в AVL-дереве
 *
 * @tparam AddressTraitsT  Traits адресного пространства (из address_traits.h).
 */
template <typename AddressTraitsT> struct FreeBlockData
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

    /// Гранульный индекс предыдущего блока в двухсвязном списке (или no_block).
    index_type prev_offset;
    /// Гранульный индекс левого дочернего узла AVL-дерева (или no_block).
    index_type left_offset;
    /// Гранульный индекс правого дочернего узла AVL-дерева (или no_block).
    index_type right_offset;
    /// Гранульный индекс родительского узла AVL-дерева (или no_block).
    index_type parent_offset;
};

// Layout: FreeBlockData is a standard-layout struct with exactly 4 index_type fields.
static_assert( std::is_standard_layout<pmm::FreeBlockData<pmm::DefaultAddressTraits>>::value,
               "FreeBlockData must be standard-layout (Issue #136)" );

} // namespace pmm
