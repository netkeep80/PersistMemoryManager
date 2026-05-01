# Обзор `include/pmm/` и трассировка PMM-anchors

Документ фиксирует структурный обзор всех публичных и приватных заголовков
PMM из каталога `include/pmm/` и связь PMM-anchor комментариев с требованиями
каталога [`req/`](../req/README.md). Документ является входной точкой аудита
требований, см. issue #379.

Каждый PMM anchor оформлен в исходном коде как блок-комментарий вида:

```c
/*
## anchor-name
req: id1, id2, id3
*/
```

Глубина `#`-маркеров соответствует количеству сегментов в `anchor-name`:
`pmm-pptr` — два сегмента, поэтому два `#`; `pmm-pptr-byte_offset` — три
сегмента, поэтому три `#`. Формат проверяется
[`scripts/check-include-anchor-comments.sh`](../scripts/check-include-anchor-comments.sh)
и [`scripts/check-requirements-traceability.py`](../scripts/check-requirements-traceability.py),
расширенная валидация добавлена в
[`scripts/check-requirements-catalog.py`](../scripts/check-requirements-catalog.py).

## Подсистемы и заголовки

### Address traits

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [address_traits.h](../include/pmm/address_traits.h) | [pmm-addresstraits](../include/pmm/address_traits.h#pmm-addresstraits) | Шаблон `AddressTraits<IndexT, GranuleSz>`, базовые алиасы `SmallAddressTraits`, `DefaultAddressTraits`, `LargeAddressTraits`. Конвертация `index ↔ byte offset` и `granules ↔ bytes`. |

Связанные требования: [dr-007](../req/06_data_requirements.md#dr-007),
[qa-mem-001](../req/08_quality_attributes.md#qa-mem-001),
[asm-002](../req/11_assumptions_dependencies.md#asm-002).

### Storage backends

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [storage_backend.h](../include/pmm/storage_backend.h) | — | Концепт storage backend. |
| [heap_storage.h](../include/pmm/heap_storage.h) | [pmm-detail-alignedalloc](../include/pmm/heap_storage.h#pmm-detail-alignedalloc), [pmm-heapstorage](../include/pmm/heap_storage.h#pmm-heapstorage) | Heap-backed storage с aligned allocation и поддержкой роста. |
| [static_storage.h](../include/pmm/static_storage.h) | [pmm-staticstorage](../include/pmm/static_storage.h#pmm-staticstorage), [pmm-staticstorage-expand](../include/pmm/static_storage.h#pmm-staticstorage-expand) | Static-buffer backend для embedded сценариев без heap. |
| [mmap_storage.h](../include/pmm/mmap_storage.h) | [pmm-mmapstorage](../include/pmm/mmap_storage.h#pmm-mmapstorage), [pmm-mmapstorage-expand](../include/pmm/mmap_storage.h#pmm-mmapstorage-expand) | File-backed mmap storage с поддержкой роста файла/маппинга. |

Связанные требования: [feat-006](../req/04_features.md#feat-006),
[if-005](../req/07_external_interfaces.md#if-005),
[fr-013](../req/05_functional_requirements.md#fr-013),
[fr-028](../req/05_functional_requirements.md#fr-028).

### Block / header / layout model

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [block.h](../include/pmm/block.h) | [pmm-block](../include/pmm/block.h#pmm-block) | Алиас `Block<AT> = BlockHeader<AT>` и static_assert размера. |
| [block_header.h](../include/pmm/block_header.h) | [pmm-nodetype](../include/pmm/block_header.h#pmm-nodetype), [pmm-nodetype-helpers](../include/pmm/block_header.h#pmm-nodetype-helpers), [pmm-nodetype-for](../include/pmm/block_header.h#pmm-nodetype-for), [pmm-blockheader](../include/pmm/block_header.h#pmm-blockheader), [pmm-blocklayoutcontract](../include/pmm/block_header.h#pmm-blocklayoutcontract) | `enum class NodeType`, `BlockHeader<AT>`, layout-контракт, helper-функции `is_free`, `is_allocated`, `is_mutable`, `can_be_deleted_from_pap`, `participates_in_free_tree`, специализация `node_type_for<T>`. |
| [block_state.h](../include/pmm/block_state.h) | [pmm-blockstatebase](../include/pmm/block_state.h#pmm-blockstatebase), [pmm-blockstatebase-recover_state](../include/pmm/block_state.h#pmm-blockstatebase-recover_state), [pmm-blockstatebase-verify_state](../include/pmm/block_state.h#pmm-blockstatebase-verify_state), [pmm-freeblock](../include/pmm/block_state.h#pmm-freeblock), [pmm-freeblock-cast_from_raw](../include/pmm/block_state.h#pmm-freeblock-cast_from_raw), [pmm-freeblock-can_cast_from_raw](../include/pmm/block_state.h#pmm-freeblock-can_cast_from_raw), [pmm-freeblock-try_cast_from_raw](../include/pmm/block_state.h#pmm-freeblock-try_cast_from_raw), [pmm-freeblock-verify_invariants](../include/pmm/block_state.h#pmm-freeblock-verify_invariants), [pmm-freeblockremovedavl](../include/pmm/block_state.h#pmm-freeblockremovedavl), [pmm-splittingblock](../include/pmm/block_state.h#pmm-splittingblock), [pmm-allocatedblock](../include/pmm/block_state.h#pmm-allocatedblock), [pmm-allocatedblock-cast_from_raw](../include/pmm/block_state.h#pmm-allocatedblock-cast_from_raw), [pmm-allocatedblock-can_cast_from_raw](../include/pmm/block_state.h#pmm-allocatedblock-can_cast_from_raw), [pmm-allocatedblock-try_cast_from_raw](../include/pmm/block_state.h#pmm-allocatedblock-try_cast_from_raw), [pmm-allocatedblock-verify_invariants](../include/pmm/block_state.h#pmm-allocatedblock-verify_invariants), [pmm-freeblocknotinavl](../include/pmm/block_state.h#pmm-freeblocknotinavl), [pmm-coalescingblock](../include/pmm/block_state.h#pmm-coalescingblock) | Type-state machine блока: `FreeBlock`, `AllocatedBlock`, `SplittingBlock`, `CoalescingBlock`, `FreeBlockRemovedAvl`, `FreeBlockNotInAvl`. Инкапсулирует операции, валидные только в данном состоянии. |
| [layout.h](../include/pmm/layout.h) | — | `ManagerHeader` (magic, sizes, counters, free-tree root, image version, granule size, CRC, root offset). |

Связанные требования: [dr-001..dr-006](../req/06_data_requirements.md#dr-001),
[dr-013..dr-020](../req/06_data_requirements.md#dr-013),
[fr-007](../req/05_functional_requirements.md#fr-007),
[feat-002](../req/04_features.md#feat-002),
[rule-002](../req/02_business_rules.md#rule-002).

### Allocator / free tree

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [allocator_policy.h](../include/pmm/allocator_policy.h) | [pmm-allocatorpolicy](../include/pmm/allocator_policy.h#pmm-allocatorpolicy) | Best-fit allocator-policy: split, coalesce, расчёт `weight`, инвариант `weight ≡ physical span` для свободных блоков. |
| [free_block_tree.h](../include/pmm/free_block_tree.h) | [pmm-avlfreetree](../include/pmm/free_block_tree.h#pmm-avlfreetree), [pmm-avlfreetree-find_best_fit](../include/pmm/free_block_tree.h#pmm-avlfreetree-find_best_fit) | Intrusive AVL-tree свободных блоков, ключ — `weight`. |
| [avl_tree_mixin.h](../include/pmm/avl_tree_mixin.h) | [pmm-detail-avlupdateheightonly](../include/pmm/avl_tree_mixin.h#pmm-detail-avlupdateheightonly), [pmm-detail-blockpptr](../include/pmm/avl_tree_mixin.h#pmm-detail-blockpptr), [pmm-detail-avlinorderiterator](../include/pmm/avl_tree_mixin.h#pmm-detail-avlinorderiterator) | Шаблонный AVL mixin, повторно используется свободным деревом и persistent контейнерами. |
| [pallocator.h](../include/pmm/pallocator.h) | [pmm-pallocator](../include/pmm/pallocator.h#pmm-pallocator) | Persistent-aware C++ allocator-адаптер. |

Связанные требования: [feat-002](../req/04_features.md#feat-002),
[fr-004..fr-013](../req/05_functional_requirements.md#fr-004),
[dr-013..dr-020](../req/06_data_requirements.md#dr-013),
[qa-perf-001](../req/08_quality_attributes.md#qa-perf-001).

### Pointer model / `pptr`

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [pptr.h](../include/pmm/pptr.h) | [pmm-pptr](../include/pmm/pptr.h#pmm-pptr), [pmm-pptr-byte_offset](../include/pmm/pptr.h#pmm-pptr-byte_offset) | `pptr<T>` как гранульный индекс, портируемый между процессами и адресами. |

Связанные требования: [feat-003](../req/04_features.md#feat-003),
[fr-007](../req/05_functional_requirements.md#fr-007),
[fr-008](../req/05_functional_requirements.md#fr-008),
[fr-030](../req/05_functional_requirements.md#fr-030),
[fr-033](../req/05_functional_requirements.md#fr-033),
[dr-007](../req/06_data_requirements.md#dr-007),
[qa-port-001](../req/08_quality_attributes.md#qa-port-001),
[con-007](../req/09_constraints.md#con-007),
[rule-001](../req/02_business_rules.md#rule-001).

### Diagnostics / validation / repair

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [diagnostics.h](../include/pmm/diagnostics.h) | [pmm-recoverymode](../include/pmm/diagnostics.h#pmm-recoverymode), [pmm-violationtype](../include/pmm/diagnostics.h#pmm-violationtype), [pmm-diagnosticaction](../include/pmm/diagnostics.h#pmm-diagnosticaction), [pmm-diagnosticentry](../include/pmm/diagnostics.h#pmm-diagnosticentry), [pmm-verifyresult](../include/pmm/diagnostics.h#pmm-verifyresult) | `RecoveryMode`, `ViolationType`, `DiagnosticAction`, `DiagnosticEntry`, `VerifyResult`. |
| [validation.h](../include/pmm/validation.h) | — | Validation/verification API, опирается на `verify_repair_mixin.inc`. |
| [verify_repair_mixin.inc](../include/pmm/verify_repair_mixin.inc) | — | Циклы валидации/repair, восстановление linked-list/free-tree. |
| [types.h](../include/pmm/types.h) | [pmm-pmmerror](../include/pmm/types.h#pmm-pmmerror), [pmm-memorystats](../include/pmm/types.h#pmm-memorystats), [pmm-blockview](../include/pmm/types.h#pmm-blockview), [pmm-freeblockview](../include/pmm/types.h#pmm-freeblockview), [pmm-detail-managerheader](../include/pmm/types.h#pmm-detail-managerheader) | `PmmError`, `MemoryStats`, `BlockView`, `FreeBlockView`, `detail::ManagerHeader`. |

Связанные требования: [feat-004](../req/04_features.md#feat-004),
[feat-010](../req/04_features.md#feat-010),
[fr-014..fr-025](../req/05_functional_requirements.md#fr-014),
[qa-rec-001](../req/08_quality_attributes.md#qa-rec-001),
[qa-rel-001](../req/08_quality_attributes.md#qa-rel-001),
[qa-rel-002](../req/08_quality_attributes.md#qa-rel-002),
[qa-diag-001](../req/08_quality_attributes.md#qa-diag-001),
[rule-006](../req/02_business_rules.md#rule-006).

### Typed API

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [typed_manager_api.h](../include/pmm/typed_manager_api.h) | [pmm-detail-persistmemorytypedapi-reallocate_typed](../include/pmm/typed_manager_api.h#pmm-detail-persistmemorytypedapi-reallocate_typed) | `allocate_typed`, `deallocate_typed`, `create_typed`, `destroy_typed`, `reallocate_typed`. |
| [typed_guard.h](../include/pmm/typed_guard.h) | — | RAII guard для `create_typed/destroy_typed` пар. |

Связанные требования: [fr-005](../req/05_functional_requirements.md#fr-005),
[fr-006](../req/05_functional_requirements.md#fr-006),
[fr-019](../req/05_functional_requirements.md#fr-019),
[fr-029](../req/05_functional_requirements.md#fr-029),
[rule-007](../req/02_business_rules.md#rule-007),
[asm-004](../req/11_assumptions_dependencies.md#asm-004).

### Persistent containers

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [pstring.h](../include/pmm/pstring.h) | [pmm-pstring](../include/pmm/pstring.h#pmm-pstring) | Persistent string. |
| [pstringview.h](../include/pmm/pstringview.h) | [pmm-pstringview](../include/pmm/pstringview.h#pmm-pstringview), [pmm-pstringview-intern](../include/pmm/pstringview.h#pmm-pstringview-intern) | Persistent immutable string view с interning через forest registry. |
| [parray.h](../include/pmm/parray.h) | [pmm-parray](../include/pmm/parray.h#pmm-parray) | Persistent dynamic array, `ensure_capacity` через `reallocate_typed`. |
| [pmap.h](../include/pmm/pmap.h) | [pmm-pmap](../include/pmm/pmap.h#pmm-pmap), [pmm-pmap-size](../include/pmm/pmap.h#pmm-pmap-size), [pmm-pmap-insert](../include/pmm/pmap.h#pmm-pmap-insert), [pmm-pmap-erase](../include/pmm/pmap.h#pmm-pmap-erase), [pmm-pmap-clear](../include/pmm/pmap.h#pmm-pmap-clear), [pmm-pmap-begin](../include/pmm/pmap.h#pmm-pmap-begin) | Persistent ordered map, AVL root в forest domain. |

Связанные требования: [feat-008](../req/04_features.md#feat-008),
[fr-017](../req/05_functional_requirements.md#fr-017),
[fr-018](../req/05_functional_requirements.md#fr-018),
[fr-031](../req/05_functional_requirements.md#fr-031),
[ur-008](../req/03_user_requirements.md#ur-008),
[con-012](../req/09_constraints.md#con-012),
[dr-009](../req/06_data_requirements.md#dr-009).

### Forest / domain registry

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [forest_registry.h](../include/pmm/forest_registry.h) | [pmm-detail-forestdomainrecord](../include/pmm/forest_registry.h#pmm-detail-forestdomainrecord), [pmm-detail-forestdomainregistry](../include/pmm/forest_registry.h#pmm-detail-forestdomainregistry) | Регистрация именованных persistent forest domains. |
| [forest_domain_mixin.inc](../include/pmm/forest_domain_mixin.inc) | — | API менеджера для domains: `register_domain`, `register_system_domain`, `has_domain`, `get_domain_root`, `set_domain_root`. |

Связанные требования: [feat-005](../req/04_features.md#feat-005),
[fr-011](../req/05_functional_requirements.md#fr-011),
[fr-012](../req/05_functional_requirements.md#fr-012),
[rule-008](../req/02_business_rules.md#rule-008),
[dr-009](../req/06_data_requirements.md#dr-009).

### IO helpers

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [io.h](../include/pmm/io.h) | — | save/load image helpers с CRC и diagnostics. |

Связанные требования: [if-003](../req/07_external_interfaces.md#if-003),
[fr-016](../req/05_functional_requirements.md#fr-016),
[ur-011](../req/03_user_requirements.md#ur-011).

### Presets / configurations

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [config.h](../include/pmm/config.h) | [pmm-config-sharedmutexlock](../include/pmm/config.h#pmm-config-sharedmutexlock), [pmm-config-nolock](../include/pmm/config.h#pmm-config-nolock) | Lock policies. |
| [logging_policy.h](../include/pmm/logging_policy.h) | [pmm-logging-nologging](../include/pmm/logging_policy.h#pmm-logging-nologging), [pmm-logging-stderrlogging](../include/pmm/logging_policy.h#pmm-logging-stderrlogging) | Logging policies. |
| [manager_concept.h](../include/pmm/manager_concept.h) | — | C++20 concept `PersistMemoryManagerConcept`. |
| [manager_configs.h](../include/pmm/manager_configs.h) | [pmm-basicconfig](../include/pmm/manager_configs.h#pmm-basicconfig), [pmm-staticconfig](../include/pmm/manager_configs.h#pmm-staticconfig) | Готовые конфигурации. |
| [pmm_presets.h](../include/pmm/pmm_presets.h) | — | Алиасы preset-ов для embedded/single-threaded/multi-threaded/industrial/large сценариев. |

Связанные требования: [feat-007](../req/04_features.md#feat-007),
[if-006](../req/07_external_interfaces.md#if-006),
[if-007](../req/07_external_interfaces.md#if-007),
[ur-006](../req/03_user_requirements.md#ur-006),
[qa-thread-001](../req/08_quality_attributes.md#qa-thread-001),
[qa-thread-002](../req/08_quality_attributes.md#qa-thread-002),
[rule-009](../req/02_business_rules.md#rule-009).

### Public manager

| Файл | Anchors | Назначение |
|------|---------|-----------|
| [persist_memory_manager.h](../include/pmm/persist_memory_manager.h) | [pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager), [pmm-persistmemorymanager-create](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-create), [pmm-persistmemorymanager-load](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-load), [pmm-persistmemorymanager-destroy](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-destroy), [pmm-persistmemorymanager-allocate](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-allocate) | Static API менеджера PMM, lifecycle (`create`/`load`/`destroy`/`is_initialized`), allocate/deallocate, root/domain registry, `last_error`/`clear_error`, статистики. |
| [arena_internals.h](../include/pmm/arena_internals.h) | [pmm-detail-checkedarithmetic](../include/pmm/arena_internals.h#pmm-detail-checkedarithmetic), [pmm-detail-arenaview](../include/pmm/arena_internals.h#pmm-detail-arenaview), [pmm-detail-walkcontrol](../include/pmm/arena_internals.h#pmm-detail-walkcontrol), [pmm-detail-blockwalker](../include/pmm/arena_internals.h#pmm-detail-blockwalker), [pmm-detail-growthpolicy](../include/pmm/arena_internals.h#pmm-detail-growthpolicy), [pmm-detail-initguard](../include/pmm/arena_internals.h#pmm-detail-initguard) | Внутренние утилиты арены: checked arithmetic, view-объекты, walker, growth policy, init guard. |

Связанные требования: [feat-001](../req/04_features.md#feat-001),
[feat-002](../req/04_features.md#feat-002),
[feat-004](../req/04_features.md#feat-004),
[fr-001..fr-034](../req/05_functional_requirements.md#fr-001),
[con-004](../req/09_constraints.md#con-004),
[con-005](../req/09_constraints.md#con-005),
[if-008](../req/07_external_interfaces.md#if-008),
[if-009](../req/07_external_interfaces.md#if-009).

## Покрытие требований PMM-anchors

В исходниках присутствует **79** PMM-anchor блоков; **17** — в тестах.
Скрипт [`scripts/check-requirements-catalog.py`](../scripts/check-requirements-catalog.py)
дополнительно проверяет:

- что каждый anchor, упомянутый в `req/*.md`, реально существует в указанном
  source-файле;
- что для каждого anchor, на который ссылается требование, в anchor-блоке есть
  обратная `req:` строка с этим id (двусторонняя трассировка);
- что все `req:` идентификаторы из anchor-блоков и тестов соответствуют
  реальным требованиям;
- что в `req/*.md` нет обращений вида `` `file` — анкер `anchor` ``: только
  Markdown-ссылки.

## Не охваченные anchors

Anchors без обратной `req:`-аннотации намеренно оставлены для внутренних
helper-точек, которые покрываются родительским anchor-ом:

- `pmm-detail-*` группы в `arena_internals.h`, `avl_tree_mixin.h`,
  `forest_registry.h`, `heap_storage.h`, `typed_manager_api.h` — внутренние
  utility-объекты, чья трассировка покрывается публичным anchor-ом подсистемы
  (`pmm-persistmemorymanager*`, `pmm-avlfreetree`, и т.д.);
- helper-anchors в `pmm-blockstatebase-*`, `pmm-freeblock-*`,
  `pmm-allocatedblock-*`, `pmm-pmap-*` — функциональные операции,
  принадлежащие parent anchor-блоку;
- `pmm-config-*`, `pmm-logging-*`, `pmm-nodetype-helpers`, `pmm-nodetype-for`
  — мелкие policy/helper anchors.

Эти anchors разрешены `check-requirements-catalog.py` через allowlist
[`req/.catalog-allowlist.json`](../req/.catalog-allowlist.json).

## Связь с каталогом требований

Документ согласован с:

- [req/README.md](../req/README.md) — общие правила каталога;
- [req/13_traceability_matrix.md](../req/13_traceability_matrix.md) — сводная
  трассировка `BR → UR/FEAT → FR/QA/DR/IF/CON → AC`;
- [req/templates/](../req/templates/README.md) — шаблоны требований по типам.
