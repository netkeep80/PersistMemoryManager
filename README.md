# PersistMemoryManager

**Single-header C++17 библиотека управления персистентной кучей памяти.**

Библиотека предоставляет низкоуровневый менеджер памяти, который хранит все метаданные внутри управляемой области. Это позволяет сохранять образ кучи в файл или shared memory и восстанавливать его между запусками программы.

## Возможности

- **Single-header** — вся реализация в одном файле `include/persist_memory_manager.h`
- **C++17** — без внешних зависимостей, только стандартная библиотека
- **Персистентность** — все ссылки хранятся как смещения, а не абсолютные указатели
- **pptr<T>** — персистный типизированный указатель, sizeof == sizeof(void*)
- **Выравнивание** — поддержка alignment от 8 до 4096 байт
- **Диагностика** — validate(), dump_stats(), get_stats()
- **Высокая производительность** — отдельный список свободных блоков, allocate 100K ≤ 7 мс
- **Синглтон** — единственный активный менеджер доступен через `PersistMemoryManager::instance()`
- **Автоматическое расширение** — при нехватке памяти буфер автоматически растёт на 25%

## Быстрый старт

```cpp
#include "persist_memory_manager.h"

int main() {
    // Выделить системную память под менеджер (например, 1 МБ)
    void* memory = std::malloc(1024 * 1024);

    // Создать менеджер (устанавливает синглтон)
    pmm::PersistMemoryManager::create(memory, 1024 * 1024);

    // Получить доступ к синглтону
    auto* mgr = pmm::PersistMemoryManager::instance();

    // Выделить блоки (при нехватке памяти автоматически расширяет буфер на 25%)
    void* block1 = mgr->allocate(256);          // 256 байт, align=16
    void* block2 = mgr->allocate(1024, 32);     // 1 КБ, align=32

    // Освободить
    mgr->deallocate(block1);

    // Получить статистику
    auto stats = pmm::get_stats(pmm::PersistMemoryManager::instance());

    // Уничтожить менеджер (освобождает управляемый буфер)
    pmm::PersistMemoryManager::destroy();
    return 0;
}
```

## Сборка

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Персистный указатель pptr<T> (Фаза 5, обновлено в Фазе 7)

`pptr<T>` — типизированный персистный указатель, который хранит смещение (offset) от базы менеджера памяти вместо абсолютного адреса. Начиная с Фазы 7, `pptr<T>` использует синглтон автоматически и не требует явной передачи менеджера.

```cpp
#include "persist_memory_manager.h"

int main() {
    void* memory = std::malloc(1024 * 1024);
    pmm::PersistMemoryManager::create(memory, 1024 * 1024);

    // Выделить один объект типа int
    pmm::pptr<int> p = pmm::PersistMemoryManager::instance()->allocate_typed<int>();
    *p = 42;           // operator* использует синглтон автоматически

    // Выделить массив из 10 double
    pmm::pptr<double> arr = pmm::PersistMemoryManager::instance()->allocate_typed<double>(10);
    for (int i = 0; i < 10; i++) {
        *arr.resolve_at(pmm::PersistMemoryManager::instance(), i) = i * 1.5;
    }

    // Проверка на нулевой указатель
    if (p) { /* ненулевой */ }

    // Сохранить образ
    pmm::PersistMemoryManager::instance()->save("heap.dat");

    // Сохраняем смещение для восстановления после load
    std::ptrdiff_t off = p.offset();

    pmm::PersistMemoryManager::instance()->deallocate_typed(arr);
    pmm::PersistMemoryManager::instance()->deallocate_typed(p);
    pmm::PersistMemoryManager::destroy(); // освобождает буфер

    // --- следующий запуск ---
    void* buf2 = std::malloc(1024 * 1024);
    pmm::load_from_file("heap.dat", buf2, 1024 * 1024); // устанавливает синглтон

    // Восстанавливаем pptr<int> по сохранённому смещению
    pmm::pptr<int> p2(off);
    std::cout << *p2 << "\n"; // выведет 42 (operator* через синглтон)

    pmm::PersistMemoryManager::destroy(); // освобождает buf2
    return 0;
}
```

Ключевые свойства `pptr<T>`:
- `sizeof(pptr<T>) == sizeof(void*)` — размер как у обычного указателя
- `pptr<T>()` — нулевой указатель по умолчанию
- `is_null()` / `operator bool()` — проверка на нулевой указатель
- `get()` / `operator*` / `operator->` — разыменование через синглтон (Фаза 7)
- `resolve(mgr)` — разыменование через явный менеджер (обратная совместимость)
- `resolve_at(mgr, index)` — доступ к элементу массива
- `offset()` — хранимое смещение (для сохранения/восстановления)
- Операторы `==` и `!=`

## Персистентность (Фаза 3)

Сохранение и восстановление кучи:

```cpp
// Программа A — создаём и сохраняем
pmm::PersistMemoryManager::create(memory, size);
auto* mgr = pmm::PersistMemoryManager::instance();
void* ptr = mgr->allocate(256);
std::strcpy((char*)ptr, "Hello, World!");
mgr->save("heap.dat");                       // сохранить образ в файл
pmm::PersistMemoryManager::destroy();        // освобождает буфер

// Программа B — восстанавливаем
void* buf = std::malloc(size);
pmm::load_from_file("heap.dat", buf, size);  // устанавливает синглтон
pmm::PersistMemoryManager::instance()->validate(); // → true
```

Поскольку все метаданные хранятся как **смещения** (а не абсолютные указатели), образ корректно загружается по любому базовому адресу без пересчёта.

## Возможности (Фаза 2)

- **Слияние блоков (coalescing)** — при освобождении блока автоматически объединяются соседние свободные блоки, что снижает фрагментацию до нуля при полном освобождении памяти

## Стресс-тест и бенчмарк

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/examples/stress_test
./build/examples/benchmark
```

Результаты на типичном железе (Фаза 6):
- **100 000 последовательных аллокаций** — ~7 мс (цель ≤ 100 мс: ✅)
- **100 000 деаллокаций** — ~0.8 мс (цель ≤ 100 мс: ✅)
- **1 000 000 чередующихся операций** — ~14 мс (~0,014 мкс на операцию)

Ускорение по сравнению с Фазой 4: ~2 200× для последовательных аллокаций.

## Структура репозитория

```
PersistMemoryManager/
├── include/
│   └── persist_memory_manager.h    # Single-header реализация
├── examples/
│   ├── basic_usage.cpp             # Базовое использование (Фаза 1)
│   ├── persistence_demo.cpp        # Демонстрация персистентности (Фаза 3)
│   ├── stress_test.cpp             # Стресс-тест 100K/1M операций (Фаза 4)
│   ├── benchmark.cpp               # Бенчмарк производительности (Фаза 6)
│   └── CMakeLists.txt
├── tests/
│   ├── test_allocate.cpp           # Тесты выделения (Фаза 1)
│   ├── test_deallocate.cpp         # Тесты освобождения (Фаза 1)
│   ├── test_coalesce.cpp           # Тесты слияния блоков (Фаза 2)
│   ├── test_persistence.cpp        # Тесты персистентности (Фаза 3)
│   ├── test_pptr.cpp               # Тесты персистного указателя pptr<T> (Фаза 5)
│   ├── test_performance.cpp        # Тесты производительности (Фаза 6)
│   └── CMakeLists.txt
├── docs/
│   ├── architecture.md             # Архитектура
│   ├── api_reference.md            # Справочник по API (Фаза 4)
│   └── performance.md              # Производительность (Фаза 4)
├── plan.md                         # План разработки
├── phase1.md                       # Фаза 1: Базовая структура
├── phase2.md                       # Фаза 2: Слияние блоков
├── phase3.md                       # Фаза 3: Персистентность
├── phase4.md                       # Фаза 4: Тесты и документация
├── phase6.md                       # Фаза 6: Оптимизация производительности
├── tz.md                           # Техническое задание
├── CMakeLists.txt
└── LICENSE
```

## Завершённые фазы

### Фаза 1 — Базовая структура

- `PersistMemoryManager::create()` — инициализация менеджера
- `PersistMemoryManager::load()` — загрузка из существующего образа
- `PersistMemoryManager::destroy()` — уничтожение
- `PersistMemoryManager::allocate()` — выделение памяти
- `PersistMemoryManager::deallocate()` — освобождение памяти
- `PersistMemoryManager::reallocate()` — перевыделение
- Метрики: `total_size()`, `used_size()`, `free_size()`, `fragmentation()`
- Диагностика: `validate()`, `dump_stats()`
- Вспомогательные функции: `get_stats()`, `get_info()`

### Фаза 2 — Слияние блоков (coalescing)

- Приватный метод `coalesce()` объединяет соседние свободные блоки при освобождении
- Слияние с левым соседом, правым соседом или обоими одновременно
- Фрагментация снижается до нуля после освобождения всех блоков

### Фаза 3 — Персистентность (save/load)

- `save(const char* filename)` — запись образа кучи в двоичный файл
- `load_from_file(filename, memory, size)` — загрузка образа из файла в буфер
- Корректная работа по любому базовому адресу (все ссылки — смещения)
- Определение повреждённых образов через магическое число

### Фаза 4 — Тесты и документация

- `examples/stress_test.cpp` — стресс-тест: 100 000 последовательных аллокаций + 1 000 000 чередующихся операций
- `docs/api_reference.md` — полный справочник по API
- `docs/performance.md` — результаты тестов и анализ производительности
- Сборка примеров через `examples/CMakeLists.txt`

### Фаза 5 — Персистный указатель pptr<T>

- `pptr<T>` — шаблонный класс персистного типизированного указателя
- `sizeof(pptr<T>) == sizeof(void*)` — размер как у обычного указателя
- Хранит смещение от базы менеджера (не абсолютный адрес)
- Корректно восстанавливается после save/load по любому базовому адресу
- `allocate_typed<T>()` / `allocate_typed<T>(count)` — выделение объекта/массива
- `deallocate_typed(pptr<T>)` — освобождение памяти
- 14 тестов в `tests/test_pptr.cpp`

### Фаза 6 — Оптимизация производительности

- Отдельный двусвязный список свободных блоков (`first_free_offset` в заголовке)
- `allocate()` обходит только свободные блоки — O(f) вместо O(n)
- При паттерне «выделить всё подряд»: O(N) вместо O(N²)
- `rebuild_free_list()` — восстановление при загрузке образа через `load()`
- 8 тестов производительности в `tests/test_performance.cpp`
- Бенчмарк в `examples/benchmark.cpp`
- Результат: allocate 100K блоков ~7 мс (цель ≤ 100 мс ✅, ускорение ~2200×)

### Фаза 7 — Синглтон + автоматическое расширение памяти

- `PersistMemoryManager::instance()` — глобальный доступ к единственному активному менеджеру
- `create()` и `load()` / `load_from_file()` устанавливают синглтон
- `destroy()` стал **статическим** и автоматически освобождает управляемый буфер (`std::free`)
- `expand()` — при нехватке памяти менеджер выделяет новый буфер на 25% больше, копирует данные и освобождает старый
- `pptr<T>::get()`, `operator*`, `operator->` — разыменование через синглтон без передачи менеджера
- Все тесты и примеры обновлены для нового API

Подробнее: [plan.md](plan.md) | [phase1.md](phase1.md) | [phase2.md](phase2.md) | [phase3.md](phase3.md) | [phase4.md](phase4.md) | [phase6.md](phase6.md) | [docs/architecture.md](docs/architecture.md) | [docs/api_reference.md](docs/api_reference.md) | [docs/performance.md](docs/performance.md)

## Лицензия

Unlicense — общественное достояние. Подробности в файле `LICENSE`.
