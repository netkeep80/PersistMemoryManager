# Техническое задание: Визуальное демо PersistMemoryManager

## 1. Обзор

Разработать кросс-платформенное демонстрационное приложение для визуализации работы
**PersistMemoryManager** (PMM) в реальном времени.

Приложение строится на базе [Dear ImGui](https://github.com/ocornut/imgui) + OpenGL 3 / GLFW и
компилируется под Windows, Linux и macOS без изменений кода.

---

## 2. Цели

| # | Цель |
|---|------|
| 1 | Наглядно показать состояние управляемой памяти (карта байт в реальном времени) |
| 2 | Дать возможность запускать и останавливать тестовые сценарии в отдельных потоках |
| 3 | Отображать метрики менеджера в реальном времени |
| 4 | Позволять просматривать внутренние структуры данных в виде дерева |

---

## 3. Архитектура

```
┌─────────────────────────────────────────────────┐
│                  DemoApp                        │
│  ┌──────────┐  ┌──────────┐  ┌──────────────┐  │
│  │MemMapView│  │MetricsView│ │StructTreeView│  │
│  └──────────┘  └──────────┘  └──────────────┘  │
│                                                 │
│  ┌──────────────────────────────────────────┐   │
│  │          ScenarioManager                 │   │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐    │   │
│  │  │Thread 1 │ │Thread 2 │ │Thread N │    │   │
│  │  └─────────┘ └─────────┘ └─────────┘    │   │
│  └──────────────────────────────────────────┘   │
│                                                 │
│  ┌──────────────────────────────────────────┐   │
│  │        PersistMemoryManager              │   │
│  │        (pmm::PersistMemoryManager)       │   │
│  └──────────────────────────────────────────┘   │
└─────────────────────────────────────────────────┘
```

### 3.1 Потоковая модель

- **Основной поток (UI)** — Dear ImGui render loop (~60 FPS).
- **Потоки сценариев** — каждый сценарий выполняется в `std::thread`; доступ к PMM
  защищён встроенным `shared_mutex` библиотеки.
- Данные для отображения копируются из PMM-структур в UI-буферы под коротким локом
  (`std::shared_lock`) один раз за кадр.

---

## 4. Структура файлов

```
demo/
├── CMakeLists.txt          # Сборка демо
├── main.cpp                # Точка входа, ImGui setup
├── demo_app.h/.cpp         # DemoApp — главный класс приложения
├── mem_map_view.h/.cpp     # Виджет карты памяти
├── metrics_view.h/.cpp     # Виджет метрик
├── struct_tree_view.h/.cpp # Виджет дерева структур
├── scenario_manager.h/.cpp # Управление тестовыми потоками
├── scenarios.h/.cpp        # Реализации 7 сценариев
└── third_party/
    ├── imgui/              # Dear ImGui (submodule / FetchContent)
    └── glfw/               # GLFW (submodule / FetchContent)
```

---

## 5. Модуль: Карта памяти (`MemMapView`)

### 5.1 Описание

Отображает управляемую область PMM как двумерный растр: **1 байт = 1 пиксель**.

| Состояние байта | Цвет |
|-----------------|------|
| Заголовок менеджера (`ManagerHeader`) | Синий `#4488FF` |
| Заголовок блока (`BlockHeader`), занятый | Тёмно-красный `#882222` |
| Данные пользователя, занятый блок | Красный `#FF4444` |
| Заголовок блока, свободный | Тёмно-серый `#444444` |
| Данные (свободный блок) | Белый `#FFFFFF` |
| Вне блоков | Чёрный `#000000` |

### 5.2 Параметры отображения

- **Ширина растра** — настраивается ползунком (32 … 512 пикс.), по умолчанию 256.
- **Масштаб** — от 1×1 до 4×4 пикселей на байт.
- **Авто-масштаб** — автоматически подбирает ширину, чтобы карта помещалась в окно.
- При наведении курсора на пиксель — всплывающая подсказка:
  смещение, тип (заголовок / данные / свободно), принадлежность блоку.

### 5.3 Обновление

Карта перерисовывается каждый кадр из снимка состояния PMM, созданного за один
`std::shared_lock`-проход по всем блокам.

---

## 6. Модуль: Метрики (`MetricsView`)

Отображает в реальном времени следующие показатели (обновляются каждый кадр):

| Метрика | Источник PMM API |
|---------|-----------------|
| Общий размер памяти | `total_size()` |
| Занято | `used_size()` |
| Свободно | `free_size()` |
| Всего блоков | `MemoryStats::total_blocks` |
| Занятых блоков | `MemoryStats::allocated_blocks` |
| Свободных блоков | `MemoryStats::free_blocks` |
| Фрагментация (число свободных сегментов) | `fragmentation()` |
| Наибольший свободный блок | `MemoryStats::largest_free` |
| Наименьший свободный блок | `MemoryStats::smallest_free` |
| Всего операций alloc/dealloc | внутренние счётчики сценариев |
| Скорость операций (ops/s) | скользящее среднее за 1 с |

Дополнительно строятся **графики истории** (scrolling plot, 256 точек):

- `used_size` во времени.
- `fragmentation` во времени.
- ops/s во времени.

---

## 7. Модуль: Дерево структур (`StructTreeView`)

Иерархическое дерево на основе `ImGui::TreeNode`:

```
PersistMemoryManager
├── ManagerHeader
│   ├── magic: 0x504D4D5F56303130
│   ├── total_size: 1048576
│   ├── used_size: 204800
│   ├── block_count: 42
│   ├── free_count: 17
│   ├── alloc_count: 25
│   ├── first_block_offset: 128
│   └── first_free_offset: 256
└── Blocks [42]
    ├── Block #0  offset=128   size=4096   FREE
    ├── Block #1  offset=4224  size=8192   USED  user_size=8000  align=16
    ├── Block #2  offset=12416 size=128    FREE
    ...
    └── Block #N  ...
```

- Клик на блок — выделяет соответствующий регион на карте памяти (подсветка).
- При количестве блоков > 1000 показывается только первые 500 и последние 500 с
  сообщением «... X блоков скрыто ...».

---

## 8. Модуль: Управление сценариями (`ScenarioManager`)

### 8.1 UI

Панель «Scenarios»:

```
[▶ Start All]  [■ Stop All]
──────────────────────────────────────────────────
 Scenario         Status    Ops   alloc/s  dealloc/s
──────────────────────────────────────────────────
[▶][■] Linear Fill         RUNNING  12 034  1 200   1 200
[▶][■] Random Stress       STOPPED      0       0       0
[▶][■] Fragmentation Demo  RUNNING   8 210    410     410
[▶][■] Large Blocks        STOPPED      0       0       0
[▶][■] Tiny Blocks         RUNNING  45 003  4 500   4 500
[▶][■] Mixed Sizes         STOPPED      0       0       0
[▶][■] Persistence Cycle   STOPPED      0       0       0
──────────────────────────────────────────────────
```

### 8.2 Параметры сценария (на каждый)

| Параметр | Тип | Диапазон | По умолчанию |
|----------|-----|----------|--------------|
| Min block size | `size_t` | 8 … 1 MB | зависит от сценария |
| Max block size | `size_t` | 8 … 1 MB | зависит от сценария |
| Alloc frequency | `float` (ops/s) | 1 … 100 000 | зависит от сценария |
| Dealloc frequency | `float` (ops/s) | 1 … 100 000 | зависит от сценария |
| Max live blocks | `int` | 1 … 10 000 | 100 |

Параметры доступны в collapsible-секции под каждым сценарием.

---

## 9. Описание сценариев

### 9.1 Linear Fill

Последовательно выделяет блоки одинакового размера до заполнения памяти, затем
освобождает всё и повторяет. Демонстрирует линейное заполнение и полную очистку.

| Параметр | Значение по умолчанию |
|----------|-----------------------|
| Min/Max block size | 256 / 256 байт |
| Alloc frequency | 500 ops/s |
| Dealloc frequency | 0 (только после полного заполнения) |

### 9.2 Random Stress

Случайно чередует `allocate` и `deallocate` в случайном порядке.
Демонстрирует стрессовую нагрузку со случайными размерами.

| Параметр | Значение по умолчанию |
|----------|-----------------------|
| Min/Max block size | 64 / 4096 байт |
| Alloc frequency | 2 000 ops/s |
| Dealloc frequency | 1 800 ops/s |

### 9.3 Fragmentation Demo

Чередует выделение маленьких и больших блоков, освобождает только маленькие,
демонстрирует фрагментацию.

| Параметр | Значение по умолчанию |
|----------|-----------------------|
| Min/Max block size | 16 / 16 384 байт |
| Alloc frequency | 300 ops/s |
| Dealloc frequency | 250 ops/s |

### 9.4 Large Blocks

Работает исключительно с крупными блоками. Показывает автоматическое расширение
памяти при нехватке.

| Параметр | Значение по умолчанию |
|----------|-----------------------|
| Min/Max block size | 65 536 / 262 144 байт |
| Alloc frequency | 20 ops/s |
| Dealloc frequency | 18 ops/s |

### 9.5 Tiny Blocks

Интенсивно выделяет и освобождает микроблоки. Проверяет накладные расходы на
управление большим числом маленьких блоков.

| Параметр | Значение по умолчанию |
|----------|-----------------------|
| Min/Max block size | 8 / 32 байт |
| Alloc frequency | 10 000 ops/s |
| Dealloc frequency | 9 500 ops/s |

### 9.6 Mixed Sizes

Несколько параллельных «рабочих» потоков с разными профилями выделения.
Имитирует реальную нагрузку приложения.

| Параметр | Значение по умолчанию |
|----------|-----------------------|
| Min/Max block size | 32 / 32 768 байт |
| Alloc frequency | 1 000 ops/s |
| Dealloc frequency | 950 ops/s |

### 9.7 Persistence Cycle

Сохраняет образ PMM в файл (`pmm_demo.bin`) с помощью `save()`, затем перезагружает
через `load()`. Демонстрирует персистентность данных.

| Параметр | Значение по умолчанию |
|----------|-----------------------|
| Min/Max block size | 128 / 1024 байт |
| Cycle period | 5 s |

---

## 10. Главное окно и навигация

```
┌────────────────────────────────────────────────────────────────────┐
│  PersistMemoryManager Demo                          [?] [Settings] │
├───────────────┬──────────────────────────┬─────────────────────────┤
│  Scenarios    │     Memory Map           │    Metrics              │
│  (§ 8)        │     (§ 5)                │    (§ 6)                │
│               │                          │                         │
│               ├──────────────────────────┤                         │
│               │     Struct Tree          │                         │
│               │     (§ 7)                │                         │
└───────────────┴──────────────────────────┴─────────────────────────┘
```

- Все панели — `ImGui::Begin` / `End` и могут быть отстыкованы (`dockspace`).
- Кнопка `[?]` — встроенная справка с описанием цветового кодирования.
- Кнопка `[Settings]` — диалог настроек:
  - Начальный размер памяти PMM (1 MB … 256 MB, по умолчанию 8 MB).
  - FPS-лимит (10 … 144, по умолчанию 60).
  - Тема ImGui (Dark / Light / Classic).

---

## 11. Используемые вызовы PMM API

| Вызов | Где используется |
|-------|-----------------|
| `PersistMemoryManager::create(buf, size)` | Инициализация при старте |
| `PersistMemoryManager::load(buf, size)` | Сценарий Persistence Cycle |
| `PersistMemoryManager::destroy()` | Сценарий Persistence Cycle, выход |
| `PersistMemoryManager::instance()` | Получение синглтона в сценариях |
| `mgr->allocate(size, align)` | Все сценарии |
| `mgr->deallocate(ptr)` | Все сценарии |
| `mgr->reallocate(ptr, new_size)` | Сценарий Mixed Sizes |
| `mgr->total_size()` | MetricsView |
| `mgr->used_size()` | MetricsView |
| `mgr->free_size()` | MetricsView |
| `mgr->fragmentation()` | MetricsView |
| `mgr->validate()` | Фоновая проверка каждые 5 с |
| `mgr->dump_stats()` | Кнопка «Dump to stdout» |
| `pmm::get_stats(mgr)` | MetricsView (MemoryStats) |
| `pmm::get_info(mgr, ptr)` | Подсказка в StructTreeView |

---

## 12. Требования к сборке

| Компонент | Версия |
|-----------|--------|
| CMake | ≥ 3.16 |
| C++ стандарт | C++17 |
| OpenGL | ≥ 3.3 |
| GLFW | 3.3+ (FetchContent) |
| Dear ImGui | 1.90+ (FetchContent, ветка `docking`) |
| Потоки | `std::thread` (POSIX / Win32) |

### 12.1 Команды сборки

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target pmm_demo
./build/demo/pmm_demo
```

### 12.2 CMakeLists.txt (demo/)

```cmake
cmake_minimum_required(VERSION 3.16)
project(pmm_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(glfw imgui)

add_executable(pmm_demo
    main.cpp
    demo_app.cpp
    mem_map_view.cpp
    metrics_view.cpp
    struct_tree_view.cpp
    scenario_manager.cpp
    scenarios.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(pmm_demo PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${CMAKE_SOURCE_DIR}/include   # persist_memory_manager.h
)

find_package(OpenGL REQUIRED)
target_link_libraries(pmm_demo PRIVATE
    glfw
    OpenGL::GL
    Threads::Threads
)
```

---

## 13. Нефункциональные требования

| Требование | Значение |
|------------|---------|
| FPS при 1 сценарии | ≥ 60 FPS |
| FPS при 7 сценариях | ≥ 30 FPS |
| Задержка обновления карты | ≤ 1 кадр (≤ ~17 мс при 60 FPS) |
| Потребление памяти процессом | ≤ 512 MB |
| Корректное завершение | без утечек памяти и гонок данных |

---

## 14. Этапы разработки

| Этап | Содержание | Результат |
|------|-----------|-----------|
| 1 | Настройка ImGui + GLFW окно, базовая структура `DemoApp` | Окно открывается |
| 2 | `MemMapView`: рендеринг карты памяти из снимка PMM | Карта отображается |
| 3 | `MetricsView`: метрики + scrolling plots | Цифры и графики в реальном времени |
| 4 | `ScenarioManager` + 3 базовых сценария (Linear Fill, Random Stress, Tiny Blocks) | Потоки запускаются и останавливаются |
| 5 | Оставшиеся 4 сценария | Все 7 сценариев работают |
| 6 | `StructTreeView` + подсветка блока в карте | Дерево кликабельно |
| 7 | Settings dialog, docking, polish | Финальный вид |
| 8 | Тесты, CI, документация | Проект готов к релизу |

---

## 15. Ссылки

- Репозиторий PersistMemoryManager: https://github.com/netkeep80/PersistMemoryManager
- Dear ImGui: https://github.com/ocornut/imgui
- GLFW: https://github.com/glfw/glfw
- Техническое задание на библиотеку: [tz.md](tz.md)
- Архитектура библиотеки: [docs/architecture.md](docs/architecture.md)
