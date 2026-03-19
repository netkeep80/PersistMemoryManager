# Фаза 4: API и удобство использования

Документация по реализации задач Фазы 4 плана развития PersistMemoryManager.

---

## 4.1 Коды ошибок вместо bool ✅ (#201)

**Проблема:** Методы `create()`, `load()`, `allocate()` возвращали только `bool` / `nullptr`.
При неудаче причина ошибки была неизвестна — невозможно отличить нехватку памяти от
ошибки CRC, переполнения или неверного magic-числа.

**Решение:**

### `enum class PmmError` (types.h)

Перечисление кодов ошибок:

| Код | Значение | Описание |
|-----|----------|----------|
| `Ok` | 0 | Операция успешна |
| `NotInitialized` | 1 | Менеджер не инициализирован |
| `InvalidSize` | 2 | Некорректный размер (ноль, слишком мал и т.д.) |
| `Overflow` | 3 | Арифметическое переполнение при вычислении размеров |
| `OutOfMemory` | 4 | Аллокация не удалась — недостаточно свободной памяти |
| `ExpandFailed` | 5 | Расширение бэкенда (`expand()`) не удалось |
| `InvalidMagic` | 6 | Несовпадение magic-числа при `load()` |
| `CrcMismatch` | 7 | Несовпадение CRC32 при загрузке (повреждённый образ) |
| `SizeMismatch` | 8 | Сохранённый `total_size` не совпадает с бэкендом |
| `GranuleMismatch` | 9 | Сохранённый `granule_size` не совпадает с `address_traits` |
| `BackendError` | 10 | Бэкенд вернул `nullptr` или недопустимое состояние |
| `InvalidPointer` | 11 | Указатель `nullptr` или вне границ |
| `BlockLocked` | 12 | Блок перманентно заблокирован (нельзя освободить) |

### API (persist_memory_manager.h)

```cpp
/// Последний код ошибки (один на специализацию менеджера).
static PmmError last_error() noexcept;

/// Сбросить код ошибки в Ok.
static void clear_error() noexcept;

/// Установить код ошибки (для утилитных функций, например io.h).
static void set_last_error(PmmError err) noexcept;
```

### Какие методы устанавливают код ошибки

- **`create(initial_size)`** — `InvalidSize`, `Overflow`, `ExpandFailed`, `BackendError`, `Ok`
- **`create()`** — `BackendError`, `InvalidSize`, `Ok`
- **`load()`** — `BackendError`, `InvalidSize`, `InvalidMagic`, `SizeMismatch`, `GranuleMismatch`, `Ok`
- **`allocate(user_size)`** — `NotInitialized`, `InvalidSize`, `Overflow`, `OutOfMemory`, `Ok`
- **`load_manager_from_file()` (io.h)** — `CrcMismatch` (+ коды из `load()`)

### Обратная совместимость

Все существующие методы по-прежнему возвращают `bool` / `nullptr`.
`last_error()` — дополнительный механизм диагностики, не заменяющий возвращаемые значения.
Код, не использующий `last_error()`, продолжает работать без изменений.

### Тесты

18 тестов в `tests/test_issue201_error_codes.cpp`:
- Проверка всех значений enum
- Ошибки create: InvalidSize, Overflow, BackendError
- Ошибки allocate: NotInitialized, InvalidSize, OutOfMemory
- Ошибки load: InvalidMagic, SizeMismatch, GranuleMismatch
- CrcMismatch через corrupted file
- clear_error / set_last_error
- SmallAddressTraits (uint16_t) и LargeAddressTraits (uint64_t)

---

## 4.2 Хуки логирования ✅ (#202)

**Проблема:** Нет механизма отслеживания событий менеджера (аллокации, ошибки, расширения).
При отладке и мониторинге невозможно узнать о внутренних событиях без модификации библиотеки.

**Решение:**

### Политики логирования (logging_policy.h)

Шаблонный параметр `LoggingPolicyT` в конфигурации менеджера. Политика определяет набор
статических `noexcept` методов-хуков, вызываемых при ключевых событиях.

Встроенные политики:

| Политика | Описание |
|----------|----------|
| `logging::NoLogging` | Заглушка — все методы пустые inline (нулевые накладные расходы, по умолчанию) |
| `logging::StderrLogging` | Логирование событий и ошибок в stderr |

### Хуки

```cpp
/// Не удалось выделить память.
static void on_allocation_failure(std::size_t user_size, PmmError err) noexcept;

/// Бэкенд расширен.
static void on_expand(std::size_t old_size, std::size_t new_size) noexcept;

/// Обнаружено повреждение данных (InvalidMagic, CrcMismatch, SizeMismatch, GranuleMismatch).
static void on_corruption_detected(PmmError err) noexcept;

/// Менеджер успешно создан.
static void on_create(std::size_t initial_size) noexcept;

/// Менеджер сброшен.
static void on_destroy() noexcept;

/// Менеджер загружен из образа.
static void on_load() noexcept;
```

### Где вызываются хуки

- **`create(initial_size)` / `create()`** — `on_create()` при успехе
- **`destroy()`** — `on_destroy()`
- **`load()`** — `on_load()` при успехе; `on_corruption_detected()` при InvalidMagic, SizeMismatch, GranuleMismatch
- **`allocate(user_size)`** — `on_allocation_failure()` при NotInitialized, InvalidSize, Overflow, OutOfMemory
- **`do_expand()`** — `on_expand()` при успешном расширении
- **`load_manager_from_file()` (io.h)** — `on_corruption_detected()` при CrcMismatch

### Обратная совместимость

Политика `logging_policy` автоматически определяется из конфигурации через SFINAE.
Если конфигурация не определяет `logging_policy`, используется `logging::NoLogging`.
Пользовательские конфигурации без `logging_policy` продолжают работать без изменений.

### Пример пользовательской политики

```cpp
struct MyLogging {
    static void on_allocation_failure(std::size_t size, pmm::PmmError err) noexcept {
        spdlog::warn("pmm: alloc({}) failed: {}", size, static_cast<int>(err));
    }
    static void on_expand(std::size_t old_sz, std::size_t new_sz) noexcept {
        spdlog::info("pmm: expanded {} -> {}", old_sz, new_sz);
    }
    static void on_corruption_detected(pmm::PmmError err) noexcept {
        spdlog::error("pmm: corruption: {}", static_cast<int>(err));
    }
    static void on_create(std::size_t size) noexcept {}
    static void on_destroy() noexcept {}
    static void on_load() noexcept {}
};

using MyConfig = pmm::BasicConfig<
    pmm::DefaultAddressTraits,
    pmm::config::NoLock,
    5, 4, 64,
    MyLogging
>;
using MyMgr = pmm::PersistMemoryManager<MyConfig>;
```

### Тесты

14 тестов в `tests/test_issue202_logging_hooks.cpp`:
- NoLogging компилируется с предустановленными конфигурациями
- on_create / on_destroy / on_load хуки вызываются при соответствующих событиях
- on_allocation_failure вызывается при OOM, InvalidSize, NotInitialized
- on_expand вызывается при расширении бэкенда
- on_corruption_detected вызывается при InvalidMagic, SizeMismatch, GranuleMismatch, CrcMismatch
- Работа с SmallAddressTraits (uint16_t) и LargeAddressTraits (uint64_t)
