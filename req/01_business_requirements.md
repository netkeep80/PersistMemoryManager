# 01. Бизнес-требования (`BR`)

Бизнес-требование фиксирует высокоуровневую бизнес-цель организации или заказчика системы.

Каждое требование оформлено как заголовок уровня `##` с идентификатором в формате `br-xxx` (lowercase). Идентификатор служит якорем для трассировки между md-файлами и исходным кодом/тестами.

## br-001

- **Требование:** PMM должен служить компактным персистентным storage-kernel для нижнего слоя `pjson_db` и смежных верхних слоев, не превращаясь в прикладной продукт.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализуется в:**
  - [ur-001](03_user_requirements.md#ur-001), [feat-001](04_features.md#feat-001)
  - [fr-001](05_functional_requirements.md#fr-001), [fr-002](05_functional_requirements.md#fr-002), [fr-003](05_functional_requirements.md#fr-003)
  - [con-003](09_constraints.md#con-003), [con-004](09_constraints.md#con-004)
  - [sys-001](10_system_requirements.md#sys-001), [sys-007](10_system_requirements.md#sys-007)
- **Проверяется в:** [ac-001](12_acceptance_criteria.md#ac-001), [ac-006](12_acceptance_criteria.md#ac-006)

## br-002

- **Требование:** PMM должен позволять сохранять и восстанавливать образ управляемой памяти так, чтобы данные оставались валидными после загрузки по другому базовому адресу.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [ur-003](03_user_requirements.md#ur-003), [feat-003](04_features.md#feat-003)
  - [fr-007](05_functional_requirements.md#fr-007)
  - [dr-007](06_data_requirements.md#dr-007)
  - [qa-port-001](08_quality_attributes.md#qa-port-001)
  - [sys-003](10_system_requirements.md#sys-003)
- **Проверяется в:** [ac-004](12_acceptance_criteria.md#ac-004)

## br-003

- **Требование:** PMM должен снижать риск повреждения персистентного образа через проверку, диагностику и восстановление служебных структур.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/validation_model.md](../docs/validation_model.md)
- **Реализуется в:**
  - [ur-004](03_user_requirements.md#ur-004), [ur-005](03_user_requirements.md#ur-005), [feat-004](04_features.md#feat-004)
  - [fr-014](05_functional_requirements.md#fr-014)
  - [qa-rel-001](08_quality_attributes.md#qa-rel-001), [qa-rel-002](08_quality_attributes.md#qa-rel-002), [qa-rec-001](08_quality_attributes.md#qa-rec-001), [qa-diag-001](08_quality_attributes.md#qa-diag-001)
- **Проверяется в:** [ac-005](12_acceptance_criteria.md#ac-005), [ac-006](12_acceptance_criteria.md#ac-006), [ac-010](12_acceptance_criteria.md#ac-010)

## br-004

- **Требование:** Эволюция PMM должна усиливать kernel-hardening, compaction, intrusive-index formalization и recovery/validation discipline, а не расширять прикладную семантику.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализуется в:**
  - [feat-004](04_features.md#feat-004), [feat-010](04_features.md#feat-010)
  - [rule-003](02_business_rules.md#rule-003), [rule-004](02_business_rules.md#rule-004)
  - [con-006](09_constraints.md#con-006)
  - [qa-maint-001](08_quality_attributes.md#qa-maint-001)
- **Проверяется в:** [ac-011](12_acceptance_criteria.md#ac-011)

## br-005

- **Требование:** PMM должен давать reusable substrate для intrusive persistent structures и persistent containers без навязывания формата данных верхнего уровня.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [ur-007](03_user_requirements.md#ur-007), [ur-008](03_user_requirements.md#ur-008)
  - [feat-005](04_features.md#feat-005), [feat-008](04_features.md#feat-008)
  - [fr-011](05_functional_requirements.md#fr-011), [fr-012](05_functional_requirements.md#fr-012)
  - [dr-009](06_data_requirements.md#dr-009)
  - [sys-004](10_system_requirements.md#sys-004)
- **Проверяется в:** [ac-002](12_acceptance_criteria.md#ac-002), [ac-003](12_acceptance_criteria.md#ac-003)

## br-006

- **Требование:** PMM должен сохранять малую поверхность репозитория и API, достаточную для роли kernel, а не product/application framework.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализуется в:**
  - [feat-009](04_features.md#feat-009)
  - [rule-005](02_business_rules.md#rule-005)
  - [con-010](09_constraints.md#con-010)
  - [qa-maint-001](08_quality_attributes.md#qa-maint-001)
- **Проверяется в:** [ac-011](12_acceptance_criteria.md#ac-011)
