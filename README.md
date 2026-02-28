# PersistMemoryManager

**Single-header C++17 библиотека управления персистентной кучей памяти.**

Библиотека предоставляет низкоуровневый менеджер памяти, который хранит все метаданные внутри управляемой области. Это позволяет сохранять образ кучи в файл или shared memory и восстанавливать его между запусками программы.

## Возможности

- **Single-header** — вся реализация в одном файле `include/persist_memory_manager.h`
- **C++17** — без внешних зависимостей, только стандартная библиотека
- **Персистентность** — все ссылки хранятся как смещения, а не абсолютные указатели
- **Выравнивание** — поддержка alignment от 8 до 4096 байт
- **Диагностика** — validate(), dump_stats(), get_stats()

## Быстрый старт

```cpp
#include "persist_memory_manager.h"

int main() {
    // Выделить системную память под менеджер (например, 1 МБ)
    void* memory = std::malloc(1024 * 1024);

    // Создать менеджер
    auto* mgr = pmm::PersistMemoryManager::create(memory, 1024 * 1024);

    // Выделить блоки
    void* block1 = mgr->allocate(256);          // 256 байт, align=16
    void* block2 = mgr->allocate(1024, 32);     // 1 КБ, align=32

    // Освободить
    mgr->deallocate(block1);

    // Получить статистику
    auto stats = pmm::get_stats(mgr);

    // Уничтожить менеджер
    mgr->destroy();
    std::free(memory);
    return 0;
}
```

## Сборка

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Персистентность (Фаза 3)

Сохранение и восстановление кучи:

```cpp
// Программа A — создаём и сохраняем
auto* mgr = pmm::PersistMemoryManager::create(memory, size);
void* ptr = mgr->allocate(256);
std::strcpy((char*)ptr, "Hello, World!");
mgr->save("heap.dat");          // сохранить образ в файл
mgr->destroy();
std::free(memory);

// Программа B — восстанавливаем
void* buf = std::malloc(size);
auto* mgr2 = pmm::load_from_file("heap.dat", buf, size);
// mgr2 полностью восстановлен: те же блоки, те же данные
mgr2->validate(); // → true
```

Поскольку все метаданные хранятся как **смещения** (а не абсолютные указатели), образ корректно загружается по любому базовому адресу без пересчёта.

## Возможности (Фаза 2)

- **Слияние блоков (coalescing)** — при освобождении блока автоматически объединяются соседние свободные блоки, что снижает фрагментацию до нуля при полном освобождении памяти

## Структура репозитория

```
PersistMemoryManager/
├── include/
│   └── persist_memory_manager.h    # Single-header реализация
├── examples/
│   ├── basic_usage.cpp             # Базовое использование
│   └── persistence_demo.cpp        # Демонстрация персистентности
├── tests/
│   ├── test_allocate.cpp           # Тесты выделения (Фаза 1)
│   ├── test_deallocate.cpp         # Тесты освобождения (Фаза 1)
│   ├── test_coalesce.cpp           # Тесты слияния блоков (Фаза 2)
│   ├── test_persistence.cpp        # Тесты персистентности (Фаза 3)
│   └── CMakeLists.txt
├── docs/
│   └── architecture.md             # Архитектура
├── plan.md                         # План разработки
├── phase1.md                       # Фаза 1: Базовая структура
├── phase2.md                       # Фаза 2: Слияние блоков
├── phase3.md                       # Фаза 3: Персистентность
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

Подробнее: [plan.md](plan.md) | [phase1.md](phase1.md) | [phase2.md](phase2.md) | [phase3.md](phase3.md) | [docs/architecture.md](docs/architecture.md)

## Лицензия

Unlicense — общественное достояние. Подробности в файле `LICENSE`.
