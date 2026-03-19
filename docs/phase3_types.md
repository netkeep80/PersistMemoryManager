# Фаза 3: Типы для BinDiffSynchronizer

> Реализация начата в Issue #45.

## 3.1 Мутабельная персистентная строка `pstring<ManagerT>` ✅

**Issue:** #45
**Файл:** `include/pmm/pstring.h`
**Тесты:** `tests/test_issue45_pstring.cpp` (21 тест)

### Описание

`pstring<ManagerT>` — мутабельная строка, хранящаяся в персистентном адресном пространстве (ПАП).
В отличие от `pstringview` (read-only, interned), `pstring` поддерживает изменение содержимого.

### Архитектура

```
pstring (в ПАП)           Блок данных (в ПАП)
┌───────────────┐         ┌─────────────────────┐
│ _length: u32  │         │ h e l l o \0        │
│ _capacity: u32│──idx──→ │                     │
│ _data_idx     │         └─────────────────────┘
└───────────────┘
```

- **Заголовок** (pstring struct): хранит длину, ёмкость и гранульный индекс блока данных
- **Блок данных**: отдельный блок в ПАП, содержащий null-terminated строку
- **Переаллокация**: при росте выделяется новый блок с удвоенной ёмкостью (amortized O(1))
- **POD-структура**: `std::is_trivially_copyable_v<pstring> == true`

### API

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

// Создание
Mgr::pptr<Mgr::pstring> p = Mgr::create_typed<Mgr::pstring>();

// Присвоение
p->assign("hello");          // assign(const char*) → bool
p->append(" world");         // append(const char*) → bool

// Чтение
const char* s = p->c_str();  // "hello world"
std::size_t n = p->size();   // 11
bool empty    = p->empty();  // false
char c        = (*p)[0];     // 'h'

// Изменение
p->assign("new value");      // переаллокация при необходимости
p->clear();                  // length = 0, буфер сохраняется

// Сравнение
*p == "test";                // operator==(const char*)
*p != "test";                // operator!=(const char*)
*p1 == *p2;                  // operator==(const pstring&)
*p1 < *p2;                   // operator<(const pstring&) — лексикографическое

// Освобождение
p->free_data();              // деаллоцировать блок данных
Mgr::destroy_typed(p);       // деаллоцировать сам pstring

Mgr::destroy();
```

### Отличия от pstringview

| Свойство | pstringview | pstring |
|----------|-------------|---------|
| Мутабельность | Read-only | Мутабельная |
| Интернирование | Да (дедупликация) | Нет |
| Блокировка блока | Навечно (lock_block_permanent) | Обычный блок |
| Хранение данных | Встроенное (flexible array) | Отдельный блок |
| Использование | Константные строки, ключи | JSON-значения, динамические данные |
