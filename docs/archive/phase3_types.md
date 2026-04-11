# Фаза 3: Типы для BinDiffSynchronizer

> Реализация начата в Issue #45. Продолжена в Issue #195.

## 3.1 Мутабельная персистентная строка `pstring<ManagerT>` ✅

**Issue:** #45
**Файл:** `include/pmm/pstring.h`
**Тесты:** `tests/test_issue45_pstring.cpp` (21 тест)

### Описание

`pstring<ManagerT>` — мутабельная строка, хранящаяся в персистентном адресном пространстве (ПАП).
В отличие от `pstringview` (read-only, interned), `pstring` поддерживает изменение содержимого.

### Архитектура

```
pstring (в ПАП)           Блок данных (в ПАП)
┌───────────────┐         ┌─────────────────────┐
│ _length: u32  │         │ h e l l o \0        │
│ _capacity: u32│──idx──→ │                     │
│ _data_idx     │         └─────────────────────┘
└───────────────┘
```

- **Заголовок** (pstring struct): хранит длину, ёмкость и гранульный индекс блока данных
- **Блок данных**: отдельный блок в ПАП, содержащий null-terminated строку
- **Переаллокация**: при росте выделяется новый блок с удвоенной ёмкостью (amortized O(1))
- **POD-структура**: `std::is_trivially_copyable_v<pstring> == true`

### API

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

// Создание
Mgr::pptr<Mgr::pstring> p = Mgr::create_typed<Mgr::pstring>();

// Присвоение
p->assign("hello");          // assign(const char*) → bool
p->append(" world");         // append(const char*) → bool

// Чтение
const char* s = p->c_str();  // "hello world"
std::size_t n = p->size();   // 11
bool empty    = p->empty();  // false
char c        = (*p)[0];     // 'h'

// Изменение
p->assign("new value");      // переаллокация при необходимости
p->clear();                  // length = 0, буфер сохраняется

// Сравнение
*p == "test";                // operator==(const char*)
*p != "test";                // operator!=(const char*)
*p1 == *p2;                  // operator==(const pstring&)
*p1 < *p2;                   // operator<(const pstring&) — лексикографическое

// Освобождение
p->free_data();              // деаллоцировать блок данных
Mgr::destroy_typed(p);       // деаллоцировать сам pstring

Mgr::destroy();
```

### Отличия от pstringview

| Свойство | pstringview | pstring |
|----------|-------------|---------|
| Мутабельность | Read-only | Мутабельная |
| Интернирование | Да (дедупликация) | Нет |
| Блокировка блока | Навечно (lock_block_permanent) | Обычный блок |
| Хранение данных | Встроенное (flexible array) | Отдельный блок |
| Использование | Константные строки, ключи | JSON-значения, динамические данные |

## 3.2 Персистентный массив `parray<T, ManagerT>` с O(1) индексацией ✅

**Issue:** #195
**Файл:** `include/pmm/parray.h`
**Тесты:** `tests/test_issue195_parray.cpp` (22 теста)

### Описание

`parray<T, ManagerT>` — динамический массив в персистентном адресном пространстве (ПАП)
с O(1) произвольным доступом. `parray` хранит элементы в непрерывном блоке памяти,
аналогично `std::vector`.

### Архитектура

```
parray<T> (в ПАП)           Блок данных (в ПАП)
┌───────────────┐           ┌──────────────────────┐
│ _size: u32    │           │ T[0] T[1] ... T[n-1] │
│ _capacity: u32│──idx────→ │                      │
│ _data_idx     │           └──────────────────────┘
└───────────────┘
```

- **Заголовок** (parray struct): хранит размер, ёмкость и гранульный индекс блока данных
- **Блок данных**: отдельный непрерывный блок в ПАП, содержащий массив элементов T
- **Переаллокация**: при росте выделяется новый блок с удвоенной ёмкостью (amortized O(1))
- **POD-структура**: `std::is_trivially_copyable_v<parray> == true`
- **Требование к T**: тип элемента должен быть trivially copyable

### API

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

// Создание
Mgr::pptr<Mgr::parray<int>> p = Mgr::create_typed<Mgr::parray<int>>();

// Добавление элементов
p->push_back(10);               // push_back(const T&) → bool
p->push_back(20);
p->push_back(30);

// O(1) произвольный доступ
int* elem = p->at(1);           // указатель на элемент, nullptr при выходе за границы
int  val  = (*p)[0];            // значение по индексу (без проверки границ)
int* first = p->front();        // первый элемент
int* last  = p->back();         // последний элемент
int* raw   = p->data();         // указатель на блок данных

// Модификация
p->set(1, 42);                  // set(i, value) → bool
p->pop_back();                  // удалить последний элемент

// Запрос состояния
std::size_t n   = p->size();    // 2
std::size_t cap = p->capacity();// >= 2
bool empty      = p->empty();   // false

// Управление ёмкостью
p->reserve(100);                // предварительное выделение
p->resize(50);                  // изменение размера (новые элементы = T{})

// Очистка
p->clear();                     // size = 0, буфер сохраняется

// Сравнение
*p1 == *p2;                     // operator==(const parray&)
*p1 != *p2;                     // operator!=(const parray&)

// Освобождение
p->free_data();                 // деаллоцировать блок данных
Mgr::destroy_typed(p);          // деаллоцировать сам parray

Mgr::destroy();
```

## 3.3 Доработка `pmap<K,V>` — erase, size, iterator, clear ✅

**Issue:** #196
**Файл:** `include/pmm/pmap.h`
**Тесты:** `tests/test_issue196_pmap_erase.cpp` (22 теста)

### Описание

Доработка персистентного словаря `pmap<_K, _V, ManagerT>` для полноценного использования
в качестве замены `pmap_pmm` из BinDiffSynchronizer. Добавлены операции удаления, подсчёта
элементов, итерации и очистки.

### Новые методы

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

using MyMap = Mgr::pmap<int, int>;
MyMap map;

// Вставка
map.insert(10, 100);
map.insert(20, 200);
map.insert(30, 300);

// Удаление по ключу — O(log n)
bool removed = map.erase(20);      // true
bool missing = map.erase(99);      // false

// Количество элементов — O(n)
std::size_t n = map.size();         // 2

// Итерация в порядке ключей (in-order обход AVL)
for (auto it = map.begin(); it != map.end(); ++it) {
    auto node = *it;
    // node->key, node->value — в порядке возрастания ключей
}

// Очистка (удаление всех элементов с деаллокацией)
map.clear();

Mgr::destroy();
```

### Детали реализации

- **erase(key)**: находит узел через `avl_find()`, удаляет из AVL-дерева через
  `detail::avl_remove()`, деаллоцирует блок в ПАП через `deallocate_typed()`
- **size()**: подсчёт элементов за O(n) через `detail::avl_subtree_count()`
- **iterator**: используется общий `detail::AvlInorderIterator<NodePPtr>` из
  `avl_tree_mixin.h` — in-order обход AVL-дерева в порядке ключей
- **clear()**: рекурсивная post-order деаллокация через `detail::avl_clear_subtree()`
- **Инициализация**: AVL-полей нового узла через `detail::avl_init_node()` с `no_block`
  sentinel вместо 0 для корректной работы итератора и AVL-операций

> **Примечание (Issue #188):** Все AVL-операции в pmap делегированы общим шаблонным
> функциям из `avl_tree_mixin.h`. pmap использует стандартный `AvlUpdateHeightOnly`
> callback (обновление только высоты).

## ~~3.4 Доработка `pvector<T>` — метод `erase(index)`~~ УДАЛЕНО (#224)

> Тип `pvector` удалён, так как полностью заменён `parray` (Issue #224).

## 3.5 STL-совместимый аллокатор `pallocator<T, ManagerT>` ✅

**Issue:** #198
**Файл:** `include/pmm/pallocator.h`
**Тесты:** `tests/test_issue198_pallocator.cpp` (18 тестов)

### Описание

`pallocator<T, ManagerT>` — STL-совместимый аллокатор, делегирующий управление памятью
в PersistMemoryManager. Позволяет использовать STL-контейнеры (`std::vector`, и т.д.)
с персистентным адресным пространством.

### Архитектура

```
std::vector<int, Mgr::pallocator<int>>
         │
         ▼
pallocator<int, Mgr>
  ├── allocate(n)    → Mgr::allocate(n * sizeof(int))  → void* в ПАП
  └── deallocate(p)  → Mgr::deallocate(p)              → освобождение в ПАП
```

- **Stateless**: все состояние хранится в статическом ManagerT
- **Все экземпляры с одним ManagerT равны** (`is_always_equal = true_type`)
- **Rebind**: поддерживается через converting constructor
- **Исключения**: `allocate()` бросает `std::bad_alloc` при неудаче (требование STL)

### API

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

// Использование со std::vector
std::vector<int, Mgr::pallocator<int>> vec;
vec.push_back(42);
vec.push_back(100);
assert(vec[0] == 42);

// Прямое использование аллокатора
Mgr::pallocator<int> alloc;
int* p = alloc.allocate(10);      // 10 int'ов в ПАП
p[0] = 1;
alloc.deallocate(p, 10);          // освобождение

// Rebinding
Mgr::pallocator<double> alloc_d(alloc);  // converting constructor

// Сравнение (всегда равны)
assert(alloc == alloc_d);

Mgr::destroy();
```

### Отличия от стандартного std::allocator

| Свойство | std::allocator | pallocator |
|----------|---------------|------------|
| Хранилище | Системная куча (malloc) | Персистентное адресное пространство (ПАП) |
| Персистентность | Нет | Данные сохраняются при save/load |
| Потокобезопасность | Да (через malloc) | Определяется LockPolicy менеджера |
| Адресная независимость | Нет | Да (через гранульные индексы pmm) |
| Использование | Общее назначение | STL-контейнеры в ПАП |

## 3.6 Пул объектов `ppool<T, ManagerT>` ✅

**Issue:** #199
**Файл:** `include/pmm/ppool.h`
**Тесты:** `tests/test_issue199_ppool.cpp` (18 тестов)

### Описание

`ppool<T, ManagerT>` — пул объектов фиксированного размера в персистентном адресном пространстве (ПАП).
Обеспечивает O(1) выделение и освобождение объектов через встроенный free-list.
Идеально подходит для массового создания узлов деревьев, списков, графов и JSON-узлов.

### Архитектура

```
ppool<T> (в ПАП)
┌─────────────────────┐
│ _free_head_idx      │──→ первый свободный слот
│ _chunk_head_idx     │──→ первый чанк
│ _objects_per_chunk  │    объектов в чанке (по умолчанию 64)
│ _total_allocated    │    текущее число живых объектов
│ _total_capacity     │    общее число слотов
└─────────────────────┘

Чанк (в ПАП):
┌──────────────────┬────────┬────────┬─────┬────────┐
│ next_chunk_idx   │ slot_0 │ slot_1 │ ... │ slot_N │
│ (1 гранула)      │ (G гранул каждый)               │
└──────────────────┴────────┴────────┴─────┴────────┘

Свободный слот хранит гранульный индекс следующего свободного слота
(встроенный free-list).
```

- **Чанки**: крупные блоки, выделяемые из ПАП через менеджер
- **Слоты**: гранульно-выровнены для корректной адресации через гранульные индексы
- **Free-list**: встроенный в неиспользуемые слоты (zero overhead)
- **POD-структура**: `std::is_trivially_copyable_v<ppool> == true`
- **Требование к T**: тип элемента должен быть trivially copyable

### API

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(256 * 1024);

// Создание пула
Mgr::pptr<Mgr::ppool<int>> pool = Mgr::create_typed<Mgr::ppool<int>>();

// Настройка размера чанка (до первой аллокации)
pool->set_objects_per_chunk(128);

// O(1) аллокация
int* a = pool->allocate();      // nullptr при неудаче
int* b = pool->allocate();
*a = 42;
*b = 99;

// O(1) деаллокация (возврат в free-list)
pool->deallocate(a);

// Статистика
pool->allocated_count();         // число живых объектов
pool->total_capacity();          // общее число слотов
pool->free_count();              // свободные слоты
pool->empty();                   // true если нет живых объектов

// Освобождение всех чанков
pool->free_all();
Mgr::destroy_typed(pool);

Mgr::destroy();
```

### Отличия от pallocator и стандартных аллокаторов

| Свойство | pallocator | ppool |
|----------|-----------|-------|
| Гранулярность | Произвольный размер | Фиксированный размер T |
| Скорость | O(log n) (поиск best-fit) | O(1) (free-list) |
| Использование | STL-контейнеры | Массовое создание однотипных объектов |
| Overhead | Заголовок блока на каждый объект | Один заголовок блока на чанк |
| Фрагментация | Возможна | Минимальная (фиксированные слоты) |

## 3.7 Корневой объект в ManagerHeader ✅

**Issue:** #200
**Файл:** `include/pmm/persist_memory_manager.h`, `include/pmm/types.h`
**Тесты:** `tests/test_issue200_root_object.cpp` (13 тестов)

### Описание

Единственный именованный указатель `root_offset` в `ManagerHeader`, позволяющий хранить
корневой объект (например, `pmap<pstringview, pptr<void>>`) и находить его после загрузки
образа. Заменяет паттерн `pam_pmm.h` из BinDiffSynchronizer.

### Архитектура

```
ManagerHeader (в ПАП)
┌─────────────────────┐
│ magic               │
│ total_size          │
│ ... (counters)      │
│ free_tree_root      │
│ crc32               │
│ root_offset  ───────│──→ pptr<T> корневого объекта (или no_block)
└─────────────────────┘
```

- **root_offset** (index_type): гранульный индекс корневого объекта
- При инициализации (`create()`) устанавливается в `no_block` (нет корня)
- Сохраняется и восстанавливается при `save_manager()`/`load_manager_from_file()`
- Заменяет поле `_reserved[4]` — размер `ManagerHeader<DefaultAddressTraits>` остаётся 64 байта

### API

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

// Создать корневой объект (например, реестр)
using Registry = Mgr::pmap<int, int>;
auto reg = Mgr::create_typed<Registry>();
reg->insert(1, 100);

// Установить как корень
Mgr::set_root(reg);

// Получить корень (например, после load)
auto root = Mgr::get_root<Registry>();
auto found = root->find(1);
// found->value == 100

// Сбросить корень
Mgr::set_root(Mgr::pptr<Registry>());

Mgr::destroy();
```

### Особенности

- Один корневой указатель на менеджер (мультитон — у каждого экземпляра свой)
- Типобезопасность: `set_root<T>()` / `get_root<T>()` — тип T должен совпадать
- Потокобезопасность: `set_root` под `unique_lock`, `get_root` под `shared_lock`
- Персистентность: корень сохраняется в образе через `root_offset` в `ManagerHeader`
- Работает со всеми address traits: `SmallAddressTraits`, `DefaultAddressTraits`, `LargeAddressTraits`
