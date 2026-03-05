/**
 * @file pmm/free_block_tree.h
 * @brief FreeBlockTree — политика дерева свободных блоков (Issue #87 Phase 4).
 *
 * Определяет концепт (C++17 SFINAE) политики дерева свободных блоков
 * и предоставляет стандартную реализацию `AvlFreeTree<AddressTraits>`,
 * обёртывающую `PersistentAvlTree`.
 *
 * Концепт `FreeBlockTreePolicy<Policy>` требует трёх статических методов:
 *   - `insert(base, hdr, blk_idx)`      — добавить блок в дерево
 *   - `remove(base, hdr, blk_idx)`      — удалить блок из дерева
 *   - `find_best_fit(base, hdr, needed)` — найти наименьший подходящий блок
 *
 * @see persist_avl_tree.h — текущая реализация AVL-дерева
 * @see plan_issue87.md §5 «Фаза 4: FreeBlockTree как шаблонная политика»
 * @version 0.1 (Issue #87 Phase 4)
 */

#pragma once

#include "persist_memory_types.h"
#include "persist_avl_tree.h"

#include <cstdint>
#include <type_traits>

namespace pmm
{

// ─── Вспомогательные утилиты для проверки концепта ────────────────────────────

namespace detail
{

/// @brief SFINAE-проверка наличия Policy::insert(uint8_t*, ManagerHeader*, uint32_t).
template <typename Policy, typename = void> struct has_insert : std::false_type
{
};
template <typename Policy>
struct has_insert<Policy, std::void_t<decltype( Policy::insert( std::declval<std::uint8_t*>(),
                                                                std::declval<pmm::detail::ManagerHeader*>(),
                                                                std::declval<std::uint32_t>() ) )>> : std::true_type
{
};

/// @brief SFINAE-проверка наличия Policy::remove(uint8_t*, ManagerHeader*, uint32_t).
template <typename Policy, typename = void> struct has_remove : std::false_type
{
};
template <typename Policy>
struct has_remove<Policy, std::void_t<decltype( Policy::remove( std::declval<std::uint8_t*>(),
                                                                std::declval<pmm::detail::ManagerHeader*>(),
                                                                std::declval<std::uint32_t>() ) )>> : std::true_type
{
};

/// @brief SFINAE-проверка наличия Policy::find_best_fit(uint8_t*, ManagerHeader*, uint32_t).
template <typename Policy, typename = void> struct has_find_best_fit : std::false_type
{
};
template <typename Policy>
struct has_find_best_fit<Policy, std::void_t<decltype( Policy::find_best_fit(
                                     std::declval<std::uint8_t*>(), std::declval<pmm::detail::ManagerHeader*>(),
                                     std::declval<std::uint32_t>() ) )>> : std::true_type
{
};

} // namespace detail

/**
 * @brief Проверка, является ли Policy корректной политикой дерева свободных блоков.
 *
 * Политика должна предоставлять три статических метода:
 *   - `insert(uint8_t* base, ManagerHeader* hdr, uint32_t blk_idx)   -> void`
 *   - `remove(uint8_t* base, ManagerHeader* hdr, uint32_t blk_idx)   -> void`
 *   - `find_best_fit(uint8_t* base, ManagerHeader* hdr, uint32_t n)  -> uint32_t`
 *
 * Используется как тип-концепт (C++17 variable template).
 *
 * @tparam Policy  Тип политики, проверяемой на соответствие.
 */
template <typename Policy>
inline constexpr bool is_free_block_tree_policy_v =
    detail::has_insert<Policy>::value && detail::has_remove<Policy>::value && detail::has_find_best_fit<Policy>::value;

/**
 * @brief Стандартная реализация политики дерева свободных блоков через AVL-дерево.
 *
 * Делегирует все вызовы `PersistentAvlTree` из `persist_avl_tree.h`.
 * Совместима с `DefaultAddressTraits` (uint32_t, 16 байт гранула).
 *
 * Шаблонный параметр `AddressTraitsT` в данный момент не используется
 * в логике (поскольку `PersistentAvlTree` работает с фиксированным `uint32_t`),
 * но включён для единообразия интерфейса с будущими обобщёнными реализациями.
 *
 * @tparam AddressTraitsT  Traits адресного пространства (совместим с DefaultAddressTraits).
 */
template <typename AddressTraitsT = DefaultAddressTraits> struct AvlFreeTree
{
    using address_traits = AddressTraitsT;
    using index_type     = typename AddressTraitsT::index_type;

    /// @brief Вставить блок `blk_idx` в AVL-дерево свободных блоков.
    static void insert( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t blk_idx )
    {
        PersistentAvlTree::insert( base, hdr, blk_idx );
    }

    /// @brief Удалить блок `blk_idx` из AVL-дерева свободных блоков.
    static void remove( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t blk_idx )
    {
        PersistentAvlTree::remove( base, hdr, blk_idx );
    }

    /// @brief Найти наименьший свободный блок >= `needed_granules` (best-fit, O(log n)).
    /// @return Гранульный индекс найденного блока или `kNoBlock`.
    static std::uint32_t find_best_fit( std::uint8_t* base, detail::ManagerHeader* hdr, std::uint32_t needed_granules )
    {
        return PersistentAvlTree::find_best_fit( base, hdr, needed_granules );
    }
};

// ─── Static_assert: AvlFreeTree соответствует концепту ────────────────────────

static_assert( is_free_block_tree_policy_v<AvlFreeTree<DefaultAddressTraits>>,
               "AvlFreeTree<DefaultAddressTraits> must satisfy FreeBlockTreePolicy" );

} // namespace pmm
