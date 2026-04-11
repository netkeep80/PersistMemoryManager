# Bootstrap Sequence for PAP Image

> **Canonical invariant reference:** [core_invariants.md](core_invariants.md) §D (Bootstrap Model).

## Overview

When a new Persistent Address Space (PAS/PAP) image is created via `create()`,
PMM performs a **canonical bootstrap sequence** that produces a minimal
self-describing persistent environment. After bootstrap, the image contains not
just allocator structures but also a forest registry, symbol dictionary, and
registered system domains.

The bootstrap is fully **deterministic**: calling `create(N)` on two independent
managers with identical `ConfigT` and size `N` produces images with identical
block layout, binding IDs, and symbol offsets.

---

## Bootstrap Steps

### Step 1: Manager Header (Block\_0)

`init_layout()` writes the first block header at offset 0. This block "owns" the
`ManagerHeader` structure, which stores:

| Field            | Value                       |
|------------------|-----------------------------|
| `magic`          | `0x504D4D5F56303938` ("PMM\_V098") |
| `total_size`     | Backend buffer size in bytes |
| `granule_size`   | `address_traits::granule_size` |
| `free_tree_root` | Granule index of Block\_1   |
| `root_offset`    | `no_block` (no user root yet) |

### Step 2: First Free Block (Block\_1)

Block\_1 is placed immediately after Block\_0 + ManagerHeader. It spans the
remaining space and becomes the initial root of the free-tree (AVL tree of free
blocks). Its `root_offset = 0` marks it as a free block.

At this point the allocator is functional: `allocate()` can split Block\_1 to
satisfy requests.

### Step 3: Forest / Domain Registry

`bootstrap_forest_registry_unlocked()` allocates a `ForestDomainRegistry`
structure. This is a persistent, locked block containing:

- Magic: `0x50465247` ("PFRG")
- Version: 1
- Up to 32 domain slots
- Legacy root offset (0 for fresh images)

The registry's granule index is stored in `hdr->root_offset`.

### Step 4: System Domain Registration

Three system domains are registered in the forest registry:

| Domain Name              | Binding Kind       | Flags  | Purpose                    |
|--------------------------|--------------------|--------|----------------------------|
| `system/free_tree`       | `kForestBindingFreeTree` | System | Free block AVL tree        |
| `system/symbols`         | `kForestBindingDirectRoot` | System | pstringview symbol dictionary |
| `system/domain_registry` | `kForestBindingDirectRoot` | System | Self-reference to registry |

Each domain receives a unique, monotonically increasing `binding_id`.

### Step 5: Symbol Dictionary Bootstrap

`bootstrap_system_symbols_unlocked()` interns the following names into the
persistent symbol dictionary (pstringview AVL tree):

- `system/free_tree`
- `system/symbols`
- `system/domain_registry`
- `type/forest_registry`
- `type/forest_domain_record`
- `type/pstringview`
- `service/legacy_root`
- `service/domain_root`
- `service/domain_symbol`

Each interned string occupies a permanently locked block (`kNodeReadOnly`) in the
PAS. After interning, every domain's `symbol_offset` field is populated with the
pptr of its interned name.

### Step 6: Invariant Verification

`validate_bootstrap_invariants()` checks the following post-conditions:

1. Manager header has valid magic, total\_size, and granule\_size.
2. Forest registry exists with valid magic and version.
3. At least 3 domains are registered (free\_tree, symbols, registry).
4. All system domains have `kForestDomainFlagSystem` flag set.
5. All system domains have non-zero `symbol_offset` (interned name).
6. `system/free_tree` has `binding_kind == kForestBindingFreeTree`.
7. Symbol dictionary root is non-zero (bootstrap symbols exist).
8. `system/domain_registry` root matches `hdr->root_offset`.

If any check fails, `create()` returns `false`.

---

## Memory Layout After Bootstrap

For `DefaultAddressTraits` (granule\_size = 16):

```
Offset (granules)  Block       Contents
─────────────────  ──────────  ─────────────────────────────────
0                  Block_0     ManagerHeader (magic, sizes, root_offset)
4                  Block_1*    [split] ForestDomainRegistry (locked)
...                Block_N*    [split] pstringview "system/free_tree" (locked)
...                Block_M*    [split] pstringview "system/symbols" (locked)
...                ...         [split] remaining bootstrap symbols (locked)
...                Free        Remaining free space
```

\* Exact offsets depend on allocation order and block splitting.

---

## Load-time Restoration

When an existing image is loaded via `load()`:

1. Validates header magic, total\_size, granule\_size.
2. Repairs the linked list (`repair_linked_list`).
3. Recomputes block counters (`recompute_counters`).
4. Rebuilds the free-tree from scratch (`rebuild_free_tree`).
5. Validates or re-creates the forest registry
   (`validate_or_bootstrap_forest_registry_unlocked`):
   - If valid registry found: re-registers system domains, ensures symbols
     populated.
   - If legacy root found (pre-registry image): creates new registry,
     preserves legacy root.
   - If neither: creates fresh registry.
6. Runs `validate_bootstrap_invariants()` to confirm the image is
   self-consistent.

---

## System Blocks

Blocks created during bootstrap are **permanently locked** (`kNodeReadOnly`):

- ForestDomainRegistry block
- All pstringview symbol blocks

These blocks cannot be deallocated. They are part of the image's
self-describing infrastructure and persist across save/load cycles.

---

## Integrity Summary

| Invariant                          | Checked by                       |
|------------------------------------|----------------------------------|
| Header magic valid                 | `load()`, `validate_bootstrap_invariants()` |
| Granule size matches               | `load()`, `validate_bootstrap_invariants()` |
| Registry exists with valid magic   | `validate_bootstrap_invariants()` |
| 3 system domains present           | `validate_bootstrap_invariants()` |
| System domains have symbols        | `validate_bootstrap_invariants()` |
| Symbol dictionary non-empty        | `validate_bootstrap_invariants()` |
| Registry root consistent           | `validate_bootstrap_invariants()` |
