# Фаза 2: Слияние блоков (coalescing)

## Статус: ✅ Завершена

## Цель

Реализовать алгоритм `coalesce()` для слияния соседних свободных блоков при освобождении памяти, снижая фрагментацию кучи.

---

## Проблема, решаемая в Фазе 2

В Фазе 1 при освобождении блока (`deallocate`) блок просто помечался как свободный, но не объединялся с соседними свободными блоками. Это приводило к фрагментации:

```
До (после нескольких allocate/deallocate):
[free 256] [alloc 256] [free 256] [alloc 256] [free большой]

Нельзя выделить блок > 256 байт, хотя суммарно свободно > 512 байт.
```

После Фазы 2 соседние свободные блоки автоматически объединяются при освобождении:

```
После deallocate с coalescing:
[free 768+]   ← p1+p2+p3 объединились

Можно выделить блок до 768 байт.
```

---

## Реализованный алгоритм

### `coalesce(BlockHeader* blk)`

```
Предусловие: blk != nullptr, blk->used == false

Шаг 1 — слияние со СЛЕДУЮЩИМ блоком:
  если blk->next_offset != kNoBlock:
    next = block_at(base, blk->next_offset)
    если !next->used:
      blk->total_size += next->total_size
      blk->next_offset = next->next_offset
      если next->next_offset != kNoBlock:
        block_at(next->next_offset)->prev_offset = offset(blk)
      next->magic = 0   // безопасность
      hdr->block_count--
      hdr->free_count--

Шаг 2 — слияние с ПРЕДЫДУЩИМ блоком:
  если blk->prev_offset != kNoBlock:
    prev = block_at(base, blk->prev_offset)
    если !prev->used:
      prev->total_size += blk->total_size
      prev->next_offset = blk->next_offset
      если blk->next_offset != kNoBlock:
        block_at(blk->next_offset)->prev_offset = offset(prev)
      blk->magic = 0   // безопасность
      hdr->block_count--
      hdr->free_count--
```

### Интеграция в `deallocate()`

```
deallocate(ptr):
  1. Найти блок по ptr (find_block_by_ptr)
  2. Пометить blk->used = false, blk->user_size = 0
  3. Обновить hdr (alloc_count--, free_count++, used_size)
  4. coalesce(blk)   ← Фаза 2
```

---

## Сложность

| Операция | Сложность | Примечание |
|----------|-----------|------------|
| `coalesce` | O(1) | Только проверка непосредственных соседей |
| `deallocate` с coalescing | O(n) + O(1) | O(n) — поиск блока, O(1) — слияние |

---

## Реализованные компоненты

| Файл | Изменение |
|------|-----------|
| `include/persist_memory_manager.h` | Добавлен приватный метод `coalesce()`, вызов в `deallocate()` |
| `tests/test_coalesce.cpp` | Новый файл с тестами Фазы 2 |
| `tests/CMakeLists.txt` | Добавлен `test_coalesce` |
| `phase2.md` | Этот файл |
| `plan.md` | Статус Фазы 2 обновлён |
| `README.md` | Информация о Фазе 2 добавлена |

---

## Тесты

| Тест | Что проверяет |
|------|---------------|
| `coalesce_with_next` | Слияние со следующим свободным блоком |
| `coalesce_with_prev` | Слияние с предыдущим свободным блоком |
| `coalesce_both_neighbors` | Слияние сразу с обоими соседями |
| `coalesce_no_merge_when_neighbors_used` | Нет слияния при занятых соседях |
| `coalesce_first_block_no_next_free` | Нет слияния у первого блока без свободного правого |
| `coalesce_zero_fragmentation_after_all_free` | Фрагментация == 0 после освобождения всех блоков |
| `coalesce_lifo_results_in_one_block` | LIFO-освобождение → единый блок |
| `coalesce_fifo_results_in_one_block` | FIFO-освобождение → единый блок |
| `coalesce_large_allocation_after_merge` | После слияния доступен блок суммарного размера |
| `coalesce_stress_interleaved` | 200 раундов allocate/deallocate не нарушают структуру |

---

## Ограничения

1. **Слияние только с непосредственными соседями** — алгоритм проверяет только `prev` и `next`, что достаточно для полного устранения фрагментации при любом порядке освобождения.
2. **Нет потокобезопасности** — однопоточное использование (Фазы 4-5).
3. **Нет file I/O** — save/load из файла реализуется в Фазе 3.

---

## Следующая фаза

**Фаза 3: Персистентность (save/load)**

Реализация сохранения образа памяти в файл и загрузки из него, включая:
- `save(const char* filename)` — запись всей управляемой области в файл;
- `load(const char* filename)` — загрузка образа из файла;
- поддержку разных базовых адресов при загрузке (смещения уже хранятся в BlockHeader);
- обнаружение повреждённых образов через магические числа.
