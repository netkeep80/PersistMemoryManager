# 08. Атрибуты качества и нефункциональные требования (`QA`)

Атрибут качества — вид нефункционального требования, описывающий характеристику сервиса или производительности продукта.

Каждое требование оформлено как заголовок уровня `##` с идентификатором в формате `qa-<категория>-xxx` (lowercase). Категория сохраняется как часть идентификатора.

## qa-rel-001

- **Атрибут:** PMM должен предотвращать silent corruption при raw-pointer/block conversions через централизованную валидацию.
- **Категория:** Reliability
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/validation_model.md](../docs/validation_model.md)
- **Реализует:** [br-003](01_business_requirements.md#br-003)
- **Реализуется в:**
  - [fr-030](05_functional_requirements.md#fr-030)
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)
- **Проверяется в:** [ac-010](12_acceptance_criteria.md#ac-010)

## qa-rel-002

- **Атрибут:** `verify()` должен выполнять read-only диагностику без ремонта образа.
- **Категория:** Reliability
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/validation_model.md](../docs/validation_model.md)
- **Реализует:** [br-003](01_business_requirements.md#br-003), [rule-006](02_business_rules.md#rule-006), [ur-004](03_user_requirements.md#ur-004)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
- **Проверяется в:** [ac-005](12_acceptance_criteria.md#ac-005)

## qa-rec-001

- **Атрибут:** `load()` должен восстанавливать служебные структуры там, где это документировано.
- **Категория:** Recoverability
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [br-003](01_business_requirements.md#br-003), [feat-004](04_features.md#feat-004)
- **Реализуется в:**
  - [fr-014](05_functional_requirements.md#fr-014)
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
- **Проверяется в:** [ac-006](12_acceptance_criteria.md#ac-006)

## qa-perf-001

- **Атрибут:** Поиск best-fit free block должен выполняться через AVL tree с логарифмической сложностью.
- **Категория:** Performance
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [feat-002](04_features.md#feat-002)
- **Реализуется в:**
  - [pmm-avlfreetree](../include/pmm/free_block_tree.h#pmm-avlfreetree)
- **Проверяется в:** [ac-003](12_acceptance_criteria.md#ac-003), [test_avl_allocator.cpp](../tests/test_avl_allocator.cpp)

## qa-perf-002

- **Атрибут:** Cheap validation fast path должен использовать O(1)-проверки без обхода linked list.
- **Категория:** Performance
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/validation_model.md](../docs/validation_model.md)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)

## qa-mem-001

- **Атрибут:** Размер `pptr<T>` должен минимизироваться через выбор address traits.
- **Категория:** Memory efficiency
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [dr-007](06_data_requirements.md#dr-007)
  - [pmm-addresstraits](../include/pmm/address_traits.h#pmm-addresstraits)
  - [pmm-pptr](../include/pmm/pptr.h#pmm-pptr)

## qa-port-001

- **Атрибут:** Persistent image должен оставаться usable после загрузки по другому базовому адресу.
- **Категория:** Portability
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/architecture.md](../docs/architecture.md)
- **Реализует:** [br-002](01_business_requirements.md#br-002), [feat-003](04_features.md#feat-003)
- **Реализуется в:**
  - [pmm-pptr](../include/pmm/pptr.h#pmm-pptr)
  - [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview)
- **Проверяется в:** [ac-004](12_acceptance_criteria.md#ac-004)

## qa-thread-001

- **Атрибут:** В многопоточном режиме read operations должны использовать shared lock, write operations — unique lock.
- **Категория:** Thread safety
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/thread_safety.md](../docs/thread_safety.md)
- **Реализует:** [feat-007](04_features.md#feat-007), [rule-009](02_business_rules.md#rule-009)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
  - [dep-005](11_assumptions_dependencies.md#dep-005)
- **Проверяется в:** [ac-009](12_acceptance_criteria.md#ac-009)

## qa-thread-002

- **Атрибут:** В `NoLock` режиме блокировки должны быть no-op, а использование должно ограничиваться single-threaded сценарием.
- **Категория:** Thread safety
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** README, [docs/thread_safety.md](../docs/thread_safety.md)
- **Реализует:** [feat-007](04_features.md#feat-007), [rule-009](02_business_rules.md#rule-009), [con-009](09_constraints.md#con-009)
- **Реализуется в:**
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
- **Проверяется в:** [ac-008](12_acceptance_criteria.md#ac-008)

## qa-maint-001

- **Атрибут:** Архитектура должна сохранять PMM как малый kernel с ограниченной поверхностью, а не расширяемый application framework.
- **Категория:** Maintainability
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/pmm_target_model.md](../docs/pmm_target_model.md)
- **Реализует:** [br-004](01_business_requirements.md#br-004), [br-006](01_business_requirements.md#br-006), [rule-003](02_business_rules.md#rule-003)
- **Реализуется в:**
  - [con-006](09_constraints.md#con-006), [con-010](09_constraints.md#con-010)
- **Проверяется в:** [ac-011](12_acceptance_criteria.md#ac-011)

## qa-maint-002

- **Атрибут:** Дублирование AVL-логики между контейнерами и free-tree должно быть устранено/минимизировано через shared AVL implementation.
- **Категория:** Maintainability
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [pmm-detail-avlupdateheightonly](../include/pmm/avl_tree_mixin.h#pmm-detail-avlupdateheightonly)
  - [pmm-avlfreetree](../include/pmm/free_block_tree.h#pmm-avlfreetree)

## qa-diag-001

- **Атрибут:** Диагностика должна различать pointer provenance, address correctness и header integrity failure categories.
- **Категория:** Diagnosability
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** [docs/validation_model.md](../docs/validation_model.md)
- **Реализует:** [feat-010](04_features.md#feat-010)
- **Реализуется в:**
  - [pmm-verifyresult](../include/pmm/diagnostics.h#pmm-verifyresult)
- **Проверяется в:** [ac-010](12_acceptance_criteria.md#ac-010)

## qa-compat-001

- **Атрибут:** Загрузка должна отвергать unsupported image versions и несовместимые granule sizes.
- **Категория:** Compatibility
- **Приоритет:** Must
- **Статус:** Recovered
- **Основание:** [docs/architecture.md](../docs/architecture.md)
- **Реализуется в:**
  - [dr-008](06_data_requirements.md#dr-008), [dr-012](06_data_requirements.md#dr-012)
  - [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)
- **Проверяется в:** [ac-007](12_acceptance_criteria.md#ac-007)

## qa-test-001

- **Атрибут:** Изменения должны проверяться через CMake/CTest и regression test suite.
- **Категория:** Testability
- **Приоритет:** Should
- **Статус:** Recovered
- **Основание:** README
- **Реализует:** [ur-009](03_user_requirements.md#ur-009)
- **Реализуется в:**
  - [tests/CMakeLists.txt](../tests/CMakeLists.txt), [CMakeLists.txt](../CMakeLists.txt)
- **Проверяется в:** [ac-012](12_acceptance_criteria.md#ac-012)
