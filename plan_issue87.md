# План рефакторинга: Повышение уровня абстракции библиотеки менеджера ПАП

> **Issue**: [#87 — Повышение уровня абстракции библиотеки менеджера ПАП](https://github.com/netkeep80/PersistMemoryManager/issues/87)
> **Базовая версия библиотеки**: 6.1.0 (после Issue #73, #75, #83)
> **Цель**: Превратить библиотеку в абстрактный конструктор менеджеров ПАП

---

## 1. Code Review текущей архитектуры

### 1.1 Текущая структура файлов

```
include/
├── persist_memory_types.h   — BlockHeader, ManagerHeader, pptr, константы, helpers
├── persist_avl_tree.h       — PersistentAvlTree (all-static)
├── pmm_config.h             — PMMConfig<>, SharedMutexLock, NoLock
├── persist_memory_manager.h — pptr<T>, PmmCore<>, StatsMixin<>, ValidationMixin<>,
│                              PersistMemoryManager<Config>
└── persist_memory_io.h      — save(), load_from_file()
```

### 1.2 Текущая цепочка CRTP

```
ValidationMixin<StatsMixin<PmmCore<PersistMemoryManager<Config>>>>
```

Единственный конфигурационный параметр — `Config = PMMConfig<GranuleSize, MaxMemoryGB, LockPolicy, GrowNum, GrowDen>`.

### 1.3 Жёстко зашитые зависимости (hardcoded)

| Зависимость | Место | Проблема |
|---|---|---|
| `kGranuleSize = 16` | `persist_memory_types.h:30` | Не параметрически заменяемо |
| Тип индекса `uint32_t` | `BlockHeader`, `pptr<T>`, все helpers | Шина адреса фиксирована (32 бит) |
| `BlockHeader` (32 байта, 6 полей) | `persist_memory_types.h:93` | Совмещает список и AVL-дерево |
| `ManagerHeader` (64 байта) | `persist_memory_types.h:118` | Содержит все поля разных абстракций |
| Стратегия выделения памяти | `expand()` в PMM: `std::malloc` | Только динамический бэкенд |
| `pptr<T>` резолвит через синглтон `PersistMemoryManager<>` | `persist_memory_manager.h:891` | Только дефолтный конфиг |
| Одновременно в памяти: 1 инстанция | `static s_instance` | Нет независимых именованных зон |
| Алгоритм поиска free-блока | AVL best-fit (фиксировано) | Нельзя заменить на first-fit |
| Слияние блоков (coalesce) | всегда, немедленно | Нельзя отключить |

### 1.4 Главные абстракции, которые необходимо выделить

На основе анализа кода и требований Issue #87 выделены следующие независимые абстракции:

```
┌─────────────────────────────────────────────────────┐
│                  PersistMemoryManager                │  ← менеджер верхнего уровня
│  (StorageBackend, AllocPolicy, ThreadPolicy, Stats)  │
├─────────────────────────────────────────────────────┤
│              Allocator Policy                        │  ← стратегия выделения
│        (FreeListAllocator<TreePolicy>)               │
├─────────────────────────────────────────────────────┤
│      FreeBlockTree (AVL / FirstFit / BestFit)        │  ← алгоритм поиска блока
├─────────────────────────────────────────────────────┤
│      Block<LinkedListNode, TreeNode, AddressTraits>  │  ← блок как компонент
├────────────────────┬────────────────────────────────┤
│  LinkedListNode<A> │     TreeNode<A, KeyType>        │  ← элементы структур данных
├────────────────────┴────────────────────────────────┤
│            AddressTraits<IndexType, GranuleSize>     │  ← адресное пространство
├─────────────────────────────────────────────────────┤
│                StorageBackend                        │  ← откуда берётся память
│     (StaticStorage / HeapStorage / MMapStorage)      │
└─────────────────────────────────────────────────────┘
```

---

## 2. Выделенные абстракции и их взаимозависимость

### 2.1 AddressTraits — адресное пространство (нет зависимостей)

Описывает свойства адресного пространства ПАП:
- `index_type` — тип гранульного индекса (`uint8_t`, `uint16_t`, `uint32_t`)
- `granule_size` — размер гранулы в байтах (степень двойки, 1..N)
- `no_block` — sentinel для «нет блока» (`max value of index_type`)
- Вспомогательные функции: `bytes_to_granules`, `granules_to_bytes`, `idx_to_byte_off`, `byte_off_to_idx`

**Зависит от**: ничего
**Нужен для**: LinkedListNode, TreeNode, StorageBackend, Block

### 2.2 LinkedListNode — узел двухсвязного списка (зависит от AddressTraits)

Поля: `prev_offset`, `next_offset` (тип — `AddressTraits::index_type`)
Методы: обход, вставка, удаление (статические, принимают `base_ptr`)

**Зависит от**: AddressTraits
**Нужен для**: Block

### 2.3 TreeNode — абстрактный узел дерева (зависит от AddressTraits)

Параметры:
- `KeyType` — тип ключа (для дерева свободных блоков — `(size_granules, block_idx)`)
- Поля: `left_offset`, `right_offset`, `parent_offset`, `avl_height`

**Зависит от**: AddressTraits
**Нужен для**: FreeBlockTree, Block

### 2.4 Block — блок памяти (зависит от LinkedListNode + TreeNode)

Наследует: `LinkedListNode<A>` + `TreeNode<A, KeyType>` через статический полиморфизм
Поле: `size` (данные или 0 = свободен)
BlockState machine — опционально (см. 2.8)

**Зависит от**: AddressTraits, LinkedListNode, TreeNode
**Нужен для**: AllocatorPolicy, StorageBackend

### 2.5 FreeBlockTree — структура данных свободных блоков (зависит от TreeNode)

Стратегии (заменяемы через политику):
- `AvlFreeTree<A>` — текущая реализация AVL best-fit
- `LinearFreeList<A>` — first-fit для embedded
- `BitmapFreeMap<A>` — bitmap для фиксированных блоков

**Зависит от**: AddressTraits, TreeNode
**Нужен для**: AllocatorPolicy

### 2.6 AllocatorPolicy — алгоритм выделения/освобождения (зависит от Block + FreeBlockTree)

Включает логику:
- `allocate_from_block()` — выделение из найденного блока
- `coalesce()` — слияние соседних свободных блоков
- `split_block()` — разбивка блока при частичном выделении

**Зависит от**: Block, FreeBlockTree, AddressTraits
**Нужен для**: StorageBackend

### 2.7 StorageBackend — бэкенд хранилища (независим от AllocatorPolicy)

Три реализации:
- `StaticStorage<Size, A>` — массив на стеке/глобально, размер = compile-time константа
- `HeapStorage<A>` — `std::malloc`/`std::free`, авторасширение (текущий `expand()`)
- `MMapStorage<A>` — отображение файла в память (для персистентности)

**Зависит от**: AddressTraits
**Нужен для**: PersistMemoryManager

### 2.8 ThreadPolicy — политика многопоточности (нет зависимостей)

*Уже реализована как `SharedMutexLock` / `NoLock` в `pmm_config.h`.*
Требуется только инжектировать как параметр шаблона глубже — в AllocatorPolicy.

### 2.9 ExtensionMixin — расширения функциональности (зависит от PersistMemoryManager)

*Уже реализовано как CRTP-миксины `StatsMixin` и `ValidationMixin`.*
Нужно обобщить механизм и задокументировать как точку расширения.

### 2.10 PersistMemoryManager — итоговый конструктор менеджера

```cpp
template<
    typename AddressTraits,     // адресное пространство
    typename StorageBackend,    // бэкенд хранилища
    typename FreeBlockTree,     // структура данных свободных блоков
    typename ThreadPolicy,      // политика многопоточности
    typename... Extensions      // CRTP-миксины расширений
>
class AbstractPersistMemoryManager;
```

**Зависит от**: всех вышеперечисленных абстракций

---

## 3. Граф зависимостей

```
AddressTraits
    ├── LinkedListNode<A>
    │       └── Block<A>
    ├── TreeNode<A, Key>
    │       ├── Block<A>
    │       └── FreeBlockTree<A>
    ├── StorageBackend<A>
    │       └── PersistMemoryManager
    └── (нет зависимостей) → ThreadPolicy
                                └── PersistMemoryManager

Block<A> + FreeBlockTree<A> + ThreadPolicy
    └── AllocatorPolicy
            └── PersistMemoryManager

PersistMemoryManager + Extensions...
    └── Конкретный менеджер (инстанциация из pmm_presets.h)
```

---

## 4. Целевая архитектура файлов

После рефакторинга:

```
include/
├── pmm/
│   ├── address_traits.h          — AddressTraits<IndexType, GranuleSize>
│   ├── linked_list_node.h        — LinkedListNode<AddressTraits>
│   ├── tree_node.h               — TreeNode<AddressTraits, KeyType>
│   ├── block.h                   — Block<AddressTraits> : LinkedListNode + TreeNode
│   ├── block_state.h             — BlockState machine (опционально, Issue #87 п.7)
│   ├── free_block_tree.h         — FreeBlockTree policy concept + AvlFreeTree<A>
│   ├── allocator_policy.h        — AllocatorPolicy<Block, FreeBlockTree>
│   ├── storage_backend.h         — StorageBackend concept
│   ├── static_storage.h          — StaticStorage<Size, AddressTraits>
│   ├── heap_storage.h            — HeapStorage<AddressTraits>
│   ├── mmap_storage.h            — MMapStorage<AddressTraits>
│   ├── thread_policy.h           — SharedMutexLock, NoLock (из pmm_config.h)
│   ├── stats_mixin.h             — StatsMixin<Base>
│   ├── validation_mixin.h        — ValidationMixin<Base>
│   ├── abstract_pmm.h            — AbstractPersistMemoryManager<...>
│   └── pmm_presets.h             — готовые инстанции менеджеров
│
├── persist_memory_manager.h      — обратная совместимость: using DefaultPMM = ...
├── persist_memory_types.h        — обратная совместимость: re-export + legacy defs
├── persist_avl_tree.h            — обратная совместимость: re-export PersistentAvlTree
├── pmm_config.h                  — обратная совместимость: re-export DefaultConfig
└── persist_memory_io.h           — save/load (обновлён для нового API)
```

### Файл `pmm_presets.h` — примеры готовых инстанций

```cpp
// pmm_presets.h
namespace pmm::presets {

// Минимальный однопоточный статический менеджер (embedded, 8-bit, 8 байт гранула)
using EmbeddedStatic1K = AbstractPersistMemoryManager<
    AddressTraits<uint8_t, 8>,
    StaticStorage<1024>,
    LinearFreeList,
    NoLock
>;

// Стандартный однопоточный динамический менеджер (совместим с текущим DefaultConfig + NoLock)
using SingleThreadedHeap = AbstractPersistMemoryManager<
    AddressTraits<uint32_t, 16>,
    HeapStorage<>,
    AvlFreeTree,
    NoLock
>;

// Стандартный многопоточный динамический менеджер (совместим с текущим DefaultConfig)
using MultiThreadedHeap = AbstractPersistMemoryManager<
    AddressTraits<uint32_t, 16>,
    HeapStorage<>,
    AvlFreeTree,
    SharedMutexLock,
    StatsMixin, ValidationMixin
>;

// Многопоточный файловый менеджер (персистентность через mmap)
using PersistentFileMapped = AbstractPersistMemoryManager<
    AddressTraits<uint32_t, 16>,
    MMapStorage<>,
    AvlFreeTree,
    SharedMutexLock,
    StatsMixin, ValidationMixin
>;

// Крупная промышленная персистентная база данных (64-bit адресация, 64 байт гранула)
using IndustrialDB = AbstractPersistMemoryManager<
    AddressTraits<uint64_t, 64>,
    MMapStorage<>,
    AvlFreeTree,
    SharedMutexLock,
    StatsMixin, ValidationMixin
>;

} // namespace pmm::presets
```

---

## 5. План постепенного перехода (фазы)

### Фаза 0: Подготовка и тесты-маяки (Issue #87, текущий PR)

**Задачи:**
1. Написать этот документ (`plan_issue87.md`).
2. Написать `test_issue87_abstraction.cpp` — тесты-маяки, которые:
   - Проверяют текущую архитектуру (проходят сейчас)
   - Описывают требуемые интерфейсы будущих абстракций (закомментированы / `#if 0`)
   - Служат спецификацией для последующих фаз

**Критерии успеха фазы 0:**
- Все существующие тесты проходят (регрессия = 0)
- `plan_issue87.md` задокументирован в PR
- Тест-маяки описывают ожидаемые интерфейсы

---

### Фаза 1: AddressTraits — адресное пространство

**Новые файлы:**
- `include/pmm/address_traits.h`

**Содержание:**
```cpp
template<typename IndexT, std::size_t GranuleSz>
struct AddressTraits {
    using index_type = IndexT;
    static constexpr std::size_t granule_size = GranuleSz;
    static constexpr index_type  no_block = std::numeric_limits<IndexT>::max();
    // ... conversion functions
};

// Стандартный 32-bit вариант (совместим с текущим)
using DefaultAddressTraits = AddressTraits<std::uint32_t, 16>;
```

**Обратная совместимость:**
- `kGranuleSize`, `kNoBlock` в `persist_memory_types.h` → делегируют в `DefaultAddressTraits`
- `bytes_to_granules`, `granules_to_bytes` и др. → шаблонные версии

**Тесты (`test_issue87_phase1.cpp`):**
- `AddressTraits<uint8_t, 8>::no_block == 0xFF`
- `AddressTraits<uint16_t, 16>::granule_size == 16`
- `AddressTraits<uint32_t, 16>` идентичен текущим константам
- Проверка static_assert'ов (степень двойки, т.д.)

---

### Фаза 2: LinkedListNode и TreeNode

**Новые файлы:**
- `include/pmm/linked_list_node.h`
- `include/pmm/tree_node.h`

**Содержание:**
```cpp
// Узел двухсвязного списка
template<typename AddressTraits>
struct LinkedListNode {
    using index_type = typename AddressTraits::index_type;
    index_type prev_offset;
    index_type next_offset;
};

// Узел AVL-дерева
template<typename AddressTraits>
struct TreeNode {
    using index_type = typename AddressTraits::index_type;
    index_type   left_offset;
    index_type   right_offset;
    index_type   parent_offset;
    std::int16_t avl_height;
    std::uint16_t _pad;
};
```

**Обратная совместимость:**
- `BlockHeader` в `persist_memory_types.h` сохраняет совместимость через `using`/`static_assert`
- Все существующие тесты проходят

---

### Фаза 3: Block — блок как составной тип

**Новые файлы:**
- `include/pmm/block.h`

**Содержание:**
```cpp
template<typename AddressTraits>
struct Block : LinkedListNode<AddressTraits>, TreeNode<AddressTraits> {
    using index_type = typename AddressTraits::index_type;
    index_type   size;        // 0 = свободен
    index_type   root_offset; // для валидации (Issue #75)
};
```

**static_assert:** `sizeof(Block<DefaultAddressTraits>) == sizeof(BlockHeader)` — гарантия совместимости.

**Обратная совместимость:**
- `using BlockHeader = Block<DefaultAddressTraits>` или бинарная совместимость через `static_assert`

---

### Фаза 4: FreeBlockTree как шаблонная политика

**Новые файлы:**
- `include/pmm/free_block_tree.h`

**Содержание:**
```cpp
// Концепт политики дерева свободных блоков (C++20 concept или SFINAE)
template<typename Policy>
concept FreeBlockTreePolicy = requires(
    typename Policy::address_traits::index_type idx,
    std::uint8_t* base,
    /* ManagerHeader */ void* hdr
) {
    { Policy::insert(base, hdr, idx) } -> std::same_as<void>;
    { Policy::remove(base, hdr, idx) } -> std::same_as<void>;
    { Policy::find_best_fit(base, hdr, idx) } -> std::same_as<decltype(idx)>;
};

// Текущая реализация становится конкретной политикой
template<typename AddressTraits>
using AvlFreeTree = PersistentAvlTree<AddressTraits>;
```

---

### Фаза 5: StorageBackend — три бэкенда

**Новые файлы:**
- `include/pmm/storage_backend.h`
- `include/pmm/static_storage.h`
- `include/pmm/heap_storage.h`
- `include/pmm/mmap_storage.h`

**Концепт:**
```cpp
template<typename Backend>
concept StorageBackendConcept = requires(Backend b, std::size_t size) {
    { b.base_ptr() } -> std::same_as<std::uint8_t*>;
    { b.total_size() } -> std::same_as<std::size_t>;
    { b.expand(size) } -> std::same_as<bool>;
    { b.owns_memory() } -> std::same_as<bool>;
};
```

**StaticStorage:**
```cpp
template<std::size_t Size, typename AddressTraits = DefaultAddressTraits>
class StaticStorage {
    alignas(AddressTraits::granule_size) std::uint8_t _buffer[Size];
public:
    std::uint8_t* base_ptr() { return _buffer; }
    constexpr std::size_t total_size() const { return Size; }
    bool expand(std::size_t) { return false; } // нельзя расширить
    constexpr bool owns_memory() const { return false; } // на стеке
};
```

**HeapStorage** — рефакторинг текущего `expand()` из `PersistMemoryManager`.

**MMapStorage** — новая реализация через `mmap`/`MapViewOfFile`.

---

### Фаза 6: AllocatorPolicy

**Новые файлы:**
- `include/pmm/allocator_policy.h`

Вынести из `PersistMemoryManager`:
- `allocate_from_block()`
- `coalesce()`
- `rebuild_free_tree()`
- `repair_linked_list()`
- `recompute_counters()`

Параметризовать через `FreeBlockTree` и `AddressTraits`.

---

### Фаза 7: AbstractPersistMemoryManager

**Новые файлы:**
- `include/pmm/abstract_pmm.h`

```cpp
template<
    typename AddressTraitsT   = DefaultAddressTraits,
    typename StorageBackendT  = HeapStorage<AddressTraitsT>,
    typename FreeBlockTreeT   = AvlFreeTree<AddressTraitsT>,
    typename ThreadPolicyT    = config::SharedMutexLock,
    template<typename> class... Extensions
>
class AbstractPersistMemoryManager
    : public Extensions<AbstractPersistMemoryManager<...>>...
{
    // ...
};
```

---

### Фаза 8: pmm_presets.h — готовые инстанции

**Новые файлы:**
- `include/pmm/pmm_presets.h`

Содержит все примеры из раздела 4 этого документа.

---

### Фаза 9: BlockState machine (опционально)

**Новые файлы:**
- `include/pmm/block_state.h`

Реализует автомат состояний блока через наследование (Issue #87 п.7):
- `FreeBlock` — только методы: `allocate()`, `coalesce()`
- `AllocatedBlock` — только методы: `deallocate()`, `reallocate()`

---

### Фаза 10: Обратная совместимость и обновление существующих заголовков

- Обновить `persist_memory_manager.h`: `using PersistMemoryManager<Config> = AbstractPersistMemoryManager<...>`
- Обновить `persist_memory_types.h`: re-export из новых заголовков
- Обновить `pmm_config.h`: re-export + добавить новые пресеты
- Все существующие тесты проходят без изменений

---

## 6. Обратная совместимость

### Принципы

1. **Бинарная совместимость**: `sizeof(Block<DefaultAddressTraits>) == sizeof(BlockHeader)` — гарантируется `static_assert`
2. **API совместимость**: `pmm::PersistMemoryManager<>` остаётся рабочим псевдонимом
3. **Формат файлов**: `kMagic` сохраняется; `granule_size` в `ManagerHeader` проверяется при загрузке
4. **Нет breaking changes** в пределах каждой фазы

### Стратегия

Каждая фаза:
- Добавляет новые абстракции в `include/pmm/` (новая директория)
- Существующие заголовки перенаправляют на новые (re-export / `using`)
- Все существующие тесты проходят **без изменений**

---

## 7. Тестовая стратегия

### Новые тест-файлы (по фазам)

| Фаза | Тест | Что проверяет |
|------|------|---------------|
| 0 | `test_issue87_abstraction.cpp` | Тесты-маяки, спецификация интерфейсов |
| 1 | `test_issue87_phase1.cpp` | AddressTraits (8/16/32 bit, разные гранулы) |
| 2 | `test_issue87_phase2.cpp` | LinkedListNode, TreeNode |
| 3 | `test_issue87_phase3.cpp` | Block (размеры, совместимость с BlockHeader) |
| 4 | `test_issue87_phase4.cpp` | FreeBlockTree как политика |
| 5 | `test_issue87_phase5.cpp` | StaticStorage, HeapStorage, MMapStorage |
| 6 | `test_issue87_phase6.cpp` | AllocatorPolicy (независимые unit-тесты) |
| 7 | `test_issue87_phase7.cpp` | AbstractPersistMemoryManager |
| 8 | `test_issue87_phase8.cpp` | pmm_presets.h — все инстанции компилируются и работают |

### Принцип тестирования

- **Независимое тестирование**: каждая абстракция тестируется отдельно
- **Мок-зависимости**: AbortStorageBackend, TrivialFreeList и т.д. для unit-тестов
- **Регрессионные тесты**: все существующие тесты проходят на каждой фазе

---

## 8. Риски и ограничения

| Риск | Митигация |
|------|-----------|
| Разбухание кода шаблонов | Явные инстанциации для популярных типов; мониторинг размера бинарника |
| Нарушение бинарной совместимости при смене AddressTraits | `static_assert` на размер структур; версионирование kMagic |
| Сложность отладки при глубокой вложенности шаблонов | Использование концептов C++20 для ранней диагностики ошибок |
| Очень большой объём работы | Фазовый подход: каждая фаза — отдельный PR, независимо тестируемый |
| MMapStorage: платформо-зависимость | Использовать `std::filesystem` + условная компиляция `#ifdef _WIN32` |

---

## 9. Оценка объёма работ

| Фаза | Сложность | Объём |
|------|-----------|-------|
| 0: Маяки и план | Низкая | ~100 строк |
| 1: AddressTraits | Низкая | ~150 строк |
| 2: LinkedListNode + TreeNode | Низкая | ~150 строк |
| 3: Block | Средняя | ~200 строк |
| 4: FreeBlockTree policy | Средняя | ~300 строк |
| 5: StorageBackend (3 варианта) | Высокая | ~500 строк |
| 6: AllocatorPolicy | Средняя | ~400 строк |
| 7: AbstractPMM | Высокая | ~600 строк |
| 8: pmm_presets.h | Низкая | ~150 строк |
| 9: BlockState machine | Высокая | ~500 строк |
| 10: Совместимость + тесты | Средняя | ~400 строк |

**Итого**: ~3450 строк нового кода, 11 новых заголовков, ~9 новых тест-файлов.

---

## 10. Следующие шаги (текущий PR #88)

Для текущего PR:

1. ✅ Написан `plan_issue87.md` — данный документ
2. ✅ Написан `test_issue87_abstraction.cpp` — тесты-маяки (Фаза 0)
3. ⬜ PR помечен как [WIP] до завершения всех фаз

Дальнейшая работа ведётся отдельными PR (по одной фазе каждый).
