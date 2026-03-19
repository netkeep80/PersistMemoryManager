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
с O(1) произвольным доступом. В отличие от `pvector<T>` (на AVL-дереве, O(log n) доступ),
`parray` хранит элементы в непрерывном блоке памяти, аналогично `std::vector`.

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

### Отличия от pvector

| Свойство | pvector | parray |
|----------|---------|--------|
| Структура данных | AVL-дерево | Непрерывный массив |
| Произвольный доступ | O(log n) | O(1) |
| Вставка в конец | O(log n) | Amortized O(1) |
| Удаление из середины | O(log n) | Не поддерживается |
| Расход памяти | Высокий (узлы дерева) | Низкий (массив + заголовок) |
| Использование | Когда нужны частые вставки/удаления | Когда нужен быстрый индексный доступ |

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

- **erase(key)**: находит узел через `_avl_find()`, удаляет из AVL-дерева через
  `detail::avl_remove()`, деаллоцирует блок в ПАП через `deallocate_typed()`
- **size()**: рекурсивный обход поддерева за O(n) (pmap не хранит weight в узлах)
- **iterator**: in-order обход AVL-дерева (левый → корень → правый), аналогичен
  итератору `pvector`, но обходит узлы в порядке ключей
- **clear()**: рекурсивная post-order деаллокация всех узлов
- **Исправление**: инициализация AVL-полей нового узла теперь использует `no_block`
  sentinel вместо 0 для корректной работы итератора и AVL-операций

## 3.4 Доработка `pvector<T>` — метод `erase(index)` ✅

**Issue:** #197
**Файл:** `include/pmm/pvector.h`
**Тесты:** `tests/test_issue197_pvector_erase.cpp` (17 тестов)

### Описание

Добавление метода `erase(index)` в `pvector<T, ManagerT>` для удаления элемента
по произвольному индексу за O(log n). Метод находит узел через order-statistic tree
(поле weight), удаляет его из AVL-дерева с перебалансировкой и освобождает память в ПАП.

### Новый метод

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

using MyVec = Mgr::pvector<int>;
MyVec vec;

vec.push_back(10);
vec.push_back(20);
vec.push_back(30);
vec.push_back(40);

// Удаление по индексу — O(log n)
bool removed = vec.erase(1);       // true, удалён элемент 20 → [10, 30, 40]
bool missing = vec.erase(100);     // false, индекс вне диапазона

// Удаление первого элемента
vec.erase(0);                      // [30, 40]

// Удаление последнего элемента
vec.erase(vec.size() - 1);         // [30]

Mgr::destroy();
```

### Детали реализации

- **erase(index)**: находит узел через `_avl_find_by_index()` (O(log n) по полю weight),
  удаляет из AVL-дерева через `detail::avl_remove()` с `_WeightUpdateFn` для обновления
  весов поддеревьев, деаллоцирует блок в ПАП через `deallocate_typed()`
- **Перебалансировка**: `_WeightUpdateFn` обновляет и высоту, и вес (размер поддерева)
  при каждой ротации, что гарантирует корректность order-statistic tree после удаления
- **Граничные случаи**: возвращает `false` для пустого вектора и для индекса >= size()

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
