# План разработки PersistMemoryManager

## Текущая фаза: Фаза 6 завершена — ожидает Фаза 7

Подробности по каждой фазе: [phase1.md](phase1.md), [phase2.md](phase2.md), [phase3.md](phase3.md), [phase4.md](phase4.md), [phase6.md](phase6.md)

---

## Общий план

| Фаза | Задача | Статус | Файл |
|------|--------|--------|------|
| 1 | Базовая структура и allocate/deallocate | ✅ Завершена | [phase1.md](phase1.md) |
| 2 | Слияние блоков (coalescing) | ✅ Завершена | [phase2.md](phase2.md) |
| 3 | Персистентность (save/load) | ✅ Завершена | [phase3.md](phase3.md) |
| 4 | Тесты и документация | ✅ Завершена | [phase4.md](phase4.md) |
| 5 | Персистный указатель pptr<T> | ✅ Завершена | — |
| 6 | Оптимизация производительности | ✅ Завершена | [phase6.md](phase6.md) |
| 7 | Интеграция с pjson_db | ⏳ Ожидает | — |

---

## Фаза 1: Базовая структура и allocate/deallocate

**Статус:** ✅ Завершена

**Задачи:**
- [x] Определить структуру блока памяти (`Block`)
- [x] Определить структуру менеджера памяти (`MemoryManager`)
- [x] Реализовать `PersistMemoryManager::create()`
- [x] Реализовать `PersistMemoryManager::destroy()`
- [x] Реализовать `PersistMemoryManager::allocate()`
- [x] Реализовать `PersistMemoryManager::deallocate()`
- [x] Реализовать метрики: `total_size()`, `used_size()`, `free_size()`, `fragmentation()`
- [x] Реализовать `validate()` и `dump_stats()`
- [x] Написать юнит-тесты для выделения памяти
- [x] Написать юнит-тесты для освобождения памяти
- [x] Создать пример базового использования

**Результаты:**
- `include/persist_memory_manager.h` — single-header реализация
- `tests/test_allocate.cpp` — тесты выделения памяти
- `tests/test_deallocate.cpp` — тесты освобождения памяти
- `examples/basic_usage.cpp` — базовый пример использования

---

## Фаза 2: Слияние блоков (coalescing)

**Статус:** ✅ Завершена

**Задачи:**
- [x] Реализовать алгоритм `coalesce()` для слияния соседних свободных блоков
- [x] Обновить `deallocate()` для вызова `coalesce()`
- [x] Написать тесты для слияния блоков
- [x] Обновить документацию

**Результаты:**
- `include/persist_memory_manager.h` — добавлен метод `coalesce()`, вызов в `deallocate()`
- `tests/test_coalesce.cpp` — 10 тестов слияния блоков
- `phase2.md` — документация по Фазе 2

---

## Фаза 3: Персистентность (save/load)

**Статус:** ✅ Завершена

**Задачи:**
- [x] Реализовать `save(const char* filename)` для сохранения образа памяти
- [x] Реализовать `load_from_file(const char* filename, void* memory, size_t size)` для загрузки образа из файла
- [x] `PersistMemoryManager::load(void* memory, size_t size)` — загрузка из существующего образа (реализован в Фазе 1)
- [x] Хранение указателей как смещений (offsets) от base_ptr (обеспечено с Фазы 1)
- [x] Поддержка разных базовых адресов при загрузке (смещения не зависят от адреса)
- [x] Написать тесты для персистентности
- [x] Демонстрационный пример: `examples/persistence_demo.cpp`

**Результаты:**
- `include/persist_memory_manager.h` — добавлен метод `save()`, свободная функция `load_from_file()`
- `tests/test_persistence.cpp` — 12 тестов персистентности
- `examples/persistence_demo.cpp` — демонстрация полного цикла save/load
- `phase3.md` — документация по Фазе 3

---

## Фаза 4: Тесты и документация

**Статус:** ✅ Завершена

**Задачи:**
- [x] Стресс-тест: 100 000 аллокаций подряд
- [x] Чередование allocate/deallocate (1 000 000 операций)
- [x] Написать `examples/stress_test.cpp`
- [x] Написать `examples/CMakeLists.txt` (сборка примеров)
- [x] Написать `docs/api_reference.md`
- [x] Написать `docs/performance.md`
- [x] Написать `phase4.md`
- [x] Обновить `plan.md` и `README.md`
- [ ] Достичь покрытия тестами ≥ 90% (требуется gcov/lcov — перенесено в Фазу 5)

**Результаты:**
- `examples/stress_test.cpp` — стресс-тест: 100K аллокаций + 1M чередующихся операций
- `examples/CMakeLists.txt` — сборка примеров (basic_usage, persistence_demo, stress_test)
- `docs/api_reference.md` — полный справочник по API
- `docs/performance.md` — документация по производительности и результаты тестов
- `phase4.md` — документация по Фазе 4

---

## Фаза 5: Персистный указатель pptr<T>

**Статус:** ✅ Завершена

**Задачи:**
- [x] Определить шаблонный класс `pptr<T>` в `include/persist_memory_manager.h`
- [x] Хранить смещение от базы менеджера (вместо абсолютного адреса)
- [x] Обеспечить `sizeof(pptr<T>) == sizeof(void*)`
- [x] Реализовать `is_null()`, `operator bool()`, `offset()`
- [x] Реализовать `resolve(mgr)` — разыменование через менеджер
- [x] Реализовать `resolve_at(mgr, index)` — доступ к элементу массива
- [x] Реализовать операторы сравнения `==` и `!=`
- [x] Добавить в `PersistMemoryManager`:
  - [x] `allocate_typed<T>()` — выделение одного объекта
  - [x] `allocate_typed<T>(count)` — выделение массива
  - [x] `deallocate_typed(pptr<T>)` — освобождение
  - [x] `offset_to_ptr(offset)` — преобразование смещения в указатель
- [x] Написать тесты `tests/test_pptr.cpp`
- [x] Обновить `tests/CMakeLists.txt`
- [x] Обновить `plan.md` и `README.md`

**Результаты:**
- `include/persist_memory_manager.h` — добавлен `pptr<T>`, методы `allocate_typed`, `deallocate_typed`, `offset_to_ptr`
- `tests/test_pptr.cpp` — 14 тестов персистного указателя

---

## Фаза 6: Оптимизация производительности

**Статус:** ✅ Завершена

**Задачи:**
- [x] Профилирование текущей реализации
- [x] Оптимизация поиска свободных блоков (отдельный список свободных блоков)
- [x] Достичь: allocate 100K блоков ≤ 100 мс
- [x] Достичь: deallocate 100K блоков ≤ 100 мс
- [x] Добавить benchmarks в репозиторий (`examples/benchmark.cpp`)
- [x] Написать тесты производительности (`tests/test_performance.cpp`)
- [x] Восстановление списка свободных блоков при `load()`

**Результаты:**
- `include/persist_memory_manager.h` — добавлен отдельный список свободных блоков, `rebuild_free_list()`, `detail::free_list_insert/remove`
- `tests/test_performance.cpp` — 8 тестов производительности и корректности
- `examples/benchmark.cpp` — бенчмарк с проверкой целевых показателей
- `phase6.md` — документация по Фазе 6

---

## Фаза 7: Интеграция с pjson_db

**Статус:** ⏳ Ожидает

**Задачи:**
- [ ] Изучить API pjson_db
- [ ] Разработать адаптер для использования PersistMemoryManager как аллокатора в pjson_db
- [ ] Написать демонстрационный пример интеграции
- [ ] Провести нагрузочное тестирование совместной работы

---

*Документ создан автоматически на основе технического задания `tz.md`*
