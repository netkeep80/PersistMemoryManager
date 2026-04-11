# Block and TreeNode Semantics

## Статус документа

Этот документ фиксирует **каноническую семантику полей `Block<AddressTraitsT>` и
`TreeNode<AddressTraitsT>`** для `PersistMemoryManager`.

Это документ уровня **storage kernel / AVL-forest substrate**. Он определяет,
как следует понимать поля блока и встроенного intrusive tree-slot,
не смешивая этот уровень с `pjson`, `pjson_db` или `pjsonAVM`.

Документ описывает **каноническую модель**, а не требует немедленного изменения кода.

Связанные документы:

- **frozen invariant set** (canonical): [core_invariants.md](core_invariants.md)
- общая forest-модель PMM: [pmm_avl_forest.md](pmm_avl_forest.md)
- free-tree forest-policy: [free_tree_forest_policy.md](free_tree_forest_policy.md)
- низкоуровневый layout и алгоритмы: [architecture.md](architecture.md)
- фактические объявления: [../include/pmm/block.h](../include/pmm/block.h), [../include/pmm/tree_node.h](../include/pmm/tree_node.h)

## Что этот документ фиксирует

Документ задаёт:

- точную семантику `prev_offset` и `next_offset`;
- точную семантику `left_offset`, `right_offset`, `parent_offset`, `avl_height`;
- каноническую трактовку `weight` как универсального `index_type` key/scalar;
- каноническую трактовку `root_offset` как owner-domain / owner-tree marker;
- каноническую трактовку `node_type` как coarse-grained type/subtype/mode layer;
- ограничение **one intrusive tree-slot per block**.

Документ **не** задаёт:

- новую state machine;
- forest registry implementation;
- layout `pjson` или прикладную семантику `pjson_db`.

## 1. Базовое разбиение на два слоя

Канонически `Block` состоит из двух смысловых слоёв:

1. **Линейный слой ПАП**.
   Это физический порядок блоков в persistent address space.
2. **Intrusive forest-slot**.
   Это один встроенный AVL-slot, через который блок может принадлежать одному forest-домену.

Отсюда следует базовое правило:

- поля `Block` описывают **физическую линейную топологию ПАП**;
- поля `TreeNode` описывают **участие блока в одном текущем intrusive AVL-дереве**.

Эти два слоя сосуществуют одновременно и не должны подменять друг друга.

## 2. Каноническая семантика `Block`

`Block` отвечает за физический, а не логический порядок памяти.

### `prev_offset`

`prev_offset` — гранульный индекс **предыдущего физического блока** в линейном ПАП.

Это поле используется для:

- линейного обхода памяти;
- восстановления связности;
- coalesce;
- диагностики целостности.

Канонический запрет:

`prev_offset` нельзя перегружать как:

- логического родителя forest-узла;
- type link;
- metadata link;
- owner-domain link;
- ссылку прикладного объекта.

Причина простая: `prev_offset` обязан оставаться частью физической топологии ПАП.

### `next_offset`

`next_offset` — гранульный индекс **следующего физического блока** в линейном ПАП.

Это поле используется для:

- линейного обхода памяти;
- вычисления физического диапазона блока;
- split;
- coalesce;
- rebuild и recovery-проходов.

Канонический запрет:

`next_offset` нельзя перегружать как:

- tree link;
- domain link;
- type link;
- служебный pointer на внешний каталог;
- прикладную ссылку.

`next_offset` несёт физическую семантику, и allocator/recovery полагаются именно на неё.

### Почему поля `Block` нельзя перегружать forest/meta/type-смыслами

`prev_offset` и `next_offset` задают **материальный линейный скелет ПАП**.
Если перегружать их логическими значениями, ломаются:

- вычисление физической длины блока;
- split/coalesce;
- repair linked list;
- проверка корректности линейного адресного пространства.

Следовательно, `Block`-поля являются **неприкосновенным physical layer** PMM.

## 3. Каноническая семантика `TreeNode`

`TreeNode` — это **один встроенный intrusive AVL-slot** текущего forest-домена.

Общее правило для всех его полей:

- они относятся к **текущему дереву / текущему домену-владельцу**;
- они не описывают линейное соседство блоков;
- они не являются автоматически прикладными object links.

### `left_offset`

`left_offset` — гранульный индекс **левого потомка в текущем AVL-дереве**.

Это поле:

- интерпретируется только внутри текущего tree-slot;
- не имеет отношения к физическому положению блока в ПАП;
- не является ссылкой на “левый” прикладной объект.

### `right_offset`

`right_offset` — гранульный индекс **правого потомка в текущем AVL-дереве**.

Это поле:

- интерпретируется только внутри текущего tree-slot;
- не имеет отношения к физическому положению блока в ПАП;
- не является ссылкой на “правый” прикладной объект.

### `parent_offset`

`parent_offset` — гранульный индекс **родителя в текущем AVL-дереве**.

Канонически это именно **структурный AVL parent-link**, а не:

- owner-domain marker;
- type owner;
- object parent;
- путь к registry или catalog.

Если нужен marker того, **какому домену принадлежит встроенный slot**, для этого
служит `root_offset`, а не `parent_offset`.

### `avl_height`

`avl_height` — **структурная высота AVL-поддерева** в текущем встроенном slot.

Каноническая трактовка:

- `0` означает, что встроенный slot сейчас **не участвует** в дереве;
- положительное значение означает, что slot структурно активен в текущем AVL-поддереве.

`avl_height` нельзя трактовать как:

- прикладной version;
- priority;
- type id;
- generation counter;
- domain code.

Это чисто структурное поле балансировки.

### `weight`

`weight` — это **универсальный ключ/скаляр типа `index_type`** для текущего tree-slot.

Канонически это означает:

- `weight` не привязан навсегда к allocator-у;
- `weight` не обязан означать только размер блока;
- `weight` интерпретируется **доменом-владельцем дерева**.

`weight` может означать, например:

- **granule count**;
- **granule index**;
- другой scalar в пространстве `index_type`, значимый для данного forest-домена.

Ключевой принцип:

**семантика `weight` определяется деревом/доменом, а free-tree является лишь частным случаем.**

#### Что это означает для PMM

В канонической модели `weight` следует понимать как **universal granule-key / granule-scalar field**.

Для систем allocator-а это может быть размерная семантика.
Для system/user forest-доменов это может быть индексная семантика.

Именно поэтому `weight` нельзя документировать только как “allocator size field”.

#### Free-tree domain: `weight` as state discriminator

In the free-tree domain, `weight` serves a specific role:

- `weight == 0` — block is free (part of the canonical `is_free()` check).
- `weight > 0` — block is allocated (data granules count).

The free-tree **does not use `weight` as its sort key**. Instead, the free-tree
derives its ordering key from linear PAP geometry: `block_size = next_offset - block_index`.

This is a deliberate forest-policy choice, not an inconsistency:

1. `weight == 0` provides O(1) free/allocated discrimination without extra fields.
2. Block size is always recoverable from the linear PAP chain.
3. The derived key is always consistent with the physical layout after split/coalesce.

The free-tree's use of `weight` as a state discriminator rather than a sort key
is consistent with the general forest model: each domain determines its own
semantics for `weight`. The free-tree domain simply interprets `weight` differently
from user domains that use it as an actual sort key.

See [free_tree_forest_policy.md](free_tree_forest_policy.md) for the full ordering policy.

### `root_offset`

`root_offset` — это **owner-domain / owner-tree marker** встроенного tree-slot.

Канонически это означает:

- поле показывает, какому дереву или домену принадлежит текущий intrusive slot;
- это не “просто техническое поле allocator-а”;
- через него tree-slot получает семантическую принадлежность.

Минимальная allocator-совместимая трактовка уже есть и остаётся допустимой:

- `0` — free-space domain / free-tree;
- `own_idx` — self-owned slot обычного выделенного блока.

Но это только **минимальная историческая кодировка**, а не предел смысла поля.

При развитии forest-domain model именно `root_offset` должен быть опорой для:

- owner-tree identity;
- owner-domain identity;
- привязки блока к системному или пользовательскому forest-домену.

#### Важное различие

Не следует путать:

- `TreeNode::root_offset` — marker домена/дерева встроенного slot;
- `ManagerHeader::root_offset` — корневой persistent object manager-а.

Это разные поля с разной ролью.

### `node_type`

`node_type` — это **coarse-grained type/subtype/mode marker** блока.

Канонически поле должно пониматься так:

- оно задаёт низкоуровневый типовой или режимный класс блока;
- оно может кодировать subtype, mode или compact type code;
- оно не обязано ограничиваться только read-write / read-only.

При этом `node_type` **не** является:

- полным schema descriptor;
- JSON type tag;
- `pjson_db` value kind;
- заменой типовой системы верхних слоёв.

Это именно coarse PMM-level marker.

#### Текущий код

Сейчас в коде `node_type` практически используется как mode-layer:

- `kNodeReadWrite`;
- `kNodeReadOnly`.

Это следует считать **текущим частным случаем** канонической модели, а не исчерпывающим смыслом поля.

## 4. Пустой и активный встроенный tree-slot

Для встроенного tree-slot полезно различать два состояния:

- **активный slot** — блок участвует в текущем AVL-дереве;
- **неактивный slot** — встроенный slot временно не участвует в дереве.

Канонически:

- `left_offset`, `right_offset`, `parent_offset` относятся только к активному slot;
- `avl_height == 0` означает, что slot структурно неактивен;
- смысл `weight`, `root_offset`, `node_type` при этом всё равно остаётся семантикой slot-а,
  а не линейного `Block`.

## 5. One Intrusive Tree-Slot Per Block

Один `Block` содержит **ровно один встроенный intrusive tree-slot**, потому что у него есть
ровно один набор полей:

- `left_offset`;
- `right_offset`;
- `parent_offset`;
- `root_offset`;
- `avl_height`;
- `weight`;
- `node_type`.

Каноническое следствие:

**один block не может одновременно состоять в двух разных встроенных intrusive AVL-деревьях.**

Если одному payload-объекту нужны несколько независимых индексов, требуется отдельное решение:

- внешние `index-node` blocks;
- service blocks;
- отдельный multi-index layer поверх PMM.

Именно так следует понимать ограничение PMM:

- один block = один встроенный tree-slot;
- настоящий multi-index = отдельная архитектурная задача, а не “скрытая возможность” текущего header layout.

## 6. Почему отсутствие новых полей считается сознательным ограничением

Текущий layout архитектурно ценен:

- `TreeNode<DefaultAddressTraits>` = 24 байта;
- `Block<DefaultAddressTraits>` = 32 байта;
- `Block<DefaultAddressTraits>` = 2 гранулы по 16 байт.

Поэтому отсутствие новых полей считается **сознательно сохраняемым ограничением дизайна**.

При наращивании семантики надо в первую очередь использовать:

- более точную трактовку уже существующих полей;
- service blocks;
- external index-node blocks;
- registries и catalogs поверх PMM.

Расширение самого block header должно рассматриваться как крайняя мера.

## 7. Граница с верхними слоями

Этот документ намеренно не смешивает PMM с `pjson_db`.

PMM на этом уровне определяет только:

- физическую топологию ПАП;
- встроенный intrusive AVL-slot;
- coarse-grained field semantics storage-kernel уровня.

А уже верхние слои определяют:

- layout значений;
- refs;
- path traversal;
- relation/object semantics;
- execution semantics.

Следовательно:

- `node_type` не равен JSON type;
- `root_offset` не равен object path;
- `weight` не равен автоматически business key;
- `parent_offset` не равен parent-child relation верхнего уровня.

## 8. Короткая каноническая сводка

`Block::prev_offset` и `Block::next_offset`:
физические соседи в линейном ПАП, не перегружаются forest/meta/type-смыслами.

`TreeNode::left_offset`, `right_offset`, `parent_offset`:
intrusive AVL-links текущего дерева.

`TreeNode::avl_height`:
структурное состояние участия встроенного slot-а в AVL-поддереве.

`TreeNode::weight`:
универсальный `index_type` key/scalar; может быть granule count, granule index
или другим доменно-значимым scalar.

`TreeNode::root_offset`:
owner-domain / owner-tree marker встроенного slot-а.

`TreeNode::node_type`:
coarse-grained type/subtype/mode layer блока.

`Block`:
атом линейного ПАП с одним встроенным intrusive tree-slot.
