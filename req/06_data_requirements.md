# 06. Требования к данным (`DR`)

Требования к данным описывают логическую структуру, хранение, целостность, получение и утилизацию данных.

Каждое требование оформлено как заголовок уровня `##` с идентификатором в формате `dr-xxx`.

## dr-001

- **Требование:** Управляемая память должна быть линейной областью, разбитой на блоки.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)
  - [pmm-block](../include/pmm/block.h#pmm-block)
  - [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)

## dr-002

- **Требование:** `ManagerHeader` должен храниться в пользовательской области `Block_0`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
  - [layout.h](../include/pmm/layout.h)
- **Проверяется в:** [ac-001](12_acceptance_criteria.md#ac-001)

## dr-003

- **Требование:** `ManagerHeader` должен содержать magic, total size, used size, block counters, free-tree root, image version, granule size, CRC32 и root offset.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [layout.h](../include/pmm/layout.h)
- **Проверяется в:** [ac-001](12_acceptance_criteria.md#ac-001)

## dr-004

- **Требование:** Каждый `BlockHeader<AT>` должен содержать в фиксированном порядке: `weight`, `left_offset`, `right_offset`, `parent_offset`, `root_offset`, `prev_offset`, `next_offset`, затем компактные `avl_height` (`std::uint8_t`) и `node_type` (`enum class NodeType : std::uint8_t`) в самом конце заголовка.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader), [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)
- **Проверяется в:** [test_block_header.cpp](../tests/test_block_header.cpp), [test_block_modernization.cpp](../tests/test_block_modernization.cpp)

## dr-005

- **Требование:** Состояние блока определяется исключительно полем `node_type`. Свободный блок имеет `node_type == NodeType::Free`; никакие косвенные проверки вида `weight == 0` использоваться не должны. Для свободного блока `root_offset == 0`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader), [pmm-blockstatebase](../include/pmm/block_state.h#pmm-blockstatebase)
- **Реализует:** [feat-002](04_features.md#feat-002)
- **Реализуется в:**
  - [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)
  - [pmm-blockstatebase](../include/pmm/block_state.h#pmm-blockstatebase)
- **Проверяется в:** [ac-003](12_acceptance_criteria.md#ac-003)

## dr-006

- **Требование:** Выделенный блок должен иметь `node_type` из множества `is_allocated(NodeType)` и `root_offset == own_granule_index`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader), [pmm-blockstatebase](../include/pmm/block_state.h#pmm-blockstatebase)
- **Реализует:** [feat-002](04_features.md#feat-002)
- **Реализуется в:**
  - [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)
  - [pmm-blockstatebase](../include/pmm/block_state.h#pmm-blockstatebase)

## dr-007

- **Требование:** `pptr<T>` должен хранить granule index размером 1/2/4/8 байт в зависимости от address traits.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-003](04_features.md#feat-003), [ur-003](03_user_requirements.md#ur-003)
- **Реализуется в:**
  - [pmm-pptr](../include/pmm/pptr.h#pmm-pptr)
  - [pmm-addresstraits](../include/pmm/address_traits.h#pmm-addresstraits)
- **Проверяется в:** [ac-004](12_acceptance_criteria.md#ac-004)

## dr-008

- **Требование:** Image version должен поддерживать legacy migration `0 → 1`; неподдерживаемая версия должна приводить к ошибке unsupported format.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
  - [layout.h](../include/pmm/layout.h)
- **Проверяется в:** [ac-007](12_acceptance_criteria.md#ac-007)

## dr-009

- **Требование:** Persistent container nodes должны храниться в PAP-блоках и использовать встроенные `TreeNode` fields как AVL links.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-005](04_features.md#feat-005), [feat-008](04_features.md#feat-008)
- **Реализуется в:**
  - [pmm-detail-avlupdateheightonly](../include/pmm/avl_tree_mixin.h#pmm-detail-avlupdateheightonly)
  - [pmm-avlfreetree](../include/pmm/free_block_tree.h#pmm-avlfreetree)

## dr-010

- **Требование:** `pmap` type identity должен быть стабильным и не зависеть от compiler-specific spellings вроде `__PRETTY_FUNCTION__`.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [pmm-pmap](../include/pmm/pmap.h#pmm-pmap)

## dr-011

- **Требование:** Для absolute control over persistent type identity должна быть предусмотрена специализация `pmap_type_identity<T>` с фиксированным ASCII-tag.
- **Приоритет:** Could
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [asm-005](11_assumptions_dependencies.md#asm-005)
- **Реализуется в:**
  - [pmm-pmap](../include/pmm/pmap.h#pmm-pmap)

## dr-012

- **Требование:** File image должен включать достаточно metadata для проверки granule size, total size и layout compatibility при загрузке.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [layout.h](../include/pmm/layout.h)
  - [io.h](../include/pmm/io.h)
- **Проверяется в:** [ac-007](12_acceptance_criteria.md#ac-007)

## dr-013

- **Требование:** `weight` интерпретируется как кэш размера блока в гранулах. Для свободного блока — полный размер блока (header + payload). Для выделенного блока — размер payload (без header). `weight` обновляется синхронно во всех split/coalesce/extend/shrink/free/allocate путях.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-allocatorpolicy](../include/pmm/allocator_policy.h#pmm-allocatorpolicy), [pmm-avlfreetree](../include/pmm/free_block_tree.h#pmm-avlfreetree)
- **Реализует:** [feat-002](04_features.md#feat-002)
- **Реализуется в:**
  - [pmm-allocatorpolicy](../include/pmm/allocator_policy.h#pmm-allocatorpolicy)
  - [pmm-avlfreetree](../include/pmm/free_block_tree.h#pmm-avlfreetree)

## dr-014

- **Требование:** `avl_height` — компактное `std::uint8_t` поле; `avl_height == 0` означает, что блок не находится в AVL free-tree.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader), [pmm-blockstatebase](../include/pmm/block_state.h#pmm-blockstatebase)
- **Реализуется в:**
  - [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)
  - [pmm-blockstatebase](../include/pmm/block_state.h#pmm-blockstatebase)

## dr-015

- **Требование:** `NodeType` — `enum class : std::uint8_t`, перечисляющий все физические/логические типы узлов: `Free`, `ManagerHeader`, `Generic`, `ReadOnlyLocked`, `PStringView`, `PString`, `PArray`, `PMap`, `PPtr`. Перечисление расширяемо: добавление нового persistent object type требует только регистрации его свойств в `is_*`-хелперах.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)
- **Реализуется в:**
  - [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)

## dr-016

- **Требование:** Свойства узла (свободный/выделенный, изменяемый, удаляемый из ПАП, участвующий в AVL free-tree) выводятся централизованно из `NodeType` через `is_free`, `is_allocated`, `is_mutable`, `can_be_deleted_from_pap`, `participates_in_free_tree`. Allocator и free-tree обязаны использовать только эти хелперы.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)
- **Реализуется в:**
  - [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)

## dr-017

- **Требование:** AVL free-tree обязан использовать `weight` как ключ размера блока в нормальном пути (insert/remove/find_best_fit/ordering invariant). Вычисление размера через `next_offset - own_idx` в нормальном пути недопустимо.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-avlfreetree](../include/pmm/free_block_tree.h#pmm-avlfreetree)
- **Реализует:** [feat-002](04_features.md#feat-002)
- **Реализуется в:**
  - [pmm-avlfreetree](../include/pmm/free_block_tree.h#pmm-avlfreetree)

## dr-018

- **Требование:** `is_allocated(NodeType)` должен быть closed-world `switch`-ем по всем известным значениям enum-а. Неизвестное значение `node_type` (повреждённый байт) не должно трактоваться как allocated. `NodeType::Free` не является user/PAP-deletable; путь `deallocate()` обязан проверять `is_allocated(nt) && can_be_deleted_from_pap(nt)`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader), [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
- **Реализуется в:**
  - [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## dr-019

- **Требование:** Typed allocation paths (`allocate_typed<T>`, `create_typed<T>`, `reallocate_typed<T>`) обязаны проставлять блоку logical `NodeType` через `node_type_for<T>::value`. Регистрация нового persistent object type выполняется только специализацией `node_type_for<T>` без изменения базовой логики allocator-а.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-detail-persistmemorytypedapi-reallocate_typed](../include/pmm/typed_manager_api.h#pmm-detail-persistmemorytypedapi-reallocate_typed), [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)
- **Реализуется в:**
  - [pmm-detail-persistmemorytypedapi-reallocate_typed](../include/pmm/typed_manager_api.h#pmm-detail-persistmemorytypedapi-reallocate_typed)
  - [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader)

## dr-020

- **Требование:** Verify-режим обязан проверять, что для свободного блока кэшированный `weight` совпадает с физическим span-ом блока, вычисленным по соседям (`next_offset - own_idx` или `total_granules - own_idx`). Расхождение должно классифицироваться как `BlockStateInconsistent`. Для использования в validation/repair предусмотрены отдельные хелперы `physical_block_total_granules` и `cached_block_total_granules`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [pmm-allocatorpolicy](../include/pmm/allocator_policy.h#pmm-allocatorpolicy), [pmm-pmmerror](../include/pmm/types.h#pmm-pmmerror)
- **Реализует:** [feat-004](04_features.md#feat-004)
- **Реализуется в:**
  - [pmm-allocatorpolicy](../include/pmm/allocator_policy.h#pmm-allocatorpolicy)

### Свойства `NodeType`

| NodeType        | Free | Allocated | Mutable | Deletable from PAP | In AVL free-tree |
|-----------------|:----:|:---------:|:-------:|:------------------:|:----------------:|
| Free            |  Y   |     N     |    Y    |         N          |        Y         |
| ManagerHeader   |  N   |     Y     |    Y    |         N          |        N         |
| Generic         |  N   |     Y     |    Y    |         Y          |        N         |
| ReadOnlyLocked  |  N   |     Y     |    N    |         N          |        N         |
| PStringView     |  N   |     Y     |    N    |         Y          |        N         |
| PString         |  N   |     Y     |    Y    |         Y          |        N         |
| PArray          |  N   |     Y     |    Y    |         Y          |        N         |
| PMap            |  N   |     Y     |    Y    |         Y          |        N         |
| PPtr            |  N   |     Y     |    Y    |         Y          |        N         |
