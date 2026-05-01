# 10. Системные требования (`SYS`)

Системное требование — требование верхнего уровня к продукту, состоящему из подсистем или взаимодействующих компонентов.

Каждое требование оформлено как заголовок уровня `##` с идентификатором в формате `sys-xxx`.

## sys-001

- **Требование:** PMM должен выступать нижним storage-kernel слоем для систем, которым нужно persistent address space.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализует:** [br-001](01_business_requirements.md#br-001)
- **Реализуется в:**
  - [feat-001](04_features.md#feat-001), [sys-003](#sys-003), [sys-004](#sys-004)
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## sys-002

- **Требование:** Верхние слои должны отвечать за payload schema, business logic, query semantics и application format.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализует:** [rule-002](02_business_rules.md#rule-002), [rule-004](02_business_rules.md#rule-004)
- **Реализуется в:**
  - [con-006](09_constraints.md#con-006), [con-011](09_constraints.md#con-011), [asm-006](11_assumptions_dependencies.md#asm-006)

## sys-003

- **Требование:** PMM должен быть пригоден для heap-backed, static embedded и mmap/file-backed сценариев.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-006](04_features.md#feat-006), [ur-010](03_user_requirements.md#ur-010), [ur-011](03_user_requirements.md#ur-011)
- **Реализуется в:**
  - [if-005](07_external_interfaces.md#if-005)
  - [pmm-heapstorage](../include/pmm/heap_storage.h#pmm-heapstorage)
  - [pmm-staticstorage](../include/pmm/static_storage.h#pmm-staticstorage)
  - [pmm-mmapstorage](../include/pmm/mmap_storage.h#pmm-mmapstorage)

## sys-004

- **Требование:** PMM должен обеспечивать reusable substrate для intrusive persistent structures, включая free-tree, symbol/string intern forest и typed persistent maps.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [br-005](01_business_requirements.md#br-005)
- **Реализуется в:**
  - [feat-005](04_features.md#feat-005), [feat-008](04_features.md#feat-008)
  - [pmm-detail-avlupdateheightonly](../include/pmm/avl_tree_mixin.h#pmm-detail-avlupdateheightonly)
  - [pmm-detail-forestdomainregistry](../include/pmm/forest_registry.h#pmm-detail-forestdomainregistry)

## sys-005

- **Требование:** PMM должен позволять нескольким независимым persistent heaps сосуществовать в одном процессе без смешения типов указателей.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [con-005](09_constraints.md#con-005), [if-009](07_external_interfaces.md#if-009)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## sys-006

- **Требование:** PMM должен интегрироваться в CMake-based проекты как include-only библиотека без отдельной runtime-сборки ядра.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [con-003](09_constraints.md#con-003), [if-001](07_external_interfaces.md#if-001)
- **Реализуется в:**
  - [CMakeLists.txt](../CMakeLists.txt)

## sys-007

- **Требование:** PMM должен быть пригоден как слой для дальнейшего `pjson_db`, но не должен сам становиться database engine.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализует:** [br-001](01_business_requirements.md#br-001), [rule-002](02_business_rules.md#rule-002), [rule-004](02_business_rules.md#rule-004)
- **Реализуется в:**
  - [con-006](09_constraints.md#con-006), [con-011](09_constraints.md#con-011)
