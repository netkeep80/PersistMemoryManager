# 12. Критерии приемки (`AC`)

Критерии приемки добавлены как практический слой поверх восстановленных требований. Это не отдельный тип Вигерса из базовой таблицы, но полезный артефакт для проверки требований.

Каждый критерий оформлен как заголовок уровня `##` с идентификатором в формате `ac-xxx`.

## ac-001

- **Критерий:** После `create(size)` менеджер инициализирован, статистика согласована, есть `Block_0` с `ManagerHeader` и свободный блок для пользовательских аллокаций.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Проверяет:** [fr-001](05_functional_requirements.md#fr-001), [dr-002](06_data_requirements.md#dr-002), [dr-003](06_data_requirements.md#dr-003)
- **Тесты:** [test_allocate.cpp](../tests/test_allocate.cpp), [test_block_modernization.cpp](../tests/test_block_modernization.cpp)

## ac-002

- **Критерий:** После `allocate_typed<T>(n)` возвращенный `pptr<T>` разрешается через `resolve`, а `is_valid_ptr` возвращает `true`.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/validation_model.md](../docs/validation_model.md)
- **Проверяет:** [fr-005](05_functional_requirements.md#fr-005), [fr-007](05_functional_requirements.md#fr-007), [fr-008](05_functional_requirements.md#fr-008)
- **Тесты:** [test_allocate.cpp](../tests/test_allocate.cpp)

## ac-003

- **Критерий:** После `deallocate_typed` блок помечается свободным, counters пересчитаны, соседние свободные блоки coalesce при наличии.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Проверяет:** [fr-004](05_functional_requirements.md#fr-004), [dr-005](06_data_requirements.md#dr-005), [qa-perf-001](08_quality_attributes.md#qa-perf-001)
- **Тесты:** [test_deallocate.cpp](../tests/test_deallocate.cpp), [test_coalesce.cpp](../tests/test_coalesce.cpp)

## ac-004

- **Критерий:** Сохраненный image после загрузки по другому base address дает корректное значение ранее сохраненного `pptr` offset.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Проверяет:** [br-002](01_business_requirements.md#br-002), [qa-port-001](08_quality_attributes.md#qa-port-001)
- **Тесты:** [test_persistence.cpp](../tests/test_persistence.cpp)

## ac-005

- **Критерий:** `verify()` на валидном image не меняет байты image и возвращает отсутствие критических нарушений.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/validation_model.md](../docs/validation_model.md)
- **Проверяет:** [rule-006](02_business_rules.md#rule-006), [qa-rel-002](08_quality_attributes.md#qa-rel-002)
- **Тесты:** [test_issue245_verify_repair.cpp](../tests/test_issue245_verify_repair.cpp), [test_issue258_verify_behavior.cpp](../tests/test_issue258_verify_behavior.cpp)

## ac-006

- **Критерий:** `load(VerifyResult&)` на допустимом image выполняет documented validation/repair и восстанавливает linked list, counters и free tree.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md), [docs/validation_model.md](../docs/validation_model.md)
- **Проверяет:** [fr-014](05_functional_requirements.md#fr-014), [qa-rec-001](08_quality_attributes.md#qa-rec-001)
- **Тесты:** [test_issue256_verify_repair_contract.cpp](../tests/test_issue256_verify_repair_contract.cpp), [test_persistence.cpp](../tests/test_persistence.cpp)

## ac-007

- **Критерий:** Image с неподдерживаемой `image_version` отвергается с диагностикой unsupported format/header corruption.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Проверяет:** [dr-008](06_data_requirements.md#dr-008), [qa-compat-001](08_quality_attributes.md#qa-compat-001)
- **Тесты:** [test_issue257_validation.cpp](../tests/test_issue257_validation.cpp)

## ac-008

- **Критерий:** В `NoLock` preset код компилируется без runtime-lock overhead; документация явно ограничивает использование single-threaded режимом.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/thread_safety.md](../docs/thread_safety.md)
- **Проверяет:** [qa-thread-002](08_quality_attributes.md#qa-thread-002), [con-009](09_constraints.md#con-009)
- **Тесты:** [test_thread_safety.cpp](../tests/test_thread_safety.cpp), [test_issue123_sh_single_threaded.cpp](../tests/test_issue123_sh_single_threaded.cpp), [test_issue123_sh_multi_threaded.cpp](../tests/test_issue123_sh_multi_threaded.cpp)

## ac-009

- **Критерий:** В `SharedMutexLock` preset read operations допускают concurrent shared locking, write operations используют unique locking.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/thread_safety.md](../docs/thread_safety.md)
- **Проверяет:** [qa-thread-001](08_quality_attributes.md#qa-thread-001)
- **Тесты:** [test_thread_safety.cpp](../tests/test_thread_safety.cpp), [test_issue123_sh_single_threaded.cpp](../tests/test_issue123_sh_single_threaded.cpp), [test_issue123_sh_multi_threaded.cpp](../tests/test_issue123_sh_multi_threaded.cpp)

## ac-010

- **Критерий:** Попытка использовать raw/foreign/misaligned pointer в API не приводит к silent corruption, а возвращает ошибку/false/null согласно validation model.
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/validation_model.md](../docs/validation_model.md)
- **Проверяет:** [qa-rel-001](08_quality_attributes.md#qa-rel-001), [qa-diag-001](08_quality_attributes.md#qa-diag-001)
- **Тесты:** [test_issue326_user_ptr_validation.cpp](../tests/test_issue326_user_ptr_validation.cpp), [test_issue257_validation.cpp](../tests/test_issue257_validation.cpp)

## ac-011

- **Критерий:** [single_include/](../single_include/) генерируется скриптом из [include/](../include/); ручное изменение generated surface не требуется для core change.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README, [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Проверяет:** [rule-005](02_business_rules.md#rule-005), [con-010](09_constraints.md#con-010)
- **Тесты:** [scripts/generate-single-headers.sh](../scripts/generate-single-headers.sh)

## ac-012

- **Критерий:** Public README/API демонстрирует CMake/CTest workflow и он проходит на поддерживаемых компиляторах.
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Проверяет:** [con-002](09_constraints.md#con-002), [qa-test-001](08_quality_attributes.md#qa-test-001)
- **Тесты:** [tests/CMakeLists.txt](../tests/CMakeLists.txt), [CMakeLists.txt](../CMakeLists.txt)
