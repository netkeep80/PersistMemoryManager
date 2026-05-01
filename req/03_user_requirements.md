# 03. Пользовательские требования (`UR`)

Пользовательское требование описывает задачу, которую определенный класс пользователей должен иметь возможность выполнять, или требуемый атрибут продукта.

Каждое требование оформлено как заголовок уровня `##` с идентификатором в формате `ur-xxx`.

## ur-001

- **Требование:** Как интегратор библиотеки, я хочу создать, загрузить и уничтожить PAP через `create`, `load`, `destroy`, чтобы управлять жизненным циклом персистентной области.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [br-001](01_business_requirements.md#br-001)
- **Реализуется в:**
  - [feat-001](04_features.md#feat-001)
  - [fr-001](05_functional_requirements.md#fr-001), [fr-002](05_functional_requirements.md#fr-002), [fr-003](05_functional_requirements.md#fr-003)
  - [pmm-persistmemorymanager-create](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-create)
  - [pmm-persistmemorymanager-load](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-load)
  - [pmm-persistmemorymanager-destroy](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-destroy)
- **Проверяется в:** [ac-001](12_acceptance_criteria.md#ac-001), [ac-006](12_acceptance_criteria.md#ac-006)

## ur-002

- **Требование:** Как разработчик persistent-структур, я хочу выделять и освобождать память через typed и untyped allocation API.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [br-001](01_business_requirements.md#br-001)
- **Реализуется в:**
  - [feat-002](04_features.md#feat-002)
  - [fr-004](05_functional_requirements.md#fr-004), [fr-005](05_functional_requirements.md#fr-005)
  - [pmm-persistmemorymanager-allocate](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-allocate)
  - [pmm-detail-persistmemorytypedapi-reallocate_typed](../include/pmm/typed_manager_api.h#pmm-detail-persistmemorytypedapi-reallocate_typed)
- **Проверяется в:** [ac-002](12_acceptance_criteria.md#ac-002), [ac-003](12_acceptance_criteria.md#ac-003)

## ur-003

- **Требование:** Как разработчик, я хочу хранить `pptr<T>` в persistent-структурах и разрешать его после повторной загрузки образа.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [br-002](01_business_requirements.md#br-002)
- **Реализуется в:**
  - [feat-003](04_features.md#feat-003)
  - [fr-007](05_functional_requirements.md#fr-007), [fr-008](05_functional_requirements.md#fr-008)
  - [dr-007](06_data_requirements.md#dr-007)
  - [qa-port-001](08_quality_attributes.md#qa-port-001)
  - [pmm-pptr](../include/pmm/pptr.h#pmm-pptr)
- **Проверяется в:** [ac-004](12_acceptance_criteria.md#ac-004)

## ur-004

- **Требование:** Как разработчик, я хочу выполнить `verify()` без модификации образа.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/validation_model.md](../docs/validation_model.md)
- **Реализует:** [br-003](01_business_requirements.md#br-003)
- **Реализуется в:**
  - [feat-004](04_features.md#feat-004)
  - [rule-006](02_business_rules.md#rule-006)
  - [qa-rel-002](08_quality_attributes.md#qa-rel-002)
- **Проверяется в:** [ac-005](12_acceptance_criteria.md#ac-005)

## ur-005

- **Требование:** Как разработчик, я хочу загружать образ через `load(VerifyResult&)`, получая диагностику и документированное восстановление служебных структур.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [br-003](01_business_requirements.md#br-003)
- **Реализуется в:**
  - [feat-004](04_features.md#feat-004)
  - [fr-002](05_functional_requirements.md#fr-002), [fr-014](05_functional_requirements.md#fr-014)
  - [qa-rec-001](08_quality_attributes.md#qa-rec-001), [qa-diag-001](08_quality_attributes.md#qa-diag-001)
- **Проверяется в:** [ac-006](12_acceptance_criteria.md#ac-006)

## ur-006

- **Требование:** Как интегратор, я хочу выбрать готовый preset: embedded/static, single-threaded, multi-threaded, industrial DB, large DB.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README preset table
- **Реализуется в:**
  - [if-007](07_external_interfaces.md#if-007)
  - [pmm-basicconfig](../include/pmm/manager_configs.h#pmm-basicconfig)
  - [pmm-staticconfig](../include/pmm/manager_configs.h#pmm-staticconfig)
- **Проверяется в:** [ac-008](12_acceptance_criteria.md#ac-008), [ac-009](12_acceptance_criteria.md#ac-009)

## ur-007

- **Требование:** Как разработчик контейнеров, я хочу использовать legacy root pointer и named forest/domain registry для нескольких persistent roots.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [br-005](01_business_requirements.md#br-005)
- **Реализуется в:**
  - [feat-005](04_features.md#feat-005)
  - [fr-011](05_functional_requirements.md#fr-011), [fr-012](05_functional_requirements.md#fr-012)
  - [rule-008](02_business_rules.md#rule-008)

## ur-008

- **Требование:** Как пользователь библиотеки, я хочу иметь базовые persistent-типы: `pstring`, `pstringview`, `pmap`, `parray`, `pallocator`.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [br-005](01_business_requirements.md#br-005)
- **Реализуется в:**
  - [feat-008](04_features.md#feat-008)
  - [dr-009](06_data_requirements.md#dr-009)
  - [pmm-pstring](../include/pmm/pstring.h#pmm-pstring)
  - [pmm-pstringview](../include/pmm/pstringview.h#pmm-pstringview)
  - [pmm-pmap](../include/pmm/pmap.h#pmm-pmap)
  - [pmm-parray](../include/pmm/parray.h#pmm-parray)
  - [pmm-pallocator](../include/pmm/pallocator.h#pmm-pallocator)

## ur-009

- **Требование:** Как сопровождающий, я хочу запускать CMake/CTest и regression tests, чтобы проверять корректность изменений.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Реализуется в:**
  - [if-004](07_external_interfaces.md#if-004)
  - [qa-test-001](08_quality_attributes.md#qa-test-001)
  - [con-002](09_constraints.md#con-002)
  - [dep-001](11_assumptions_dependencies.md#dep-001)
- **Проверяется в:** [ac-012](12_acceptance_criteria.md#ac-012)

## ur-010

- **Требование:** Как разработчик embedded-сценариев, я хочу использовать статический storage без heap allocation.
- **Приоритет:** Could
- **Статус:** Recovered
- **Основание:** README preset table
- **Реализуется в:**
  - [feat-006](04_features.md#feat-006)
  - [if-005](07_external_interfaces.md#if-005)
  - [con-008](09_constraints.md#con-008)
  - [sys-003](10_system_requirements.md#sys-003)
  - [pmm-staticstorage](../include/pmm/static_storage.h#pmm-staticstorage)

## ur-011

- **Требование:** Как разработчик file-backed persistence, я хочу использовать `MMapStorage` и helper-функции сохранения/загрузки.
- **Приоритет:** Could
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [feat-006](04_features.md#feat-006)
  - [fr-016](05_functional_requirements.md#fr-016)
  - [if-003](07_external_interfaces.md#if-003), [if-005](07_external_interfaces.md#if-005)
  - [dep-004](11_assumptions_dependencies.md#dep-004)
  - [pmm-mmapstorage](../include/pmm/mmap_storage.h#pmm-mmapstorage)
