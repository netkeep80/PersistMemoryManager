# Атомизация записи и модернизация блока (Issue #69, Issue #75)

## Обзор

Данный документ описывает алгоритмы модификации блоков в образе персистентного пространства адресов (ПАП/PAP), анализ критичности каждой операции записи, а также алгоритмы верификации и восстановления образа при незавершённой операции.

Цель: обеспечить математически строгую гарантию того, что если менеджер был прерван на любом этапе записи блока, образ можно проверить, определить стадию прерывания и завершить (или откатить) операцию.

---

## Структуры данных

### ManagerHeader (64 байта, 4 гранулы)

```
Байты 0-7:   magic           — магическое число менеджера ("PMM_V050")
Байты 8-15:  total_size      — полный размер управляемой области в байтах
Байты 16-19: used_size       — занятый размер в гранулах
Байты 20-23: block_count     — общее число блоков
Байты 24-27: free_count      — число свободных блоков
Байты 28-31: alloc_count     — число занятых блоков
Байты 32-35: first_block_offset — первый блок (гранульный индекс)
Байты 36-39: last_block_offset  — последний блок (гранульный индекс)
Байты 40-43: free_tree_root  — корень AVL-дерева свободных блоков
Байты 44:    owns_memory     — runtime-only (не персистентно)
Байты 45:    prev_owns_memory — runtime-only (не персистентно)
Байты 46-47: _pad
Байты 48-55: prev_total_size — runtime-only (не персистентно)
Байты 56-63: prev_base_ptr   — runtime-only (не персистентно)
```

### BlockHeader (32 байта = 2 гранулы, Issue #75)

```
Байты 0-3:   size          — [1] занятый размер в гранулах (0 = свободный блок, Issue #75)
Байты 4-7:   prev_offset   — [2] предыдущий блок (гранульный индекс)
Байты 8-11:  next_offset   — [3] следующий блок (гранульный индекс)
Байты 12-15: left_offset   — [4] левый дочерний узел AVL-дерева (гранульный индекс)
Байты 16-19: right_offset  — [5] правый дочерний узел AVL-дерева (гранульный индекс)
Байты 20-23: parent_offset — [6] родительский узел AVL-дерева (гранульный индекс)
Байты 24-25: avl_height    — высота AVL-поддерева (0 = не в дереве)
Байты 26-27: _pad          — зарезервировано
Байты 28-31: root_offset   — корень дерева: 0 = дерево свободных блоков;
                              own_index = занятый блок является корнем своего AVL-дерева (Issue #75)
```

**Issue #69:** поле `magic` удалено из `BlockHeader`. Валидность блока теперь определяется структурными инвариантами (см. раздел "Верификация блока").

**Issue #75:** поля переименованы: `used_size` → `size`, `_reserved` → `root_offset`. Семантика `root_offset`: значение `0` означает принадлежность дереву свободных блоков; значение равное собственному гранульному индексу означает, что блок является корнем своего AVL-дерева (задел для будущей типизации на основе ПАП-блоков).

---

## Инварианты валидности блока (заменяют magic)

Блок считается валидным, если **все** следующие условия выполнены:

1. **`size < total_granules`**: `size` строго меньше числа гранул до следующего блока (вычисляемого через `next_offset`).
2. **`prev_offset < this_idx < next_offset`**: гранульный индекс данного блока строго между `prev_offset` и `next_offset` (если они не равны `kNoBlock`).
3. **`avl_height < 32`**: высота AVL-поддерева не превышает 32 (разумный предел для дерева, которое может содержать до 2^32 узлов).
4. **AVL-указатели**: либо все из `left_offset`, `right_offset`, `parent_offset` равны `kNoBlock` (блок не в дереве), либо ни один не совпадает с другим (они все разные).

### Псевдокод `is_valid_block`

```cpp
bool is_valid_block(const uint8_t* base, const ManagerHeader* hdr, uint32_t idx) {
    if (idx == kNoBlock) return false;
    if (idx_to_byte_off(idx) + sizeof(BlockHeader) > hdr->total_size) return false;

    const BlockHeader* blk = block_at(base, idx);

    // 1. Проверка size vs total_granules
    uint32_t total_gran = block_total_granules(base, hdr, blk);
    if (blk->size >= total_gran) return false;  // size должен быть СТРОГО меньше

    // 2. Проверка prev_offset < this_idx < next_offset
    if (blk->prev_offset != kNoBlock && blk->prev_offset >= idx) return false;
    if (blk->next_offset != kNoBlock && blk->next_offset <= idx) return false;

    // 3. Проверка avl_height < 32
    if (blk->avl_height >= 32) return false;

    // 4. Проверка уникальности AVL-ссылок
    bool l_valid = (blk->left_offset   != kNoBlock);
    bool r_valid = (blk->right_offset  != kNoBlock);
    bool p_valid = (blk->parent_offset != kNoBlock);
    if (l_valid || r_valid || p_valid) {
        // Не все kNoBlock — проверяем, что все три разные
        if (l_valid && r_valid && blk->left_offset == blk->right_offset)   return false;
        if (l_valid && p_valid && blk->left_offset == blk->parent_offset)  return false;
        if (r_valid && p_valid && blk->right_offset == blk->parent_offset) return false;
    }

    return true;
}
```

---

## Критические операции

Обозначения:
- **W(addr, val)** — запись значения `val` по адресу `addr`
- **КРИТИЧНО** — если прерывание после этого шага приводит к несогласованному образу
- **НЕ КРИТИЧНО** — прерывание безопасно, образ остаётся согласованным

---

## Алгоритм 1: Добавление нового блока (allocate_from_block + splitting)

При выделении памяти, если найденный свободный блок можно разбить (splitting):

### Этапы записи

| # | Операция | КРИТИЧНО? | Обоснование |
|---|----------|-----------|-------------|
| 1 | `avl_remove(blk_idx)` — удалить блок из AVL | НЕ КРИТИЧНО | Блок ещё существует в линейном списке. AVL-дерево перестраивается при `load()`. |
| 2 | `W(new_blk, 0)` — `memset` нового заголовка split-блока | НЕ КРИТИЧНО | Блок ещё не включён в линейный список. |
| 3 | `W(new_blk->next_offset, blk->next_offset)` | КРИТИЧНО | Если прервать здесь — `new_blk->next_offset` ещё не задан. |
| 4 | `W(new_blk->prev_offset, blk_idx)` | КРИТИЧНО | Обратная ссылка на разбиваемый блок. |
| 5 | `W(old_next->prev_offset, new_idx)` (если next != kNoBlock) | КРИТИЧНО | Прямая ссылка на новый блок из следующего блока. |
| 6 | `W(blk->next_offset, new_idx)` | КРИТИЧНО | Прямая ссылка на новый блок из разбиваемого блока. |
| 7 | `W(hdr->last_block_offset, new_idx)` (если это последний блок) | НЕ КРИТИЧНО | Оптимизация. Перестраивается при `load()`. |
| 8 | `W(hdr->block_count, hdr->block_count + 1)` | НЕ КРИТИЧНО | Счётчик перестраивается при `load()`. |
| 9 | `W(hdr->free_count, hdr->free_count + 1)` | НЕ КРИТИЧНО | Счётчик перестраивается при `load()`. |
| 10 | `W(hdr->used_size, hdr->used_size + kBlockHeaderGranules)` | НЕ КРИТИЧНО | Вычисляется при `load()`. |
| 11 | `avl_insert(new_idx)` — вставить новый свободный блок в AVL | НЕ КРИТИЧНО | AVL перестраивается при `load()`. |
| 12 | `W(blk->size, data_gran)` + `W(blk->root_offset, blk_idx)` | КРИТИЧНО | Это маркирует блок как занятый и устанавливает его корень (Issue #75). |
| 13 | `W(blk->avl_height, 0)` + обнуление AVL-ссылок blk | НЕ КРИТИЧНО | AVL перестраивается при `load()`. |
| 14 | `W(hdr->alloc_count, hdr->alloc_count + 1)` | НЕ КРИТИЧНО | Перестраивается при `load()`. |
| 15 | `W(hdr->free_count, hdr->free_count - 1)` | НЕ КРИТИЧНО | Перестраивается при `load()`. |
| 16 | `W(hdr->used_size, hdr->used_size + data_gran)` | НЕ КРИТИЧНО | Вычисляется при `load()`. |

### Анализ критических точек прерывания

**Прерывание между шагами 2 и 3** (после memset нового блока, до установки next_offset):
- Состояние: `blk->next_offset` ещё указывает на старый следующий блок, `new_blk` инициализирован нулями/kNoBlock.
- Восстановление при load(): новый блок не может быть найден через линейный список (blk->next_offset ≠ new_idx), поэтому он невидим. Образ согласован.

**Прерывание между шагами 5 и 6** (old_next->prev_offset обновлён, но blk->next_offset ещё не):
- Состояние: `old_next->prev_offset = new_idx`, но `blk->next_offset = old_next_idx`.
- Recovery: двойной список несогласован (forward != backward). Обнаруживается при traverse.

**Прерывание после шага 6, до шага 12** (новый блок включён, но разбиваемый ещё помечен как свободный):
- Состояние: линейный список согласован, блок `blk` в AVL-дереве (точнее, был удалён в шаге 1, но ещё не переинсертирован как занятый).
- Восстановление: при load() rebuild_free_tree() видит блок со старым `size=0` и вставляет его в AVL как свободный. Это корректно, но означает "утечку" — блок был выдан пользователю, но после recovery снова свободен. Данные пользователя потеряны (допустимо при crash recovery).

---

## Алгоритм 2: Удаление свободного блока (deallocate_raw + coalesce)

### Этапы записи

| # | Операция | КРИТИЧНО? | Обоснование |
|---|----------|-----------|-------------|
| 1 | `W(blk->size, 0)` + `W(blk->root_offset, 0)` | КРИТИЧНО | Маркирует блок как свободный и сбрасывает корень (Issue #75). |
| 2 | `W(hdr->alloc_count, -1)` | НЕ КРИТИЧНО | Счётчик, перестраивается при load(). |
| 3 | `W(hdr->free_count, +1)` | НЕ КРИТИЧНО | Счётчик, перестраивается при load(). |
| 4 | `W(hdr->used_size, -= freed)` | НЕ КРИТИЧНО | Вычисляется при load(). |
| 5 | **Слияние со следующим (coalesce next):** | | |
| 5a | `avl_remove(nxt_idx)` — удалить следующий свободный блок из AVL | НЕ КРИТИЧНО | AVL перестраивается при load(). |
| 5b | `W(blk->next_offset, nxt->next_offset)` | КРИТИЧНО | Изменяет линейный список. |
| 5c | `W(nxt->next->prev_offset, b_idx)` (если есть) | КРИТИЧНО | Обратная ссылка. |
| 5d | `W(hdr->last_block_offset, b_idx)` (если nxt был последним) | НЕ КРИТИЧНО | Оптимизация, перестраивается при load(). |
| 5e | `W(nxt->magic, 0)` / zeroing nxt header | КРИТИЧНО | Уничтожение заголовка слитого блока. |
| 5f | `W(hdr->block_count, -1)` | НЕ КРИТИЧНО | Перестраивается при load(). |
| 5g | `W(hdr->free_count, -1)` | НЕ КРИТИЧНО | Перестраивается при load(). |
| 6 | **Слияние с предыдущим (coalesce prev):** | | |
| 6a | `avl_remove(prv_idx)` | НЕ КРИТИЧНО | AVL перестраивается при load(). |
| 6b | `W(prv->next_offset, blk->next_offset)` | КРИТИЧНО | Изменяет линейный список. |
| 6c | `W(blk->next->prev_offset, prv_idx)` (если есть) | КРИТИЧНО | Обратная ссылка. |
| 6d | `W(hdr->last_block_offset, prv_idx)` (если blk был последним) | НЕ КРИТИЧНО | Оптимизация. |
| 6e | `W(blk->magic, 0)` / zeroing blk header | КРИТИЧНО | Уничтожение заголовка слитого блока. |
| 6f | `W(hdr->block_count, -1)` | НЕ КРИТИЧНО | Перестраивается при load(). |
| 6g | `W(hdr->free_count, -1)` | НЕ КРИТИЧНО | Перестраивается при load(). |
| 7 | `avl_insert(result_blk_idx)` | НЕ КРИТИЧНО | AVL перестраивается при load(). |

### Анализ критических точек прерывания (coalesce)

**Прерывание между 5b и 5c** (blk->next_offset обновлён, но nxt->next->prev_offset нет):
- Состояние: линейный список несогласован. `traverse_forward` и `traverse_backward` дадут разные результаты.
- Обнаружение: при load() traverse видит `blk->next_offset = new_next`, но `new_next->prev_offset = nxt_idx` (старый). Несоответствие обнаруживается.

**Прерывание между 5c и 5e** (ссылки обновлены, но nxt header не обнулён):
- Состояние: `nxt` недостижим из линейного списка (блок пропущен), но его заголовок всё ещё выглядит как валидный блок.
- Обнаружение: при traversal через линейный список `nxt` не будет найден (т.к. `blk->next_offset` уже указывает на `nxt->next`). Однако при проверке границ блоков будет видно, что `blk` занимает область, включающую байты `nxt`.

---

## Алгоритм 3: Перевыделение (reallocate_typed) — не критично

Алгоритм `reallocate_typed` состоит из:
1. `allocate_typed` (Алгоритм 1) — выделить новый блок.
2. `memcpy` — скопировать данные.
3. `deallocate_typed` (Алгоритм 2) — освободить старый блок.

Если прерывание происходит:
- После `allocate_typed`, до `deallocate_typed`: оба блока существуют, данные дублируются. Пользователь теряет результат reallocate (новый указатель не был возвращён), но старые данные в исходном блоке сохраняются.
- После `deallocate_typed`: старый блок освобождён, новый блок содержит копию данных. Пользователь не получил новый указатель, но данные не потеряны в ОЗУ (только как «утечка» нового блока).

---

## Алгоритм 4: Балансировка AVL-дерева — не критично

Балансировка AVL-дерева свободных блоков происходит как часть `avl_insert` и `avl_remove`. Операции AVL (повороты, обновление высот) не критичны, потому что:

1. **При load()** вызывается `rebuild_free_tree()`, которая полностью перестраивает AVL-дерево из нуля, проходя по линейному списку всех блоков.
2. Блоки доступны через линейный список (`first_block_offset` → `next_offset`), который не зависит от AVL-структуры.

Таким образом, любое прерывание во время AVL-операций не влияет на корректность образа после load()+rebuild().

---

## Алгоритм верификации и восстановления при загрузке

### Фаза 1: Верификация ManagerHeader

```
1. Проверить hdr->magic == kMagic ("PMM_V050")
2. Проверить hdr->total_size == переданный size
3. Если не прошло — ошибка загрузки, образ недействителен
```

### Фаза 2: Rebuild linear list — rebuild_free_tree()

При загрузке всегда вызывается `rebuild_free_tree()`, которая:

```
1. Сбросить hdr->free_tree_root = kNoBlock
2. Сбросить hdr->last_block_offset = kNoBlock
3. Сбросить все AVL-ссылки (left/right/parent/height) каждого блока в 0/kNoBlock
4. Проходить по линейному списку (first_block_offset → next_offset):
   - Для каждого свободного блока (size == 0): avl_insert
   - Отслеживать last_block_offset
```

### Фаза 3: Обнаружение частично завершённых операций

Обнаружение происходит через проверку инвариантов двусвязного списка:

```
for each block B at index idx (traversing forward):
    if B.next_offset != kNoBlock:
        next_block = block_at(B.next_offset)
        assert(next_block.prev_offset == idx)  // forward/backward consistency
    if B.prev_offset != kNoBlock:
        prev_block = block_at(B.prev_offset)
        assert(prev_block.next_offset == idx)  // backward/forward consistency
```

Если нарушение обнаружено — классифицируем состояние:

**Случай A: `blk->next_offset = new_idx`, но `new_next->prev_offset = old_idx`**
- Диагноз: Алгоритм 2, прерывание между шагами 5b и 5c (или 6b и 6c).
- Восстановление: Установить `new_next->prev_offset = b_idx`.

**Случай B: Блок `nxt` существует в границах `blk`, но не включён в список**
- Диагноз: Алгоритм 2, прерывание на шаге 5e (или 6e) — заголовок не обнулён.
- Восстановление: Обнулить (`memset`) недостижимый заголовок.

**Случай C: `blk->size = 0`, но был предоставлен пользователю**
- Диагноз: Алгоритм 1, прерывание между шагами 1 и 12 (блок удалён из AVL, но ещё не помечен как занятый).
- Восстановление: Блок считается свободным (данные пользователя потеряны). Корректное поведение crash recovery.

### Полный алгоритм load() с recovery

```cpp
bool load_with_recovery(void* memory, size_t size) {
    // Фаза 1: Проверить ManagerHeader
    ManagerHeader* hdr = (ManagerHeader*)memory;
    if (hdr->magic != kMagic || hdr->total_size != size)
        return false;  // Образ недействителен

    // Фаза 2: Сбросить runtime-поля
    hdr->owns_memory = hdr->prev_owns_memory = false;
    hdr->prev_total_size = 0;
    hdr->prev_base_ptr = nullptr;

    // Фаза 3: Проверить и исправить двусвязный список
    repair_linked_list(memory, hdr);

    // Фаза 4: Пересчитать счётчики и перестроить AVL
    rebuild_free_tree();  // Перестраивает AVL, last_block_offset, сбрасывает AVL-ссылки блоков
    recompute_counters(); // Пересчитать block_count, free_count, alloc_count, used_size

    s_instance = (PersistMemoryManager*)memory;
    return true;
}

void repair_linked_list(uint8_t* base, ManagerHeader* hdr) {
    uint32_t idx = hdr->first_block_offset;
    while (idx != kNoBlock) {
        BlockHeader* blk = block_at(base, idx);

        // Проверить консистентность forward/backward
        if (blk->next_offset != kNoBlock) {
            BlockHeader* nxt = block_at(base, blk->next_offset);
            if (nxt->prev_offset != idx) {
                // Исправить несоответствие
                nxt->prev_offset = idx;
            }
        }
        idx = blk->next_offset;
    }
}

void recompute_counters(uint8_t* base, ManagerHeader* hdr) {
    uint32_t block_count = 0, free_count = 0, alloc_count = 0;
    uint32_t used_gran = kManagerHeaderGranules;  // Начальный overhead

    uint32_t idx = hdr->first_block_offset;
    while (idx != kNoBlock) {
        BlockHeader* blk = block_at(base, idx);
        block_count++;
        used_gran += kBlockHeaderGranules;  // Overhead заголовка

        if (blk->size > 0) {
            alloc_count++;
            used_gran += blk->size;
        } else {
            free_count++;
        }
        idx = blk->next_offset;
    }

    hdr->block_count = block_count;
    hdr->free_count  = free_count;
    hdr->alloc_count = alloc_count;
    hdr->used_size   = used_gran;
}
```

---

## Итоги и требования

### Требования к реализации

1. **BlockHeader без magic**: Удалить поле `magic` из `BlockHeader`. Освободившиеся 4 байта могут быть использованы в будущем для интеграции строкового словаря в ПАП.

2. **Структурная валидация блока**: Функция `is_valid_block()` должна проверять все 4 инварианта (см. выше) вместо проверки magic.

3. **`header_from_ptr()` без magic**: Использовать `is_valid_block()` и `blk->size > 0` вместо `blk->magic == kBlockMagic`.

4. **`validate()` без magic**: Использовать `is_valid_block()` при проходе по списку.

5. **`rebuild_free_tree()` + `repair_linked_list()`**: Вызывать при load() для восстановления AVL-дерева и исправления несоответствий в двусвязном списке.

6. **`recompute_counters()`**: Вызывать при load() для пересчёта счётчиков (block_count, free_count, alloc_count, used_size) из актуального состояния блоков.

7. **Обновить magic менеджера**: Изменить `kMagic` на `"PMM_V050"` для отказа от несовместимых старых образов (Issue #75: переименование `used_size`/`_reserved`).

### Гарантии корректности при crash recovery

| Сценарий прерывания | Результат после load() |
|---------------------|------------------------|
| До любой записи | Образ неизменён, корректен |
| Во время AVL-операций | AVL перестраивается, образ корректен |
| Во время splitting (до записи blk->next_offset) | Новый блок невидим, корректен |
| Во время splitting (после записи blk->next_offset) | Список исправляется при repair_linked_list() |
| После splitting, до маркировки occupied | Блок возвращается в свободные (данные потеряны) |
| Во время coalesce (ссылки частично обновлены) | Список исправляется при repair_linked_list() |
| После coalesce (старый заголовок не обнулён) | Заголовок недостижим, область занята слитым блоком |

### Важное замечание о гарантиях

Данный алгоритм обеспечивает **crash consistency** (согласованность после сбоя) в смысле: образ после восстановления будет структурно валидным, и менеджер сможет продолжить работу. Однако **полная атомарность операций выделения/освобождения** (т.е. гарантия того, что операция либо полностью применена, либо полностью откатится) требует журналирования (write-ahead log, WAL), что выходит за рамки данного issue.

В рамках данного issue достигается: **структурная корректность образа после load() при любом сбое**, при этом частично выполненные операции выделения трактуются как "не выполненные" (блоки возвращаются в свободные).
