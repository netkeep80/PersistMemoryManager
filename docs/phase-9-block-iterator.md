# Задача 9: API итератора блоков PMM — Завершена

**Статус**: ✅ Выполнена
**Ссылка на план**: [plan.md — Открытые вопросы и риски](../plan.md#открытые-вопросы-и-риски), риски #1 и #6

---

## Описание задачи

В `plan.md` риски №1 и №6 были помечены как открытые:

| Риск | Описание |
|------|----------|
| #1 | Доступ к внутренним заголовкам PMM для MemMapView — обходил через `reinterpret_cast` к `detail::ManagerHeader` и `detail::BlockHeader` |
| #6 | Thread safety: MemMapView читала PMM без API итератора — нет явной защиты от гонок при обходе |

Решение: добавить **публичный API итератора блоков** в `persist_memory_manager.h`, избавившись от прямого обращения к `detail::` в демо-приложении.

---

## Что реализовано

### Новые структуры в `include/persist_memory_manager.h`

#### `ManagerInfo`

```cpp
struct ManagerInfo {
    std::uint64_t  magic;               // магическое число заголовка
    std::size_t    total_size;          // полный размер области (байт)
    std::size_t    used_size;           // занятый объём (байт)
    std::size_t    block_count;         // общее число блоков
    std::size_t    free_count;          // число свободных блоков
    std::size_t    alloc_count;         // число занятых блоков
    std::ptrdiff_t first_block_offset;  // смещение первого блока
    std::ptrdiff_t first_free_offset;   // смещение первого свободного блока
    std::size_t    manager_header_size; // размер служебного заголовка (байт)
};
```

#### `BlockView`

```cpp
struct BlockView {
    std::size_t    index;       // порядковый индекс блока (0-based)
    std::ptrdiff_t offset;      // смещение блока от начала PMM-области
    std::size_t    total_size;  // полный размер блока (заголовок + данные)
    std::size_t    header_size; // размер служебного заголовка блока
    std::size_t    user_size;   // размер данных пользователя (0 для свободных)
    std::size_t    alignment;   // выравнивание данных
    bool           used;        // true — занят, false — свободен
};
```

### Новые функции в `include/persist_memory_manager.h`

#### `PersistMemoryManager::manager_header_size()`

```cpp
static std::size_t manager_header_size() noexcept;
```

Возвращает размер служебного заголовка менеджера (первые `manager_header_size()` байт
управляемой области заняты под метаданные).

#### `get_manager_info()`

```cpp
ManagerInfo get_manager_info( const PersistMemoryManager* mgr );
```

Безопасный снимок полей заголовка менеджера. Не требует блокировки со стороны вызывающего.

#### `for_each_block()`

```cpp
template <typename Callback>
void for_each_block( const PersistMemoryManager* mgr, Callback&& callback );
// Callback: void(const BlockView&)
```

Обходит все блоки PMM по связному списку и вызывает `callback` для каждого.
Берёт `shared_lock` внутри, что гарантирует потокобезопасное чтение при параллельных
`allocate` / `deallocate` из сценариев.

**Пример:**

```cpp
pmm::for_each_block( mgr, []( const pmm::BlockView& blk ) {
    std::cout << "Block #" << blk.index
              << " offset=" << blk.offset
              << " size="   << blk.total_size
              << ( blk.used ? " USED" : " FREE" ) << "\n";
} );
```

---

## Изменения в демо-приложении

### `demo/mem_map_view.cpp`

**До** (прямой доступ к `detail::`):
```cpp
const auto* base = reinterpret_cast<const std::uint8_t*>( mgr );
const auto* hdr  = reinterpret_cast<const pmm::detail::ManagerHeader*>( base );
// ...
const auto* blk = reinterpret_cast<const pmm::detail::BlockHeader*>( base + offset );
```

**После** (публичный API):
```cpp
const std::size_t mgr_hdr_sz = pmm::PersistMemoryManager::manager_header_size();
pmm::for_each_block( mgr, [&]( const pmm::BlockView& blk ) { ... } );
```

### `demo/struct_tree_view.cpp`

**До** (прямой доступ к `detail::`):
```cpp
const auto* hdr = reinterpret_cast<const pmm::detail::ManagerHeader*>( base );
snapshot_.magic = hdr->magic; // ...
```

**После** (публичный API):
```cpp
const pmm::ManagerInfo info = pmm::get_manager_info( mgr );
snapshot_.magic = info.magic; // ...
pmm::for_each_block( mgr, [&]( const pmm::BlockView& blk ) { ... } );
```

---

## Обновления в документации

- `plan.md`: риски #1 и #6 помечены как ✅ Решены
- `README.md`: добавлен раздел «API итератора блоков» с описанием `for_each_block()`,
  `get_manager_info()` и `manager_header_size()`

---

## Проверочные критерии

- [x] `for_each_block()` добавлен в публичный API `persist_memory_manager.h`
- [x] `get_manager_info()` добавлен в публичный API `persist_memory_manager.h`
- [x] `manager_header_size()` добавлен как `static` метод `PersistMemoryManager`
- [x] `mem_map_view.cpp` не использует `detail::ManagerHeader` / `detail::BlockHeader`
- [x] `struct_tree_view.cpp` не использует `detail::ManagerHeader` / `detail::BlockHeader`
- [x] `for_each_block()` берёт `shared_lock` — потокобезопасное чтение
- [x] Все существующие тесты (`ctest`) проходят без изменений
- [x] `clang-format` — код соответствует `.clang-format`
- [x] Нет новых предупреждений `cppcheck`
