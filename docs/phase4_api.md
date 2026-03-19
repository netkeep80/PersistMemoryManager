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
