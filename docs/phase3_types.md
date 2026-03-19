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
