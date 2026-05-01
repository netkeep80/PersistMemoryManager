# 13. Матрица трассируемости

Упрощенная трассировка `BR → UR/FEAT → FR/QA/DR/IF/CON → AC`.

Идентификаторы ниже являются ссылками на якоря (заголовки уровня `##`) в соответствующих md-файлах. Каждый якорь — отдельное требование с собственным набором ссылок.

> Состав связей дублирует поля `Реализует` / `Реализуется в` / `Проверяется в`
> внутри отдельных требований. Согласованность матрицы и отдельных
> требований проверяется скриптами
> [scripts/check-requirements-traceability.py](../scripts/check-requirements-traceability.py)
> и [scripts/check-requirements-catalog.py](../scripts/check-requirements-catalog.py)
> в workflow `Docs Consistency`. Список допустимых исключений (orphan-узлы и
> якоря, у которых сознательно нет `req:`-аннотации) лежит в
> [req/.catalog-allowlist.json](.catalog-allowlist.json).

## Бизнес-цели → реализация

### br-001
- **Бизнес-цель:** [br-001](01_business_requirements.md#br-001)
- **Пользовательские/feature:** [ur-001](03_user_requirements.md#ur-001), [feat-001](04_features.md#feat-001)
- **Реализационные:** [fr-001](05_functional_requirements.md#fr-001), [fr-002](05_functional_requirements.md#fr-002), [fr-003](05_functional_requirements.md#fr-003), [con-003](09_constraints.md#con-003), [con-004](09_constraints.md#con-004)
- **Критерии приемки:** [ac-001](12_acceptance_criteria.md#ac-001), [ac-006](12_acceptance_criteria.md#ac-006)

### br-002
- **Бизнес-цель:** [br-002](01_business_requirements.md#br-002)
- **Пользовательские/feature:** [ur-003](03_user_requirements.md#ur-003), [feat-003](04_features.md#feat-003)
- **Реализационные:** [fr-007](05_functional_requirements.md#fr-007), [dr-007](06_data_requirements.md#dr-007), [qa-port-001](08_quality_attributes.md#qa-port-001), [sys-003](10_system_requirements.md#sys-003)
- **Критерии приемки:** [ac-004](12_acceptance_criteria.md#ac-004)

### br-003
- **Бизнес-цель:** [br-003](01_business_requirements.md#br-003)
- **Пользовательские/feature:** [ur-004](03_user_requirements.md#ur-004), [ur-005](03_user_requirements.md#ur-005), [feat-004](04_features.md#feat-004)
- **Реализационные:** [fr-014](05_functional_requirements.md#fr-014), [qa-rel-001](08_quality_attributes.md#qa-rel-001), [qa-rel-002](08_quality_attributes.md#qa-rel-002), [qa-rec-001](08_quality_attributes.md#qa-rec-001), [qa-diag-001](08_quality_attributes.md#qa-diag-001)
- **Критерии приемки:** [ac-005](12_acceptance_criteria.md#ac-005), [ac-006](12_acceptance_criteria.md#ac-006), [ac-010](12_acceptance_criteria.md#ac-010)

### br-004
- **Бизнес-цель:** [br-004](01_business_requirements.md#br-004)
- **Пользовательские/feature:** [feat-004](04_features.md#feat-004), [feat-010](04_features.md#feat-010)
- **Реализационные:** [rule-003](02_business_rules.md#rule-003), [rule-004](02_business_rules.md#rule-004), [con-006](09_constraints.md#con-006), [qa-maint-001](08_quality_attributes.md#qa-maint-001)
- **Критерии приемки:** [ac-011](12_acceptance_criteria.md#ac-011)

### br-005
- **Бизнес-цель:** [br-005](01_business_requirements.md#br-005)
- **Пользовательские/feature:** [ur-007](03_user_requirements.md#ur-007), [ur-008](03_user_requirements.md#ur-008), [feat-005](04_features.md#feat-005), [feat-008](04_features.md#feat-008)
- **Реализационные:** [fr-011](05_functional_requirements.md#fr-011), [fr-012](05_functional_requirements.md#fr-012), [dr-009](06_data_requirements.md#dr-009), [sys-004](10_system_requirements.md#sys-004)
- **Критерии приемки:** [ac-002](12_acceptance_criteria.md#ac-002), [ac-003](12_acceptance_criteria.md#ac-003)

### br-006
- **Бизнес-цель:** [br-006](01_business_requirements.md#br-006)
- **Пользовательские/feature:** [feat-009](04_features.md#feat-009)
- **Реализационные:** [rule-005](02_business_rules.md#rule-005), [con-010](09_constraints.md#con-010), [qa-maint-001](08_quality_attributes.md#qa-maint-001)
- **Критерии приемки:** [ac-011](12_acceptance_criteria.md#ac-011)

## Feature-level trace

### feat-001
- **Feature:** [feat-001](04_features.md#feat-001)
- **Functional/data/interface:** [fr-001](05_functional_requirements.md#fr-001), [fr-002](05_functional_requirements.md#fr-002), [fr-003](05_functional_requirements.md#fr-003)
- **Quality/constraints:** [con-004](09_constraints.md#con-004)

### feat-002
- **Feature:** [feat-002](04_features.md#feat-002)
- **Functional/data/interface:** [fr-004](05_functional_requirements.md#fr-004), [fr-013](05_functional_requirements.md#fr-013), [dr-005](06_data_requirements.md#dr-005), [dr-006](06_data_requirements.md#dr-006)
- **Quality/constraints:** [qa-perf-001](08_quality_attributes.md#qa-perf-001)

### feat-003
- **Feature:** [feat-003](04_features.md#feat-003)
- **Functional/data/interface:** [fr-007](05_functional_requirements.md#fr-007), [fr-008](05_functional_requirements.md#fr-008), [dr-007](06_data_requirements.md#dr-007)
- **Quality/constraints:** [qa-port-001](08_quality_attributes.md#qa-port-001), [con-007](09_constraints.md#con-007)

### feat-004
- **Feature:** [feat-004](04_features.md#feat-004)
- **Functional/data/interface:** [fr-014](05_functional_requirements.md#fr-014), [fr-015](05_functional_requirements.md#fr-015)
- **Quality/constraints:** [qa-rel-001](08_quality_attributes.md#qa-rel-001), [qa-rel-002](08_quality_attributes.md#qa-rel-002), [qa-rec-001](08_quality_attributes.md#qa-rec-001), [qa-diag-001](08_quality_attributes.md#qa-diag-001)

### feat-005
- **Feature:** [feat-005](04_features.md#feat-005)
- **Functional/data/interface:** [fr-011](05_functional_requirements.md#fr-011), [fr-012](05_functional_requirements.md#fr-012), [dr-009](06_data_requirements.md#dr-009)
- **Quality/constraints:** [sys-004](10_system_requirements.md#sys-004)

### feat-006
- **Feature:** [feat-006](04_features.md#feat-006)
- **Functional/data/interface:** [if-005](07_external_interfaces.md#if-005), [sys-003](10_system_requirements.md#sys-003)
- **Quality/constraints:** [con-008](09_constraints.md#con-008)

### feat-007
- **Feature:** [feat-007](04_features.md#feat-007)
- **Functional/data/interface:** [if-006](07_external_interfaces.md#if-006)
- **Quality/constraints:** [qa-thread-001](08_quality_attributes.md#qa-thread-001), [qa-thread-002](08_quality_attributes.md#qa-thread-002), [con-009](09_constraints.md#con-009)

### feat-008
- **Feature:** [feat-008](04_features.md#feat-008)
- **Functional/data/interface:** [fr-017](05_functional_requirements.md#fr-017), [fr-018](05_functional_requirements.md#fr-018), [dr-009](06_data_requirements.md#dr-009), [dr-010](06_data_requirements.md#dr-010), [dr-011](06_data_requirements.md#dr-011)
- **Quality/constraints:** [con-012](09_constraints.md#con-012)

### feat-009
- **Feature:** [feat-009](04_features.md#feat-009)
- **Functional/data/interface:** [if-002](07_external_interfaces.md#if-002)
- **Quality/constraints:** [rule-005](02_business_rules.md#rule-005), [con-010](09_constraints.md#con-010)

### feat-010
- **Feature:** [feat-010](04_features.md#feat-010)
- **Functional/data/interface:** [fr-015](05_functional_requirements.md#fr-015), [if-010](07_external_interfaces.md#if-010)
- **Quality/constraints:** [qa-diag-001](08_quality_attributes.md#qa-diag-001)
