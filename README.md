# PersistMemoryManager

Менеджер Персистентного Адресного Пространства (ПАП) — header-only C++20 библиотека для управления
персистентной памятью со статическим API, конфигурируемыми бэкендами хранения и политиками потокобезопасности.

[![CI](https://github.com/netkeep80/PersistMemoryManager/actions/workflows/ci.yml/badge.svg)](https://github.com/netkeep80/PersistMemoryManager/actions/workflows/ci.yml)
[![License: Unlicense](https://img.shields.io/badge/license-Unlicense-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![Version](https://img.shields.io/badge/version-0.26.0-green.svg)](CHANGELOG.md)
[![Docs](https://img.shields.io/badge/docs-Doxygen-informational)](https://netkeep80.github.io/PersistMemoryManager/)

## Обзор

PersistMemoryManager (pmm) — блочный аллокатор памяти, хранящий все метаданные внутри управляемой
области с использованием гранульных смещений вместо сырых указателей. Это делает образ кучи
переносимым: его можно сохранить в файл, загрузить по другому базовому адресу и продолжить
работу без корректировки указателей.

pmm является фундаментом для построения персистентных систем хранения данных, таких как
[BinDiffSynchronizer](https://github.com/netkeep80/BinDiffSynchronizer) (pjson_db_pmm) —
персистентная JSON-база данных.

**Принцип:** pmm — менеджер ПАП и специальные типы объектов для использования в ПАП.
pmm предоставляет типы и хранилище, а не конкретные прикладные реализации (JSON, базы данных и т.д.).

### Ключевые свойства

- **Header-only** — подключи и используй, без компиляции
- **C++20** — использует concepts (`requires`) для валидации политик
- **Статический API** — без экземпляров, все методы `static`
- **Мультитон** — несколько независимых менеджеров через шаблонный параметр `InstanceId`
- **Конфигурируемость** — бэкенд хранения, политика блокировки и разрядность адресов настраиваются независимо
- **Персистентность** — сохранение/загрузка образа кучи в файл; все внутренние ссылки — смещения
- **Best-fit аллокация** — AVL-дерево свободных блоков с коалесценцией
- **Персистентные типы** — встроенные `pstringview` (интернированные строки), `pmap<K,V>` (AVL-словарь), `pvector<T>` (вектор)
- **Безопасность** — CRC32 контрольные суммы, атомарное сохранение, проверка границ, защита от переполнения

## Быстрый старт

### Вариант 1: Single-header (рекомендуется)

Скачайте `single_include/pmm/pmm.h` — полная библиотека без пресетов — и используйте любую конфигурацию:

```cpp
#include "pmm.h"
#include "pmm/pmm_presets.h"

using Mgr = pmm::presets::SingleThreadedHeap;

int main() {
    Mgr::create(64 * 1024);  // 64 КБ куча

    Mgr::pptr<int> p = Mgr::allocate_typed<int>();
    *p = 42;
    int value = *p;  // 42

    Mgr::deallocate_typed(p);
    Mgr::destroy();
}
```

Или используйте single-header файл с пресетом (библиотека + alias пресета в одном файле):

```cpp
#include "pmm_single_threaded_heap.h"

using Mgr = pmm::presets::SingleThreadedHeap;
```

Доступные single-header файлы в `single_include/pmm/`:

| Файл | Пресет | Индекс | Потокобезопасность | Применение |
|------|--------|--------|--------------------|------------|
| `pmm.h` | *(нет — полная библиотека)* | любой | любая | Свои конфигурации |
| `pmm_small_embedded_static_heap.h` | `SmallEmbeddedStaticHeap<N>` | `uint16_t` (2 Б) | Нет | ARM Cortex-M, AVR, ESP32 |
| `pmm_embedded_static_heap.h` | `EmbeddedStaticHeap<N>` | `uint32_t` (4 Б) | Нет | Bare-metal, RTOS |
| `pmm_embedded_heap.h` | `EmbeddedHeap` | `uint32_t` (4 Б) | Нет | Встраиваемые с heap |
| `pmm_single_threaded_heap.h` | `SingleThreadedHeap` | `uint32_t` (4 Б) | Нет | Кэши, однопоточные утилиты |
| `pmm_multi_threaded_heap.h` | `MultiThreadedHeap` | `uint32_t` (4 Б) | `shared_mutex` | Конкурентные сервисы |
| `pmm_industrial_db_heap.h` | `IndustrialDBHeap` | `uint32_t` (4 Б) | `shared_mutex` | Нагруженные базы данных |
| `pmm_large_db_heap.h` | `LargeDBHeap` | `uint64_t` (8 Б) | `shared_mutex` | Петабайтные базы данных |

### Вариант 2: Multi-header

Подключите модульные заголовки из `include/pmm/`:

```cpp
#include "pmm/pmm_presets.h"

using Mgr = pmm::presets::MultiThreadedHeap;

int main() {
    Mgr::create(1024 * 1024);  // 1 МБ куча

    Mgr::pptr<double> p = Mgr::allocate_typed<double>(4);  // массив из 4 double
    (*p) = 3.14;

    Mgr::deallocate_typed(p);
    Mgr::destroy();
}
```

## Сборка

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

**Требования:** CMake 3.16+, компилятор C++20 (GCC 10+, Clang 10+, MSVC 2019 16.3+).

Демо-приложение (требует OpenGL + GLFW):

```bash
cmake -B build -DPMM_BUILD_DEMO=ON
cmake --build build --target pmm_demo
```

## Справочник API

### Жизненный цикл

```cpp
// Создать новую кучу размером initial_size байт
static bool create(std::size_t initial_size) noexcept;

// Инициализировать над уже заполненным бэкендом (например, MMapStorage)
static bool create() noexcept;

// Загрузить состояние из существующего образа бэкенда (проверяет magic + размеры + CRC32)
static bool load() noexcept;

// Сбросить менеджер (не освобождает буфер бэкенда)
static void destroy() noexcept;

// True, если менеджер успешно инициализирован
static bool is_initialized() noexcept;
```

### Аллокация

```cpp
// Выделить count объектов типа T; вернёт null pptr при неудаче
template <typename T>
static pptr<T> allocate_typed(std::size_t count = 1) noexcept;

// Освободить блок, полученный от allocate_typed
template <typename T>
static void deallocate_typed(pptr<T> p) noexcept;

// Создать объект (allocate + placement new); T должен быть nothrow-constructible
template <typename T, typename... Args>
static pptr<T> create_typed(Args&&... args) noexcept;

// Разрушить объект (explicit destructor + deallocate); T должен быть nothrow-destructible
template <typename T>
static void destroy_typed(pptr<T> p) noexcept;

// Сырая аллокация / деаллокация (размер в байтах)
static void* allocate(std::size_t size) noexcept;
static void  deallocate(void* ptr) noexcept;
```

### Статистика

```cpp
static std::size_t total_size()    noexcept;  // всего управляемых байт
static std::size_t used_size()     noexcept;  // байт в живых аллокациях
static std::size_t free_size()     noexcept;  // доступно байт
static double      fragmentation() noexcept;  // 0.0 – 1.0
static MemoryStats get_stats()     noexcept;  // снимок всех счётчиков
static ManagerInfo get_manager_info() noexcept;
```

### Диагностика

```cpp
static bool        validate()     noexcept;  // проверка структурной целостности
static bool        dump_stats()   noexcept;  // вывод статистики в stdout
template <typename T>
static bool        is_valid_ptr(pptr<T> p) noexcept;  // валидация указателя
```

### Персистентность (io.h)

```cpp
#include "pmm/io.h"

// Сохранить управляемую область в файл (CRC32 + атомарная запись)
template <typename MgrT>
bool pmm::save_manager(const char* filename);

// Загрузить ранее сохранённый образ (с проверкой CRC32)
template <typename MgrT>
bool pmm::load_manager_from_file(const char* filename);
```

Пример:

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

Mgr::pptr<int> p = Mgr::allocate_typed<int>();
*p = 99;

// Сохранение
pmm::save_manager<Mgr>("heap.dat");
Mgr::destroy();

// Восстановление в новом процессе (или после перезапуска)
Mgr::create(64 * 1024);
pmm::load_manager_from_file<Mgr>("heap.dat");

std::cout << *p;  // 99 — значение сохранено
Mgr::destroy();
```

### Блокировка блоков

Блок можно пометить как read-only для защиты от случайной деаллокации. Используется
внутри `pstringview` для гарантии, что интернированные строки не будут освобождены:

```cpp
Mgr::lock_block_permanent(p);           // запретить deallocate()
bool ro = Mgr::is_permanently_locked(p);
```

## Персистентный указатель — pptr\<T\>

`pptr<T, ManagerT>` хранит гранульный индекс (2, 4 или 8 байт в зависимости от address traits)
вместо сырого указателя. Он адресно-независим: образ кучи можно маппировать по любому
базовому адресу, и значения `pptr` останутся валидными.

```cpp
Mgr::pptr<int> p = Mgr::allocate_typed<int>();

if (p) {           // явная конверсия в bool
    *p = 42;       // разыменование через operator*
    p->field;      // доступ к полям через operator->
    p.offset();    // гранульный индекс
    p.is_null();   // то же, что !p
}
```

**Запрещённые операции** — арифметика указателей (`p++`, `p--`) удалена для безопасности.

### Доступ к узлу AVL-дерева (pptr)

`pptr` предоставляет прямой доступ к внутреннему `TreeNode` блока через `tree_node()`,
позволяя строить пользовательские AVL-деревья поверх блоков pmm:

```cpp
auto& tn = p.tree_node();  // ссылка на TreeNode в заголовке блока

// Чтение связей (возвращают гранульный индекс или no_block)
tn.get_left();     // index_type — индекс левого потомка
tn.get_right();    // index_type — индекс правого потомка
tn.get_parent();   // index_type — индекс родителя
tn.get_weight();   // index_type — вес узла (размер данных в гранулах)
tn.get_height();   // std::int16_t — высота поддерева AVL

// Запись связей
tn.set_left(child.offset());
tn.set_right(child.offset());
tn.set_parent(parent.offset());
tn.set_height(h);
```

> **Примечание:** Отсутствующие связи хранятся как `address_traits::no_block` sentinel, а не как ноль.

## Персистентная строка — pstringview

`pstringview<ManagerT>` — интернированная, read-only персистентная строка. Равные строки
всегда хранятся один раз и возвращают один и тот же `pptr` — дедупликация гарантирована.

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

// Интернировать строку — создаёт в ПАП при первом вызове
Mgr::pptr<Mgr::pstringview> p = Mgr::pstringview("hello");
if (p) {
    const char* s = p->c_str();   // "hello"
    std::size_t n = p->size();    // 5
}

// Повторное интернирование возвращает тот же pptr
Mgr::pptr<Mgr::pstringview> p2 = Mgr::pstringview("hello");
assert(p == p2);  // идентичный гранульный индекс

Mgr::destroy();
```

**API:**

```cpp
const char* c_str()  const noexcept;  // null-terminated строка
std::size_t size()   const noexcept;  // длина без нуль-терминатора
bool        empty()  const noexcept;

bool operator==(const pstringview& o) const noexcept;
bool operator!=(const pstringview& o) const noexcept;
bool operator< (const pstringview& o) const noexcept;

static Mgr::pptr<pstringview> intern(const char* s) noexcept;
static void reset() noexcept;
```

**Особенности:**
- Все блоки `pstringview` перманентно заблокированы — не могут быть освобождены
- Дедупликация через встроенное AVL-дерево (поля `TreeNode` в заголовке блока)

## Персистентный словарь — pmap\<K, V\>

`pmap<_K, _V, ManagerT>` — персистентный AVL-словарь, хранящийся целиком в управляемой области.
Узлы используют встроенные поля `TreeNode` для AVL-связей.

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

using MyMap = Mgr::pmap<int, int>;

MyMap map;
map.insert(42, 100);
map.insert(10, 200);

auto p = map.find(42);
if (!p.is_null()) {
    int val = p->value;  // 100
}

map.insert(42, 300);  // дублирующий ключ — обновляет значение

Mgr::destroy();
```

**Особенности:**
- O(log n) insert, find, contains
- Дублирующий ключ при `insert` обновляет значение
- Узлы **не** перманентно заблокированы (в отличие от `pstringview`)
- Тип ключа `_K` должен поддерживать `operator<` и `operator==`

## Персистентный вектор — pvector\<T\>

`pvector<T, ManagerT>` — персистентный последовательный контейнер в управляемой области.
Реализован как AVL order-statistic дерево для O(log n) доступа по индексу.

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

using MyVec = Mgr::pvector<int>;

MyVec vec;
vec.push_back(10);
vec.push_back(20);
vec.push_back(30);

auto p = vec.at(1);
if (!p.is_null()) {
    int val = p->value;  // 20
}

// Итерация
for (auto it = vec.begin(); it != vec.end(); ++it) {
    auto node = *it;
    // node->value — элемент
}

vec.pop_back();  // удаляет 30
vec.clear();     // удаляет все элементы

Mgr::destroy();
```

**Особенности:**
- O(log n) push_back, pop_back, at(i)
- O(1) size, front, back
- Итератор для range-based for
- Узлы **не** перманентно заблокированы

## Конфигурация

### Встроенные пресеты

```cpp
#include "pmm/pmm_presets.h"

namespace pmm::presets {
    template <std::size_t N = 1024>
    using SmallEmbeddedStaticHeap = ...;  // 16-bit индекс, статический буфер

    template <std::size_t N = 4096>
    using EmbeddedStaticHeap = ...;       // 32-bit, статический буфер

    using EmbeddedHeap        = ...;      // 32-bit, динамический heap
    using SingleThreadedHeap  = ...;      // 32-bit, однопоточный
    using MultiThreadedHeap   = ...;      // 32-bit, многопоточный
    using IndustrialDBHeap    = ...;      // 32-bit, для нагруженных БД
    using LargeDBHeap         = ...;      // 64-bit, петабайтный масштаб
}
```

| Пресет | Индекс | pptr | Блокировка | Рост | Макс. куча | Применение |
|--------|--------|------|------------|------|------------|------------|
| `SmallEmbeddedStaticHeap<N>` | `uint16_t` | 2 Б | `NoLock` | нет | ~1 МБ | ARM Cortex-M, AVR, ESP32 |
| `EmbeddedStaticHeap<N>` | `uint32_t` | 4 Б | `NoLock` | нет | 64 ГБ | Bare-metal, RTOS |
| `EmbeddedHeap` | `uint32_t` | 4 Б | `NoLock` | 50% | 64 ГБ | Встраиваемые с heap |
| `SingleThreadedHeap` | `uint32_t` | 4 Б | `NoLock` | 25% | 64 ГБ | Кэши, утилиты |
| `MultiThreadedHeap` | `uint32_t` | 4 Б | `SharedMutexLock` | 25% | 64 ГБ | Конкурентные сервисы |
| `IndustrialDBHeap` | `uint32_t` | 4 Б | `SharedMutexLock` | 100% | 64 ГБ | Нагруженные БД |
| `LargeDBHeap` | `uint64_t` | 8 Б | `SharedMutexLock` | 100% | петабайт | Крупные БД |

### Пользовательская конфигурация

```cpp
#include "pmm/address_traits.h"
#include "pmm/config.h"
#include "pmm/heap_storage.h"
#include "pmm/mmap_storage.h"
#include "pmm/free_block_tree.h"

struct MyConfig {
    using address_traits  = pmm::DefaultAddressTraits;          // uint32_t индекс, 16-байт гранула
    using storage_backend = pmm::MMapStorage<address_traits>;   // файл-маппированное хранилище
    using free_block_tree = pmm::AvlFreeTree<address_traits>;   // AVL-дерево (обязательно)
    using lock_policy     = pmm::config::SharedMutexLock;       // многопоточность

    static constexpr std::size_t grow_numerator   = 3;  // рост на 50%
    static constexpr std::size_t grow_denominator = 2;
};

using MyMgr = pmm::PersistMemoryManager<MyConfig, 0>;
```

### Мультитон — несколько независимых экземпляров

```cpp
using Cache0 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 0>;
using Cache1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 1>;

Cache0::create(64 * 1024);
Cache1::create(32 * 1024);

Cache0::pptr<int> p0 = Cache0::allocate_typed<int>();
Cache1::pptr<int> p1 = Cache1::allocate_typed<int>();
// p0 и p1 — несовместимые типы, смешивание — ошибка компиляции
```

### Address traits

| Тип | Индекс | Гранула | pptr | Макс. адресация |
|-----|--------|---------|------|-----------------|
| `SmallAddressTraits` | `uint16_t` | 16 Б | 2 Б | ~1 МБ |
| `DefaultAddressTraits` | `uint32_t` | 16 Б | 4 Б | 64 ГБ |
| `LargeAddressTraits` | `uint64_t` | 64 Б | 8 Б | петабайт |

### Бэкенды хранения

| Класс | Описание |
|-------|----------|
| `HeapStorage<A>` | Динамическая аллокация через `malloc` / `realloc` |
| `MMapStorage<A>` | Файл-маппированная память (`mmap` / `MapViewOfFile`) — персистентность между запусками |
| `StaticStorage<Size, A>` | Статический массив фиксированного размера — для встраиваемых систем |

### Политики блокировки

| Политика | Описание |
|----------|----------|
| `config::NoLock` | Без синхронизации — для однопоточного кода |
| `config::SharedMutexLock` | `std::shared_mutex` — конкурентное чтение, эксклюзивная запись |

## C++20 Concepts

pmm предоставляет concepts для compile-time валидации пользовательских типов:

```cpp
#include "pmm/manager_concept.h"
#include "pmm/storage_backend.h"

static_assert(pmm::PersistMemoryManagerConcept<MyMgr>);
static_assert(pmm::StorageBackendConcept<MyStorage>);
```

## Архитектура

```
┌─────────────────────────────────────────────────────────┐
│                  Публичный API                           │
│  create / load / destroy / allocate / deallocate        │
│  pptr<T> / pstringview / pmap<K,V> / pvector<T>        │
├─────────────────────────────────────────────────────────┤
│             AllocatorPolicy                              │
│  best-fit поиск · разделение блоков · коалесценция      │
│  авто-расширение · AVL-ребалансировка                   │
├─────────────────────────────────────────────────────────┤
│              Слой сырой памяти                           │
│  StorageBackend → непрерывный буфер байтов              │
│  Block<AT> (TreeNode + prev/next смещения, 32 байта)    │
│  ManagerHeader (magic, размеры, счётчики, CRC32)        │
└─────────────────────────────────────────────────────────┘
```

**Схема памяти внутри управляемой области:**

```
[ManagerHeader][Block_0][data_0][Block_1][data_1] ...
```

- `ManagerHeader` хранится по смещению 0 в управляемой области
- Каждый блок имеет 32-байтный заголовок (`TreeNode` поля 0–23, `prev`/`next` 24–31)
- Все межблочные ссылки — гранульные индексы, не сырые указатели
- При `load()` связный список восстанавливается, AVL-дерево свободных блоков перестраивается

## Производительность

Измерено на одном ядре (Release-сборка, Linux x86-64, GCC 13):

| Операция | Количество | Время |
|----------|-----------|-------|
| `allocate` | 100 000 | ~7 мс |
| `deallocate` | 100 000 | ~0.8 мс |
| смешанный alloc/dealloc | 1 000 000 | ~14 мс (~14 нс/оп) |

## Структура репозитория

```
PersistMemoryManager/
├── include/
│   └── pmm/                          # Модульные заголовки
│       ├── persist_memory_manager.h  # Главный класс менеджера
│       ├── pptr.h                    # Персистентный указатель
│       ├── pstringview.h             # Интернированная строка (v0.11.0)
│       ├── pmap.h                    # AVL-словарь (v0.12.0)
│       ├── pvector.h                 # Вектор на AVL-дереве (v0.21.0)
│       ├── avl_tree_mixin.h          # Общие AVL-хелперы (v0.13.0)
│       ├── pmm_presets.h             # Алиасы пресетов
│       ├── manager_configs.h         # Конфигурации
│       ├── address_traits.h          # Address traits (Small/Default/Large)
│       ├── config.h                  # Политики блокировки
│       ├── heap_storage.h            # malloc-бэкенд
│       ├── mmap_storage.h            # mmap-бэкенд
│       ├── static_storage.h          # Статический бэкенд
│       ├── storage_backend.h         # Concept бэкенда
│       ├── allocator_policy.h        # Алгоритмы аллокации
│       ├── block.h                   # Схема блока
│       ├── block_state.h             # Машина состояний блока
│       ├── free_block_tree.h         # AVL-дерево свободных блоков
│       ├── tree_node.h               # Поля AVL-узла
│       ├── types.h                   # ManagerInfo, MemoryStats, константы
│       ├── io.h                      # Утилиты save/load
│       └── manager_concept.h         # C++20 concepts
├── single_include/
│   └── pmm/                          # Single-header файлы
│       ├── pmm.h                     # Полная библиотека (v0.10.0)
│       └── pmm_*.h                   # Файлы пресетов
├── examples/                         # Примеры использования
├── tests/                            # Тесты (130+)
├── demo/                             # Визуальное ImGui/OpenGL демо
├── docs/                             # Архитектура, API, план развития
│   ├── plan.md                       # План развития pmm
│   └── plan4BinDiffSynchronizer.md   # План миграции BinDiffSynchronizer
├── scripts/                          # Утилиты для релиза
└── CMakeLists.txt
```

## План развития

Полный план развития: [docs/plan.md](docs/plan.md)

**Ближайшие приоритеты (Фаза 3)** — типы для [BinDiffSynchronizer](https://github.com/netkeep80/BinDiffSynchronizer):

- `pstring<ManagerT>` — мутабельная персистентная строка
- `parray<T, ManagerT>` — массив с O(1) индексацией
- `pmap::erase()` — удаление из словаря
- `pallocator<T>` — STL-совместимый аллокатор
- `ppool<T>` — пул объектов

План миграции BinDiffSynchronizer: [docs/plan4BinDiffSynchronizer.md](docs/plan4BinDiffSynchronizer.md)

## Контрибьюция

См. [CONTRIBUTING.md](CONTRIBUTING.md). Ключевые моменты:

- C++20; целевые компиляторы: GCC 10+, Clang 10+, MSVC 2019 16.3+
- `pre-commit install` для локальных проверок (clang-format, cppcheck, secrets scan)
- Добавляйте [changelog fragment](changelog.d/README.md) в `changelog.d/` для каждого PR
- Лимит размера файла: 1500 строк
- Все новые фичи должны включать тесты

```bash
# Форматирование
clang-format -i include/pmm/your_file.h

# Статический анализ
cppcheck --std=c++20 include/

# Сборка и тесты
cmake -B build && cmake --build build && ctest --test-dir build
```

## Документация

- [API Reference (Doxygen)](https://netkeep80.github.io/PersistMemoryManager/)
- [Архитектура](docs/architecture.md)
- [API Reference (Markdown)](docs/api_reference.md)
- [Changelog](CHANGELOG.md)
- [План развития](docs/plan.md)
- [План миграции BinDiffSynchronizer](docs/plan4BinDiffSynchronizer.md)

## Лицензия

Свободное ПО, выпущенное в общественное достояние. См. [LICENSE](LICENSE).
