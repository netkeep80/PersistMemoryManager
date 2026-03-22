# Фаза 5: Тестирование и качество

Документация по реализации задач Фазы 5 плана развития PersistMemoryManager.

---

## 5.1 Миграция на Catch2 ✅ (#212)

### Проблема

Проект использовал собственные макросы `PMM_TEST(expr)` / `PMM_RUN(name, fn)`, определённые
локально в каждом из 75 тестовых файлов. Это приводило к:
- Дублированию кода макросов (каждый файл содержал ~20 строк определений)
- Отсутствию стандартных средств: `SECTION()` для изоляции, `INFO()` для контекста, `CHECK()` для soft-assertions
- Минимальной диагностике при неудаче (только `FAIL [file:line] expr`)
- Необходимости вручную писать `main()` и `all_passed` логику в каждом файле

### Решение

Миграция всех тестов на Catch2 v3 — зрелый, header-only фреймворк тестирования для C++.

#### Зависимость (CMakeLists.txt)

```cmake
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)
```

#### Схема миграции

Три паттерна тестов были мигрированы:

**Паттерн A** (65 файлов): `PMM_TEST(expr)` + `PMM_RUN(name, fn)` + `static bool test_xxx()`:
```cpp
// Было:
#define PMM_TEST(expr) do { if (!(expr)) { std::cerr << "FAIL..."; return false; } } while(false)
static bool test_basic() { PMM_TEST(x == 42); return true; }
int main() { bool all_passed = true; PMM_RUN("basic", test_basic); return all_passed ? 0 : 1; }

// Стало:
#include <catch2/catch_test_macros.hpp>
TEST_CASE("basic", "[test_suite]") { REQUIRE(x == 42); }
```

**Паттерн B** (2 файла): `PMM_TEST(cond, msg)` + `static void test_xxx()`:
```cpp
// Было:
#define PMM_TEST(cond, msg) do { if (!(cond)) { std::cerr << msg; std::exit(1); } } while(false)

// Стало:
INFO(msg); REQUIRE(cond);
```

**Паттерн C** (8 файлов): `assert(expr)` в single-header тестах:
```cpp
// Было:
int main() { assert(created); assert(STH::is_initialized()); return 0; }

// Стало:
TEST_CASE("test_name", "[test_name]") { REQUIRE( created ); REQUIRE( STH::is_initialized() ); }
```

#### Обновление CMake

```cmake
# tests/CMakeLists.txt — все тесты линкуются с Catch2WithMain:
function(pmm_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE pmm Catch2::Catch2WithMain)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
```

`Catch2::Catch2WithMain` предоставляет `main()` автоматически — не нужно писать вручную.

### Тесты

67 тестовых исполняемых файлов, все проходят (100% pass rate).

Миграция включает:
- 73 файла мигрированы (65 паттерн A + 2 паттерн B + 8 паттерн C)
- 2 файла пропущены (demo-only: `test_demo_headless.cpp`, `test_background_validator.cpp`)
- Удалено ~4200 строк дублирующегося кода макросов и ручной `main()` логики
- Скрипт миграции сохранён как `scripts/migrate_to_catch2.py` для справки

### Преимущества

- **Стандартизация:** Catch2 — де-факто стандарт тестирования в C++ проектах
- **Диагностика:** При неудаче Catch2 показывает значения обеих сторон выражения
- **Изоляция:** `SECTION()` позволяет группировать связанные проверки
- **Масштабируемость:** Новые тесты пишутся без boilerplate (нет `main()`, нет макросов)
- **CI-интеграция:** Catch2 поддерживает JUnit XML, TAP и другие форматы отчётов

---

## 5.2 Расширение покрытия тестами ✅ (#213)

### Проблема

Существующие тесты покрывали функциональные сценарии, но не проверяли:
- Граничные случаи арифметического переполнения при вычислении размеров
- Конкурентные операции аллокации/деаллокации при высокой нагрузке
- Случайные последовательности операций (fuzz-тестирование)

### Решение

Добавлены три новых тестовых файла с 31 тест-кейсом, а также libFuzzer-совместимый harness.

#### 5.2.1 Тесты переполнения (`test_issue213_overflow.cpp`)

19 тест-кейсов, проверяющих:
- `bytes_to_granules_t` возвращает 0 при переполнении `size_t`
- `bytes_to_granules_t` возвращает 0 при превышении `index_type::max`
- `bytes_to_idx_t` возвращает `no_block` при переполнении
- `allocate()` / `allocate_typed()` / `reallocate_typed()` для `size_t::max`
- `allocate(0)` возвращает `nullptr` с `PmmError::InvalidSize`
- `allocate_typed(0)` возвращает нулевой `pptr`
- Статическое хранилище: отказ при выходе за пределы буфера
- 16-bit индекс (`SmallAddressTraits`): переполнение при малом адресном пространстве
- 64-bit индекс (`LargeAddressTraits`): базовая аллокация и устойчивость к переполнению
- Исчерпание памяти и восстановление (exhaust → free → re-allocate)
- Циклы аллокации/деаллокации с проверкой целостности
- Граничные размеры: ровно 1 гранула, на границе гранулы, между гранулами

Все варианты `AddressTraits` протестированы: `SmallAddressTraits` (16-bit),
`DefaultAddressTraits` (32-bit), `LargeAddressTraits` (64-bit).

#### 5.2.2 Конкурентные тесты (`test_issue213_concurrent.cpp`)

6 тест-кейсов, проверяющих:
- Конкурентная аллокация с варьирующимися размерами (8 потоков, 200 операций каждый)
- LIFO-деаллокация (4 потока × 100 блоков, освобождение в обратном порядке)
- Случайный порядок деаллокации (Fisher-Yates shuffle в каждом потоке)
- Высокая конкуренция (16 потоков, малый heap 4 МБ)
- Проверка целостности данных под конкуренцией (8 потоков, write/read/verify)
- Конкурентный `reallocate_typed` (4 потока, рост массивов с проверкой sentinel-значений)

Все тесты используют `MultiThreadedHeap` с `SharedMutexLock`.

#### 5.2.3 Fuzz-тестирование (`test_issue213_fuzz.cpp` + `fuzz_allocator.cpp`)

**Детерминистические fuzz-тесты** (6 тест-кейсов через Catch2):
- Случайные alloc/dealloc на статическом хранилище (5000 итераций, проверка паттернов)
- Случайные alloc/dealloc на heap-хранилище (5000 итераций, три класса размеров)
- Смешанные alloc/dealloc/reallocate (3000 итераций, проверка сохранности данных)
- 16-bit индекс стресс (2000 итераций на 8 КБ буфере)
- Фрагментационный стресс (заполнение → освобождение через один → средние блоки)
- Sweep по 6 различным seed-значениям (1000 итераций каждый)

Все fuzz-тесты используют LCG PRNG для детерминистической воспроизводимости.
Каждый тест записывает уникальный паттерн в каждый блок и проверяет его перед деаллокацией.

**libFuzzer harness** (`tests/fuzz_allocator.cpp`):
- Покрытие-управляемый фаззинг для Clang с `-fsanitize=fuzzer,address`
- Интерпретирует входные байты как последовательность команд (alloc/dealloc/reallocate)
- Использует статическое хранилище 64 КБ для ограниченного состояния
- Инструкции по сборке и запуску в заголовке файла

### Тесты

70 тестовых исполняемых файлов (67 + 3 новых), все проходят (100% pass rate).
31 новый тест-кейс суммарно в трёх файлах.

---

## 5.3 Бенчмарки производительности ✅ (#214)

### Проблема

Проект не имел формальных бенчмарков производительности. Существовал только
`examples/benchmark.cpp` с ручным замером времени и `test_performance.cpp` с
проверкой «≤ 100 мс». Не было инструмента для систематического измерения
производительности различных операций и сравнения с базовым аллокатором (malloc).

### Решение

Интеграция Google Benchmark v1.9.1 через CMake FetchContent с опциональной сборкой
(`PMM_BUILD_BENCHMARKS=ON`). Один бенчмарк-файл `benchmarks/bench_allocator.cpp`
покрывает все ключевые операции библиотеки.

#### Зависимость (CMakeLists.txt)

```cmake
option(PMM_BUILD_BENCHMARKS "Build Google Benchmark performance benchmarks" OFF)
if(PMM_BUILD_BENCHMARKS)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(benchmarks)
endif()
```

#### Бенчмарки

**Аллокатор** (5 бенчмарков):
- `BM_Allocate` — аллокация одного блока (16, 64, 256, 1024, 4096 байт)
- `BM_Deallocate` — деаллокация одного блока (64, 256, 1024 байт)
- `BM_AllocDeallocMixed` — цикл alloc/dealloc со смешанными размерами (32–256 байт)
- `BM_ReallocateTyped` — нативное перераспределение (64↔128 байт)
- `BM_AllocateBatch` — пакетная аллокация (1K, 10K, 100K блоков по 64 байт)

**Сравнение с malloc** (2 бенчмарка):
- `BM_MallocFree` — malloc/free одного блока (16–4096 байт)
- `BM_MallocBatch` — пакетная аллокация через malloc (1K, 10K, 100K блоков)

**pmap** (3 бенчмарка):
- `BM_PmapInsert` — вставка N элементов (100, 1K, 10K)
- `BM_PmapFind` — поиск по ключу в дереве из N элементов
- `BM_PmapErase` — удаление N элементов (100, 1K)


**parray** (2 бенчмарка):
- `BM_ParrayPushBack` — добавление N элементов (100, 1K, 10K)
- `BM_ParrayRandomAccess` — O(1) произвольный доступ

**ppool** (2 бенчмарка):
- `BM_PpoolAllocate` — O(1) аллокация/деаллокация одного объекта
- `BM_PpoolBatch` — пакетная аллокация из пула (100, 1K, 10K объектов)

**pstring** (2 бенчмарка):
- `BM_PstringAssign` — присвоение строки (16, 256, 4096 символов)
- `BM_PstringAppend` — amortized O(1) append

**pstringview** (1 бенчмарк):
- `BM_PstringviewInternExisting` — поиск уже интернированной строки (AVL lookup)

**Многопоточность** (1 бенчмарк):
- `BM_AllocateMT` — alloc/dealloc с SharedMutexLock (1, 2, 4, 8 потоков)

#### Сборка и запуск

```bash
cmake -B build -DPMM_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target pmm_benchmarks
./build/benchmarks/pmm_benchmarks
```

#### Результаты (GCC 13, Linux x86-64, Release)

| Операция | Время (нс) | Примечание |
|----------|-----------|------------|
| `allocate(64)` | ~20 | Одиночная аллокация |
| `deallocate(64)` | ~680 | С PauseTiming на подготовку |
| `alloc+dealloc mixed` | ~22 | Смешанные размеры 32–256 |
| `reallocate_typed` | ~12 | Чередование 64↔128 |
| `malloc+free(64)` | ~7 | Базовый аллокатор |
| `pmap find(10K)` | ~49 | O(log n) AVL-поиск |
| `pmap insert(10K)` | ~102 нс/эл | С аллокацией узлов |
| `parray [](10K)` | ~3 | O(1) произвольный доступ |
| `ppool allocate` | ~1.4 | O(1) из free-list |
| `pstring append` | ~1.1 | Amortized O(1) |
| `pstringview intern(10K)` | ~107 | AVL lookup существующей строки |

### Преимущества

- **Воспроизводимость:** Google Benchmark обеспечивает статистическую надёжность
- **Сравнимость:** Прямое сравнение pmm с malloc для оценки накладных расходов
- **Полнота:** Покрыты все ключевые типы и операции библиотеки
- **Опциональность:** Сборка бенчмарков по флагу `PMM_BUILD_BENCHMARKS=ON`
