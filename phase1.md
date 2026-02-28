# Фаза 1: Базовая структура и allocate/deallocate

## Статус: ✅ Завершена

## Цель

Реализовать минимально работающий менеджер персистентной памяти с поддержкой:
- создания/уничтожения менеджера;
- выделения и освобождения блоков произвольного размера;
- базовой диагностики и метрик.

---

## Реализованные компоненты

### Структуры данных

#### `pmm::detail::ManagerHeader`

Заголовок, расположенный в самом начале управляемой области памяти:

| Поле | Тип | Описание |
|------|-----|----------|
| `magic` | `uint64_t` | Магическое число `"PMM_V010"` |
| `total_size` | `size_t` | Полный размер области |
| `used_size` | `size_t` | Занятый объём (метаданные + данные) |
| `block_count` | `size_t` | Общее количество блоков |
| `free_count` | `size_t` | Количество свободных блоков |
| `alloc_count` | `size_t` | Количество занятых блоков |
| `first_block_offset` | `ptrdiff_t` | Смещение первого блока (-1 = нет) |

#### `pmm::detail::BlockHeader`

Заголовок каждого блока памяти (хранится непосредственно перед данными):

| Поле | Тип | Описание |
|------|-----|----------|
| `magic` | `uint64_t` | Магическое число `"BLOCKHDR"` |
| `prev_offset` | `ptrdiff_t` | Смещение предыдущего блока |
| `next_offset` | `ptrdiff_t` | Смещение следующего блока |
| `total_size` | `size_t` | Полный размер блока включая заголовок |
| `user_size` | `size_t` | Размер пользовательских данных |
| `alignment` | `size_t` | Выравнивание данных пользователя |
| `used` | `bool` | Флаг занятости |

### Публичный API

```cpp
namespace pmm {

class PersistMemoryManager {
public:
    // Создать новый менеджер в переданном буфере
    static PersistMemoryManager* create(void* memory, size_t size);

    // Загрузить из существующего образа
    static PersistMemoryManager* load(void* memory, size_t size);

    // Уничтожить (обнулить заголовок)
    void destroy();

    // Выделить блок (alignment — степень двойки, [8..4096])
    void* allocate(size_t size, size_t alignment = 16);

    // Освободить блок
    void  deallocate(void* ptr);

    // Перевыделить блок
    void* reallocate(void* ptr, size_t new_size);

    // Метрики
    size_t total_size() const;
    size_t used_size() const;
    size_t free_size() const;
    size_t fragmentation() const;

    // Диагностика
    bool validate() const;
    void dump_stats() const;
};

// Вспомогательные функции
MemoryStats    get_stats(const PersistMemoryManager* mgr);
AllocationInfo get_info(const PersistMemoryManager* mgr, void* ptr);

} // namespace pmm
```

---

## Алгоритм выделения (Фаза 1: first-fit)

```
1. Проверить user_size > 0, is_valid_alignment(alignment)
2. Вычислить required = sizeof(BlockHeader) + (alignment-1) + user_size
3. Линейный обход связного списка — найти первый свободный блок
   с total_size >= required
4. Если нашли:
   a. Если (total_size - required) >= sizeof(BlockHeader) + kMinBlockSize:
      - Разделить блок на два
      - Второй блок = свободный, вставить в список
   b. Пометить блок used=true, user_size=user_size, alignment=alignment
   c. Обновить счётчики ManagerHeader
   d. Вернуть user_ptr(blk) — указатель, выровненный на alignment
5. Иначе: вернуть nullptr
```

## Алгоритм освобождения (Фаза 1: без слияния)

```
1. Если ptr == nullptr: нет операции
2. Линейный обход — найти блок по user_ptr == ptr
3. Если найден и used == true:
   - used = false
   - user_size = 0
   - Обновить счётчики ManagerHeader
```

> Слияние соседних свободных блоков (coalescing) будет реализовано в Фазе 2.

---

## Тесты

### test_allocate

| Тест | Проверяет |
|------|-----------|
| `create_basic` | create() возвращает ненулевой указатель |
| `create_too_small` | create() с маленьким буфером → nullptr |
| `create_null` | create(nullptr, ...) → nullptr |
| `allocate_single_small` | Выделение одного блока, проверка выравнивания |
| `allocate_alignment_32` | Выравнивание на 32 байта |
| `allocate_alignment_64` | Выравнивание на 64 байта |
| `allocate_multiple` | Несколько блоков, все указатели разные |
| `allocate_zero` | allocate(0) → nullptr |
| `allocate_out_of_memory` | Запрос больше доступного → nullptr |
| `allocate_invalid_alignment` | Некорректное выравнивание → nullptr |
| `allocate_write_read` | Запись и чтение без перекрытий |
| `allocate_metrics` | Метрики корректны после выделений |

### test_deallocate

| Тест | Проверяет |
|------|-----------|
| `deallocate_null` | deallocate(nullptr) — безопасно |
| `deallocate_single` | Освобождение одного блока |
| `deallocate_reuse` | После освобождения память доступна повторно |
| `deallocate_multiple_fifo` | Освобождение в порядке FIFO |
| `deallocate_multiple_lifo` | Освобождение в порядке LIFO |
| `deallocate_random_order` | Освобождение в случайном порядке |
| `deallocate_all_then_check_free` | free_size восстанавливается |
| `deallocate_interleaved` | Чередование allocate/deallocate |
| `reallocate_grow` | Перевыделение с увеличением размера |
| `reallocate_from_null` | reallocate(nullptr, N) == allocate(N) |
| `get_info` | Корректная информация о блоке |

---

## Ограничения и известные упрощения

1. **Без coalescing** — соседние свободные блоки не сливаются (Фаза 2).
2. **Линейный поиск** — O(n) по количеству блоков (оптимизация в Фазе 5).
3. **Нет потокобезопасности** — однопоточное использование.
4. **Нет file I/O** — save/load из файла (Фаза 3).

---

## Принятые решения

### Смещения вместо указателей

Все поля-ссылки в заголовках (`prev_offset`, `next_offset`, `first_block_offset`)
хранятся как `ptrdiff_t` — смещение в байтах от начала управляемой области.
Это ключевое требование для персистентности: образ памяти можно загрузить
по любому адресу без пересчёта указателей.

### Магические числа

Каждый заголовок (ManagerHeader и BlockHeader) содержит `uint64_t magic`.
Это позволяет `validate()` и `load()` обнаруживать повреждённые или
несовместимые образы.

### Вычисление user_ptr

```
raw = (uint8_t*)BlockHeader + sizeof(BlockHeader)
addr = (uintptr_t)raw
aligned_addr = (addr + alignment - 1) & ~(alignment - 1)
user_ptr = (void*)aligned_addr
```

Поиск BlockHeader по user_ptr выполняется через `find_block_by_ptr` —
линейный обход всего связного списка, сравнивая `user_ptr(blk) == ptr`.

---

## Файлы фазы

| Файл | Описание |
|------|----------|
| `include/persist_memory_manager.h` | Single-header реализация |
| `tests/test_allocate.cpp` | Тесты выделения памяти |
| `tests/test_deallocate.cpp` | Тесты освобождения памяти |
| `tests/CMakeLists.txt` | Сборка тестов |
| `examples/basic_usage.cpp` | Пример использования |
| `docs/architecture.md` | Архитектурная документация |
| `CMakeLists.txt` | Обновлённая конфигурация сборки |
| `README.md` | Обновлённое описание проекта |
| `plan.md` | Обновлённый план разработки |

---

## Следующая фаза

**Фаза 2: Слияние блоков (coalescing)**

При освобождении блока проверять соседей (prev/next) и объединять соседние
свободные блоки в один, чтобы снизить фрагментацию и позволить повторно
использовать крупные области после множества мелких освобождений.
