# Фаза 3: Персистентность (save/load)

## Статус: ✅ Завершена

## Цель

Реализовать сохранение образа управляемой памяти в файл и загрузку из файла, обеспечивая полное восстановление состояния кучи между запусками программы.

---

## Решённая задача

После Фазы 2 менеджер умеет создавать и управлять кучей в произвольном буфере памяти. Однако при завершении программы все данные теряются. Фаза 3 добавляет персистентность:

```
Программа A:
  create() → allocate() × N → save("heap.dat") → exit()

Программа B:
  load_from_file("heap.dat", buf, size) → продолжить работу с теми же блоками
```

Ключевое преимущество архитектуры: все метаданные (`BlockHeader.prev_offset`, `BlockHeader.next_offset`, `ManagerHeader.first_block_offset`) хранятся как **смещения** (ptrdiff_t) от начала буфера, а не как абсолютные указатели. Поэтому образ корректно загружается по любому базовому адресу — пересчёт указателей не требуется.

---

## Реализованный API

### Метод `save()`

```cpp
/**
 * @brief Сохранить образ управляемой памяти в файл.
 * @param filename Путь к выходному файлу.
 * @return true при успешной записи, false при ошибке.
 */
bool save(const char* filename) const;
```

Записывает весь буфер (от `base_ptr` до `base_ptr + total_size`) в двоичный файл. Поскольку образ содержит все метаданные, он самодостаточен для восстановления.

### Свободная функция `load_from_file()`

```cpp
/**
 * @brief Загрузить образ менеджера из файла в существующий буфер.
 * @param filename Путь к файлу с образом.
 * @param memory   Буфер для загрузки (размер >= размер файла).
 * @param size     Размер буфера в байтах.
 * @return Указатель на восстановленный менеджер или nullptr при ошибке.
 */
PersistMemoryManager* load_from_file(const char* filename, void* memory, std::size_t size);
```

Алгоритм:
1. Открыть файл в режиме `"rb"`.
2. Определить размер файла через `fseek(SEEK_END)` + `ftell()`.
3. Если размер файла > `size` буфера → вернуть `nullptr`.
4. Прочитать файл в `memory`.
5. Вызвать `PersistMemoryManager::load(memory, file_size)` для проверки магического числа.

### Существующий `load(void*, size_t)`

Метод `load()` (реализован в Фазе 1) проверяет:
- `hdr->magic == kMagic` (`"PMM_V010"`).
- `hdr->total_size == size`.

Если проверки пройдены, возвращает `reinterpret_cast<PersistMemoryManager*>(base)`.

---

## Алгоритм работы с персистентностью

```
Сохранение:
  1. Проверить filename != nullptr
  2. Открыть файл: fopen(filename, "wb")
  3. Записать: fwrite(base_ptr, 1, total_size, f)
  4. Закрыть файл, вернуть (written == total_size)

Загрузка:
  1. Проверить filename, memory, size != nullptr/0
  2. Открыть файл: fopen(filename, "rb")
  3. Определить file_size: fseek(SEEK_END) + ftell()
  4. Если file_size > size → вернуть nullptr
  5. Прочитать: fread(memory, 1, file_size, f)
  6. Вызвать: PersistMemoryManager::load(memory, file_size)
```

---

## Поддержка разных базовых адресов

Поскольку `BlockHeader` и `ManagerHeader` используют `ptrdiff_t` смещения вместо абсолютных указателей, загрузка по другому базовому адресу работает автоматически:

```
Запуск 1: base_ptr = 0x7F000000
  BlockHeader.next_offset = 512   ← смещение в буфере, не адрес

Запуск 2: base_ptr = 0x60000000
  BlockHeader.next_offset = 512   ← то же смещение, другой адрес
  block_at(base, 512) → 0x60000000 + 512 = 0x60000200  ✓
```

---

## Реализованные компоненты

| Файл | Изменение |
|------|-----------|
| `include/persist_memory_manager.h` | Добавлен метод `save()`, свободная функция `load_from_file()`, `FILE_IO_ERROR` в `ErrorCode`, `#include <cstdio>` |
| `tests/test_persistence.cpp` | Новый файл с 12 тестами Фазы 3 |
| `tests/CMakeLists.txt` | Добавлен `test_persistence` |
| `examples/persistence_demo.cpp` | Демонстрационный пример персистентности |
| `phase3.md` | Этот файл |
| `plan.md` | Статус Фазы 3 обновлён |
| `README.md` | Информация о Фазе 3 добавлена |

---

## Тесты

| Тест | Что проверяет |
|------|---------------|
| `persistence_basic_roundtrip` | Базовый цикл create → save → load → validate |
| `persistence_user_data_preserved` | Данные пользователя сохраняются без искажений |
| `persistence_multiple_blocks` | Несколько блоков разных размеров, часть освобождены |
| `persistence_allocate_after_load` | allocate() работает после загрузки образа |
| `persistence_save_null_filename` | save(nullptr) возвращает false |
| `persistence_load_nonexistent_file` | load_from_file несуществующего файла → nullptr |
| `persistence_load_null_args` | Все null/нулевые аргументы → nullptr |
| `persistence_corrupted_image` | Файл с нулями (не PMM образ) → nullptr |
| `persistence_buffer_too_small` | Буфер меньше файла → nullptr |
| `persistence_double_save_load` | Двойной цикл save/load идемпотентен |
| `persistence_empty_manager` | Сохранение и загрузка пустого менеджера |
| `persistence_deallocate_after_load` | deallocate() работает после загрузки |

---

## Ограничения

1. **Файловый I/O только через stdio** — `fopen/fwrite/fread/fclose`. Нет поддержки mmap или Windows-специфичного I/O.
2. **Нет потокобезопасности** — однопоточное использование.
3. **Нет сжатия** — образ записывается как есть (включая неиспользуемые байты).
4. **Нет шифрования** — образ в открытом виде.

---

## Следующая фаза

**Фаза 4: Тесты и документация**

- Достичь покрытия тестами ≥ 90%.
- Стресс-тест: 100 000 аллокаций.
- Написать `docs/api_reference.md` и `docs/performance.md`.
