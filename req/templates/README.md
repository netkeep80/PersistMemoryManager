# Шаблоны требований PMM

Каталог содержит Markdown-контракты для всех типов требований, используемых в
[`req/`](../README.md). Каждый шаблон описывает обязательные поля, допустимые
значения и правила трассировки. Шаблоны являются исходной точкой и при
заведении нового требования; CI/CD-проверка
[`scripts/check-requirements-catalog.py`](../../scripts/check-requirements-catalog.py)
валидирует требования по соответствующему шаблону, поэтому отклонения от
формата приводят к падению `Docs Consistency`.

Шаблоны являются Markdown-контрактом, а не JSON Schema, но семантически
работают как schema: набор обязательных полей, допустимые enum-значения и
правила ссылок зафиксированы и обязательны.

## Список шаблонов

| Тип   | Шаблон                                              | Каталог требований                                        |
|-------|-----------------------------------------------------|------------------------------------------------------------|
| `br`  | [business_requirement.md](business_requirement.md)  | [01_business_requirements.md](../01_business_requirements.md) |
| `rule`| [business_rule.md](business_rule.md)                | [02_business_rules.md](../02_business_rules.md)            |
| `ur`  | [user_requirement.md](user_requirement.md)          | [03_user_requirements.md](../03_user_requirements.md)      |
| `feat`| [feature.md](feature.md)                            | [04_features.md](../04_features.md)                        |
| `fr`  | [functional_requirement.md](functional_requirement.md) | [05_functional_requirements.md](../05_functional_requirements.md) |
| `dr`  | [data_requirement.md](data_requirement.md)          | [06_data_requirements.md](../06_data_requirements.md)      |
| `if`  | [external_interface.md](external_interface.md)      | [07_external_interfaces.md](../07_external_interfaces.md)  |
| `qa`  | [quality_attribute.md](quality_attribute.md)        | [08_quality_attributes.md](../08_quality_attributes.md)    |
| `con` | [constraint.md](constraint.md)                      | [09_constraints.md](../09_constraints.md)                  |
| `sys` | [system_requirement.md](system_requirement.md)      | [10_system_requirements.md](../10_system_requirements.md)  |
| `asm` | [assumption.md](assumption.md)                      | [11_assumptions_dependencies.md](../11_assumptions_dependencies.md) |
| `dep` | [dependency.md](dependency.md)                      | [11_assumptions_dependencies.md](../11_assumptions_dependencies.md) |
| `ac`  | [acceptance_criterion.md](acceptance_criterion.md)  | [12_acceptance_criteria.md](../12_acceptance_criteria.md)  |

## Общие правила

### Идентификатор и заголовок

- Заголовок требования — это `## <id>` на уровне 2.
- `<id>` имеет формат `ttt-xxx` либо `ttt-<sub>-xxx`, где `ttt` — префикс типа
  из таблицы выше.
- `<id>` записан в нижнем регистре, ASCII, сегменты разделены `-`.
- `<id>` уникален в пределах своего файла; пара `(тип, номер)` уникальна в
  пределах всего каталога.

### Обязательные поля

Каждое требование содержит как минимум:

| Поле               | Назначение                                                      |
|--------------------|------------------------------------------------------------------|
| `Тип`              | Полное имя типа требования (например, `functional requirement`). |
| `Название`         | Короткий русскоязычный заголовок требования.                     |
| `Приоритет`        | Один из enum-значений `Must` / `Should` / `Could` / `Won't`.     |
| `Статус`           | Один из enum-значений `Draft` / `Active` / `Recovered` / `Deprecated` / `Superseded`. |
| `Источник`         | Документ, обзор, обсуждение или анкер, ставший основанием для требования. |
| `Формулировка`     | Подробное русскоязычное описание требования (минимум 2 строки). |
| `Контекст и обоснование` | Почему это требование существует, какую задачу решает, какие альтернативы рассматривались. |

Дополнительные обязательные поля зависят от типа требования и описаны в
конкретных шаблонах. Поля, имеющие смысл блока ссылок (`Реализует`,
`Реализуется в`, `Проверяется в`, `Проверяет`, `Связано с`, `Тесты`),
оформляются как маркированные списки Markdown-ссылок.

### Допустимые enum-значения

#### `Приоритет`

| Значение  | Семантика                                                       |
|-----------|------------------------------------------------------------------|
| `Must`    | Невыполнение блокирует выпуск или нарушает основной use case.    |
| `Should`  | Сильно желательно, но при отсутствии релиз возможен.             |
| `Could`   | Опционально, может быть отложено.                                |
| `Won't`   | Намеренно не реализуется в текущем релизе/области.               |

#### `Статус`

| Значение     | Семантика                                                       |
|--------------|------------------------------------------------------------------|
| `Draft`      | Черновое требование, ещё формируется.                            |
| `Active`     | Действующее требование с реализацией и тестами.                  |
| `Recovered`  | Требование восстановлено по существующему коду/документации.     |
| `Deprecated` | Требование снято, но сохранено для истории.                      |
| `Superseded` | Заменено другим требованием (указать ссылку в `Примечания`).     |

### Ссылки и трассировка

- Все ссылки на другие требования — Markdown-ссылки вида
  `[fr-002](05_functional_requirements.md#fr-002)`.
- Все ссылки на исходный код и тесты — Markdown-ссылки на конкретный PMM
  anchor:
  `[pmm-persistmemorymanager-load](../../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-load)`
  для шаблонов в `req/templates/` и
  `[pmm-persistmemorymanager-load](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-load)`
  для требований в `req/*.md`.
- Текстовые упоминания вида `` `include/pmm/file.h` — анкер `anchor` ``
  запрещены и блокируются CI.
- Каждое требование, кроме допустимых top-level (`br-*`), должно иметь хотя бы
  одну входящую ссылку из требования более высокого уровня или из критерия
  приёмки, и хотя бы одну исходящую ссылку (на дочернее требование, исходник
  или тест).

### Поле `Реализуется в`

Маркированный список из:

- ссылок на дочерние/реализационные требования
  (`[fr-001](05_functional_requirements.md#fr-001)`);
- ссылок на PMM anchors в `include/pmm/**`
  (`[pmm-persistmemorymanager-load](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager-load)`).

PMM anchor, упомянутый в `Реализуется в`, должен содержать обратную
`req:`-аннотацию на этот требование в исходнике.

### Поле `Проверяется в`

Маркированный список из ссылок на acceptance criteria
(`[ac-001](12_acceptance_criteria.md#ac-001)`) и/или тесты
(`[tests/test_allocate.cpp](../tests/test_allocate.cpp)`).

### Поле `Источник`

Текстовое поле, может содержать Markdown-ссылки на документы (`docs/...`,
`README.md`), discussion threads, issues. Для требований со статусом
`Recovered` источник часто ссылается на существующий обзорный документ
(`docs/architecture.md`, `docs/pmm_target_model.md`).

## Как добавлять новое требование

1. Скопировать соответствующий шаблон из `req/templates/`.
2. Получить следующий свободный номер для типа из соответствующего файла
   `req/<NN>_*.md`.
3. Заполнить все обязательные поля шаблона.
4. Добавить требование в файл рядом с близкими по смыслу.
5. Добавить ссылки в `req/13_traceability_matrix.md`, если требование является
   br/feat-уровневым или должно быть видно в матрице.
6. Если требование реализуется в исходнике — добавить или обновить PMM anchor
   с `req:` аннотацией; см. правила в
   [`scripts/check-include-anchor-comments.sh`](../../scripts/check-include-anchor-comments.sh).
7. Запустить локально:

   ```bash
   python3 scripts/check-requirements-traceability.py
   python3 scripts/check-requirements-catalog.py
   ```

8. Если CI должен пропустить требование от reverse-trace проверки (например,
   намеренно неимплементированное `Won't`/`Draft`), добавить запись в
   [`req/.catalog-allowlist.json`](../.catalog-allowlist.json) с обоснованием.

## Как добавлять новый source anchor

1. Добавить блок-комментарий PMM-формата в `.h`/`.inc`:

   ```c
   /*
   ## anchor-name
   req: fr-XXX, qa-YYY
   */
   ```

   Глубина `#` равна количеству сегментов в имени anchor, имя — slug в нижнем
   регистре, `req:` — список существующих требований.
2. Добавить ссылку на anchor в поле `Реализуется в` каждого упомянутого
   требования.
3. Запустить локально:

   ```bash
   bash scripts/check-include-anchor-comments.sh
   python3 scripts/check-requirements-catalog.py
   ```

## Как добавлять новый acceptance criterion

1. Добавить заголовок `## ac-NNN` в `req/12_acceptance_criteria.md`.
2. Заполнить шаблон `acceptance_criterion.md`.
3. В каждом проверяемом требовании добавить ссылку на новый AC в поле
   `Проверяется в`.
4. Если AC привязан к существующим тестам — указать пути к тестам в поле
   `Тесты` и убедиться, что соответствующие тесты содержат `req:` ссылку.
