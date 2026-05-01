# 09. Ограничения (`CON`)

Ограничение задает пределы выбора вариантов, доступных разработчику при проектировании и реализации продукта.

Каждое ограничение оформлено как заголовок уровня `##` с идентификатором в формате `con-xxx`.

## con-001

- **Ограничение:** Язык реализации и использования: C++20.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README
- **Реализуется в:**
  - [if-001](07_external_interfaces.md#if-001)
  - [CMakeLists.txt](../CMakeLists.txt)

## con-002

- **Ограничение:** Минимальные build-инструменты: CMake 3.16+ и компилятор GCC 10+, Clang 10+ или MSVC 2019 16.3+.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [ur-009](03_user_requirements.md#ur-009)
- **Реализуется в:**
  - [dep-001](11_assumptions_dependencies.md#dep-001)
  - [CMakeLists.txt](../CMakeLists.txt)
- **Проверяется в:** [ac-012](12_acceptance_criteria.md#ac-012)

## con-003

- **Ограничение:** Библиотека должна оставаться header-only.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [br-001](01_business_requirements.md#br-001), [if-001](07_external_interfaces.md#if-001)
- **Реализуется в:**
  - [include/pmm/](../include/pmm/), [CMakeLists.txt](../CMakeLists.txt)

## con-004

- **Ограничение:** API менеджера должен быть static; экземпляры менеджера не создаются.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-001](04_features.md#feat-001), [if-008](07_external_interfaces.md#if-008)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## con-005

- **Ограничение:** Multiple independent managers должны различаться через `InstanceId`.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [if-009](07_external_interfaces.md#if-009), [sys-005](10_system_requirements.md#sys-005)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## con-006

- **Ограничение:** PMM не должен включать JSON/schema/database/query/sync semantics.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализует:** [br-004](01_business_requirements.md#br-004), [rule-002](02_business_rules.md#rule-002), [rule-004](02_business_rules.md#rule-004)
- **Реализуется в:**
  - [qa-maint-001](08_quality_attributes.md#qa-maint-001)

## con-007

- **Ограничение:** Pointer arithmetic для `pptr<T>` намеренно отсутствует.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [feat-003](04_features.md#feat-003)
- **Реализуется в:**
  - [pmm-pptr](../include/pmm/pptr.h#pmm-pptr)

## con-008

- **Ограничение:** При использовании `StaticStorage` рост памяти невозможен.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README preset table
- **Реализует:** [ur-010](03_user_requirements.md#ur-010), [feat-006](04_features.md#feat-006)
- **Реализуется в:**
  - [pmm-staticstorage](../include/pmm/static_storage.h#pmm-staticstorage)

## con-009

- **Ограничение:** При использовании `NoLock` потокобезопасность не обеспечивается.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/thread_safety.md](../docs/thread_safety.md)
- **Реализует:** [rule-009](02_business_rules.md#rule-009), [qa-thread-002](08_quality_attributes.md#qa-thread-002)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
- **Проверяется в:** [ac-008](12_acceptance_criteria.md#ac-008)

## con-010

- **Ограничение:** [single_include/](../single_include/) генерируется из [include/](../include/) и не редактируется вручную.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [br-006](01_business_requirements.md#br-006), [rule-005](02_business_rules.md#rule-005), [feat-009](04_features.md#feat-009)
- **Реализуется в:**
  - [fr-034](05_functional_requirements.md#fr-034)
  - [single_include/](../single_include/), [scripts/generate-single-headers.sh](../scripts/generate-single-headers.sh), [scripts/check-source-loc-budget.sh](../scripts/check-source-loc-budget.sh)
- **Проверяется в:** [ac-011](12_acceptance_criteria.md#ac-011)

## con-011

- **Ограничение:** PMM не должен выполнять валидацию `pjson`/верхнеуровневых объектов; это out of scope.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/validation_model.md](../docs/validation_model.md), [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализует:** [rule-002](02_business_rules.md#rule-002)
- **Реализуется в:**
  - [sys-002](10_system_requirements.md#sys-002), [sys-007](10_system_requirements.md#sys-007)

## con-012

- **Ограничение:** `pstringview` blocks могут быть permanently locked и не должны освобождаться обычным `deallocate()`.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-008](04_features.md#feat-008)
- **Реализуется в:**
  - [pmm-pstringview](../include/pmm/pstringview.h#pmm-pstringview)
