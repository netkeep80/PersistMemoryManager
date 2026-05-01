# Шаблон: external interface (`if-xxx`)

External interface описывает контракт API/интерфейса, открытого PMM наружу
(публичный заголовок, storage backend concept, IO helper). Размещается в
[`07_external_interfaces.md`](../07_external_interfaces.md). Идентификатор:
`if-xxx`.

## Обязательные поля

| Поле                          | Назначение                                                       |
|-------------------------------|-------------------------------------------------------------------|
| `Тип`                         | Должно быть `external interface`.                                |
| `Название`                    | Короткое русскоязычное название.                                 |
| `Приоритет`                   | `Must` / `Should` / `Could` / `Won't`.                            |
| `Статус`                      | `Draft` / `Active` / `Recovered` / `Deprecated` / `Superseded`.   |
| `Источник`                    | Документ или анкер исходного кода.                                |
| `Формулировка`                | Подробное русскоязычное описание интерфейса.                     |
| `Внешний API/интерфейс`       | Что именно экспортируется (функция/класс/concept).                |
| `Сигнатуры или contract-level описание` | Сигнатуры C++ или абстрактный контракт.                |
| `Preconditions`               | Условия, которые должен обеспечить вызывающий код.                |
| `Postconditions`              | Гарантии после успешного вызова.                                  |
| `Ошибки`                      | Способ сигнализации ошибок (исключения, return codes, ASSERT).    |
| `Compatibility notes`         | ABI/API стабильность, версии, breaking changes.                   |
| `Контекст и обоснование`      | Почему интерфейс нужен в текущем виде.                            |
| `Реализует`                   | Ссылки на `feat-*`/`fr-*`, которые интерфейс реализует.           |
| `Реализуется в`               | Ссылки на PMM anchors в `include/pmm/**`.                          |
| `Проверяется в`               | Ссылки на acceptance criteria и/или тесты.                        |
| `Примечания` *(опц.)*         | Свободные пояснения.                                              |

## Правила трассировки

- `Реализуется в` обязательно содержит ссылку на anchor публичного типа/функции.
- `Compatibility notes` обязательно для статуса `Active`/`Recovered`.

## Корректный пример

```md
## if-005

**Тип:** external interface
**Название:** Storage backend concept
**Приоритет:** Must
**Статус:** Active
**Источник:** [docs/storage_seams.md](../docs/storage_seams.md)

**Формулировка:**
PMM экспортирует concept storage backend, описывающий минимальный набор
операций (`data()`, `size()`, `expand(...)`), которые требуются менеджеру
памяти от любого storage. Heap, static и mmap backends удовлетворяют этому
concept'у и могут использоваться взаимозаменяемо.

**Внешний API/интерфейс:** C++20 concept `pmm::storage_backend`.

**Сигнатуры или contract-level описание:**
`requires (T t) { { t.data() } -> ...; { t.size() } -> ...; { t.expand(n) } -> ...; }`

**Preconditions:** backend инициализирован, объём ≥ требуемого минимума.

**Postconditions:** менеджер видит непрерывный участок памяти соответствующего
размера.

**Ошибки:** `expand` может вернуть отказ, что транслируется в ошибку аллокации.

**Compatibility notes:** изменение concept является ABI/API breaking change
для пользовательских backend'ов.

**Контекст и обоснование:**
Concept позволяет PMM работать с разными storage без условной компиляции.

**Реализует:**

- [feat-006](../04_features.md#feat-006)

**Реализуется в:**

- [pmm-heapstorage](../../include/pmm/heap_storage.h#pmm-heapstorage)
- [pmm-mmapstorage](../../include/pmm/mmap_storage.h#pmm-mmapstorage)
- [pmm-staticstorage](../../include/pmm/static_storage.h#pmm-staticstorage)

**Проверяется в:**

- [ac-004](../12_acceptance_criteria.md#ac-004)
```

## Некорректные примеры

- Отсутствует `Compatibility notes` — нельзя оценить риски изменения.
- `Сигнатуры` отсутствуют, требование описано общими словами.
