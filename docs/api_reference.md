# Справочник по API PersistMemoryManager

## Обзор

`PersistMemoryManager` — single-header C++17 библиотека управления персистентной кучей памяти.
Все метаданные хранятся внутри управляемой области, что позволяет сохранять и загружать образ
памяти из файла или shared memory. Взаимодействие с данными в управляемой памяти осуществляется
через персистные типизированные указатели `pptr<T>`.

**Issue #61:** Менеджер реализован как полностью статический класс — нет экземпляров, нет `PersistMemoryManager*` в пользовательском коде. Весь API доступен через статические методы.

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

Полностью статический класс. Одновременно может существовать только один активный менеджер (синглтон). Прямой вызов конструктора/деструктора не предусмотрен.

### Создание и уничтожение

#### `create()`

```cpp
static bool create(void* memory, std::size_t size);
```

Создаёт новый менеджер памяти в переданном буфере и устанавливает синглтон.

**Параметры:**
- `memory` — указатель на буфер. Не должен быть `nullptr`. Должен быть выровнен по `kGranuleSize` (16 байт).
- `size` — размер буфера в байтах. Должен быть ≥ `kMinMemorySize` (4096) и кратен `kGranuleSize`.

**Возвращает:** `true` при успехе, `false` при ошибке.

**Важно:** Переданный буфер (`memory`) НЕ освобождается при `destroy()`. Вызывающий код ответственен за его освобождение.

**Пример:**
```cpp
void* mem = std::malloc(1024 * 1024);
bool ok = pmm::PersistMemoryManager::create(mem, 1024 * 1024);
// ok == true при успехе
```

---

#### `load()`

```cpp
static bool load(void* memory, std::size_t size);
```

Загружает менеджер из существующего образа в памяти. Проверяет магическое число и размер. Устанавливает синглтон.

**Параметры:**
- `memory` — буфер с ранее сохранённым образом.
- `size` — размер образа (должен совпадать с `total_size` из заголовка).

**Возвращает:** `true` при успехе, `false` если образ некорректен.

**Пример:**
```cpp
// После fread(memory, 1, file_size, f):
bool ok = pmm::PersistMemoryManager::load(memory, file_size);
```

---

#### `destroy()`

```cpp
static void destroy();
```

Уничтожает синглтон: обнуляет метаданные, освобождает все буферы, выделенные через авто-расширение (`expand()`), сбрасывает `instance()` в `nullptr`.

**Важно:** Буфер, переданный в `create()` или `load()` (внешняя память), НЕ освобождается. Вызывающий код должен освободить его самостоятельно.

**Пример:**
```cpp
pmm::PersistMemoryManager::destroy();
std::free(mem); // Освобождаем внешний буфер самостоятельно
```

---

### Типизированное выделение памяти (основной API)

#### `allocate_typed<T>()`

```cpp
template <class T>
static pptr<T> allocate_typed();

template <class T>
static pptr<T> allocate_typed(std::size_t count);
```

Выделяет `sizeof(T)` байт (или `sizeof(T) * count` для массива) с выравниванием `alignof(T)`.

**Возвращает:** `pptr<T>` на выделенный объект/массив. Нулевой `pptr` при ошибке (не null означает 0 гранульный индекс).

**Особенность:** при нехватке памяти менеджер автоматически расширяет управляемую область на 25%.

**Пример:**
```cpp
pmm::pptr<int>    p1 = pmm::PersistMemoryManager::allocate_typed<int>();
pmm::pptr<double> p2 = pmm::PersistMemoryManager::allocate_typed<double>(10);  // массив из 10 double
*p1 = 42;
p2[0] = 3.14;
```

---

#### `deallocate_typed<T>()`

```cpp
template <class T>
static void deallocate_typed(pptr<T> p);
```

Освобождает блок памяти, на который указывает `p`. Нулевой `pptr` игнорируется.

**Пример:**
```cpp
pmm::PersistMemoryManager::deallocate_typed(p1);
```

---

#### `reallocate_typed<T>()`

```cpp
template <class T>
static pptr<T> reallocate_typed(pptr<T> p, std::size_t new_count);
```

Изменяет размер блока до `sizeof(T) * new_count` байт. Данные из старого блока копируются в новый (до минимума из старого и нового размера). Если `p` нулевой — работает как `allocate_typed<T>(new_count)`.

**Возвращает:** `pptr<T>` на новый блок, нулевой `pptr` при ошибке.

**Пример:**
```cpp
pmm::pptr<uint8_t> p = pmm::PersistMemoryManager::allocate_typed<uint8_t>(128);
p = pmm::PersistMemoryManager::reallocate_typed(p, 512); // увеличить до 512 байт
```

---

### Метрики

Все методы метрик — статические, потокобезопасные (shared_lock).

#### `total_size()`

```cpp
static std::size_t total_size() noexcept;
```

Полный размер управляемой области в байтах.

---

#### `used_size()`

```cpp
static std::size_t used_size() noexcept;
```

Объём занятой памяти: метаданные (заголовки) плюс пользовательские данные.

---

#### `free_size()`

```cpp
static std::size_t free_size() noexcept;
```

Объём доступной свободной памяти внутри управляемой области.

**Инвариант:** `used_size() + free_size() <= total_size()`.

---

#### `fragmentation()`

```cpp
static std::size_t fragmentation() noexcept;
```

Количество лишних свободных фрагментов (число свободных блоков минус один).

`0` — нет фрагментации, `> 0` — есть несмежные свободные регионы.

---

### Диагностика

#### `validate()`

```cpp
static bool validate();
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
static void dump_stats();
```

Выводит в `std::cout` диагностическую информацию: размеры, счётчики блоков, фрагментацию.

---

#### `is_initialized()`

```cpp
static bool is_initialized() noexcept;
```

Возвращает `true`, если менеджер создан (синглтон не `nullptr`).

---

#### `block_data_size_bytes()`

```cpp
static std::size_t block_data_size_bytes(uint32_t granule_idx) noexcept;
```

Возвращает размер пользовательских данных блока по гранульному индексу (в байтах, округлено до кратного `kGranuleSize`).

---

#### `offset_to_ptr()`

```cpp
static void* offset_to_ptr(uint32_t granule_idx) noexcept;
```

Преобразует гранульный индекс в абсолютный указатель. `0` → `nullptr`.

---

#### `instance()`

```cpp
static PersistMemoryManager* instance() noexcept;
```

Возвращает указатель на текущий экземпляр менеджера (для внутреннего использования в IO-утилитах и демо). `nullptr`, если менеджер не создан.

---

## Класс `pptr<T>`

```cpp
namespace pmm {
    template <class T>
    class pptr;
}
```

Персистный типизированный указатель. Хранит 32-битный гранульный индекс (16 байт/гранула) вместо абсолютного адреса, что обеспечивает корректную работу после загрузки образа по другому базовому адресу.

**Требование:** `sizeof(pptr<T>) == 4`.

### Конструкторы

```cpp
pptr();                               // нулевой указатель (индекс 0)
explicit pptr(std::uint32_t offset);  // из гранульного индекса
pptr(const pptr<T>&) = default;
```

### Проверка на null

```cpp
bool is_null() const noexcept;
explicit operator bool() const noexcept;
```

### Получение гранульного индекса

```cpp
std::uint32_t offset() const noexcept;
```

### Разыменование через синглтон

```cpp
T*  get() const noexcept;          // указатель на объект
T&  operator*() const noexcept;    // разыменование
T*  operator->() const noexcept;   // доступ к членам
T&  operator[](std::size_t i) const noexcept; // элемент массива (с проверкой границ)
```

Используют `PersistMemoryManager::instance()` автоматически.

### Операторы сравнения

```cpp
bool operator==(const pptr<T>& other) const noexcept;
bool operator!=(const pptr<T>& other) const noexcept;
```

### Пример использования

```cpp
// Создать менеджер
void* mem = std::malloc(1 << 20);
pmm::PersistMemoryManager::create(mem, 1 << 20);

// Выделить и использовать типизированный указатель
pmm::pptr<int> p = pmm::PersistMemoryManager::allocate_typed<int>();
*p = 123;

// Сохранить гранульный индекс для восстановления после перезагрузки
uint32_t saved = p.offset();

// Сохранить образ в файл
pmm::save("heap.dat");
pmm::PersistMemoryManager::destroy();
std::free(mem);

// Загрузить образ
void* mem2 = std::malloc(1 << 20);
pmm::load_from_file("heap.dat", mem2, 1 << 20);
pmm::pptr<int> p2(saved);
assert(*p2 == 123);
pmm::PersistMemoryManager::destroy();
std::free(mem2);
```

---

## Свободные функции

### `save()`

```cpp
namespace pmm {
    bool save(const char* filename);
}
```

Сохраняет образ управляемой области памяти в двоичный файл. Использует синглтон внутри.

**Параметры:**
- `filename` — путь к выходному файлу. Не должен быть `nullptr`.

**Возвращает:** `true` при успешной записи, `false` при ошибке.

**Пример:**
```cpp
#include "persist_memory_io.h"

if (!pmm::save("heap.dat")) {
    // ошибка записи
}
```

---

### `load_from_file()`

```cpp
namespace pmm {
    bool load_from_file(
        const char* filename,
        void*       memory,
        std::size_t size
    );
}
```

Загружает образ менеджера из файла в существующий буфер и устанавливает синглтон.

**Параметры:**
- `filename` — путь к файлу с образом.
- `memory` — буфер для загрузки. Размер должен быть ≥ размера файла.
- `size` — размер буфера в байтах.

**Возвращает:** `true` при успехе, `false` при ошибке.

**Пример:**
```cpp
#include "persist_memory_io.h"

void* buf = std::malloc(1024 * 1024);
bool ok = pmm::load_from_file("heap.dat", buf, 1024 * 1024);
if (ok && pmm::PersistMemoryManager::validate()) {
    // образ корректно загружен
}
```

---

### `get_stats()`

```cpp
namespace pmm {
    MemoryStats get_stats();
}
```

Возвращает структуру со статистикой состояния менеджера. Использует синглтон внутри.

---

### `get_manager_info()`

```cpp
namespace pmm {
    ManagerInfo get_manager_info();
}
```

Возвращает снимок полей заголовка менеджера. Использует синглтон внутри.

---

### `for_each_block()`

```cpp
namespace pmm {
    template <typename Callback>
    void for_each_block(Callback&& callback);
    // Callback: void(const pmm::BlockView&)
}
```

Вызывает `callback` для каждого блока памяти. Использует синглтон внутри. Потокобезопасно (shared_lock).

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

## Константы

| Константа | Значение | Описание |
|-----------|----------|----------|
| `kGranuleSize` | 16 | Размер гранулы в байтах (единица адресации) |
| `kMinAlignment` | 16 | Минимальное выравнивание (байт) |
| `kMinMemorySize` | 4096 | Минимальный размер буфера (байт) |
| `kMinBlockSize` | 16 | Минимальный размер блока данных (байт) |
| `kGrowNumerator` | 5 | Числитель коэффициента расширения (5/4 = 25%) |
| `kGrowDenominator` | 4 | Знаменатель коэффициента расширения |

---

## Поведение граничных условий

| Условие | Возвращаемое значение |
|---------|----------------------|
| `create(nullptr, size)` | `false` |
| `create(mem, < 4096)` | `false` |
| `allocate_typed<T>()` при нехватке памяти | автоматическое расширение на 25% |
| `deallocate_typed(null pptr)` | нет операции |
| `save(nullptr)` | `false` |
| `load_from_file(nullptr, ...)` | `false` |
| `load_from_file(file, nullptr, ...)` | `false` |
| `load_from_file(несуществующий файл, ...)` | `false` |
| `load_from_file(файл > size, ...)` | `false` |
| `load(повреждённый образ, ...)` | `false` |

---

## Потокобезопасность

Все публичные методы потокобезопасны. Используется `std::shared_mutex`:
- методы чтения (`total_size`, `used_size`, `free_size`, `fragmentation`, `validate`, `dump_stats`, `get_stats`, `get_manager_info`, `for_each_block`) захватывают разделённую блокировку (`shared_lock`) и могут выполняться параллельно;
- методы записи (`create`, `load`, `destroy`, `allocate_typed`, `deallocate_typed`, `reallocate_typed`) захватывают эксклюзивную блокировку (`unique_lock`).

---

## Ограничения

- Файловый ввод/вывод: только `stdio` (`fopen`/`fread`/`fwrite`/`fclose`). Нет поддержки `mmap`.
- Алгоритм поиска свободного блока: best-fit через AVL-дерево свободных блоков (O(log n)).
- Нет сжатия или шифрования образа.
- Одновременно может существовать только один экземпляр менеджера (синглтон).
- Максимальный адресуемый объём: 64 ГБ (2^32 × 16 байт/гранула).

---

*Версия документа 6.0. Соответствует версии библиотеки 6.0.0 (Issue #75: PAP-гомогенизация — `ManagerHeader` внутри `BlockHeader_0`).*
