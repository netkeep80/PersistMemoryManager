# Справочник по API PersistMemoryManager

## Обзор

`PersistMemoryManager` — single-header C++17 библиотека управления персистентной кучей памяти.
Все метаданные хранятся внутри управляемой области, что позволяет сохранять и загружать образ
памяти из файла или shared memory. Взаимодействие с данными в управляемой памяти осуществляется
через персистные типизированные указатели `pptr<T>`.

Реализация находится в двух файлах:
- `include/persist_memory_manager.h` — менеджер памяти и `pptr<T>`
- `include/persist_memory_io.h` — утилиты файлового ввода/вывода (`save` / `load_from_file`)

Пространство имён: `pmm`

---

## Подключение

```cpp
#include "persist_memory_manager.h"
#include "persist_memory_io.h"  // для save() / load_from_file()
```

Никаких `.cpp` файлов, никакой линковки — только включите заголовки.

---

## Класс `PersistMemoryManager`

```cpp
namespace pmm {
    class PersistMemoryManager;
}
```

Все объекты создаются и уничтожаются через статические методы — прямой вызов конструктора/деструктора не предусмотрен.

Одновременно может существовать только один экземпляр менеджера (синглтон). Доступ к нему осуществляется
через `PersistMemoryManager::instance()`.

### Статические методы

#### `instance()`

```cpp
static PersistMemoryManager* instance() noexcept;
```

Возвращает указатель на текущий экземпляр менеджера. `nullptr`, если менеджер не создан.

---

#### `create()`

```cpp
static PersistMemoryManager* create(void* memory, std::size_t size);
```

Создаёт новый менеджер памяти в переданном буфере и устанавливает синглтон.

**Параметры:**
- `memory` — указатель на буфер. Не должен быть `nullptr`.
- `size` — размер буфера в байтах. Должен быть ≥ `kMinMemorySize` (4096).

**Возвращает:** указатель на менеджер или `nullptr` при ошибке.

**Постусловие:** `PersistMemoryManager::instance()` указывает на созданный менеджер. Буфер принадлежит менеджеру и будет освобождён при `destroy()`.

**Пример:**
```cpp
void* mem = std::malloc(1024 * 1024);
auto* mgr = pmm::PersistMemoryManager::create(mem, 1024 * 1024);
// mgr != nullptr при успехе
```

---

#### `load()`

```cpp
static PersistMemoryManager* load(void* memory, std::size_t size);
```

Загружает менеджер из существующего образа в памяти. Проверяет магическое число и размер. Устанавливает синглтон.

**Параметры:**
- `memory` — буфер с ранее сохранённым образом.
- `size` — размер образа (должен совпадать с `total_size` из заголовка).

**Возвращает:** указатель на менеджер или `nullptr`, если образ некорректен.

**Пример:**
```cpp
// После fread(memory, 1, file_size, f):
auto* mgr = pmm::PersistMemoryManager::load(memory, file_size);
```

---

#### `destroy()`

```cpp
static void destroy();
```

Уничтожает синглтон: обнуляет метаданные, освобождает все управляемые буферы (включая цепочку расширений), сбрасывает `instance()` в `nullptr`.

**Постусловие:** `PersistMemoryManager::instance() == nullptr`.

**Пример:**
```cpp
pmm::PersistMemoryManager::destroy();
// Буфер, переданный в create(), уже освобождён
```

---

### Персистные типизированные указатели (pptr<T>)

Основной способ работы с данными в управляемой памяти — через `pptr<T>`.

#### `allocate_typed<T>()`

```cpp
template <class T>
pptr<T> allocate_typed();

template <class T>
pptr<T> allocate_typed(std::size_t count);
```

Выделяет `sizeof(T)` байт (или `sizeof(T) * count` для массива) с выравниванием `alignof(T)`.

**Возвращает:** `pptr<T>` на выделенный объект/массив. Нулевой `pptr` при ошибке.

**Особенность:** при нехватке памяти менеджер автоматически расширяет управляемую область на 25%. После расширения `PersistMemoryManager::instance()` указывает на новый буфер.

**Пример:**
```cpp
pmm::pptr<int>    p1 = mgr->allocate_typed<int>();
pmm::pptr<double> p2 = mgr->allocate_typed<double>(10);  // массив из 10 double
*p1 = 42;
*p2.get_at(0) = 3.14;
```

---

#### `deallocate_typed<T>()`

```cpp
template <class T>
void deallocate_typed(pptr<T> p);
```

Освобождает блок памяти, на который указывает `p`. Нулевой `pptr` игнорируется.

**Пример:**
```cpp
mgr->deallocate_typed(p1);
```

---

### Вспомогательный метод

#### `offset_to_ptr()`

```cpp
void*       offset_to_ptr(std::ptrdiff_t offset) noexcept;
const void* offset_to_ptr(std::ptrdiff_t offset) const noexcept;
```

Преобразует смещение от базы управляемой области в абсолютный указатель. `offset == 0` → `nullptr`.

---

### Метрики

#### `total_size()`

```cpp
std::size_t total_size() const;
```

Возвращает полный размер управляемой области (равен `size`, переданному в `create()`).

---

#### `used_size()`

```cpp
std::size_t used_size() const;
```

Возвращает объём занятой памяти: метаданные (заголовки) плюс пользовательские данные.

---

#### `free_size()`

```cpp
std::size_t free_size() const;
```

Возвращает объём доступной свободной памяти внутри управляемой области.

**Инвариант:** `used_size() + free_size() <= total_size()`.

---

#### `fragmentation()`

```cpp
std::size_t fragmentation() const;
```

Возвращает количество лишних свободных фрагментов (число свободных блоков минус один).

`0` — нет фрагментации, `> 0` — есть несмежные свободные регионы.

---

### Диагностика

#### `validate()`

```cpp
bool validate() const;
```

Выполняет полную проверку целостности структур данных:
- Проверяет магические числа `ManagerHeader` и каждого `BlockHeader`.
- Проверяет связность двусвязного списка блоков.
- Проверяет счётчики блоков.

**Возвращает:** `true`, если все структуры корректны.

**Примечание:** O(n) — линейный обход всех блоков. Используется для отладки.

---

#### `dump_stats()`

```cpp
void dump_stats() const;
```

Выводит в `std::cout` диагностическую информацию: размеры, счётчики блоков, фрагментацию.

---

## Класс `pptr<T>`

```cpp
namespace pmm {
    template <class T>
    class pptr;
}
```

Персистный типизированный указатель. Хранит смещение от начала управляемой области вместо абсолютного адреса, что обеспечивает корректную работу после загрузки образа по другому базовому адресу.

**Требование:** `sizeof(pptr<T>) == sizeof(void*)`.

### Конструкторы

```cpp
pptr();                               // нулевой указатель
explicit pptr(std::ptrdiff_t offset); // из смещения (используется менеджером)
pptr(const pptr<T>&) = default;
```

### Проверка на null

```cpp
bool is_null() const noexcept;
explicit operator bool() const noexcept;
```

### Получение смещения

```cpp
std::ptrdiff_t offset() const noexcept;
```

### Разыменование через синглтон

```cpp
T*  get() const noexcept;          // указатель на объект
T&  operator*() const noexcept;    // разыменование
T*  operator->() const noexcept;   // доступ к членам
T*  get_at(std::size_t index) const noexcept;  // элемент массива
```

Используют `PersistMemoryManager::instance()` автоматически.

### Разыменование с явным менеджером

```cpp
T*       resolve(PersistMemoryManager* mgr) const noexcept;
const T* resolve(const PersistMemoryManager* mgr) const noexcept;
T*       resolve_at(PersistMemoryManager* mgr, std::size_t index) const noexcept;
```

### Операторы сравнения

```cpp
bool operator==(const pptr<T>& other) const noexcept;
bool operator!=(const pptr<T>& other) const noexcept;
```

### Пример использования

```cpp
// Создать менеджер
auto* mgr = pmm::PersistMemoryManager::create(std::malloc(1 << 20), 1 << 20);

// Выделить и использовать типизированный указатель
pmm::pptr<int> p = mgr->allocate_typed<int>();
*p = 123;

// Сохранить смещение для восстановления после перезагрузки
std::ptrdiff_t saved = p.offset();

// Сохранить образ в файл
pmm::save(mgr, "heap.dat");
pmm::PersistMemoryManager::destroy();

// Загрузить образ
auto* mgr2 = pmm::load_from_file("heap.dat", std::malloc(1 << 20), 1 << 20);
pmm::pptr<int> p2(saved);
assert(*p2 == 123);
pmm::PersistMemoryManager::destroy();
```

---

## Свободные функции

### `save()`

```cpp
namespace pmm {
    bool save(const PersistMemoryManager* mgr, const char* filename);
}
```

Сохраняет образ управляемой области памяти в двоичный файл.

**Параметры:**
- `mgr` — указатель на менеджер. Не должен быть `nullptr`.
- `filename` — путь к выходному файлу. Не должен быть `nullptr`.

**Возвращает:** `true` при успешной записи, `false` при ошибке.

**Пример:**
```cpp
#include "persist_memory_io.h"

if (!pmm::save(mgr, "heap.dat")) {
    // ошибка записи
}
```

---

### `load_from_file()`

```cpp
namespace pmm {
    PersistMemoryManager* load_from_file(
        const char* filename,
        void*       memory,
        std::size_t size
    );
}
```

Загружает образ менеджера из файла в существующий буфер.

**Параметры:**
- `filename` — путь к файлу с образом.
- `memory` — буфер для загрузки. Размер должен быть ≥ размера файла.
- `size` — размер буфера в байтах.

**Возвращает:** указатель на восстановленный менеджер или `nullptr` при ошибке.

**Пример:**
```cpp
#include "persist_memory_io.h"

void* buf = std::malloc(1024 * 1024);
auto* mgr = pmm::load_from_file("heap.dat", buf, 1024 * 1024);
if (mgr != nullptr && mgr->validate()) {
    // образ корректно загружен
}
```

---

### `get_stats()`

```cpp
namespace pmm {
    MemoryStats get_stats(const PersistMemoryManager* mgr);
}
```

Возвращает структуру со статистикой состояния менеджера.

---

### `get_info()`

```cpp
namespace pmm {
    AllocationInfo get_info(const PersistMemoryManager* mgr, void* ptr);
}
```

Возвращает информацию о конкретном выделенном блоке по абсолютному указателю на данные пользователя.

---

## Структуры данных

### `MemoryStats`

```cpp
struct MemoryStats {
    std::size_t total_blocks;        // Общее количество блоков
    std::size_t free_blocks;         // Количество свободных блоков
    std::size_t allocated_blocks;    // Количество занятых блоков
    std::size_t largest_free;        // Размер наибольшего свободного блока (байт)
    std::size_t smallest_free;       // Размер наименьшего свободного блока (байт)
    std::size_t total_fragmentation; // Суммарная фрагментация (байт)
};
```

---

### `AllocationInfo`

```cpp
struct AllocationInfo {
    void*       ptr;       // Указатель на данные пользователя
    std::size_t size;      // Размер пользовательских данных (байт)
    std::size_t alignment; // Выравнивание
    bool        is_valid;  // true — блок найден и корректен
};
```

---

## Константы

| Константа | Значение | Описание |
|-----------|----------|----------|
| `kDefaultAlignment` | 16 | Выравнивание по умолчанию (байт) |
| `kMinAlignment` | 8 | Минимальное выравнивание (байт) |
| `kMaxAlignment` | 4096 | Максимальное выравнивание (байт) |
| `kMinMemorySize` | 4096 | Минимальный размер буфера (байт) |
| `kMinBlockSize` | 32 | Минимальный размер блока данных (байт) |
| `kGrowNumerator` | 5 | Числитель коэффициента расширения (5/4 = 25%) |
| `kGrowDenominator` | 4 | Знаменатель коэффициента расширения |

---

## Поведение граничных условий

| Условие | Возвращаемое значение |
|---------|----------------------|
| `create(nullptr, size)` | `nullptr` |
| `create(mem, < 4096)` | `nullptr` |
| `allocate_typed<T>()` при нехватке памяти | автоматическое расширение на 25% |
| `deallocate_typed(null pptr)` | нет операции |
| `save(nullptr, ...)` | `false` |
| `save(mgr, nullptr)` | `false` |
| `load_from_file(nullptr, ...)` | `nullptr` |
| `load_from_file(file, nullptr, ...)` | `nullptr` |
| `load_from_file(несуществующий файл, ...)` | `nullptr` |
| `load_from_file(файл > size, ...)` | `nullptr` |
| `load(повреждённый образ, ...)` | `nullptr` |

---

## Потокобезопасность

Все публичные методы потокобезопасны. Используется `std::shared_mutex`:
- методы чтения (`total_size`, `used_size`, `free_size`, `fragmentation`, `validate`, `dump_stats`, `get_stats`, `get_info`) захватывают разделённую блокировку (`shared_lock`) и могут выполняться параллельно;
- методы записи (`create`, `load`, `destroy`, `allocate_typed`, `deallocate_typed`) захватывают эксклюзивную блокировку (`unique_lock`).

---

## Ограничения

- Файловый ввод/вывод: только `stdio` (`fopen`/`fread`/`fwrite`/`fclose`). Нет поддержки `mmap`.
- Алгоритм поиска свободного блока: first-fit по отдельному списку свободных блоков.
- Нет сжатия или шифрования образа.
- Одновременно может существовать только один экземпляр менеджера (синглтон).

---

*Версия документа 2.0. Соответствует версии библиотеки 1.0.0.*
