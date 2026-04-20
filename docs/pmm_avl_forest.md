# PMM AVL-Forest

## Статус документа

Этот документ фиксирует **каноническую прикладную архитектурную модель `PersistMemoryManager`**.
Для PMM именно `AVL-forest` является **first-class abstraction**, а встроенный allocator
является только главным системным случаем её применения.

Документ задаёт язык для `README`, архитектурных заметок и последующих issue.
Низкоуровневый layout и алгоритмы описаны отдельно в [architecture.md](architecture.md).

Этот документ **не** определяет:

- layout `pjson`;
- `ref` semantics;
- path traversal уровня `pjson_db`;
- VM/execution semantics.

## Каноническая формула

**PMM = persistent address space manager**

**AVL-forest = основной индексный слой PMM**

**free-tree = главное дерево свободного ПАП**

**Block = атом линейного ПАП и атом intrusive forest**

## 1. Что такое ПАП

PMM управляет **персистентным адресным пространством (ПАП)**: линейной областью памяти,
которую можно сохранить, загрузить по другому базовому адресу и продолжить использовать
без переписывания внутренних ссылок.

Ключевая единица ПАП — **гранула**. В гранулах измеряются:

- адреса блоков;
- offsets и persistent-ссылки;
- размеры блоков;
- связи между узлами деревьев.

Именно поэтому PMM работает не с process-local raw pointers как с канонической моделью,
а с гранульной адресацией поверх линейного persistent image.

## 2. Линейный ПАП и AVL-forest существуют одновременно

PMM одновременно поддерживает два разных, но совместимых порядка:

1. **Линейный порядок ПАП**.
   Он задаётся физическим расположением блоков и связями `prev_offset` / `next_offset`.
2. **Лес AVL-деревьев**.
   Он задаётся встроенными tree-полями блока и образует системные и пользовательские индексы.

Forest не заменяет линейный ПАП. Он живёт **поверх** него.

Линейный ПАП нужен для:

- split и coalesce;
- физического обхода блоков;
- восстановления связности после `load()`;
- проверки целостности адресного пространства.

AVL-forest нужен для:

- быстрого поиска свободного пространства;
- построения системных индексов;
- построения пользовательских persistent-индексов над теми же блоками.

## 3. Почему всё измеряется в гранулах

Гранулы делают модель PMM переносимой и самосогласованной:

- image можно перемапить по другому base address;
- внутренние связи остаются валидны после reload;
- один и тот же способ адресации работает для allocator-а, контейнеров и системных индексов.

Для PMM гранула — это не просто единица выравнивания. Это базовая единица адресного и индексного
пространства.

## 4. Почему главное дерево — дерево свободных блоков

Главное дерево PMM — **free-tree**, то есть дерево свободного ПАП.

Оно является главным, потому что:

- через него allocator находит свободные диапазоны;
- оно отражает текущее состояние свободной части линейного ПАП;
- именно оно обслуживает material control над адресным пространством, а не прикладную семантику;
- от него зависит базовая способность PMM выделять, освобождать и коалесцировать блоки.

Все остальные деревья forest строятся **поверх уже существующих блоков** и не отменяют того,
что free-tree остаётся первичным системным индексом свободного пространства.

Практически это означает следующее:

- allocator-first API остаётся важным;
- но allocator больше не считается исчерпывающим объяснением архитектуры PMM;
- правильная ментальная модель — это **persistent address space manager с главным free-tree и остальными forest-domain trees**.

## 5. Почему любой блок — атом forest

`Block` является базовым атомом PMM, потому что каждый блок одновременно несёт:

- семантику линейного положения в ПАП;
- intrusive tree-slot для участия в одном из AVL-деревьев;
- payload, принадлежащий системному или пользовательскому домену.

Отсюда следует каноническая формула:

**любой блок PMM — это атом forest, а не только free-block node allocator-а**

`TreeNode` в заголовке блока следует понимать как **универсальный intrusive AVL-slot**.
В free-tree он обслуживает поиск свободных диапазонов. В остальных forest-доменах те же поля
обслуживают уже не allocator, а другой индекс над тем же persistent address space.

Это и есть смысл пары `Block` / `TreeNode`:

- `Block` связывает forest с линейным ПАП;
- `TreeNode` делает этот блок узлом одного forest-домена.

Одного встроенного tree-slot на блок достаточно для главной модели PMM. Если одному payload
нужно несколько независимых индексов, канонический путь — использовать отдельные service/index blocks,
а не превращать PMM в многоиндексный прикладной runtime.

## 6. Forest-домены: системные и пользовательские

У PMM есть **системные** и **пользовательские** forest-домены.

Уже материализовано в репозитории:

- free-tree свободных блоков;
- generic AVL-примитивы в `avl_tree_mixin.h`;
- forest-использование встроенных tree-полей в `pstringview`;
- forest-использование встроенных tree-полей в `pmap`, чей root хранится в domain binding
  `container/pmap`.

Та же модель должна использоваться и для других доменов, если они строятся на PMM:

- symbol interning;
- service catalogs;
- object/entity indexes;
- secondary indexes;
- metadata indexes.

Это не отдельные «надстройки сбоку». Это естественные forest-домены над тем же ПАП,
в котором живут их блоки.

## 7. Семантика free-tree и остальных деревьев

У деревьев в PMM разные роли:

- **free-tree** индексирует свободный ПАП и обслуживает allocator;
- **system trees** индексируют служебные persistent-структуры PMM или близких к нему подсистем;
- **user trees** индексируют прикладные объекты, которые уже размещены в ПАП.

Таким образом, free-tree отличается не тем, что он единственный AVL в системе,
а тем, что он является **главным деревом свободного адресного пространства**.

Все остальные деревья являются forest-domain indices над теми же intrusive block headers.

### 7.1. Free-tree как специализированная forest-policy

Free-tree реализован как `AvlFreeTree<AddressTraitsT>` — специализированная forest-policy,
которая использует общий AVL-substrate (shared rotations, rebalancing из `avl_tree_mixin.h`),
но имеет собственную политику ordering:

- **Sort key**: `(block_size_in_granules, block_index)` — strict total ordering.
- **Key derivation**: размер блока вычисляется из линейной геометрии ПАП
  (`next_offset - block_index`), а не из поля `weight`.
- **Роль `weight`**: state discriminator (`weight == 0` = free block), а не sort key.
- **Best-fit search**: O(log n) поиск минимального подходящего блока.

Это **осознанный выбор** forest-policy, а не отклонение от общей модели.
Каждый forest-домен определяет свою интерпретацию `weight` и свою политику ordering.
Free-tree выводит ordering key из физической геометрии,
а user-домены (pstringview, pmap) используют `weight` как sort key напрямую.

Подробная документация: [free_tree_forest_policy.md](free_tree_forest_policy.md).

## 8. PMM остаётся type-erased storage kernel

PMM должен оставаться **type-erased storage kernel**.

Это означает, что PMM знает про:

- блоки;
- гранулы и offsets;
- persistent pointers;
- линейное адресное пространство;
- intrusive AVL-индексацию;
- восстановление и базовую структурную валидацию.

Но PMM не знает и не должен знать про:

- JSON object semantics;
- JSON array semantics;
- `$ref` semantics;
- path semantics;
- relation semantics;
- execution/VM semantics.

PMM может быть фундаментом для typed и semantically rich систем, но сам по себе
остаётся нижним storage/index kernel.

## 9. Граница между PMM, pjson, pjson_db и pjsonAVM

Граница слоёв должна быть следующей:

- **PMM** управляет ПАП, блоками, offsets и intrusive AVL-forest.
- **`pjson`** задаёт модель persistent values и их layout поверх PMM.
- **`pjson_db`** задаёт object/entity semantics, каталоги, refs, path traversal и прикладные индексы.
- **`pjsonAVM`** задаёт execution semantics и runtime-модель поверх `pjson_db`.

Следствие простое:

PMM используется **ниже** `pjson_db`, а не **вместо** него.

## 10. Как PMM должен обслуживать Secure Vault и Secure Messenger

Для продуктовых режимов вроде Secure Vault и Secure Messenger PMM не должен поглощать
прикладную модель данных.

Правильный стек такой:

1. PMM даёт persistent address space, block substrate и AVL-forest.
2. `pjson_db` использует этот substrate для persistent object/value model и каталогов.
3. `pjsonAVM` использует `pjson_db` для runtime semantics, symbol resolution и execution.
4. Продуктовые режимы работают поверх этих слоёв, а не вшиваются в PMM напрямую.

Иными словами, PMM обслуживает Secure Vault / Secure Messenger **через** `pjson_db`
и `pjsonAVM`, сохраняя границу между storage-kernel и прикладной семантикой.

## 11. Что из этого следует для документации и issue

При описании PMM в репозитории следует исходить из следующих формулировок:

- PMM — это не просто allocator с AVL-деревом свободных блоков.
- AVL-forest — first-class abstraction PMM.
- free-tree — главное дерево свободного ПАП.
- любой блок — атом forest.
- `Block` и `TreeNode` нужно объяснять через forest-модель, а не только через allocator.
- верхние слои строятся поверх PMM, но не сливаются с ним.

## 12. Архитектурные ограничения

1. **Размер блока не должен увеличиваться.**
   Для `DefaultAddressTraits` блок = 32 байта (2 гранулы).
   Если нужна новая семантика, используйте переосмысление существующих полей,
   service-blocks, external index nodes или domain structures вместо расширения заголовка.

2. **Один tree-slot на блок — фундаментальное ограничение.**
   Block header содержит один intrusive AVL tree-slot.
   Для multi-index используйте отдельные service/index blocks,
   а не превращайте PMM в многоиндексный прикладной runtime.

3. **Минимальное состояние после `create()`.**
   ПАП после инициализации должен содержать как минимум:
   free-tree, forest registry, системный словарь символов.
   Подробности в [bootstrap.md](bootstrap.md).

## 13. Связанные документы

- **Frozen invariant set** (canonical, with traceability): [core_invariants.md](core_invariants.md)
- Каноническая семантика полей блока и tree-slot: [block_and_treenode_semantics.md](block_and_treenode_semantics.md)
- Free-tree forest-policy (ordering, weight role, bucketed design): [free_tree_forest_policy.md](free_tree_forest_policy.md)
- Низкоуровневая архитектура и layout: [architecture.md](architecture.md)
- API в Markdown: [api_reference.md](api_reference.md)
