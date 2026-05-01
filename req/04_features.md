# 04. Характеристики продукта (`FEAT`)

Характеристика — одна или несколько логически связанных возможностей системы, представляющих ценность для пользователя и раскрываемых функциональными требованиями.

Каждая характеристика оформлена как заголовок уровня `##` с идентификатором в формате `feat-xxx`.

## feat-001

- **Характеристика:** Lifecycle management: `create`, `load`, `destroy`, `is_initialized`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Реализует:** [br-001](01_business_requirements.md#br-001)
- **Реализуется в:**
  - [ur-001](03_user_requirements.md#ur-001)
  - [fr-001](05_functional_requirements.md#fr-001), [fr-002](05_functional_requirements.md#fr-002), [fr-003](05_functional_requirements.md#fr-003)
  - [con-004](09_constraints.md#con-004)
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## feat-002

- **Характеристика:** Persistent allocator: best-fit allocator поверх intrusive AVL free-tree.
- **Приоритет:** Must
- **Статус:** Recovered
- **Реализует:** [br-001](01_business_requirements.md#br-001)
- **Реализуется в:**
  - [ur-002](03_user_requirements.md#ur-002)
  - [fr-004](05_functional_requirements.md#fr-004), [fr-013](05_functional_requirements.md#fr-013)
  - [dr-005](06_data_requirements.md#dr-005), [dr-006](06_data_requirements.md#dr-006)
  - [qa-perf-001](08_quality_attributes.md#qa-perf-001)
  - [pmm-allocatorpolicy](../include/pmm/allocator_policy.h#pmm-allocatorpolicy)
  - [pmm-avlfreetree](../include/pmm/free_block_tree.h#pmm-avlfreetree)

## feat-003

- **Характеристика:** Persistent pointers: `pptr<T>` как гранульный индекс вместо raw pointer.
- **Приоритет:** Must
- **Статус:** Recovered
- **Реализует:** [br-002](01_business_requirements.md#br-002)
- **Реализуется в:**
  - [ur-003](03_user_requirements.md#ur-003)
  - [fr-007](05_functional_requirements.md#fr-007), [fr-008](05_functional_requirements.md#fr-008)
  - [dr-007](06_data_requirements.md#dr-007)
  - [qa-port-001](08_quality_attributes.md#qa-port-001)
  - [con-007](09_constraints.md#con-007)
  - [pmm-pptr](../include/pmm/pptr.h#pmm-pptr)

## feat-004

- **Характеристика:** Verification and recovery: `verify`, `load(VerifyResult&)`, rebuild/repair служебных структур.
- **Приоритет:** Must
- **Статус:** Recovered
- **Реализует:** [br-003](01_business_requirements.md#br-003), [br-004](01_business_requirements.md#br-004)
- **Реализуется в:**
  - [ur-004](03_user_requirements.md#ur-004), [ur-005](03_user_requirements.md#ur-005)
  - [fr-014](05_functional_requirements.md#fr-014), [fr-015](05_functional_requirements.md#fr-015), [fr-025](05_functional_requirements.md#fr-025)
  - [qa-rel-002](08_quality_attributes.md#qa-rel-002), [qa-rec-001](08_quality_attributes.md#qa-rec-001), [qa-diag-001](08_quality_attributes.md#qa-diag-001)
  - [rule-006](02_business_rules.md#rule-006)

## feat-005

- **Характеристика:** Root/domain registry для нескольких persistent forest roots.
- **Приоритет:** Should
- **Статус:** Recovered
- **Реализует:** [br-005](01_business_requirements.md#br-005)
- **Реализуется в:**
  - [ur-007](03_user_requirements.md#ur-007)
  - [fr-011](05_functional_requirements.md#fr-011), [fr-012](05_functional_requirements.md#fr-012)
  - [dr-009](06_data_requirements.md#dr-009)
  - [rule-008](02_business_rules.md#rule-008)
  - [pmm-detail-forestdomainregistry](../include/pmm/forest_registry.h#pmm-detail-forestdomainregistry)

## feat-006

- **Характеристика:** Storage backends: heap, static, mmap.
- **Приоритет:** Should
- **Статус:** Recovered
- **Реализуется в:**
  - [ur-010](03_user_requirements.md#ur-010), [ur-011](03_user_requirements.md#ur-011)
  - [if-005](07_external_interfaces.md#if-005)
  - [sys-003](10_system_requirements.md#sys-003)
  - [con-008](09_constraints.md#con-008)
  - [pmm-heapstorage](../include/pmm/heap_storage.h#pmm-heapstorage)
  - [pmm-staticstorage](../include/pmm/static_storage.h#pmm-staticstorage)
  - [pmm-mmapstorage](../include/pmm/mmap_storage.h#pmm-mmapstorage)

## feat-007

- **Характеристика:** Lock policies: no-lock и shared-mutex режимы.
- **Приоритет:** Should
- **Статус:** Recovered
- **Реализуется в:**
  - [if-006](07_external_interfaces.md#if-006)
  - [qa-thread-001](08_quality_attributes.md#qa-thread-001), [qa-thread-002](08_quality_attributes.md#qa-thread-002)
  - [rule-009](02_business_rules.md#rule-009)
  - [con-009](09_constraints.md#con-009)
- **Проверяется в:** [ac-008](12_acceptance_criteria.md#ac-008), [ac-009](12_acceptance_criteria.md#ac-009)

## feat-008

- **Характеристика:** Persistent containers/types: `pstringview`, `pstring`, `pmap`, `parray`, `pallocator`.
- **Приоритет:** Should
- **Статус:** Recovered
- **Реализует:** [br-005](01_business_requirements.md#br-005)
- **Реализуется в:**
  - [ur-008](03_user_requirements.md#ur-008)
  - [dr-009](06_data_requirements.md#dr-009)
  - [fr-017](05_functional_requirements.md#fr-017), [fr-018](05_functional_requirements.md#fr-018), [fr-031](05_functional_requirements.md#fr-031)
  - [con-012](09_constraints.md#con-012)
  - [pmm-pstring](../include/pmm/pstring.h#pmm-pstring)
  - [pmm-pstringview](../include/pmm/pstringview.h#pmm-pstringview)
  - [pmm-pmap](../include/pmm/pmap.h#pmm-pmap)
  - [pmm-parray](../include/pmm/parray.h#pmm-parray)
  - [pmm-pallocator](../include/pmm/pallocator.h#pmm-pallocator)

## feat-009

- **Характеристика:** Single-header distribution surface.
- **Приоритет:** Could
- **Статус:** Recovered
- **Реализует:** [br-006](01_business_requirements.md#br-006)
- **Реализуется в:**
  - [if-002](07_external_interfaces.md#if-002)
  - [con-010](09_constraints.md#con-010)
  - [rule-005](02_business_rules.md#rule-005)
  - [single_include/pmm/pmm.h](../single_include/pmm/pmm.h)
  - [scripts/generate-single-headers.sh](../scripts/generate-single-headers.sh)
- **Проверяется в:** [ac-011](12_acceptance_criteria.md#ac-011)

## feat-010

- **Характеристика:** Diagnostics taxonomy through `VerifyResult`, `PmmError`, violation types.
- **Приоритет:** Should
- **Статус:** Recovered
- **Реализует:** [br-004](01_business_requirements.md#br-004)
- **Реализуется в:**
  - [fr-015](05_functional_requirements.md#fr-015)
  - [if-010](07_external_interfaces.md#if-010)
  - [qa-diag-001](08_quality_attributes.md#qa-diag-001)
  - [pmm-verifyresult](../include/pmm/diagnostics.h#pmm-verifyresult)
  - [pmm-pmmerror](../include/pmm/types.h#pmm-pmmerror)
