# 11. Предположения и зависимости (`ASM`, `DEP`)

Предположения и зависимости фиксируют внешние условия, которые влияют на реализацию и эксплуатацию требований.

Каждое предположение/зависимость оформлены как заголовок уровня `##` с идентификатором в формате `asm-xxx` или `dep-xxx`.

### Предположения

## asm-001

- **Предположение:** Клиентский код не хранит raw pointers как persistent references.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Связано с:** [rule-001](02_business_rules.md#rule-001), [feat-003](04_features.md#feat-003)

## asm-002

- **Предположение:** Клиентский код выбирает address traits, достаточные для максимального размера PAP.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Связано с:** [dr-007](06_data_requirements.md#dr-007), [qa-mem-001](08_quality_attributes.md#qa-mem-001)

## asm-003

- **Предположение:** Клиентский код выбирает lock policy согласно модели конкурентного доступа.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/thread_safety.md](../docs/thread_safety.md)
- **Связано с:** [rule-009](02_business_rules.md#rule-009), [feat-007](04_features.md#feat-007)

## asm-004

- **Предположение:** Пользовательские типы, создаваемые через `create_typed`, совместимы с nothrow lifecycle constraints.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README
- **Связано с:** [rule-007](02_business_rules.md#rule-007), [fr-006](05_functional_requirements.md#fr-006)

## asm-005

- **Предположение:** Для абсолютного контроля persistent type identity в `pmap` пользователь может специализировать `pmap_type_identity<T>` фиксированным ASCII-tag.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Связано с:** [dr-010](06_data_requirements.md#dr-010), [dr-011](06_data_requirements.md#dr-011)

## asm-006

- **Предположение:** Верхние слои отвечают за schema migration собственных объектов, PMM отвечает только за свой image/layout/kernel metadata.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/pmm_target_model.md](../docs/pmm_target_model.md), [docs/validation_model.md](../docs/validation_model.md)
- **Связано с:** [rule-002](02_business_rules.md#rule-002), [sys-002](10_system_requirements.md#sys-002)

### Зависимости

## dep-001

- **Зависимость:** Для build/test workflow нужны CMake, поддерживаемый C++20 compiler и CTest.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README
- **Связано с:** [con-001](09_constraints.md#con-001), [con-002](09_constraints.md#con-002), [if-004](07_external_interfaces.md#if-004), [ur-009](03_user_requirements.md#ur-009)

## dep-002

- **Зависимость:** Для optional demo нужны GLFW, Dear ImGui и OpenGL.
- **Приоритет:** Could
- **Статус:** Recovered
- **Основание:** README
- **Связано с:** [if-011](07_external_interfaces.md#if-011)

## dep-003

- **Зависимость:** Для benchmark workflow нужен Google Benchmark target, включаемый отдельной CMake option.
- **Приоритет:** Could
- **Статус:** Recovered
- **Основание:** README
- **Связано с:** [benchmarks/](../benchmarks/)

## dep-004

- **Зависимость:** Для file-backed persistence на платформах с mmap требуется поддержка соответствующих OS primitives.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Связано с:** [if-005](07_external_interfaces.md#if-005), [ur-011](03_user_requirements.md#ur-011)

## dep-005

- **Зависимость:** Для многопоточного режима требуется доступный `std::shared_mutex`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/thread_safety.md](../docs/thread_safety.md)
- **Связано с:** [qa-thread-001](08_quality_attributes.md#qa-thread-001)
