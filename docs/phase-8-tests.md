# Фаза 8: Тесты, CI, документация — Завершена

**Статус**: ✅ Выполнена
**Ссылка на план**: [plan.md — Фаза 8](../plan.md#фаза-8-тесты-ci-документация)

---

## Что реализовано

### Тестовые файлы (Phase 8.1)

Три новых headless-теста для демо-приложения. Тесты собираются автоматически
вместе с демо при включённой опции `PMM_BUILD_DEMO=ON`. Они не требуют
графического окна — только PMM, ScenarioManager и MemMapView.

#### tests/test_demo_headless.cpp

Smoke-тест всего демо без окна:

| Тест | Описание |
|------|----------|
| `all_scenarios_run` | Запускает все 7 сценариев на 2 секунды, проверяет `validate() == true` и отсутствие сбоев |
| `ops_counter_increments` | Проверяет, что счётчики операций растут при работе сценариев |
| `stop_all_fast` | Проверяет, что `stop_all()` + `join_all()` завершается за ≤ 5 секунд |

#### tests/test_mem_map_view.cpp

Юнит-тесты `MemMapView::update_snapshot()`:

| Тест | Описание |
|------|----------|
| `manager_header_region` | Вызов `update_snapshot()` не падает на корректном PMM |
| `snapshot_after_alloc` | Снимок перестраивается без ошибок после выделения и освобождения блоков |
| `snapshot_null_mgr` | `update_snapshot(nullptr)` не приводит к сбою (graceful no-op) |
| `highlighted_block_preserved` | `highlighted_block` не сбрасывается при обновлении снимка |

#### tests/test_scenario_manager.cpp

Юнит-тесты жизненного цикла `ScenarioManager`:

| Тест | Описание |
|------|----------|
| `scenario_count` | `ScenarioManager::count()` возвращает 7 |
| `stop_all_within_deadline` | Запуск 3 сценариев → `stop_all()` + `join_all()` за ≤ 5 секунд |
| `start_stop_single` | Повторный запуск и остановка одного сценария (идемпотентность) |
| `destructor_cleans_up` | Деструктор корректно завершает запущенные потоки |

### Изменения в системе сборки

#### demo/CMakeLists.txt

- Введена статическая библиотека **`pmm_demo_lib`**, объединяющая все демо-источники
  (кроме `main.cpp`) и зависимости ImGui.
- `pmm_demo` линкуется против `pmm_demo_lib`.
- Три тестовых исполняемых файла (`test_demo_headless`, `test_mem_map_view`,
  `test_scenario_manager`) также линкуются против `pmm_demo_lib`.
- Все тесты зарегистрированы через `add_test()` — запускаются командой `ctest`.

### CI (GitHub Actions)

Job `build-demo` в `.github/workflows/ci.yml` уже присутствовал с Фазы 1.
В рамках Фазы 8 подтверждено и дополнено:

- Сборка проходит на ubuntu-latest, windows-latest, macos-latest.
- Linux: установка `libgl1-mesa-dev` и зависимостей X11/Wayland в CI.
- Шаг Build теперь собирает все цели (`cmake --build build --config Release`),
  включая headless-тесты.
- Добавлен шаг **Test** — запускает все три headless-теста через `ctest`:
  ```
  ctest --test-dir build --build-config Release --output-on-failure \
    -R "test_demo_headless|test_mem_map_view|test_scenario_manager"
  ```

---

## Запуск тестов

```bash
# Сборка с демо и тестами
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DPMM_BUILD_DEMO=ON
cmake --build build

# Запуск всех тестов (включая headless-тесты демо)
ctest --test-dir build --output-on-failure

# Запуск только демо-тестов
ctest --test-dir build -R "test_demo_headless|test_mem_map_view|test_scenario_manager" --output-on-failure
```

---

## Проверочные критерии

| Критерий | Статус |
|----------|--------|
| `tests/test_demo_headless.cpp` создан | ✅ |
| `tests/test_mem_map_view.cpp` создан | ✅ |
| `tests/test_scenario_manager.cpp` создан | ✅ |
| `demo/CMakeLists.txt` разделён на `pmm_demo_lib` + тесты | ✅ |
| `clang-format` проходит без ошибок | ✅ |
| `cppcheck` проходит без ошибок | ✅ |
| Все файлы ≤ 1500 строк | ✅ |
| `cmake --build build --target pmm_demo` — успешно | ✅ |
| CI job `build-demo` запускает headless-тесты через `ctest` | ✅ |
| Документация обновлена в `README.md` и `docs/` | ✅ |

---

## Следующие шаги

Все фазы плана выполнены. Демо-приложение готово к финальному code review.
