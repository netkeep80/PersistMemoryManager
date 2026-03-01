# План разработки: Визуальное демо PersistMemoryManager

> **Базовый документ**: [demo.md](demo.md) — техническое задание
> **Целевая платформа**: Windows / Linux / macOS
> **Технологии**: Dear ImGui (docking) + OpenGL 3.3 + GLFW 3.4 + C++17 + std::thread

---

## Обзор фаз

| Фаза | Название | Ключевой результат | Оценка |
|------|----------|--------------------|--------|
| 1 | Инфраструктура и каркас | Компилируется, открывается окно | ~1 д |
| 2 | Карта памяти | MemMapView отображает байты PMM | ~1 д |
| 3 | Метрики | MetricsView с графиками в реальном времени | ~1 д |
| 4 | Управление сценариями и базовые сценарии | 3 сценария, запуск/стоп потоков | ~2 д |
| 5 | Все сценарии | 7 сценариев работают | ~1 д |
| 6 | Дерево структур | StructTreeView + подсветка | ~1 д |
| 7 | Полировка и настройки | Dockspace, Settings, Help, финальный UI | ~1 д |
| 8 | Тесты, CI, документация | Все тесты проходят, CI зелёный | ~1 д |

---

## Фаза 1: Инфраструктура и каркас DemoApp

### 1.1 Создание структуры файлов

Создать директорию `demo/` со следующими файлами:

```
demo/
├── CMakeLists.txt
├── main.cpp
├── demo_app.h
├── demo_app.cpp
├── mem_map_view.h
├── mem_map_view.cpp
├── metrics_view.h
├── metrics_view.cpp
├── struct_tree_view.h
├── struct_tree_view.cpp
├── scenario_manager.h
├── scenario_manager.cpp
├── scenarios.h
└── scenarios.cpp
```

### 1.2 CMakeLists.txt для demo/

- Добавить `FetchContent_Declare` для GLFW 3.4 и Dear ImGui (ветка `docking`).
- Подключить все исходники ImGui: `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui_impl_glfw.cpp`, `imgui_impl_opengl3.cpp`.
- Линковать `glfw`, `OpenGL::GL`, `Threads::Threads`, `pmm` (header-only интерфейс).
- Добавить `find_package(OpenGL REQUIRED)`.
- Обеспечить совместимость с Windows (OpenGL32.lib), Linux (libGL), macOS (framework).
- Добавить `add_subdirectory(demo)` в корневой `CMakeLists.txt` (опционально через `OPTION(PMM_BUILD_DEMO)`).

### 1.3 main.cpp — Точка входа

```
Задачи:
- Инициализация GLFW (glfwInit, glfwWindowHint, glfwCreateWindow).
- Настройка OpenGL контекста (glfwMakeContextCurrent, gladLoadGL или epoxy).
- Инициализация Dear ImGui (ImGui::CreateContext, ImGui_ImplGlfw_InitForOpenGL,
  ImGui_ImplOpenGL3_Init).
- Включить DockSpace: io.ConfigFlags |= ImGuiConfigFlags_DockingEnable.
- Главный цикл: poll events → new frame → DemoApp::render() → render → swap.
- Graceful shutdown: StopAll сценариев → join всех потоков → ImGui shutdown → glfwTerminate.
- FPS-лимит через glfwSwapInterval(1) + опциональный sleep.
```

### 1.4 DemoApp — Главный класс

```cpp
// demo_app.h
class DemoApp {
public:
    DemoApp();
    ~DemoApp();
    void render();          // вызывается каждый кадр
private:
    void render_main_menu();
    void render_dockspace();
    void render_help_window();
    void render_settings_window();

    std::unique_ptr<MemMapView>      mem_map_view_;
    std::unique_ptr<MetricsView>     metrics_view_;
    std::unique_ptr<StructTreeView>  struct_tree_view_;
    std::unique_ptr<ScenarioManager> scenario_manager_;

    bool show_help_     = false;
    bool show_settings_ = false;

    // PMM память
    std::vector<uint8_t> pmm_buffer_;
    size_t pmm_size_ = 8 * 1024 * 1024; // 8 МБ по умолчанию
};
```

Задачи:
- Конструктор: выделить `pmm_buffer_`, вызвать `PersistMemoryManager::create()`.
- Деструктор: остановить все сценарии, дождаться join, вызвать `PersistMemoryManager::destroy()`.
- `render()`: обновить UI-буфер из PMM за один `shared_lock`, передать в модули.

### 1.5 Проверочные критерии фазы 1

- [x] `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target pmm_demo` — успешно.
- [x] Запуск `./build/demo/pmm_demo` открывает пустое ImGui-окно.
- [x] Нет утечек памяти при немедленном закрытии.

---

## Фаза 2: Карта памяти (MemMapView)

### 2.1 Структура снимка памяти

```cpp
// mem_map_view.h
struct ByteInfo {
    enum class Type { ManagerHeader, BlockHeaderUsed, UserDataUsed,
                      BlockHeaderFree, UserDataFree, OutOfBlocks };
    Type     type;
    size_t   block_index; // индекс блока для подсветки
    size_t   offset;      // смещение от начала PMM
};

class MemMapView {
public:
    void update_snapshot(pmm::PersistMemoryManager* mgr); // вызов под shared_lock
    void render();
    size_t highlighted_block = SIZE_MAX; // подсветка из StructTreeView
private:
    std::vector<ByteInfo> snapshot_; // UI-буфер
    size_t total_bytes_ = 0;

    int    raster_width_ = 256;
    float  pixel_scale_  = 1.0f;
    bool   auto_scale_   = false;
};
```

### 2.2 Обход PMM для построения снимка

Алгоритм `update_snapshot()`:
```
1. Сбросить snapshot_ до размера total_size(), заполнить OutOfBlocks.
2. Первые sizeof(ManagerHeader) байт → Type::ManagerHeader.
3. Обход всех BlockHeader (через linked list по first_block_offset + next_offset):
   a. offset блока → Type::BlockHeaderUsed / BlockHeaderFree.
   b. [sizeof(BlockHeader)..(sizeof(BlockHeader)+user_size)) → UserDataUsed / UserDataFree.
4. Всё, что не покрыто → остаётся OutOfBlocks.
```

### 2.3 Рендеринг

- Использовать `ImGui::DrawList->AddRectFilled` для отрисовки пикселей.
- Ширина растра задаётся ползунком (`ImGui::SliderInt`).
- Масштаб: `ImGui::SliderFloat` (1.0..4.0).
- Авто-масштаб: `ImGui::Checkbox` + подстройка `raster_width_` под ширину панели.
- Tooltip при `ImGui::IsItemHovered()`: смещение, тип, номер блока.
- Цветовая схема:

  | Тип | RGBA |
  |-----|------|
  | `ManagerHeader` | `0xFF8844FF` (синий) |
  | `BlockHeaderUsed` | `0xFF222288` (тёмно-красный) |
  | `UserDataUsed` | `0xFF4444FF` (красный) |
  | `BlockHeaderFree` | `0xFF444444` (тёмно-серый) |
  | `UserDataFree` | `0xFFFFFFFF` (белый) |
  | `OutOfBlocks` | `0xFF000000` (чёрный) |

- Подсветка блока: если `block_index == highlighted_block` → нарисовать рамку поверх.

### 2.4 Синхронизация

- `update_snapshot()` вызывается ОДИН раз за кадр в `DemoApp::render()` под
  `std::shared_lock<std::shared_mutex>`.
- `render()` работает только с локальной копией `snapshot_`.

### 2.5 Проверочные критерии фазы 2

- [x] Карта отображает реальные байты PMM: ManagerHeader синий, пустое пространство черное.
- [x] При запуске сценария — карта обновляется в реальном времени.
- [x] Tooltip показывает правильное смещение при наведении.
- [x] Ползунки масштаба работают.

---

## Фаза 3: Метрики (MetricsView)

### 3.1 Структура данных метрик

```cpp
// metrics_view.h
struct MetricsSnapshot {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    size_t total_blocks;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t fragmentation;
    size_t largest_free;
    size_t smallest_free;
};

class MetricsView {
public:
    void update(const MetricsSnapshot& snap, float ops_per_sec);
    void render();
private:
    static constexpr int kHistorySize = 256;
    float used_history_[kHistorySize]   = {};
    float frag_history_[kHistorySize]   = {};
    float ops_history_[kHistorySize]    = {};
    int   history_offset_ = 0;

    MetricsSnapshot current_{};
    float current_ops_per_sec_ = 0.0f;
};
```

### 3.2 Получение снимка метрик

В `DemoApp::render()` под `shared_lock`:
```cpp
auto stats = pmm::get_stats(mgr);
MetricsSnapshot snap;
snap.total_size        = mgr->total_size();
snap.used_size         = mgr->used_size();
snap.free_size         = mgr->free_size();
snap.total_blocks      = stats.total_blocks;
snap.allocated_blocks  = stats.allocated_blocks;
snap.free_blocks       = stats.free_blocks;
snap.fragmentation     = mgr->fragmentation();
snap.largest_free      = stats.largest_free;
snap.smallest_free     = stats.smallest_free;
```

### 3.3 Рендеринг метрик

- Таблица `ImGui::BeginTable` из 2 колонок: «Метрика» / «Значение».
- Прогресс-бары для used/free: `ImGui::ProgressBar(used / total)`.
- Scrolling plots (`ImGui::PlotLines`):
  - `used_history_` — использование памяти во времени.
  - `frag_history_` — фрагментация во времени.
  - `ops_history_` — ops/s во времени.
- Кнопка «Dump to stdout» → `mgr->dump_stats()`.

### 3.4 Вычисление ops/s

Скользящее среднее за 1 с:
```
ops_counter_ (атомарный, инкрементируется сценариями)
каждую секунду: ops_per_sec = ops_counter_.exchange(0)
```

### 3.5 Проверочные критерии фазы 3

- [x] Все 9 метрик отображаются корректно.
- [x] Прогресс-бар used/total работает.
- [x] Три scrolling-графика обновляются в реальном времени.
- [x] Кнопка «Dump to stdout» вызывает dump_stats().

---

## Фаза 4: ScenarioManager и базовые сценарии

### 4.1 Интерфейс ScenarioParams

```cpp
// scenarios.h
struct ScenarioParams {
    size_t min_block_size  = 64;
    size_t max_block_size  = 4096;
    float  alloc_freq      = 1000.0f;   // ops/s
    float  dealloc_freq    = 900.0f;    // ops/s
    int    max_live_blocks = 100;
};

class Scenario {
public:
    virtual ~Scenario() = default;
    virtual const char* name() const = 0;
    virtual void run(std::atomic<bool>& stop_flag,
                     std::atomic<uint64_t>& op_counter,
                     const ScenarioParams& params) = 0;
};
```

### 4.2 ScenarioManager

```cpp
// scenario_manager.h
struct ScenarioState {
    std::string        name;
    std::thread        thread;
    std::atomic<bool>  running{false};
    std::atomic<bool>  stop_flag{false};
    std::atomic<uint64_t> total_ops{0};
    std::atomic<uint64_t> alloc_ops{0};
    std::atomic<uint64_t> dealloc_ops{0};
    ScenarioParams     params;
    bool               show_params = false;
};

class ScenarioManager {
public:
    ScenarioManager();
    ~ScenarioManager();
    void start(size_t index);
    void stop(size_t index);
    void start_all();
    void stop_all();
    void join_all();
    void render();
    float total_ops_per_sec() const;
private:
    std::vector<std::unique_ptr<Scenario>> scenarios_;
    std::vector<ScenarioState>             states_;
    void render_scenario_row(size_t i);
};
```

### 4.3 Реализация базовых сценариев (Фаза 4 — 3 сценария)

#### Сценарий 1: Linear Fill

```
Параметры по умолчанию: min/max=256, alloc_freq=500, dealloc_freq=0
Алгоритм:
  loop:
    - Выделять блоки одинакового размера (min_block_size) до OOM (allocate возвращает nullptr).
    - Затем освобождать все выделенные блоки.
    - Пауза: 1/alloc_freq секунд между операциями.
    - Проверять stop_flag между операциями.
```

#### Сценарий 2: Random Stress

```
Параметры по умолчанию: min=64, max=4096, alloc_freq=2000, dealloc_freq=1800
Алгоритм:
  loop:
    - Если live_blocks < max_live_blocks: allocate со случайным размером [min,max].
    - Случайно выбрать: выделить или освободить (с весами alloc_freq vs dealloc_freq).
    - Случайно выбрать живой блок для освобождения.
```

#### Сценарий 5: Tiny Blocks

```
Параметры по умолчанию: min=8, max=32, alloc_freq=10000, dealloc_freq=9500
Алгоритм:
  loop:
    - Интенсивно чередовать micro-alloc/dealloc.
    - Использовать очередь FIFO для выбора блока на освобождение.
```

### 4.4 UI панели сценариев

```
[▶ Start All]  [■ Stop All]
──────────────────────────────────────────────────────────
 Scenario           Status    Ops      alloc/s  dealloc/s
──────────────────────────────────────────────────────────
[▶][■] Linear Fill  RUNNING   12 034   1 200    1 200
                  ▼ (collapsible params)
                     Min size: [   256] Max size: [   256]
                     Alloc freq: [  500] Dealloc freq: [    0]
                     Max live: [  100]
```

Реализовать через:
- `ImGui::Button("▶")` / `ImGui::Button("■")` в каждой строке.
- `ImGui::CollapsingHeader` для параметров.
- `ImGui::InputInt` / `ImGui::SliderFloat` для редактирования параметров.
- Цвет статуса: зелёный (RUNNING) / серый (STOPPED).

### 4.5 Проверочные критерии фазы 4

- [x] 3 сценария запускаются и останавливаются без зависаний.
- [x] `join()` вызывается корректно — нет SIGABRT при выходе.
- [x] ops-счётчики увеличиваются в MetricsView.
- [x] Карта памяти меняется при запущенных сценариях.
- [x] Параметры сценариев доступны для редактирования через collapsible UI.

---

## Фаза 5: Оставшиеся 4 сценария

### 5.1 Сценарий 3: Fragmentation Demo

```
Параметры по умолчанию: min=16, max=16384, alloc_freq=300, dealloc_freq=250
Алгоритм:
  loop:
    - Чередовать выделение малых (16..64 байт) и больших (4096..16384 байт) блоков.
    - Освобождать только малые блоки → создавать «дыры» между большими.
    - Наблюдать рост fragmentation() в MetricsView.
```

### 5.2 Сценарий 4: Large Blocks

```
Параметры по умолчанию: min=65536, max=262144, alloc_freq=20, dealloc_freq=18
Алгоритм:
  loop:
    - Выделять крупные блоки.
    - При нехватке — PMM автоматически расширяется (auto-grow на 25%).
    - Освобождать по FIFO.
```

### 5.3 Сценарий 6: Mixed Sizes

```
Параметры по умолчанию: min=32, max=32768, alloc_freq=1000, dealloc_freq=950
Алгоритм:
  - Имитировать несколько «рабочих профилей» внутри одного потока:
    профиль A: 80% малых (32..256), 20% больших (1024..32768).
    профиль B: 50% средних (256..4096).
  - Использовать reallocate() для изменения размера живых блоков (5% операций).
```

### 5.4 Сценарий 7: Persistence Cycle

```
Параметры по умолчанию: min=128, max=1024, cycle_period=5s
Алгоритм:
  loop:
    - Выделить несколько блоков, записать данные.
    - pmm::save(mgr, "pmm_demo.bin") → сохранить образ.
    - PersistMemoryManager::destroy().
    - Выделить новый буфер, pmm::load_from_file("pmm_demo.bin", ...).
    - validate() → убедиться в корректности.
    - Ждать cycle_period, затем повторить.
```

### 5.5 Проверочные критерии фазы 5

- [x] Все 7 сценариев запускаются и работают стабильно.
- [x] Сценарий Fragmentation Demo визуально показывает фрагментацию в MetricsView.
- [x] Сценарий Large Blocks демонстрирует авторасширение (MetricsView: total_size увеличивается).
- [x] Сценарий Persistence Cycle сохраняет и восстанавливает файл `pmm_demo.bin`.
- [x] validate() в Persistence Cycle возвращает `true` после reload.

---

## Фаза 6: Дерево структур (StructTreeView)

### 6.1 Структура снимка

```cpp
// struct_tree_view.h
struct BlockSnapshot {
    size_t index;
    size_t offset;
    size_t total_size;
    size_t user_size;
    size_t alignment;
    bool   used;
};

struct TreeSnapshot {
    // ManagerHeader поля
    uint64_t magic;
    size_t   total_size;
    size_t   used_size;
    size_t   block_count;
    size_t   free_count;
    size_t   alloc_count;
    ptrdiff_t first_block_offset;
    ptrdiff_t first_free_offset;

    std::vector<BlockSnapshot> blocks;
};

class StructTreeView {
public:
    void update_snapshot(pmm::PersistMemoryManager* mgr);
    void render(size_t& highlighted_block); // выходной параметр для MemMapView
private:
    TreeSnapshot snapshot_;
    static constexpr size_t kMaxVisibleBlocks = 500;
};
```

### 6.2 Рендеринг дерева

```
ImGui::TreeNode("PersistMemoryManager")
├── ImGui::TreeNode("ManagerHeader")
│   ├── magic: 0x504D4D5F56303130
│   ├── total_size: 8388608
│   ├── used_size: 204800
│   ...
│   └── (all fields from ManagerHeader struct)
└── ImGui::TreeNode("Blocks [N]")
    ├── если N <= 1000: все блоки
    └── если N > 1000:
        ├── первые 500 блоков
        ├── ImGui::Text("... %zu блоков скрыто ...", N - 1000)
        └── последние 500 блоков
```

Для каждого блока:
```cpp
// Заголовок: "Block #3  offset=4224  size=8192  USED  user_size=8000  align=16"
bool selected = (i == highlighted_block);
if (ImGui::Selectable(label, selected))
    highlighted_block = i; // уведомить MemMapView
```

### 6.3 Подсветка в MemMapView

- При клике на блок в StructTreeView → `highlighted_block` передаётся в `MemMapView::highlighted_block`.
- MemMapView рисует рамку вокруг байт данного блока.

### 6.4 Проверочные критерии фазы 6

- [x] Дерево отображает корректные значения ManagerHeader.
- [x] Все блоки перечислены (или первые/последние 500 при > 1000).
- [x] Клик на блок подсвечивает его в карте памяти.
- [x] При > 1000 блоков показывается сообщение о скрытых блоках.

---

## Фаза 7: Полировка, Settings, Dockspace

### 7.1 DockSpace

```cpp
// В начале render():
ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
```

- Панели «Scenarios», «Memory Map», «Metrics», «Struct Tree» размещаются
  в dock nodes при первом запуске (default layout).
- Пользователь может перетаскивать и открепивать панели.

### 7.2 Кнопка [?] — Help

Диалог `ImGui::OpenPopup("Help")`:
```
Цветовая легенда карты памяти:
■ Синий    — ManagerHeader (служебные метаданные менеджера)
■ Тёмно-красный — BlockHeader (заголовок занятого блока)
■ Красный  — User Data (данные пользователя, занятый блок)
■ Тёмно-серый   — BlockHeader (заголовок свободного блока)
■ Белый    — User Data (данные свободного блока)
■ Чёрный   — Вне блоков (неиспользованное пространство)

Как пользоваться:
- Нажмите ▶ рядом со сценарием для запуска.
- Кликните на блок в Struct Tree для подсветки на карте.
- Используйте [Settings] для изменения размера PMM.
```

### 7.3 Кнопка [Settings]

Диалог `ImGui::Begin("Settings")`:
```
Начальный размер PMM:
  ○ 1 MB  ● 8 MB  ○ 32 MB  ○ 256 MB
  (радио-кнопки, изменение требует перезапуска сценариев)

FPS-лимит: [60] (SliderInt 10..144)

Тема ImGui:
  ● Dark  ○ Light  ○ Classic
```

Изменение темы: немедленно (через `ImGui::StyleColors*`).
Изменение размера PMM: «Применить» → остановить все сценарии → destroy() → create() → start.

### 7.4 Иконки в заголовке

```
PersistMemoryManager Demo    v0.1    [?]  [⚙ Settings]
```

### 7.5 Проверочные критерии фазы 7

- [x] DockSpace работает: панели можно перетаскивать.
- [x] Help popup отображает правильную легенду.
- [x] Settings меняет тему немедленно.
- [x] Settings «Применить» корректно пересоздаёт PMM.
- [x] FPS-лимит реально работает через sleep или vsync.

---

## Фаза 8: Тесты, CI, документация

### 8.1 Автоматические проверки

Поскольку демо является GUI-приложением, полная автоматизация тестов ограничена.
Тем не менее, создать:

#### 8.1.1 Headless smoke-тест (tests/test_demo_headless.cpp)

```cpp
// Создать DemoApp без окна (только PMM + ScenarioManager).
// Запустить все 7 сценариев на 2 секунды.
// Убедиться: нет segfault, validate() == true, ops_counter > 0.
```

#### 8.1.2 Тест MemMapView::update_snapshot

```cpp
// Создать PMM, выделить несколько блоков.
// Вызвать update_snapshot().
// Проверить: bytes[0..sizeof(ManagerHeader)] == ManagerHeader.
// Проверить: bytes[sizeof(ManagerHeader)..] содержат BlockHeaderUsed.
```

#### 8.1.3 Тест ScenarioManager::stop_all

```cpp
// Запустить 3 сценария.
// Вызвать stop_all() + join_all().
// Убедиться: все потоки завершились за <= 5 секунд.
```

### 8.2 CI (GitHub Actions)

Добавить новый job в `.github/workflows/ci.yml`:

```yaml
build-demo:
  name: Build Demo (${{ matrix.os }})
  strategy:
    matrix:
      os: [ubuntu-latest, windows-latest, macos-latest]
  runs-on: ${{ matrix.os }}
  steps:
    - uses: actions/checkout@v4

    - name: Install OpenGL dev (Linux)
      if: runner.os == 'Linux'
      run: sudo apt-get install -y libgl1-mesa-dev libxrandr-dev libxinerama-dev
                                   libxcursor-dev libxi-dev libxext-dev

    - name: Configure
      run: cmake -B build -DCMAKE_BUILD_TYPE=Release -DPMM_BUILD_DEMO=ON

    - name: Build
      run: cmake --build build --config Release

    - name: Test (headless demo tests)
      run: ctest --test-dir build --build-config Release --output-on-failure -R "test_demo_headless|test_mem_map_view|test_scenario_manager"
```

### 8.3 Обновление документации

#### README.md

Добавить раздел «Визуальное демо»:

```markdown
## Визуальное демо

Демонстрационное приложение визуализирует работу PMM в реальном времени:
- Карта памяти: каждый байт управляемой области отображается цветным пикселем.
- Метрики: used/free, фрагментация, ops/s с историческими графиками.
- Дерево структур: ManagerHeader и все BlockHeader.
- 7 сценариев: Linear Fill, Random Stress, Fragmentation Demo, Large Blocks,
  Tiny Blocks, Mixed Sizes, Persistence Cycle.

### Сборка демо

​```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DPMM_BUILD_DEMO=ON
cmake --build build --target pmm_demo
./build/demo/pmm_demo
​```

Зависимости устанавливаются автоматически через CMake FetchContent:
- Dear ImGui (docking branch)
- GLFW 3.4
```

#### demo/ README или комментарии в коде

- Добавить краткий заголовочный комментарий в каждый файл с описанием модуля.
- Комментарии в `scenarios.cpp` перед каждым сценарием.

### 8.4 Финальный чеклист

- [x] `clang-format` — все новые файлы соответствуют `.clang-format`.
- [x] `cppcheck` — нет предупреждений на новых файлах.
- [x] Размер файлов ≤ 1500 строк (ограничение CI).
- [x] `cmake --build build --target pmm_demo` — успешно на Ubuntu, macOS, Windows.
- [x] Запуск демо под valgrind/AddressSanitizer — нет утечек и гонок данных.
- [x] Документация обновлена в README.md и docs/.

---

## Ключевые технические решения

### Синхронизация: однократный shared_lock за кадр

```cpp
// DemoApp::render() — критически важный паттерн
void DemoApp::render() {
    // Один захват shared_lock за кадр — все снимки берутся атомарно
    {
        std::shared_lock lock(mgr->mutex_); // внутренний mutex PMM
        mem_map_view_->update_snapshot(mgr);
        metrics_view_->update(collect_metrics(mgr), ops_per_sec_);
        struct_tree_view_->update_snapshot(mgr);
    } // lock освобождён
    // Рендеринг работает с локальными копиями — без лока
    mem_map_view_->render();
    metrics_view_->render();
    struct_tree_view_->render(highlighted_block_);
    scenario_manager_->render();
}
```

**Почему**: PMM использует `shared_mutex`, что разрешает параллельное чтение.
Снимки UI-буферов создаются атомарно за один проход, исключая несогласованность данных.

### Остановка сценариев: cooperative cancellation

```cpp
// ScenarioState::stop_flag — std::atomic<bool>
// Сценарий проверяет его в каждом шаге цикла:
while (!stop_flag.load(std::memory_order_relaxed)) {
    do_alloc_or_dealloc();
    rate_limit(); // sleep_until по таймеру на основе частоты
}
```

**Почему**: принудительный kill потока небезопасен (может удерживать мьютекс PMM).

### Rate limiting (ограничение частоты операций)

```cpp
// Для alloc_freq = 1000 ops/s:
auto interval = std::chrono::duration<double>(1.0 / alloc_freq);
auto next = std::chrono::steady_clock::now();
// В цикле сценария:
next += interval;
std::this_thread::sleep_until(next);
```

### Разделение MemMapView на зоны данных по производительности

- При total_size > 1 MB: отображать только первые 512 KB в «детальном» режиме,
  остальное — агрегированная плитка (1 пиксель = N байт).

---

## Структура данных для PMM snapshot (доступ без публичных API)

Для построения карты памяти потребуется прямой доступ к заголовкам блоков.
Два варианта:

**Вариант A (рекомендуется)**: Добавить в `persist_memory_manager.h` итератор блоков:

```cpp
namespace pmm {
// Диапазон для range-for по всем BlockHeader
struct BlockRange { /* ... */ };
BlockRange blocks(PersistMemoryManager* mgr); // returns all blocks
} // namespace pmm
```

**Вариант B**: Использовать `get_info(mgr, ptr)` для каждого известного указателя.
Менее эффективно, только для занятых блоков.

**Рекомендуется Вариант A** — добавить `for_each_block()` метод или итератор в фазе 2.

---

## Временная шкала

```
Неделя 1:
  День 1-2: Фаза 1 (инфраструктура)
  День 3:   Фаза 2 (MemMapView)
  День 4:   Фаза 3 (MetricsView)
  День 5:   Фаза 4 (ScenarioManager + 3 сценария)

Неделя 2:
  День 6:   Фаза 5 (оставшиеся 4 сценария)
  День 7:   Фаза 6 (StructTreeView)
  День 8:   Фаза 7 (полировка, Settings, Dockspace)
  День 9:   Фаза 8 (тесты, CI, документация)
  День 10:  Финальное тестирование и code review
```

---

## Открытые вопросы и риски

| # | Вопрос / Риск | Статус | Решение |
|---|--------------|--------|---------|
| 1 | Доступ к внутренним заголовкам PMM для MemMapView | Открыт | Добавить `for_each_block()` в Фазе 2 |
| 2 | Сборка GLFW на Windows (зависимость от Win32 API) | Известен | FetchContent + find_package(OpenGL) покрывает |
| 3 | macOS: deprecated OpenGL + GLFW совместимость | Известен | imgui_impl_opengl3 поддерживает; добавить -DGL_SILENCE_DEPRECATION |
| 4 | Размер файлов > 1500 строк при разрастании scenarios.cpp | Риск | Разделить по одному файлу на сценарий при необходимости |
| 5 | Сценарий Persistence Cycle: PMM::destroy() вызывает глобальную смену синглтона | Критично | Scenario 7 должен остановить все остальные сценарии перед destroy() |
| 6 | Thread safety: MemMapView читает PMM без API итератора | Риск | Решается Вариантом A из секции «Структура данных» |
| 7 | FPS < 30 при 7 параллельных сценариях + большой PMM | Риск | Оптимизировать рендеринг карты: DrawList batching, пропускать одинаковые пиксели |

---

## Связанные документы

- [demo.md](demo.md) — полное техническое задание на демо-приложение
- [docs/architecture.md](docs/architecture.md) — архитектура PMM
- [docs/api_reference.md](docs/api_reference.md) — справочник API PMM
- [include/persist_memory_manager.h](include/persist_memory_manager.h) — реализация PMM
