# Target repository shape

## Purpose

Этот документ фиксирует **целевую форму** репозитория `PersistMemoryManager`.
Любое добавление нового top-level элемента или нового каталога должно соответствовать
описанным здесь правилам, иначе оно считается нарушением target shape.

## Scope

Описывается только структура репозитория: каталоги, их роль, допустимое содержимое.
Стиль кода, комментариев и документации описан в `tasks/00_REPOSITORY_STYLE.md`.
Правила удаления и архивации описаны в `docs/deletion_policy.md`.

## Core model / invariants

### Target top-level surface

Целевая поверхность корня репозитория ограничивается следующими категориями:

| Категория | Элементы | Роль |
|-----------|----------|------|
| Build / release | `CMakeLists.txt`, `.clang-format`, `.pre-commit-config.yaml`, `.gitignore` | Конфигурация сборки, форматирования, CI |
| CI / CD | `.github/` | GitHub Actions workflows and repository templates |
| Changelog | `changelog.d/`, `CHANGELOG.md` | Фрагменты и собранная история релизов |
| Library source | `include/` | Публичные заголовки библиотеки (canonical source) |
| Amalgamated headers | `single_include/` | Генерируемые single-header варианты |
| Tests | `tests/` | Автоматические тесты |
| Benchmarks | `benchmarks/` | Бенчмарки производительности |
| Documentation | `docs/` | Каноническая документация |
| Demo | `demo/` | Визуальное демо-приложение (ImGui) |
| Examples | `examples/` | Примеры использования библиотеки |
| Scripts | `scripts/` | Вспомогательные скрипты (сборка, релиз, генерация) |
| Root docs | `README.md`, `CONTRIBUTING.md`, `LICENSE` | Главные точки входа и правовая информация |
| Operational placeholders | `.gitkeep` | Пустой root-marker для рабочих процессов роя AI-агентов |

Ничего другого в корне репозитория быть не должно.

> **Примечание:** `tasks/` — временный planning-layer для backlog компактификации.
> Он не является first-class частью target shape (см. `tasks/00_REPOSITORY_STYLE.md` § 2.1)
> и будет удалён после завершения всех задач компактификации.

### Directory contracts

#### `include/pmm/`

- Содержит все публичные заголовки библиотеки.
- `.h` файлы и `.inc` mixin-файлы.
- Не содержит реализаций за пределами шаблонного / inline-кода.
- Является canonical source; `single_include/` генерируется из `include/`.

#### `single_include/pmm/`

- Генерируется скриптом `scripts/generate-single-headers.sh`.
- Не редактируется вручную.
- Содержит preset-варианты single-header для различных конфигураций.

#### `tests/`

- Один тест — один файл.
- Имя файла: `test_<topic>.cpp` или `test_issue<N>_<topic>.cpp`.
- Собственный `CMakeLists.txt`.

#### `benchmarks/`

- Бенчмарки производительности.
- Собственный `CMakeLists.txt`.

#### `docs/`

- Каноническая документация по текущему и целевому состоянию.
- Исторические документы не участвуют в основной навигации (см. `deletion_policy.md`).
- Каждый canonical doc следует структуре из `tasks/00_REPOSITORY_STYLE.md` § 3.2.

#### `demo/`

- Исходники визуального демо-приложения (ImGui + OpenGL).
- Собственный `CMakeLists.txt`.

#### `examples/`

- Примеры использования библиотеки.
- Собственный `CMakeLists.txt`.

#### `scripts/`

- Вспомогательные скрипты: shell, Python.
- Используются CI и разработчиками локально.
- Включает: генерацию single-header, проверку changelog-фрагментов, проверку размера файлов, сбор changelog, миграцию тестов, очистку комментариев.

#### `changelog.d/`

- Фрагменты changelog для текущего цикла.
- Собираются при релизе скриптом `scripts/collect-changelog.sh`.
- Формат описан в `changelog.d/README.md`.

#### `tasks/` (временный planning-layer)

- Задачи компактификации.
- Индекс: `00_INDEX.md`.
- Стиль: `00_REPOSITORY_STYLE.md`.
- Номерные задачи: `01_*.md` – `10_*.md`.
- **Не является first-class частью target shape** (см. § 2.1 style guide).
- Будет удалён после завершения всех задач компактификации.

#### `.github/`

- Pull request template: `PULL_REQUEST_TEMPLATE.md` — repo-guard YAML change contract template.
- Issue template: `ISSUE_TEMPLATE/change-contract.yml` — issue form for changes with repo-guard contracts.

#### `.github/workflows/`

- CI: `ci.yml` — сборка, тесты, проверки.
- Docs-consistency: `docs-consistency.yml` — проверка консистентности документации и repo-guard rollout wiring.
- Repo-guard: `repo-guard.yml` — advisory policy check through the reusable repo-guard Action.
- Release: `release.yml` — автоматический релиз.

## Rules / contracts

### Root-level inventory and decisions

Для каждого существующего top-level элемента принято явное решение:

#### keep — активная часть репозитория

| Элемент | Обоснование |
|---------|-------------|
| `.clang-format` | Конфигурация стиля кода |
| `.github/` | CI/CD workflows and repository templates |
| `.gitignore` | Конфигурация Git |
| `.pre-commit-config.yaml` | Pre-commit hooks |
| `CHANGELOG.md` | История релизов |
| `CMakeLists.txt` | Корневой build-файл |
| `CONTRIBUTING.md` | Руководство для контрибьюторов |
| `LICENSE` | Лицензия |
| `README.md` | Главная точка входа |
| `benchmarks/` | Бенчмарки производительности |
| `changelog.d/` | Фрагменты changelog |
| `demo/` | Визуальное демо-приложение |
| `docs/` | Каноническая документация |
| `examples/` | Примеры использования |
| `include/` | Исходники библиотеки |
| `scripts/` | Вспомогательные скрипты |
| `single_include/` | Генерируемые single-header |
| `tests/` | Автоматические тесты |
| `.gitkeep` | Пустой root-level marker нужен для рабочих процессов роя AI-агентов; содержимое запрещено |

#### move — нужен, но лежит не там

| Элемент | Куда | Обоснование |
|---------|------|-------------|
| `demo.bat` | `scripts/demo.bat` | Windows-скрипт сборки демо — должен быть в `scripts/` |
| `test.bat` | `scripts/test.bat` | Windows-скрипт запуска тестов — должен быть в `scripts/` |
| `demo.md` | `docs/demo.md` | Техническое задание на демо — документация, не корень |

#### delete — не должен оставаться в репозитории

| Элемент | Обоснование |
|---------|-------------|
| `imgui.ini` | Файл состояния ImGui GUI layout, генерируется автоматически, не должен быть в VCS |

### Navigation policy

Официальный маршрут чтения:

1. `README.md` — обзор, быстрый старт, ссылки на документацию.
2. `docs/index.md` — перечень канонических, supporting и архивных документов.
3. Канонические docs перечислены только в `docs/index.md` и
   `repo-policy.json` `paths.canonical_docs`; эти списки должны совпадать.

Исторические и архивные документы не входят в маршрут и не должны выглядеть
как обязательная часть чтения.

### docs/ internal inventory

#### Canonical

Canonical docs are listed in `docs/index.md` and enforced by
`repo-policy.json` `paths.canonical_docs`. This document must not duplicate the
registry.

#### Supporting

- `repository_shape.md` — целевая структура репозитория
- `deletion_policy.md` — правила удаления и архивации
- `comment_policy.md` — дисциплина текстовой поверхности
- `index.md` — единая точка входа в документацию

#### Archive (`docs/archive/`)

- `PMM_AVL_Forest_Concept.md` — пересекается с `pmm_avl_forest.md`
- `avl_forest_analysis_ru.md` — исторический анализ
- `demo.md` — ТЗ на визуальное демо
- `phase1_safety.md` — фазовый документ
- `phase2_persistence.md` — фазовый документ
- `phase3_types.md` — фазовый документ
- `phase4_api.md` — фазовый документ
- `phase5_testing.md` — фазовый документ
- `phase6_documentation.md` — фазовый документ
- `phase7_4_encryption_compression.md` — фазовый документ
- `plan.md` — исторический план
- `plan4BinDiffSynchronizer.md` — план внешнего проекта

## Out of scope

- Содержимое файлов (код, тексты документов).
- API design.
- Тестовая матрица.
- Storage seams.

## Related docs

- `docs/deletion_policy.md` — правила удаления и архивации
- `tasks/00_REPOSITORY_STYLE.md` — стиль репозитория
- `tasks/00_INDEX.md` — индекс задач компактификации
