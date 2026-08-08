# Low-Level Validation Model

## Document status

Canonical specification for pointer and block validation in PMM.
Defines the validation layer that all raw-pointer-to-block transitions must pass through.

Related documents:

- invariant set: [core_invariants.md](core_invariants.md)
- block semantics: [block_and_treenode_semantics.md](block_and_treenode_semantics.md)
- verify/repair contract: [verify_repair_contract.md](verify_repair_contract.md)
- diagnostics taxonomy: [diagnostics_taxonomy.md](diagnostics_taxonomy.md)

---

## Motivation

PMM manages a linear persistent address space where every allocation and
deallocation passes through low-level pointer/block conversions. A corrupted
or foreign pointer that silently passes through these conversions can damage:

- the block linked list;
- the AVL free-tree;
- the forest domain registry;
- user data in adjacent blocks.

All dangerous transitions — raw pointer to block, granule index to block,
user pointer to block header — must be validated.

---

## Validation modes

PMM defines two validation levels. Both share the same checks but differ
in cost and usage context.

### Cheap (fast-path)

Used by the normal API (`allocate`, `deallocate`, `resolve`, `reallocate`).
Goal: reject obviously invalid inputs with O(1) checks, no linked-list walks.

Checks performed:

| Check | Description |
|-------|-------------|
| Null pointer | `ptr != nullptr` |
| Image membership | `ptr >= base` and `ptr < base + total_size` |
| Granule alignment | `(ptr - base) % granule_size == 0` |
| Minimum address | `ptr >= first_user_data_address` (after Block_0 + Header + Block_1) |
| Block weight | `weight != 0` for user-pointer-to-header (allocated blocks only) |
| Index range | `idx != no_block` and `idx * granule_size + sizeof(Block) <= total_size` |
| Overflow | No arithmetic overflow in offset ↔ pointer conversion |

### Full (verify-level)

Used by `verify()` and the diagnostic phase of `load()`. Walks structures.

Checks performed (in addition to cheap checks):

| Check | Description |
|-------|-------------|
| Block state consistency | `weight` and `root_offset` form a valid pair |
| prev_offset chain | Each block's `prev_offset` matches the preceding block index |
| next_offset chain | `next_offset` does not point outside image bounds |
| Block size consistency | `block_total_granules` does not exceed image boundary |
| Counter consistency | `block_count`, `free_count`, `alloc_count`, `used_size` match walk |
| Free tree consistency | `free_tree_root` consistent with presence of free blocks |
| Tree/domain integrity | `root_offset` is valid, `node_type` is a known value |

---

## Conversion paths and their validation

Every raw/user pointer → block transition is listed below with its validation.

| # | Path | Location | Cheap check | Full check |
|---|------|----------|-------------|------------|
| 1 | Granule index → `Block*` | `detail::block_at<AT>()` | `idx != no_block` (assert) | Index range via `validate_block_index()` |
| 2 | User pointer → `Block*` | `detail::header_from_ptr_t<AT>()` | null, min_addr, bounds, alignment, weight | — (already comprehensive) |
| 3 | `pptr<T>` → `T*` | `resolve<T>()` | null, initialized, bounds | — |
| 4 | `pptr<T>` → block raw ptr | `block_raw_ptr_from_pptr()` | Relies on [pptr](../include/pmm/pptr.h#pmm-pptr) validity | `validate_block_index()` |
| 5 | Raw `void*` → `pptr<T>` | `make_pptr_from_raw<T>()` | Pointer provenance (allocated by us) | — |
| 6 | Byte offset → `pptr<T>` | `pptr_from_byte_offset<T>()` | null, alignment, overflow | — |
| 7 | Granule index → `void*` | `detail::resolve_granule_ptr<AT>()` | null-index sentinel | `validate_block_index()` |
| 8 | `void*` → granule index | `detail::ptr_to_granule_idx<AT>()` | — | Alignment, range |

---

## Validation helpers

All low-level validation is centralized in `include/pmm/validation.h`:

| Helper | Purpose |
|--------|---------|
| `validate_block_index<AT>(base, total_size, idx)` | Check granule index is in-range and fits a Block header |
| `validate_user_ptr<AT>(base, total_size, ptr)` | Check user-data pointer: null, bounds, alignment, min address |
| `validate_block_header<AT>(base, total_size, blk, own_idx)` | Full header integrity: weight/root_offset pair, prev/next bounds, node_type |
| `validate_block_range<AT>(base, total_size, idx)` | Block plus its data does not exceed image boundary |

---

## Error categories

The validation layer distinguishes three categories of invalid input:

| Category | Examples | Response |
|----------|----------|----------|
| Pointer provenance | Foreign pointer, null, out of image bounds | Return `nullptr` / `false` (fast-path), report `InvalidPointer` |
| Address correctness | Misaligned, index overflow, beyond image extent | Return `nullptr` / `false` (fast-path), report `InvalidPointer` or `Overflow` |
| Header integrity | Inconsistent weight/root_offset, bad node_type, next/prev out of bounds | Report `BlockStateInconsistent` (verify/repair level) |

These categories map to the existing [PmmError](../include/pmm/types.h#pmm-pmmerror) and [ViolationType](../include/pmm/diagnostics.h#pmm-violationtype) enums:

- Pointer provenance / address correctness → `PmmError::InvalidPointer`, `PmmError::Overflow`
- Header integrity → `ViolationType::BlockStateInconsistent`

---

## Invariants enforced

The validation layer enforces the following invariants from `core_invariants.md`:

- **B1a/B1b**: `prev_offset` and `next_offset` are valid block indices or `no_block`.
- **B2**: `weight == 0 ↔ root_offset == 0` (free block), `weight > 0 ↔ root_offset == own_idx` (allocated block).
- **B3**: `node_type` is either `kNodeNormal` (0) or `kNodeReadOnly`.
- **E1**: Every block index used in AVL operations points to a valid block within the image.

---

## Out of scope

- Image recovery (handled by `load()` repair phase).
- `pjson` or upper-layer object validation.
- Thread safety (validation runs under existing lock policy).
