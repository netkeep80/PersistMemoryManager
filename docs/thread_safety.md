# Потокобезопасность PersistMemoryManager

Документация по модели потокобезопасности библиотеки PersistMemoryManager.

---

## Модель потокобезопасности

PersistMemoryManager реализует стратегию «readers–writer lock» через шаблонную политику
блокировок (`lock_policy`). Политика выбирается на этапе компиляции и определяет,
защищены ли операции мьютексом или нет.

### Политики блокировок

Определены в `include/pmm/config.h`:

| Политика | Тип мьютекса | Назначение |
|----------|-------------|------------|
| `config::SharedMutexLock` | `std::shared_mutex` | Многопоточный доступ (readers–writer) |
| `config::NoLock` | no-op (пустые методы) | Однопоточный режим, нулевые накладные расходы |

#### `SharedMutexLock`

```cpp
struct SharedMutexLock
{
    using mutex_type       = std::shared_mutex;
    using shared_lock_type = std::shared_lock<std::shared_mutex>;
    using unique_lock_type = std::unique_lock<std::shared_mutex>;
};
```

- **shared_lock** — разрешает конкурентное чтение (несколько потоков одновременно)
- **unique_lock** — эксклюзивный доступ для записи (блокирует все остальные потоки)

#### `NoLock`

```cpp
struct NoLock
{
    struct mutex_type   { void lock() {} void unlock() {} /* ... */ };
    struct shared_lock_type { explicit shared_lock_type(mutex_type&) {} };
    struct unique_lock_type { explicit unique_lock_type(mutex_type&) {} };
};
```

Все операции блокировки — пустые. Компилятор полностью оптимизирует их.
Подходит только для однопоточного кода.

### Выбор политики

Политика задаётся через шаблонный параметр `BasicConfig`:

```cpp
// Однопоточный менеджер (по умолчанию)
using MyMgr = PersistMemoryManager<CacheManagerConfig>;        // NoLock

// Многопоточный менеджер
using MyMgr = PersistMemoryManager<PersistentDataConfig>;      // SharedMutexLock

// Явная настройка через BasicConfig
using MyConfig = BasicConfig<DefaultAddressTraits, config::SharedMutexLock>;
using MyMgr = PersistMemoryManager<MyConfig>;
```

Готовые конфигурации в `include/pmm/manager_configs.h`:

| Конфигурация | Политика | Типичный сценарий |
|-------------|----------|-------------------|
| `CacheManagerConfig` | `NoLock` | Временный кеш, однопоточный код |
| `EmbeddedManagerConfig` | `NoLock` | Встраиваемые системы |
| `PersistentDataConfig` | `SharedMutexLock` | Долговременное хранение, файловые менеджеры |
| `IndustrialDBConfig` | `SharedMutexLock` | Промышленные базы данных |
| `LargeDBConfig` | `SharedMutexLock` | Крупные хранилища (64-bit индекс) |

Пресеты в `pmm::presets`:

| Пресет | Конфигурация |
|--------|-------------|
| `presets::MultiThreadedHeap` | `PersistentDataConfig` (SharedMutexLock) |
| `presets::SingleThreadedHeap` | `CacheManagerConfig` (NoLock) |

---

## Контракты операций

### Операции с эксклюзивной блокировкой (unique_lock)

Эти операции изменяют внутреннее состояние менеджера и требуют эксклюзивного доступа.
При использовании `SharedMutexLock` они блокируют все остальные потоки.

| Метод | Описание |
|-------|----------|
| `create(size)` | Инициализация менеджера с заданным размером |
| `create()` | Инициализация поверх готового бэкенда |
| `load()` | Загрузка существующего состояния |
| `destroy()` | Сброс состояния менеджера |
| `allocate(size)` | Выделение блока памяти |
| `deallocate(ptr)` | Освобождение блока |
| `allocate_typed<T>()` | Типизированное выделение (вызывает `allocate`) |
| `deallocate_typed<T>(pptr)` | Типизированное освобождение (вызывает `deallocate`) |
| `reallocate_typed<T>(pptr, old, new)` | Перераспределение (вызывает `allocate`/`deallocate`) |
| `create_typed<T>(args...)` | Создание объекта (вызывает `allocate`) |
| `destroy_typed<T>(pptr)` | Уничтожение объекта (вызывает `deallocate`) |
| `lock_block_permanent(ptr)` | Блокировка блока навечно |
| `set_root<T>(pptr)` | Установка корневого объекта |

### Операции с разделяемой блокировкой (shared_lock)

Эти операции выполняют только чтение и могут работать конкурентно с другими читателями.

| Метод | Описание |
|-------|----------|
| `get_root<T>()` | Получение корневого объекта |
| `is_permanently_locked(ptr)` | Проверка блокировки блока |
| `total_size()` | Общий размер памяти |
| `used_size()` | Использованная память |
| `free_size()` | Свободная память |
| `block_count()` | Общее количество блоков |
| `free_block_count()` | Количество свободных блоков |
| `alloc_block_count()` | Количество выделенных блоков |
| `for_each_block(callback)` | Итерация по всем блокам |
| `for_each_free_block(callback)` | Итерация по свободным блокам |

### Операции без блокировки

Следующие методы **не захватывают мьютекс**:

| Метод | Механизм безопасности | Примечание |
|-------|----------------------|------------|
| `is_initialized()` | `std::atomic<bool>` с `memory_order_acquire` | Lock-free fast path |
| `resolve(pptr)` | Проверка `_initialized` (atomic) | См. раздел «resolve()» |
| `resolve_at(pptr, i)` | Вызывает `resolve()` | Наследует контракт |
| `is_valid_ptr(pptr)` | Проверка `_initialized` (atomic) | Только валидация границ |
| `pptr_from_byte_offset<T>(off)` | Чистое вычисление | Не обращается к памяти менеджера |

---

## resolve() и pptr<T>

### Контракт resolve()

`resolve(pptr<T>)` — ключевой метод для разыменования персистентных указателей.
Он преобразует гранульный индекс в сырой указатель `T*`:

```cpp
template <typename T>
static T* resolve(pptr<T> p) noexcept
{
    if (p.is_null() || !_initialized)
        return nullptr;
    std::uint8_t* base = _backend.base_ptr();
    std::size_t byte_off = static_cast<std::size_t>(p.offset()) * address_traits::granule_size;
    assert(byte_off + sizeof(T) <= _backend.total_size());
    return reinterpret_cast<T*>(base + byte_off);
}
```

**Критически важно:** `resolve()` **не захватывает мьютекс**. Это осознанное решение —
resolve() вызывается на горячем пути (часто и повсеместно), и блокировка здесь
создала бы неприемлемые накладные расходы.

### Гарантии resolve()

1. **Атомарная проверка инициализации** — `_initialized` является `std::atomic<bool>`,
   загрузка с `memory_order_acquire` гарантирует видимость данных, записанных до вызова
   `create()`/`load()`.

2. **Стабильность базового адреса** — после инициализации `_backend.base_ptr()` не
   изменяется при аллокациях. Расширение хранилища (`expand()`) происходит внутри
   `allocate()` под `unique_lock`, и базовый адрес остаётся стабильным для
   `HeapStorage` (realloc может переместить буфер, но это происходит внутри
   защищённой секции).

3. **Возвращённый указатель валиден** — пока менеджер инициализирован и `pptr` указывает
   на выделенный блок, указатель `T*` валиден.

### Ограничения resolve()

- **Данные не защищены** — resolve() возвращает сырой `T*`. Защита данных по этому
  указателю — ответственность пользователя.
- **Не вызывайте resolve() одновременно с destroy()** — если один поток вызывает
  `destroy()`, другие потоки не должны в это время вызывать `resolve()`.
- **Время жизни указателя** — указатель `T*` валиден до следующего `destroy()`.
  `deallocate()` освобождает блок, но память остаётся адресуемой (хотя данные
  могут быть перезаписаны последующими аллокациями).

---

## Атомарный флаг _initialized

```cpp
static inline std::atomic<bool> _initialized{false};
```

Флаг `_initialized` реализует паттерн lock-free fast path:

1. **Запись** — `_initialized = true` выполняется внутри `create()`/`load()` под
   `unique_lock`. Гарантирует, что все записи в внутренние структуры (заголовок,
   дерево свободных блоков) видны другим потокам.

2. **Чтение (is_initialized)** — загрузка с `memory_order_acquire` без мьютекса:
   ```cpp
   static bool is_initialized() noexcept {
       return _initialized.load(std::memory_order_acquire);
   }
   ```

3. **Fast path в статистических методах** — каждый метод статистики (total_size, used_size
   и т.д.) сначала проверяет `_initialized` без мьютекса, и только затем захватывает
   `shared_lock`:
   ```cpp
   static std::size_t total_size() noexcept {
       if (!_initialized.load(std::memory_order_acquire))
           return 0;  // Fast path — без блокировки
       typename thread_policy::shared_lock_type lock(_mutex);
       return _initialized.load(std::memory_order_relaxed) ? _backend.total_size() : 0;
   }
   ```
   Повторная проверка под мьютексом (`memory_order_relaxed`) необходима для защиты от
   гонки между fast path и `destroy()`.

---

## Правила конкурентного доступа

### Безопасные паттерны (корректное использование)

**Паттерн 1: Конкурентная аллокация/деаллокация**

```cpp
using Mgr = pmm::presets::MultiThreadedHeap;
Mgr::create(4 * 1024 * 1024);

// Потоки могут безопасно вызывать allocate/deallocate конкурентно
std::thread t1([]{
    void* p = Mgr::allocate(256);
    // ... использование p ...
    Mgr::deallocate(p);
});
std::thread t2([]{
    void* p = Mgr::allocate(512);
    // ... использование p ...
    Mgr::deallocate(p);
});
t1.join(); t2.join();
Mgr::destroy();
```

**Паттерн 2: Конкурентное чтение данных**

```cpp
using Mgr = pmm::presets::MultiThreadedHeap;
Mgr::create(1024 * 1024);

auto ptr = Mgr::allocate_typed<int>();
int* raw = ptr.resolve();
*raw = 42;

// Несколько потоков могут безопасно ЧИТАТЬ одни и те же данные
std::thread reader1([&]{ assert(*ptr.resolve() == 42); });
std::thread reader2([&]{ assert(*ptr.resolve() == 42); });
reader1.join(); reader2.join();

Mgr::destroy();
```

**Паттерн 3: Чтение статистики во время аллокаций**

```cpp
// Читатели статистики работают конкурентно с аллокаторами
std::thread writer([]{
    for (int i = 0; i < 1000; ++i) {
        void* p = Mgr::allocate(64);
        Mgr::deallocate(p);
    }
});
std::thread reader([]{
    for (int i = 0; i < 1000; ++i) {
        std::size_t used = Mgr::used_size();   // shared_lock
        std::size_t free = Mgr::free_size();   // shared_lock
        // Значения консистентны в рамках каждого вызова,
        // но могут меняться между вызовами.
    }
});
writer.join(); reader.join();
```

### Опасные паттерны (некорректное использование)

**Антипаттерн 1: Конкурентная запись в одну ячейку**

```cpp
auto ptr = Mgr::allocate_typed<int>();
int* raw = ptr.resolve();

// ОШИБКА: data race — два потока пишут по одному адресу без синхронизации
std::thread t1([raw]{ *raw = 1; });
std::thread t2([raw]{ *raw = 2; });
// Неопределённое поведение!
```

**Решение:** используйте `std::atomic`, `std::mutex` или иную синхронизацию
для защиты пользовательских данных.

**Антипаттерн 2: destroy() во время работы других потоков**

```cpp
// ОШИБКА: destroy() во время конкурентных операций
std::thread t1([]{ void* p = Mgr::allocate(256); });
std::thread t2([]{ Mgr::destroy(); });  // Гонка!
```

**Решение:** вызывайте `destroy()` только когда все остальные потоки завершили
работу с менеджером.

**Антипаттерн 3: Вызов allocate/deallocate из callback for_each_block**

```cpp
// ОШИБКА: for_each_block держит shared_lock, allocate требует unique_lock → дедлок
Mgr::for_each_block([](const pmm::BlockView& view) {
    Mgr::allocate(64);  // Дедлок!
});
```

**Решение:** собирайте данные в callback, модифицируйте менеджер после завершения итерации.

---

## Персистентные контейнеры и потокобезопасность

Персистентные контейнеры (`pstring`, `parray`, `pmap`, `pvector`, `ppool`) **не имеют
собственной синхронизации**. Их операции вызывают `allocate()`/`deallocate()` менеджера,
которые защищены мьютексом, но промежуточное состояние контейнера (размер, ёмкость,
указатели на данные) не защищено.

### Правила

1. **Мутирующие операции** (push_back, erase, assign, append, insert и т.д.) на одном
   экземпляре контейнера из разных потоков — **не потокобезопасны**. Необходима внешняя
   синхронизация.

2. **Чтение** (at, size, c_str, find и т.д.) одного экземпляра контейнера из нескольких
   потоков — **безопасно**, если ни один поток не мутирует контейнер одновременно.

3. **Разные экземпляры** контейнеров могут безопасно использоваться из разных потоков
   без дополнительной синхронизации (аллокация/деаллокация менеджера уже защищена).

```cpp
using Mgr = pmm::presets::MultiThreadedHeap;

// Безопасно: разные контейнеры в разных потоках
std::thread t1([]{
    pmm::pstring<Mgr> s1;
    s1.assign("hello");
});
std::thread t2([]{
    pmm::pstring<Mgr> s2;
    s2.assign("world");
});

// Небезопасно: один контейнер в нескольких потоках без синхронизации
pmm::parray<int, Mgr> shared_array;
std::thread t1([&]{ shared_array.push_back(1); });  // Data race!
std::thread t2([&]{ shared_array.push_back(2); });  // Data race!
```

---

## Рекомендации

### Выбор политики

- Используйте `NoLock` (или `CacheManagerConfig`) для однопоточных приложений —
  нулевые накладные расходы на блокировку.
- Используйте `SharedMutexLock` (или `PersistentDataConfig`, `IndustrialDBConfig`)
  для многопоточных приложений.
- Все предустановки с `SharedMutexLock` поддерживают конкурентное чтение
  (shared_lock для read-only операций).

### Производительность

- `resolve()` не захватывает мьютекс — вызывайте его свободно на горячем пути.
- `is_initialized()` — lock-free (atomic load).
- Статистические методы используют fast path: если менеджер не инициализирован,
  возвращают 0 без захвата мьютекса.
- Для минимизации конкуренции группируйте аллокации: выделяйте массивы через
  `allocate_typed<T>(count)` вместо множества отдельных аллокаций.

### Типичный многопоточный сценарий

```cpp
#include "pmm_multi_threaded_heap.h"
#include <thread>
#include <vector>

using Mgr = pmm::presets::MultiThreadedHeap;

int main()
{
    // Инициализация — однопоточно
    Mgr::create(16 * 1024 * 1024);  // 16 МБ

    // Многопоточная работа
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i)
    {
        workers.emplace_back([i]{
            // Каждый поток может безопасно аллоцировать/деаллоцировать
            auto ptr = Mgr::allocate_typed<int>();
            int* raw = ptr.resolve();
            *raw = i * 100;

            // Чтение статистики безопасно из любого потока
            std::size_t used = Mgr::used_size();

            Mgr::deallocate_typed(ptr);
        });
    }
    for (auto& w : workers) w.join();

    // Завершение — однопоточно
    Mgr::destroy();
}
```

---

## Тесты потокобезопасности

Потокобезопасность покрыта тестами в:

- `tests/test_thread_safety.cpp` — 4 тест-кейса: конкурентная аллокация,
  аллокация/деаллокация, ручной рост, проверка data races (4–8 потоков)
- `tests/test_shared_mutex.cpp` — 4 тест-кейса: конкурентный is_initialized,
  readers–writers, корректность данных при росте, консистентность счётчиков блоков
- `tests/test_issue213_concurrent.cpp` — 6 тест-кейсов: варьирующиеся размеры (8 потоков),
  LIFO-деаллокация, случайный порядок, высокая конкуренция (16 потоков),
  целостность данных, конкурентный reallocate_typed
- `benchmarks/bench_allocator.cpp` — бенчмарк `BM_AllocateMT` (1–8 потоков)

Суммарно: **14 конкурентных тест-кейсов** + 1 многопоточный бенчмарк.

---

*Документ создан 2026-03-20 в рамках Phase 6.1 (#215).*
