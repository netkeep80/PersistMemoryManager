# PersistMemoryManager

**Single-header C++17 библиотека управления персистентной кучей памяти.**

📚 **[Документация API (Doxygen)](https://netkeep80.github.io/PersistMemoryManager/)**

Библиотека предоставляет низкоуровневый менеджер памяти, который хранит все метаданные внутри управляемой области. Это позволяет сохранять образ кучи в файл или shared memory и восстанавливать его между запусками программы.

## Возможности

- **Single-header** — вся реализация в одном файле `include/persist_memory_manager.h`
- **C++17** — без внешних зависимостей, только стандартная библиотека
- **Персистентность** — все ссылки хранятся как смещения, а не абсолютные указатели
- **pptr<T>** — персистный типизированный указатель, sizeof == 4 байта (32-битный гранульный индекс)
- **Статический класс** — весь API через статические методы, без экземпляров класса
- **16-байтная гранулярность** — поддержка адресации до 64 ГБ
- **Диагностика** — validate(), dump_stats(), get_stats(), get_manager_info(), for_each_block()
- **Высокая производительность** — AVL-дерево свободных блоков, allocate 100K ≤ 7 мс
- **Синглтон** — единственный активный менеджер доступен через `PersistMemoryManager::instance()` (для внутреннего использования)
- **Автоматическое расширение** — при нехватке памяти буфер автоматически растёт на 25%
- **Потокобезопасность** — разделённые блокировки через `std::shared_mutex` (параллельное чтение, эксклюзивная запись)

## Быстрый старт

```cpp
#include "persist_memory_manager.h"

int main() {
    // Выделить системную память под менеджер (например, 1 МБ)
    void* memory = std::malloc(1024 * 1024);

    // Создать менеджер (устанавливает синглтон)
    bool ok = pmm::PersistMemoryManager::create(memory, 1024 * 1024);

    // Выделить блоки через типизированный API (при нехватке памяти автоматически расширяет буфер на 25%)
    pmm::pptr<uint8_t> block1 = pmm::PersistMemoryManager::allocate_typed<uint8_t>(256);  // 256 байт
    pmm::pptr<uint8_t> block2 = pmm::PersistMemoryManager::allocate_typed<uint8_t>(1024); // 1 КБ

    // Освободить
    pmm::PersistMemoryManager::deallocate_typed(block1);

    // Получить статистику
    auto stats = pmm::get_stats();

    // Уничтожить менеджер
    pmm::PersistMemoryManager::destroy();
    std::free(memory); // Внешняя память освобождается вызывающим кодом
    return 0;
}
```

## Сборка

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Персистный указатель pptr<T>

`pptr<T>` — типизированный персистный указатель, который хранит 32-битный гранульный индекс (16 байт/гранула). Использует синглтон автоматически и не требует явной передачи менеджера.

```cpp
#include "persist_memory_manager.h"

int main() {
    void* memory = std::malloc(1024 * 1024);
    pmm::PersistMemoryManager::create(memory, 1024 * 1024);

    // Выделить один объект типа int
    pmm::pptr<int> p = pmm::PersistMemoryManager::allocate_typed<int>();
    *p = 42;           // operator* использует синглтон автоматически

    // Выделить массив из 10 double
    pmm::pptr<double> arr = pmm::PersistMemoryManager::allocate_typed<double>(10);
    for (int i = 0; i < 10; i++) {
        arr[i] = i * 1.5; // operator[] с проверкой границ
    }

    // Проверка на нулевой указатель
    if (!p.is_null()) { /* ненулевой */ }

    // Сохранить образ
    pmm::save("heap.dat");

    // Сохраняем смещение для восстановления после load
    uint32_t off = p.offset();

    pmm::PersistMemoryManager::deallocate_typed(arr);
    pmm::PersistMemoryManager::deallocate_typed(p);
    pmm::PersistMemoryManager::destroy();
    std::free(memory);

    // --- следующий запуск ---
    void* buf2 = std::malloc(1024 * 1024);
    pmm::load_from_file("heap.dat", buf2, 1024 * 1024); // устанавливает синглтон, возвращает bool

    // Восстанавливаем pptr<int> по сохранённому смещению
    pmm::pptr<int> p2(off);
    std::cout << *p2 << "\n"; // выведет 42 (operator* через синглтон)

    pmm::PersistMemoryManager::destroy();
    std::free(buf2);
    return 0;
}
```

Ключевые свойства `pptr<T>`:
- `sizeof(pptr<T>) == 4` — 32-битный гранульный индекс (16 байт/гранула)
- `pptr<T>()` — нулевой указатель по умолчанию
- `is_null()` / `operator bool()` — проверка на нулевой указатель
- `get()` / `operator*` / `operator->` — разыменование через синглтон
- `operator[]` — доступ к элементу массива (с проверкой границ)
- `offset()` — хранимый гранульный индекс (для сохранения/восстановления)
- Операторы `==` и `!=`

## Персистентность

Сохранение и восстановление кучи:

```cpp
// Программа A — создаём и сохраняем
pmm::PersistMemoryManager::create(memory, size);
pmm::pptr<char> ptr = pmm::PersistMemoryManager::allocate_typed<char>(256);
std::strcpy(ptr.get(), "Hello, World!");
pmm::save("heap.dat");                           // сохранить образ в файл
pmm::PersistMemoryManager::destroy();
std::free(memory);

// Программа B — восстанавливаем
void* buf = std::malloc(size);
pmm::load_from_file("heap.dat", buf, size);      // устанавливает синглтон
pmm::PersistMemoryManager::validate();           // → true
```

Поскольку все метаданные хранятся как **смещения** (а не абсолютные указатели), образ корректно загружается по любому базовому адресу без пересчёта.

- **Слияние блоков (coalescing)** — при освобождении блока автоматически объединяются соседние свободные блоки, что снижает фрагментацию до нуля при полном освобождении памяти

## API итератора блоков

Для безопасного обхода блоков и чтения метаданных менеджера без прямого доступа к внутренним
структурам библиотека предоставляет три функции:

### `for_each_block()`

```cpp
template <typename Callback>
void pmm::for_each_block( Callback&& callback );
// Callback: void(const pmm::BlockView&)
```

Вызывает `callback` для каждого блока памяти. Берёт `shared_lock` внутри — потокобезопасно
при параллельных `allocate` / `deallocate`. Каждый `BlockView` содержит: `index`, `offset`,
`total_size`, `header_size`, `user_size`, `alignment`, `used`.

```cpp
pmm::for_each_block( []( const pmm::BlockView& blk ) {
    std::cout << "Block #" << blk.index
              << " offset=" << blk.offset
              << " size="   << blk.total_size
              << ( blk.used ? " USED" : " FREE" ) << "\n";
} );
```

### `get_manager_info()`

```cpp
pmm::ManagerInfo pmm::get_manager_info();
```

Возвращает снимок полей заголовка менеджера: `magic`, `total_size`, `used_size`,
`block_count`, `free_count`, `alloc_count`, `first_block_offset`, `first_free_offset`,
`manager_header_size`.

### `PersistMemoryManager::manager_header_size()`

```cpp
static std::size_t pmm::PersistMemoryManager::manager_header_size() noexcept;
```

Возвращает размер служебного заголовка менеджера в байтах (первые N байт управляемой
области заняты под метаданные PMM).

## Визуальное демо

Демонстрационное приложение `pmm_demo` визуализирует работу PMM в реальном времени.
Построено на базе [Dear ImGui](https://github.com/ocornut/imgui) + OpenGL 3.3 + GLFW 3.4.
Работает на Windows, Linux и macOS без изменений кода.

**Возможности демо:**
- **Карта памяти** — каждый байт управляемой области отображается цветным пикселем в реальном времени
- **Метрики** — used/free, фрагментация, ops/s с историческими scrolling-графиками
- **Дерево структур** — ManagerHeader и все BlockHeader с кликабельной подсветкой на карте
- **7 сценариев нагрузки** — Linear Fill, Random Stress, Fragmentation Demo, Large Blocks,
  Tiny Blocks, Mixed Sizes, Persistence Cycle

**Сборка демо:**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DPMM_BUILD_DEMO=ON
cmake --build build --target pmm_demo
./build/demo/pmm_demo
```

Зависимости (Dear ImGui, GLFW) устанавливаются автоматически через CMake FetchContent.

**Headless-тесты демо (Фаза 8):**

При сборке с `PMM_BUILD_DEMO=ON` дополнительно компилируются три headless-теста.
Они не требуют графического окна и автоматически запускаются в CI (job `build-demo`):

```bash
# Сборка с демо-тестами
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DPMM_BUILD_DEMO=ON
cmake --build build

# Запуск всех тестов, включая тесты демо
ctest --test-dir build --output-on-failure

# Запуск только тестов демо
ctest --test-dir build -R "test_demo_headless|test_mem_map_view|test_scenario_manager" --output-on-failure
```

- `test_demo_headless` — запускает все 7 сценариев на 2 с, проверяет `validate()` и завершение потоков
- `test_mem_map_view` — тесты `MemMapView::update_snapshot()` без графического окна
- `test_scenario_manager` — тесты жизненного цикла `ScenarioManager::stop_all()` / `join_all()`
- `test_scenario_coordinator` — тесты `ScenarioCoordinator`: пауза/возобновление потоков, безопасная замена синглтона PMM (Фаза 10)
- `test_mem_map_view_tile` — тесты тайловой агрегации `MemMapView` для больших PMM: bytes_per_tile, доминирующий тип, ограничение ≤ 65536 тайлов (Фаза 11)
- `test_background_validator` — тесты фонового валидатора: `ValidationResult`, периодичность проверки, кнопка «Validate now» (Фаза 12)

Подробное техническое задание: [demo.md](demo.md) | План разработки: [plan.md](plan.md) | Фаза 12: [docs/phase-12-background-validator.md](docs/phase-12-background-validator.md)

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
│   ├── persist_memory_manager.h    # Single-header реализация
│   └── persist_memory_io.h         # Утилиты save/load
├── examples/
│   ├── basic_usage.cpp             # Базовое использование
│   ├── persistence_demo.cpp        # Демонстрация персистентности
│   ├── stress_test.cpp             # Стресс-тест 100K/1M операций
│   ├── benchmark.cpp               # Бенчмарк производительности
│   └── CMakeLists.txt
├── tests/
│   ├── test_allocate.cpp           # Тесты выделения
│   ├── test_deallocate.cpp         # Тесты освобождения
│   ├── test_coalesce.cpp           # Тесты слияния блоков
│   ├── test_persistence.cpp        # Тесты персистентности
│   ├── test_pptr.cpp               # Тесты pptr<T>
│   ├── test_performance.cpp        # Тесты производительности
│   ├── test_stress_realistic.cpp   # Реалистичный стресс-тест
│   ├── test_thread_safety.cpp      # Тесты потокобезопасности
│   ├── test_shared_mutex.cpp       # Тесты разделённых блокировок
│   ├── test_demo_headless.cpp          # Фаза 8: headless smoke-тест 7 сценариев
│   ├── test_mem_map_view.cpp           # Фаза 8: тесты MemMapView::update_snapshot
│   ├── test_scenario_manager.cpp       # Фаза 8: тесты ScenarioManager lifecycle
│   ├── test_scenario_coordinator.cpp   # Фаза 10: тесты ScenarioCoordinator
│   └── CMakeLists.txt
├── demo/                           # Визуальное демо (Dear ImGui + OpenGL)
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── demo_app.h/.cpp             # Главный класс приложения
│   ├── mem_map_view.h/.cpp         # Виджет карты памяти
│   ├── metrics_view.h/.cpp         # Виджет метрик
│   ├── struct_tree_view.h/.cpp     # Виджет дерева структур
│   ├── scenario_manager.h/.cpp     # Управление тестовыми потоками
│   └── scenarios.h/.cpp            # 7 сценариев нагрузки
├── docs/
│   ├── architecture.md             # Архитектура
│   ├── api_reference.md            # Справочник по API
│   ├── performance.md              # Производительность
│   ├── phase-1-infrastructure.md               # Отчёт о реализации Фазы 1
│   ├── phase-8-tests.md                        # Отчёт о реализации Фазы 8
│   ├── phase-9-block-iterator.md               # Отчёт о реализации Фазы 9
│   ├── phase-10-scenario-coordinator.md        # Отчёт о реализации Фазы 10
│   ├── phase-11-mem-map-tile.md                # Отчёт о реализации Фазы 11
│   └── phase-12-background-validator.md        # Отчёт о реализации Фазы 12
├── demo.md                         # Техническое задание на демо
├── plan.md                         # План разработки демо
├── CMakeLists.txt
└── LICENSE
```

Подробнее: [docs/architecture.md](docs/architecture.md) | [docs/api_reference.md](docs/api_reference.md) | [docs/performance.md](docs/performance.md) | [demo.md](demo.md) | [plan.md](plan.md) | [Doxygen API](https://netkeep80.github.io/PersistMemoryManager/)

**Версия 6.0.0** — PAP-гомогенизация: `ManagerHeader` встроен в `BlockHeader_0` (Issue #75).
PAP стал полностью однородным: весь образ — это лес блоков, первый из которых (`BlockHeader_0`) хранит метаданные менеджера.

## Лицензия

Unlicense — общественное достояние. Подробности в файле `LICENSE`.
