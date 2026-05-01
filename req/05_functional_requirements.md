# 05. Функциональные требования (`FR`)

Функциональное требование описывает требуемое поведение системы при определенных условиях.

Каждое требование оформлено как заголовок уровня `##` с идентификатором в формате `fr-xxx`.

## fr-001

- **Требование:** Менеджер должен создавать PAP заданного размера через `create(std::size_t initial_size)`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [ur-001](03_user_requirements.md#ur-001), [feat-001](04_features.md#feat-001)
- **Реализуется в:**
  - [pmm-persistmemorymanager-create](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-create)
- **Проверяется в:**
  - [ac-001](12_acceptance_criteria.md#ac-001)
  - [tests/test_allocate.cpp](../tests/test_allocate.cpp)
  - [tests/test_block_modernization.cpp](../tests/test_block_modernization.cpp)

## fr-002

- **Требование:** Менеджер должен загружать существующий PAP через `load(VerifyResult&)`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [ur-001](03_user_requirements.md#ur-001), [ur-005](03_user_requirements.md#ur-005), [feat-001](04_features.md#feat-001)
- **Реализуется в:**
  - [pmm-persistmemorymanager-load](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-load)
- **Проверяется в:** [ac-006](12_acceptance_criteria.md#ac-006)

## fr-003

- **Требование:** Менеджер должен освобождать runtime-состояние через `destroy()`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [ur-001](03_user_requirements.md#ur-001), [feat-001](04_features.md#feat-001)
- **Реализуется в:**
  - [pmm-persistmemorymanager-destroy](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-destroy)

## fr-004

- **Требование:** Менеджер должен выделять память через `allocate(user_size)` и освобождать через `deallocate(ptr)`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [ur-002](03_user_requirements.md#ur-002), [feat-002](04_features.md#feat-002)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
- **Проверяется в:** [ac-003](12_acceptance_criteria.md#ac-003), [test_allocate.cpp](../tests/test_allocate.cpp), [test_deallocate.cpp](../tests/test_deallocate.cpp)

## fr-005

- **Требование:** Менеджер должен поддерживать `allocate_typed<T>`, `deallocate_typed<T>`, `create_typed<T>`, `destroy_typed<T>` и `reallocate_typed<T>`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [ur-002](03_user_requirements.md#ur-002), [feat-002](04_features.md#feat-002)
- **Реализуется в:**
  - [pmm-detail-persistmemorytypedapi-reallocate_typed](../include/pmm/typed_manager_api.h#pmm-detail-persistmemorytypedapi-reallocate_typed)
- **Проверяется в:** [ac-002](12_acceptance_criteria.md#ac-002)

## fr-006

- **Требование:** `create_typed` должен отказывать/не компилироваться для неподходящих типов согласно nothrow lifecycle constraints.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [rule-007](02_business_rules.md#rule-007)
- **Реализуется в:**
  - [pmm-detail-persistmemorytypedapi-reallocate_typed](../include/pmm/typed_manager_api.h#pmm-detail-persistmemorytypedapi-reallocate_typed)

## fr-007

- **Требование:** Менеджер должен разрешать `pptr<T>` в runtime-указатель через `resolve` и `resolve_at`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [ur-003](03_user_requirements.md#ur-003), [feat-003](04_features.md#feat-003), [rule-001](02_business_rules.md#rule-001)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
  - [pmm-pptr](../include/pmm/pptr.h#pmm-pptr)
- **Проверяется в:** [ac-002](12_acceptance_criteria.md#ac-002), [ac-004](12_acceptance_criteria.md#ac-004)

## fr-008

- **Требование:** Менеджер должен проверять валидность persistent pointer через `is_valid_ptr`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [feat-003](04_features.md#feat-003)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
- **Проверяется в:** [ac-002](12_acceptance_criteria.md#ac-002), [ac-010](12_acceptance_criteria.md#ac-010)

## fr-009

- **Требование:** Менеджер должен предоставлять обход всех блоков и свободных блоков через `for_each_block` и `for_each_free_block`.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README API
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## fr-010

- **Требование:** Менеджер должен возвращать статистику: total, used, free size, block count, free block count, allocated block count.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README API
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## fr-011

- **Требование:** Менеджер должен поддерживать legacy root pointer: `set_root`, `get_root`.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [ur-007](03_user_requirements.md#ur-007), [feat-005](04_features.md#feat-005)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## fr-012

- **Требование:** Менеджер должен поддерживать named domains: `register_domain`, `register_system_domain`, `has_domain`, `get_domain_root`, `set_domain_root`.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [ur-007](03_user_requirements.md#ur-007), [feat-005](04_features.md#feat-005), [rule-008](02_business_rules.md#rule-008)
- **Реализуется в:**
  - [pmm-detail-forestdomainregistry](../include/pmm/forest_registry.h#pmm-detail-forestdomainregistry)
  - [forest_domain_mixin.inc](../include/pmm/forest_domain_mixin.inc)
- **Проверяется в:** [test_forest_registry.cpp](../tests/test_forest_registry.cpp)

## fr-013

- **Требование:** При нехватке памяти backend с ростом должен расширять буфер, копировать старое содержимое, добавлять/расширять free block и обновлять singleton pointer.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-002](04_features.md#feat-002)
- **Реализуется в:**
  - [pmm-heapstorage](../include/pmm/heap_storage.h#pmm-heapstorage)
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)

## fr-014

- **Требование:** При `load()` библиотека должна валидировать magic, image version, total size и granule size, затем восстанавливать linked list, counters и free tree.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [br-003](01_business_requirements.md#br-003), [feat-004](04_features.md#feat-004), [ur-005](03_user_requirements.md#ur-005)
- **Реализуется в:**
  - [pmm-persistmemorymanager-load](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-load)
- **Проверяется в:** [ac-006](12_acceptance_criteria.md#ac-006)

## fr-015

- **Требование:** Менеджер должен предоставлять `last_error()` и `clear_error()` для диагностики последней ошибки.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README API
- **Реализует:** [feat-004](04_features.md#feat-004), [feat-010](04_features.md#feat-010)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
  - [pmm-verifyresult](../include/pmm/diagnostics.h#pmm-verifyresult)

## fr-016

- **Требование:** Файловые helper-функции должны сохранять и загружать image с CRC/diagnostics через `pmm/io.h`.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README save/load section
- **Реализует:** [ur-011](03_user_requirements.md#ur-011)
- **Реализуется в:**
  - [io.h](../include/pmm/io.h)

## fr-017

- **Требование:** Persistent string interning должен возвращать один и тот же `pstringview` для одинакового содержимого, где это поддерживается.
- **Приоритет:** Could
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-008](04_features.md#feat-008)
- **Реализуется в:**
  - [pmm-pstringview](../include/pmm/pstringview.h#pmm-pstringview)

## fr-018

- **Требование:** `pmap` должен хранить AVL root в type-scoped forest domain, а не в transient runtime state.
- **Приоритет:** Could
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-008](04_features.md#feat-008), [rule-008](02_business_rules.md#rule-008)
- **Реализуется в:**
  - [pmm-pmap](../include/pmm/pmap.h#pmm-pmap)
  - [pmm-detail-forestdomainregistry](../include/pmm/forest_registry.h#pmm-detail-forestdomainregistry)

## fr-019

- **Требование:** `make_guard` должен обеспечивать безопасную очистку typed objects с `free_data()` или `free_all()` перед `destroy_typed()`.
- **Приоритет:** Could
- **Статус:** Recovered
- **Основание:** README API
- **Реализуется в:**
  - [typed_guard.h](../include/pmm/typed_guard.h)

## fr-020

- **Требование:** Конвертация байтов в гранулы должна быть проверяемой и не должна кодировать переполнение значением `0`.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #373
- **Основание:** `pmm/arena_internals.h`
- **Реализуется в:**
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)

## fr-021

- **Требование:** Публичный API `allocate(0)` должен отклоняться с `PmmError::InvalidSize`; `allocate(overflowing_size)` — с `PmmError::Overflow`.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #373
- **Основание:** `pmm/persist_memory_manager.h`
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## fr-022

- **Требование:** Путь аллокации должен вычислять `data_gran` ровно один раз через `bytes_to_granules_checked` до выбора свободного блока.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #373
- **Основание:** `pmm/persist_memory_manager.h`
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## fr-023

- **Требование:** Все проверки диапазонов арены должны быть overflow-safe (через `fits_range` / `checked_add` / `checked_mul`).
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #373
- **Основание:** `pmm/arena_internals.h`
- **Реализуется в:**
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)

## fr-024

- **Требование:** Внутренние операции над физической ареной должны принимать `ArenaView<AT>` или эквивалентную сильно-парную пару `base+header`.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #373
- **Основание:** `pmm/arena_internals.h`
- **Реализуется в:**
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)

## fr-025

- **Требование:** Верификация и repair физической цепочки блоков должны завершаться даже на повреждённых образах (детектор циклов).
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #373
- **Основание:** `pmm/arena_internals.h`
- **Реализует:** [feat-004](04_features.md#feat-004)
- **Реализуется в:**
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)

## fr-026

- **Требование:** Инициализация менеджера должна быть транзакционной: при сбое `create()` `_initialized` должен оставаться `false`.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #373
- **Основание:** `pmm/persist_memory_manager.h`
- **Реализуется в:**
  - [pmm-persistmemorymanager-create](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-create)

## fr-027

- **Требование:** Расширение хранилища должно использовать единую проверяемую growth-policy, учитывающую grow ratio и max-memory лимит.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #373
- **Основание:** `pmm/arena_internals.h`, `pmm/layout.h`
- **Реализуется в:**
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)
  - [layout.h](../include/pmm/layout.h)

## fr-028

- **Требование:** Heap backend должен обеспечивать выравнивание базы по `AT::granule_size` (для `LargeAddressTraits` granule = 64).
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #373
- **Основание:** `pmm/heap_storage.h`
- **Реализуется в:**
  - [pmm-heapstorage](../include/pmm/heap_storage.h#pmm-heapstorage)

## fr-029

- **Требование:** Typed allocation должна допускать только типы, выравнивание которых представимо granule_size.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #373
- **Основание:** `pmm/typed_manager_api.h`
- **Реализуется в:**
  - [pmm-detail-persistmemorytypedapi-reallocate_typed](../include/pmm/typed_manager_api.h#pmm-detail-persistmemorytypedapi-reallocate_typed)

## fr-030

- **Требование:** Все pptr/raw pointer/block index конверсии должны проходить через канонический address layer (`detail::ArenaAddress<AT>`/`ConstArenaAddress<AT>`); невалидные конверсии возвращают `nullptr`/`std::nullopt`, никогда — sentinel-объект.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #375
- **Основание:** `pmm/arena_internals.h`
- **Реализует:** [rule-001](02_business_rules.md#rule-001)
- **Реализуется в:**
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)

## fr-031

- **Требование:** `parray::ensure_capacity` и `pstring::ensure_capacity` должны расти через `reallocate_typed`; ручной путь `allocate -> memcpy -> deallocate` запрещён.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #375
- **Основание:** `pmm/parray.h`, `pmm/pstring.h`
- **Реализует:** [feat-008](04_features.md#feat-008)
- **Реализуется в:**
  - [pmm-parray](../include/pmm/parray.h#pmm-parray)
  - [pmm-pstring](../include/pmm/pstring.h#pmm-pstring)

## fr-032

- **Требование:** Доступ к AVL/tree header через `pptr<T>` должен явно различать checked (`ManagerT::try_tree_node` → `BlockHeader*`/`nullptr` + `_last_error`) и unchecked (`ManagerT::tree_node_unchecked` → `BlockHeader&`, precondition); ref-returning `tree_node(pptr)` с sentinel-объектом удалён.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #375
- **Основание:** `pmm/persist_memory_manager.h`, `pmm/pptr.h`
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
  - [pmm-pptr](../include/pmm/pptr.h#pmm-pptr)

## fr-033

- **Требование:** Null-конвенции явные: user-index `0` = persistent null; `AT::no_block` = physical block-list null; helpers не должны их молча конфлатить.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #375
- **Основание:** `pmm/arena_internals.h`
- **Реализуется в:**
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)

## fr-034

- **Требование:** Production source LOC ([single_include/pmm/pmm.h](../single_include/pmm/pmm.h)) не должен расти выше зафиксированного baseline; CI скрипт [scripts/check-source-loc-budget.sh](../scripts/check-source-loc-budget.sh) валит PR при превышении.
- **Приоритет:** Must
- **Статус:** Active
- **Tracking issue:** #375
- **Основание:** [scripts/source-loc-baseline.txt](../scripts/source-loc-baseline.txt)
- **Реализует:** [rule-005](02_business_rules.md#rule-005), [con-010](09_constraints.md#con-010)
- **Реализуется в:**
  - [scripts/check-source-loc-budget.sh](../scripts/check-source-loc-budget.sh), [scripts/source-loc-baseline.txt](../scripts/source-loc-baseline.txt)
