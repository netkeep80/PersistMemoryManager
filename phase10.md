# Фаза 10: std::shared_mutex — разделённые блокировки

**Статус:** ✅ Завершена

---

## Постановка задачи

Фаза 9 добавила базовую потокобезопасность через `std::recursive_mutex`, однако все методы — в том числе read-only (`validate()`, `save()`, `dump_stats()`) — захватывали **эксклюзивную** блокировку. Это не позволяло параллельно выполнять несколько операций чтения.

Задача Фазы 10: перейти на `std::shared_mutex`, разделив методы на:
- **читатели** (`validate()`, `save()`, `dump_stats()`) — захватывают `shared_lock`; могут работать параллельно;
- **писатели** (`create()`, `load()`, `destroy()`, `allocate()`, `deallocate()`) — захватывают `unique_lock`; гарантируют эксклюзивный доступ.

Дополнительно: рефакторинг `reallocate()` для предотвращения deadlock при вызове публичных методов под блокировкой.

---

## Решение

### 1. Замена `std::recursive_mutex` на `std::shared_mutex`

| До (Фаза 9) | После (Фаза 10) |
|-------------|-----------------|
| `static std::recursive_mutex s_mutex;` | `static std::shared_mutex s_mutex;` |
| `std::lock_guard<std::recursive_mutex>` (везде) | `std::unique_lock<std::shared_mutex>` (писатели) |
| — | `std::shared_lock<std::shared_mutex>` (читатели) |

`std::shared_mutex` позволяет параллельные `shared_lock` (читатели не блокируют друг друга) и эксклюзивный `unique_lock` (писатель блокирует всех).

### 2. Рефакторинг `reallocate()`

`std::recursive_mutex` допускал повторный захват одним потоком: `reallocate()` могла вызывать `allocate()` и `deallocate()` под удерживаемой блокировкой.

`std::shared_mutex` — **нерекурсивный**. Решение: `reallocate()` освобождает `unique_lock` перед вызовом `allocate()`/`deallocate()`:

```cpp
void* reallocate( void* ptr, std::size_t new_size )
{
    if ( ptr == nullptr ) return allocate( new_size );   // без захвата
    if ( new_size == 0 ) { deallocate( ptr ); return nullptr; }

    std::unique_lock<std::shared_mutex> lock( s_mutex );
    // ... проверяем блок, сохраняем нужные данные ...
    std::size_t old_user_size = blk->user_size;
    std::size_t align         = blk->alignment;
    lock.unlock();                                       // снимаем блокировку

    void* new_ptr = allocate( new_size, align );         // теперь не deadlock
    if ( new_ptr == nullptr ) return nullptr;
    std::memcpy( new_ptr, ptr, old_user_size );
    deallocate( ptr );
    return new_ptr;
}
```

Бонус: рефакторинг сократил `reallocate()` с 41 до 35 строк, освободив бюджет для добавления `shared_lock` в read-only методы без превышения лимита 1500 строк.

---

## Изменения в `include/persist_memory_manager.h`

| Место | Изменение |
|-------|-----------|
| Заголовок файла | Добавлен `#include <shared_mutex>` |
| Версия | Обновлена до `0.8.0` |
| `private:` | `static std::recursive_mutex s_mutex;` → `static std::shared_mutex s_mutex;` |
| Определение статического члена | `inline std::recursive_mutex` → `inline std::shared_mutex` |
| `create()`, `load()`, `destroy()` | `lock_guard<recursive_mutex>` → `unique_lock<shared_mutex>` |
| `allocate()`, `deallocate()` | `lock_guard<recursive_mutex>` → `unique_lock<shared_mutex>` |
| `reallocate()` | Рефакторинг с `lock.unlock()` перед подвызовами |
| `validate()` | Добавлен `std::shared_lock<std::shared_mutex>` |
| `dump_stats()` | Добавлен `std::shared_lock<std::shared_mutex>` |
| `save()` | Добавлен `std::shared_lock<std::shared_mutex>` |

---

## Тесты

Создан `tests/test_shared_mutex.cpp` с 4 тестами:

| Тест | Описание |
|------|----------|
| `test_concurrent_validate` | 8 потоков параллельно вызывают `validate()` (shared_lock). Все должны получать `true`. |
| `test_readers_writers` | 4 читателя (`validate()`) и 2 писателя (`allocate`/`deallocate`) работают одновременно. |
| `test_reallocate_correctness` | 4 потока параллельно `reallocate` своих блоков; проверяется сохранность маркеров данных. |
| `test_concurrent_get_stats` | 6 потоков параллельно вызывают `get_stats()`. Счётчики должны быть согласованны. |

### Обновлён `tests/CMakeLists.txt`

Добавлена сборка `test_shared_mutex` с линковкой к `Threads::Threads`.

---

## Результаты

- Все 9 тестов проходят (8 из Фаз 1–9 + 1 новый).
- `clang-format` проходит без нарушений.
- Размер заголовочного файла: **1499 строк** (лимит CI: ≤ 1500 строк ✅).
- Версия заголовочного файла обновлена до `0.8.0`.

---

## Ограничения

- Метрические методы (`total_size()`, `used_size()`, `free_size()`, `fragmentation()`) не используют `shared_lock`. Они читают единственное поле `size_t` из выровненного адреса — атомарная операция на x86_64. Добавление `shared_lock` здесь привело бы к рекурсивному захвату из `dump_stats()`, которая уже держит `shared_lock` и вызывает `free_size()` и `fragmentation()`. Поскольку `std::shared_mutex` нерекурсивен, это вызвало бы deadlock.
- Один мьютекс на весь менеджер (coarse-grained). При высокой конкуренции на запись это может стать узким местом.
