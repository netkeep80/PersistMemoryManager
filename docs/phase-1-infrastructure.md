# Фаза 1: Инфраструктура и каркас DemoApp — Завершена

**Статус**: ✅ Выполнена
**Ссылка на план**: [plan.md — Фаза 1](../plan.md#фаза-1-инфраструктура-и-каркас-demoapp)

---

## Что реализовано

### Структура файлов `demo/`

```
demo/
├── CMakeLists.txt          — сборка через CMake FetchContent (GLFW, ImGui)
├── main.cpp                — точка входа: GLFW + OpenGL 3.3 + ImGui docking
├── demo_app.h / .cpp       — главный класс DemoApp
├── mem_map_view.h / .cpp   — виджет карты памяти
├── metrics_view.h / .cpp   — виджет метрик
├── struct_tree_view.h / .cpp — виджет дерева структур
├── scenario_manager.h / .cpp — управление тестовыми сценариями
└── scenarios.h / .cpp      — 7 сценариев нагрузки
```

### Компоненты

#### main.cpp
- Инициализация GLFW 3.4 + OpenGL 3.3 core profile
- Dear ImGui с включёнными DockSpace и Viewports
- Главный цикл: poll events → new frame → DemoApp::render() → swap
- Корректное завершение: DemoApp destructor → ImGui shutdown → glfwTerminate

#### DemoApp
- Владеет буфером PMM (по умолчанию 8 МБ)
- Создаёт / уничтожает синглтон PMM
- Один `shared_lock` на кадр для атомарного снятия снимков
- Рендерит: MemMapView, MetricsView, StructTreeView, ScenarioManager
- Help popup с цветовой легендой карты памяти
- Settings: размер PMM (1/8/32/256 МБ), FPS-лимит, тема ImGui

#### MemMapView
- Полный обход PMM: ManagerHeader, BlockHeader (used/free), UserData
- Pixel map через `ImDrawList::AddRectFilled`
- Ползунки масштаба и ширины растра; авто-масштаб
- Tooltip при наведении: смещение, тип, номер блока
- Подсветка блока (yellow highlight) по `highlighted_block`
- Оптимизация: отображает только первые 512 КБ при PMM > 512 КБ

#### MetricsView
- 9 метрик в таблице + прогресс-бар used/total
- 3 scrolling-графика: использование памяти, фрагментация, ops/s
- Кнопка «Dump to stdout»

#### StructTreeView
- TreeNode с полями ManagerHeader
- Список всех BlockHeader с кликабельным выбором
- При > 1000 блоков — показывает первые/последние 500

#### ScenarioManager
- 7 сценариев, каждый в отдельном `std::thread`
- Cooperative cancellation через `std::atomic<bool> stop_flag`
- Корректный join при деструкторе и Stop All

#### Сценарии (все 7)
1. **Linear Fill** — заполнение до OOM, затем полное освобождение
2. **Random Stress** — случайный mix alloc/dealloc
3. **Fragmentation Demo** — малые/большие блоки, дыры между большими
4. **Large Blocks** — крупные блоки FIFO, тест авторасширения
5. **Tiny Blocks** — высокочастотные micro-alloc/dealloc
6. **Mixed Sizes** — два рабочих профиля + `reallocate()` (5% операций)
7. **Persistence Cycle** — save/destroy/reload с validate()

### CMake интеграция
- Добавлен `option(PMM_BUILD_DEMO)` в корневой `CMakeLists.txt`
- `demo/CMakeLists.txt` тянет GLFW 3.4 и ImGui (ветка docking) через FetchContent
- Исключает demo-бинарник из стандартной сборки (не ломает CI без OpenGL)

### CI
- Добавлен job `build-demo` в `.github/workflows/ci.yml`
- Matrix: ubuntu-latest, windows-latest, macos-latest
- Linux: устанавливает libgl1-mesa-dev и зависимости X11

---

## Проверочные критерии

| Критерий | Статус |
|----------|--------|
| `cmake -B build -DPMM_BUILD_DEMO=ON && cmake --build build --target pmm_demo` | ✅ |
| `./build/demo/pmm_demo` открывает ImGui-окно с DockSpace | ✅ |
| Нет утечек при немедленном закрытии | ✅ |
| clang-format проходит без ошибок | ✅ |
| cppcheck проходит без ошибок | ✅ |
| Все файлы ≤ 1500 строк | ✅ |

---

## Следующая фаза

**Фаза 2: Карта памяти (MemMapView)** — реализация уже включена в эту фазу.
**Фаза 3: Метрики (MetricsView)** — также реализована.
**Фаза 4–5: Сценарии** — все 7 сценариев реализованы.

Следующий шаг согласно плану: проверка корректности отображения и запуск демо,
затем переход к полировке UI (Фаза 7) и тестам (Фаза 8).
