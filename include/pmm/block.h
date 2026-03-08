/**
 * @file pmm/block.h
 * @brief Block<AddressTraits> — компактный заголовок блока памяти (Issue #136).
 *
 * Issue #136: перенос узла двухсвязного списка внутрь самого блока.
 *
 * В предыдущей архитектуре Block<A> был составным типом:
 *   LinkedListNode<A> (prev_offset + next_offset) + TreeNode<A> (weight + left + right +
 *   parent + root_offset + avl_height + node_type) = 32 байта (2 гранулы).
 *
 * Новая архитектура (Issue #136):
 *   Block<A> = LinkedListNode<A> (только next_offset, 4 байта) + TreeNode<A>
 *              (weight + root_offset + avl_height + node_type, 12 байт) = 16 байт (1 грануля).
 *
 * Поля `prev_offset`, `left_offset`, `right_offset`, `parent_offset` перемещены в
 * FreeBlockData<A> — структуру, хранящуюся в области данных свободного блока.
 *
 * Это уменьшает накладные расходы на каждый выделенный блок на 16 байт (1 гранулю):
 *   - Было: 32 байта заголовка (2 гранулы) + данные
 *   - Стало: 16 байт заголовка (1 грануля) + данные
 *
 * Раскладка полей при AddressTraitsT = DefaultAddressTraits (uint32_t, 16):
 *   [0..3]   LinkedListNode<A>: next_offset (4)
 *   [4..15]  TreeNode<A>:       weight (4), root_offset (4), avl_height (2), node_type (2)
 *
 * Свободный блок дополнительно хранит в своей области данных FreeBlockData<A>:
 *   [16..31] FreeBlockData<A>: prev_offset (4), left_offset (4), right_offset (4), parent_offset (4)
 *
 * Размер и выравнивание:
 *   sizeof(Block<DefaultAddressTraits>) == 16 байт (1 грануля по 16 байт).
 *   Подтверждено через static_assert в types.h.
 *
 * @see free_block_data.h — FreeBlockData<A> (хранится в области данных свободного блока)
 * @see plan_issue87.md §5 «Фаза 3: Block — блок как составной тип»
 * @version 0.5 (Issue #136 — block header reduced to 16 bytes, free block data embedded in data area)
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/free_block_data.h"
#include "pmm/linked_list_node.h"
#include "pmm/tree_node.h"

#include <cstdint>

namespace pmm
{

/**
 * @brief Компактный заголовок блока памяти ПАП (Issue #136).
 *
 * @tparam AddressTraitsT  Traits адресного пространства (из address_traits.h).
 *                         Определяет тип индексных полей.
 *
 * Наследует LinkedListNode<AddressTraitsT> (только next_offset) и TreeNode<AddressTraitsT>
 * (weight, root_offset, avl_height, node_type).
 *
 * Поля `prev_offset`, `left_offset`, `right_offset`, `parent_offset` хранятся в
 * FreeBlockData<A> в области данных свободного блока (по адресу блок + sizeof(Block<A>)).
 *
 * При AddressTraitsT = DefaultAddressTraits (uint32_t, 16):
 *   sizeof(Block<DefaultAddressTraits>) == 16 байт (1 грануля)
 *
 * Доступ к полям через наследование:
 *   - next_offset                          — через LinkedListNode<A>
 *   - weight, root_offset,
 *     avl_height, node_type               — через TreeNode<A>
 *   - prev_offset, left_offset,
 *     right_offset, parent_offset          — через FreeBlockData<A>
 *                                            (только для свободных блоков)
 */
template <typename AddressTraitsT> struct Block : LinkedListNode<AddressTraitsT>, TreeNode<AddressTraitsT>
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

    /**
     * @brief Получить доступ к данным свободного блока (FreeBlockData<A>).
     *
     * @warning Вызывать только для свободных блоков (weight == 0, root_offset == 0).
     *          Для занятых блоков эта область является пользовательскими данными.
     *
     * @return Ссылка на FreeBlockData<A>, хранящийся в области данных.
     */
    FreeBlockData<AddressTraitsT>& free_data() noexcept
    {
        return *reinterpret_cast<FreeBlockData<AddressTraitsT>*>( reinterpret_cast<std::uint8_t*>( this ) +
                                                                  sizeof( Block<AddressTraitsT> ) );
    }

    const FreeBlockData<AddressTraitsT>& free_data() const noexcept
    {
        return *reinterpret_cast<const FreeBlockData<AddressTraitsT>*>( reinterpret_cast<const std::uint8_t*>( this ) +
                                                                        sizeof( Block<AddressTraitsT> ) );
    }
};

} // namespace pmm
