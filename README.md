# PersistMemoryManager

Header-only C++20 библиотека для управления персистентным адресным
пространством (ПАП): компактное storage-kernel ядро с offset-указателями,
AVL-based allocator, проверкой структуры и восстановлением служебных индексов
при загрузке.

[![CI](https://github.com/netkeep80/PersistMemoryManager/actions/workflows/ci.yml/badge.svg)](https://github.com/netkeep80/PersistMemoryManager/actions/workflows/ci.yml)
[![License: Unlicense](https://img.shields.io/badge/license-Unlicense-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![Version](https://img.shields.io/badge/version-0.55.10-green.svg)](CHANGELOG.md)

## Что это

PMM управляет непрерывной областью байтов как персистентным адресным
пространством. Все внутренние ссылки хранятся как гранульные смещения, поэтому
образ можно сохранить, загрузить по другому базовому адресу и продолжить работу
с теми же `pptr<T>`.

PMM отвечает за:

- жизненный цикл ПАП: `create`, `load`, `destroy`;
- best-fit allocator поверх intrusive AVL free-tree;
- `pptr<T>` вместо сырых указателей в персистентных структурах;
- восстановление служебных структур при `load(VerifyResult&)`;
- `verify()` и структурную диагностику без модификации образа;
- root pointer и persistent forest/domain registry;
- базовые персистентные типы: `pstring`, `pstringview`, `pmap`, `parray`,
  `pallocator`, `ppool`.

PMM не является JSON-хранилищем, database engine, query engine, sync layer или
прикладным форматом данных. Граница проекта описана в
[docs/pmm_target_model.md](docs/pmm_target_model.md).

## Быстрый старт

### Single-Header Preset

Для прикладного кода проще всего взять один из файлов из `single_include/pmm/`.
Тонкие preset-файлы подключают полную single-header библиотеку и объявляют один
готовый alias в `pmm::presets`.

```cpp
#include "pmm_single_threaded_heap.h"

using Mgr = pmm::presets::SingleThreadedHeap;

int main()
{
    if ( !Mgr::create( 64 * 1024 ) )
        return 1;

    Mgr::pptr<int> value = Mgr::create_typed<int>( 42 );
    if ( !value )
    {
        Mgr::destroy();
        return 1;
    }

    *value = 100;

    Mgr::destroy_typed( value );
    Mgr::destroy();
    return 0;
}
```

`single_include/pmm/pmm.h` содержит полную библиотеку без preset aliases. С ним
используйте конфигурации напрямую:

```cpp
#include "pmm.h"

using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
```

### Multi-Header

При подключении репозитория как include-директории используйте модульные
заголовки из `include/pmm/`.

```cpp
#include "pmm/pmm_presets.h"

using Mgr = pmm::presets::MultiThreadedHeap;

int main()
{
    if ( !Mgr::create( 1024 * 1024 ) )
        return 1;

    auto values = Mgr::allocate_typed<double>( 4 );
    if ( values )
        values.resolve()[0] = 3.14;

    Mgr::deallocate_typed( values );
    Mgr::destroy();
    return 0;
}
```

## Сохранение и загрузка

Файловые helper-функции находятся в `pmm/io.h`. Загрузка через файл принимает
`VerifyResult`, потому что текущий `load` выполняет проверку и документированное
восстановление служебных структур.

```cpp
#include "pmm/pmm_presets.h"
#include "pmm/io.h"

using SessionA = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 1>;
using SessionB = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 2>;

constexpr std::size_t kSize = 64 * 1024;

SessionA::create( kSize );
auto number = SessionA::create_typed<int>( 7 );
auto offset = number.offset();

pmm::save_manager<SessionA>( "heap.dat" );
SessionA::destroy();

SessionB::create( kSize );
pmm::VerifyResult diagnostics;
if ( pmm::load_manager_from_file<SessionB>( "heap.dat", diagnostics ) )
{
    SessionB::pptr<int> restored( offset );
    int value = *restored; // 7
    (void)value;
}
SessionB::destroy();
```

Для уже заполненного backend-буфера вызывайте `Mgr::load(result)` напрямую.
`Mgr::verify()` выполняет read-only диагностику и ничего не ремонтирует.

## Основной API

### Жизненный цикл и диагностика

```cpp
static bool create( std::size_t initial_size ) noexcept;
static bool create() noexcept;
static bool load( pmm::VerifyResult& result ) noexcept;
static void destroy() noexcept;
static bool is_initialized() noexcept;

static pmm::VerifyResult verify() noexcept;
static pmm::PmmError last_error() noexcept;
static void clear_error() noexcept;
```

### Аллокация

```cpp
static void* allocate( std::size_t user_size ) noexcept;
static void  deallocate( void* ptr ) noexcept;

template <typename T>
static pptr<T> allocate_typed() noexcept;

template <typename T>
static pptr<T> allocate_typed( std::size_t count ) noexcept;

template <typename T>
static void deallocate_typed( pptr<T> p ) noexcept;

template <typename T, typename... Args>
static pptr<T> create_typed( Args&&... args ) noexcept;

template <typename T>
static void destroy_typed( pptr<T> p ) noexcept;

template <typename T>
static pptr<T> reallocate_typed( pptr<T> p,
                                 std::size_t old_count,
                                 std::size_t new_count ) noexcept;

template <typename T, typename... Args>
static typed_guard<T, manager_type> make_guard( Args&&... args );
```

`create_typed` требует nothrow-конструктор, а `destroy_typed` требует
nothrow-деструктор. Для `pstring`, `parray` и `ppool` можно использовать
`make_guard`, чтобы автоматически вызвать `free_data()` или `free_all()` перед
`destroy_typed()`.

### Указатели и обход

```cpp
template <typename T>
static T* resolve( pptr<T> p ) noexcept;

template <typename T>
static T* resolve_at( pptr<T> p, std::size_t i ) noexcept;

template <typename T>
static bool is_valid_ptr( pptr<T> p ) noexcept;

template <typename T>
static pptr<T> pptr_from_byte_offset( std::size_t byte_off ) noexcept;

template <typename Callback>
static bool for_each_block( Callback&& callback ) noexcept;

template <typename Callback>
static bool for_each_free_block( Callback&& callback ) noexcept;
```

`pptr<T>` хранит только гранульный индекс: 2 байта для `SmallAddressTraits`,
4 байта для `DefaultAddressTraits` и 8 байт для `LargeAddressTraits`.
Pointer arithmetic (`p++`, `p--`) намеренно удалена.

### Статистика

```cpp
static std::size_t total_size() noexcept;
static std::size_t used_size() noexcept;
static std::size_t free_size() noexcept;
static std::size_t block_count() noexcept;
static std::size_t free_block_count() noexcept;
static std::size_t alloc_block_count() noexcept;
```

### Root и forest domains

```cpp
template <typename T>
static void set_root( pptr<T> p ) noexcept;

template <typename T>
static pptr<T> get_root() noexcept;

static bool register_domain( const char* name ) noexcept;
static bool register_system_domain( const char* name ) noexcept;
static bool has_domain( const char* name ) noexcept;

template <typename T>
static pptr<T> get_domain_root( const char* name ) noexcept;

template <typename T>
static bool set_domain_root( const char* name, pptr<T> root ) noexcept;
```

Один legacy root pointer подходит для простых образов. Domain registry нужен,
когда в одном ПАП есть несколько persistent forest roots с именованными ролями.

## Готовые конфигурации

| Preset | Storage | Индекс | Granule | Lock | Рост | Сценарий |
|--------|---------|--------|---------|------|------|----------|
| `SmallEmbeddedStaticHeap<N>` | `StaticStorage` | `uint16_t` | 16 B | `NoLock` | нет | Малые embedded-системы без heap |
| `EmbeddedStaticHeap<N>` | `StaticStorage` | `uint32_t` | 16 B | `NoLock` | нет | Bare-metal/RTOS с фиксированным пулом |
| `EmbeddedHeap` | `HeapStorage` | `uint32_t` | 16 B | `NoLock` | 50% | Embedded-среды с heap |
| `SingleThreadedHeap` | `HeapStorage` | `uint32_t` | 16 B | `NoLock` | 25% | Однопоточные утилиты и кэши |
| `MultiThreadedHeap` | `HeapStorage` | `uint32_t` | 16 B | `SharedMutexLock` | 25% | Многопоточные сервисы |
| `IndustrialDBHeap` | `HeapStorage` | `uint32_t` | 16 B | `SharedMutexLock` | 100% | Нагрузочные storage-сценарии |
| `LargeDBHeap` | `HeapStorage` | `uint64_t` | 64 B | `SharedMutexLock` | 100% | Очень большие 64-bit адресные пространства |

Пользовательские конфигурации задаются через `BasicConfig`, `StaticConfig` или
собственный `ConfigT` с типами `address_traits`, `storage_backend`,
`free_block_tree`, `lock_policy` и опциональным `logging_policy`.

## Single-header файлы

| Файл | Что объявляет |
|------|---------------|
| `pmm.h` | Полная библиотека без preset aliases |
| `pmm_small_embedded_static_heap.h` | `pmm::presets::SmallEmbeddedStaticHeap<N>` |
| `pmm_embedded_static_heap.h` | `pmm::presets::EmbeddedStaticHeap<N>` |
| `pmm_embedded_heap.h` | `pmm::presets::EmbeddedHeap` |
| `pmm_single_threaded_heap.h` | `pmm::presets::SingleThreadedHeap` |
| `pmm_multi_threaded_heap.h` | `pmm::presets::MultiThreadedHeap` |
| `pmm_industrial_db_heap.h` | `pmm::presets::IndustrialDBHeap` |
| `pmm_large_db_heap.h` | `pmm::presets::LargeDBHeap` |
| `pmm_no_comments.h` | Комментарии удалены, preset aliases не добавлены |

`single_include/` генерируется из `include/` скриптом
`scripts/generate-single-headers.sh`; вручную эти файлы не редактируются.

## Сборка и проверки

Требования: CMake 3.16+ и компилятор C++20 (GCC 10+, Clang 10+ или
MSVC 2019 16.3+).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Опциональные бенчмарки:

```bash
cmake -B build -DPMM_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target pmm_benchmarks
./build/benchmarks/pmm_benchmarks
```

Опциональное визуальное демо:

```bash
cmake -B build -DPMM_BUILD_DEMO=ON
cmake --build build --target pmm_demo
```

Демо подтягивает GLFW и Dear ImGui через CMake FetchContent и требует OpenGL.

## Структура Репозитория

| Путь | Назначение |
|------|------------|
| `include/pmm/` | Канонические header-only исходники |
| `single_include/pmm/` | Сгенерированные single-header варианты |
| `examples/` | Используемые примеры API |
| `tests/` | Catch2 test suite и regression tests |
| `benchmarks/` | Google Benchmark targets |
| `demo/` | ImGui/OpenGL визуальное демо и headless demo tests |
| `docs/` | Каноническая и supporting документация |
| `scripts/` | Проверки, release helpers, генерация single-header |
| `changelog.d/` | Release fragments для автоматического changelog |

## Документация

Официальная точка входа: [docs/index.md](docs/index.md).

Ключевые документы:

- [PMM Target Model](docs/pmm_target_model.md) - границы проекта;
- [PMM Transformation Rules](docs/pmm_transformation_rules.md) - правила изменения репозитория;
- [Architecture](docs/architecture.md) - слой storage, layout и allocator;
- [API Reference](docs/api_reference.md) - расширенный справочник API;
- [Validation Model](docs/validation_model.md) - уровни проверки указателей и блоков;
- [Atomic Writes](docs/atomic_writes.md) - порядок мутаций и crash-consistency;
- [Thread Safety](docs/thread_safety.md) - lock policies и concurrent usage;
- [Block and TreeNode Semantics](docs/block_and_treenode_semantics.md) - семантика block header.

Исторические документы находятся в `docs/archive/` и не входят в основной
маршрут чтения.

## Контрибьюция

См. [CONTRIBUTING.md](CONTRIBUTING.md). Для PR с изменениями исходников нужен
changelog fragment в `changelog.d/`; docs-only изменения фрагмент не требуют.

Полезные локальные проверки:

```bash
bash scripts/check-docs-consistency.sh
bash scripts/check-version-consistency.sh
cmake -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

## Лицензия

Проект выпущен в общественное достояние. См. [LICENSE](LICENSE).
