# 07. Требования к внешним интерфейсам (`IF`)

Внешний интерфейс описывает взаимодействие между ПО и пользователем, другой программной системой или устройством.

Каждое требование оформлено как заголовок уровня `##` с идентификатором в формате `if-xxx`.

## if-001

- **Требование:** Основной внешний интерфейс должен быть C++20 header-only API через `include/pmm/`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, repository structure
- **Реализуется в:**
  - [con-001](09_constraints.md#con-001), [con-003](09_constraints.md#con-003)
  - [include/pmm/](../include/pmm/)

## if-002

- **Требование:** Библиотека должна предоставлять single-header варианты в [single_include/pmm/](../single_include/pmm/).
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [feat-009](04_features.md#feat-009)
- **Реализуется в:**
  - [single_include/pmm/pmm.h](../single_include/pmm/pmm.h)
  - [con-010](09_constraints.md#con-010)

## if-003

- **Требование:** Файловые helper-функции должны находиться в `pmm/io.h`.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [ur-011](03_user_requirements.md#ur-011)
- **Реализуется в:**
  - [io.h](../include/pmm/io.h)

## if-004

- **Требование:** Сборка и проверки должны поддерживаться через CMake/CTest.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [ur-009](03_user_requirements.md#ur-009)
- **Реализуется в:**
  - [CMakeLists.txt](../CMakeLists.txt), [tests/CMakeLists.txt](../tests/CMakeLists.txt)
- **Проверяется в:** [ac-012](12_acceptance_criteria.md#ac-012)

## if-005

- **Требование:** Storage backend interface должен позволять HeapStorage, StaticStorage и MMapStorage.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-006](04_features.md#feat-006), [ur-010](03_user_requirements.md#ur-010), [ur-011](03_user_requirements.md#ur-011)
- **Реализуется в:**
  - [storage_backend.h](../include/pmm/storage_backend.h)
  - [pmm-heapstorage](../include/pmm/heap_storage.h#pmm-heapstorage)
  - [pmm-staticstorage](../include/pmm/static_storage.h#pmm-staticstorage)
  - [pmm-mmapstorage](../include/pmm/mmap_storage.h#pmm-mmapstorage)

## if-006

- **Требование:** Конфигурационный интерфейс должен задавать `address_traits`, `storage_backend`, `free_block_tree`, `lock_policy` и optional `logging_policy`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-007](04_features.md#feat-007)
- **Реализуется в:**
  - [pmm-config-nolock](../include/pmm/config.h#pmm-config-nolock)
  - [pmm-basicconfig](../include/pmm/manager_configs.h#pmm-basicconfig)
  - [manager_concept.h](../include/pmm/manager_concept.h)

## if-007

- **Требование:** Preset interface должен предоставлять алиасы для типовых сценариев: embedded static, embedded heap, single-threaded heap, multi-threaded heap, industrial DB heap, large DB heap.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [ur-006](03_user_requirements.md#ur-006)
- **Реализуется в:**
  - [pmm_presets.h](../include/pmm/pmm_presets.h)

## if-008

- **Требование:** Public API должен быть static API manager type; пользователь не должен создавать instance менеджера.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [con-004](09_constraints.md#con-004)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## if-009

- **Требование:** `InstanceId` должен позволять сосуществование нескольких independent managers с одной конфигурацией.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [con-005](09_constraints.md#con-005), [sys-005](10_system_requirements.md#sys-005)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## if-010

- **Требование:** Public diagnostics interface должен экспонировать `VerifyResult`, `PmmError` и related diagnostic structures.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README, [docs/validation_model.md](../docs/validation_model.md)
- **Реализует:** [feat-010](04_features.md#feat-010)
- **Реализуется в:**
  - [pmm-verifyresult](../include/pmm/diagnostics.h#pmm-verifyresult)

## if-011

- **Требование:** Optional demo interface должен собираться отдельно и не быть обязательной runtime-зависимостью ядра.
- **Приоритет:** Could
- **Статус:** Recovered
- **Основание:** README
- **Реализуется в:**
  - [demo/](../demo/), [dep-002](11_assumptions_dependencies.md#dep-002)
