# 02. Бизнес-правила (`RULE`)

Бизнес-правило — политика, предписание, стандарт или правило, которое определяет или ограничивает бизнес-процесс/разработку и может порождать требования.

Каждое правило оформлено как заголовок уровня `##` с идентификатором в формате `rule-xxx`.

## rule-001

- **Правило:** Все персистентные ссылки внутри PAP должны храниться как offset/granule indices, а не как raw pointers.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [feat-003](04_features.md#feat-003)
  - [fr-007](05_functional_requirements.md#fr-007), [fr-030](05_functional_requirements.md#fr-030)
  - [dr-007](06_data_requirements.md#dr-007)
  - [pmm-pptr](../include/pmm/pptr.h#pmm-pptr)
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)

## rule-002

- **Правило:** PMM не должен интерпретировать schema/payload верхнего уровня.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализуется в:**
  - [con-006](09_constraints.md#con-006), [con-011](09_constraints.md#con-011)
  - [sys-002](10_system_requirements.md#sys-002), [sys-007](10_system_requirements.md#sys-007)

## rule-003

- **Правило:** Любое изменение PMM должно быть трассируемо к разрешенным направлениям развития: hardening, compaction, extraction prep, intrusive-index formalization, validation/invariants/recovery strengthening.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализуется в:**
  - [br-004](01_business_requirements.md#br-004)
  - [qa-maint-001](08_quality_attributes.md#qa-maint-001)

## rule-004

- **Правило:** PMM не должен включать query engine, VM/execution engine, replication/sync, business logic или product/application semantics.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализуется в:**
  - [br-004](01_business_requirements.md#br-004)
  - [con-006](09_constraints.md#con-006)
  - [sys-002](10_system_requirements.md#sys-002), [sys-007](10_system_requirements.md#sys-007)

## rule-005

- **Правило:** Generated surface, например [single_include/](../single_include/), не должен смешиваться с core changes в одном изменении.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/pmm_target_model.md](../docs/pmm_target_model.md), README
- **Реализуется в:**
  - [br-006](01_business_requirements.md#br-006), [feat-009](04_features.md#feat-009)
  - [con-010](09_constraints.md#con-010)
- **Проверяется в:** [ac-011](12_acceptance_criteria.md#ac-011)

## rule-006

- **Правило:** `verify()` должен оставаться диагностической операцией без модификации образа; восстановление допускается в документированных фазах `load`/repair.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/validation_model.md](../docs/validation_model.md)
- **Реализуется в:**
  - [ur-004](03_user_requirements.md#ur-004), [feat-004](04_features.md#feat-004)
  - [qa-rel-002](08_quality_attributes.md#qa-rel-002)
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
- **Проверяется в:** [ac-005](12_acceptance_criteria.md#ac-005)

## rule-007

- **Правило:** `create_typed` должен использоваться только для типов с nothrow-конструктором; `destroy_typed` — только для типов с nothrow-деструктором.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README API section
- **Реализуется в:**
  - [fr-005](05_functional_requirements.md#fr-005), [fr-006](05_functional_requirements.md#fr-006)
  - [asm-004](11_assumptions_dependencies.md#asm-004)
  - [pmm-detail-persistmemorytypedapi-reallocate_typed](../include/pmm/typed_manager_api.h#pmm-detail-persistmemorytypedapi-reallocate_typed)

## rule-008

- **Правило:** Persistent container/domain semantics должны храниться в forest/domain registry, не в разрозненных глобальных runtime-таблицах.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [feat-005](04_features.md#feat-005)
  - [fr-011](05_functional_requirements.md#fr-011), [fr-012](05_functional_requirements.md#fr-012), [fr-018](05_functional_requirements.md#fr-018)
  - [dr-009](06_data_requirements.md#dr-009)
  - [pmm-detail-forestdomainregistry](../include/pmm/forest_registry.h#pmm-detail-forestdomainregistry)
  - [forest_domain_mixin.inc](../include/pmm/forest_domain_mixin.inc)

## rule-009

- **Правило:** `NoLock` допустим только для single-threaded сценариев; многопоточный доступ должен использовать `SharedMutexLock` или эквивалентную lock policy.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/thread_safety.md](../docs/thread_safety.md)
- **Реализуется в:**
  - [feat-007](04_features.md#feat-007)
  - [qa-thread-001](08_quality_attributes.md#qa-thread-001), [qa-thread-002](08_quality_attributes.md#qa-thread-002)
  - [con-009](09_constraints.md#con-009)
  - [asm-003](11_assumptions_dependencies.md#asm-003)
- **Проверяется в:** [ac-008](12_acceptance_criteria.md#ac-008), [ac-009](12_acceptance_criteria.md#ac-009)
