# Verify / Repair Operational Contract

> **Canonical invariant reference:** [core_invariants.md](core_invariants.md) §F (Verify/Repair Contract).

This document defines the operational boundary between **verify**, **repair**,
and **load** in PersistMemoryManager. It serves as the binding contract for
tasks 08–10 and any future validation, corruption testing, or storage seam work.

---

## Roles and boundaries

PMM has three distinct operational modes that touch structural integrity.
Their responsibilities do not overlap.

| Operation | Entry point | Lock | Modifies image | Reports diagnostics |
|-----------|-------------|------|----------------|---------------------|
| **Verify** | `Mgr::verify()` | shared (read) | **No** | `VerifyResult` with `NoAction` entries |
| **Repair** | `Mgr::load(VerifyResult&)` | unique (write) | **Yes** | `VerifyResult` with `Repaired` / `Rebuilt` / `Aborted` entries |
| **Load** | `Mgr::load(VerifyResult&)` | unique (write) | **Yes** (repair phase) | Same `VerifyResult` as repair |

**Key principle:** repair is never hidden. Every structural modification during
`load()` is recorded in `VerifyResult` with an explicit `DiagnosticAction`.

---

## Verify contract

### What verify does

1. Acquires a **shared read lock** — no concurrent writes allowed during verify.
2. Performs **read-only** checks on the loaded image:
   - Header validation (magic, total_size, granule_size).
   - Block state consistency (weight/root_offset invariants).
   - Linked list prev_offset correctness.
   - Counter consistency (block_count, free_count, alloc_count, used_size).
   - Free tree root consistency (root presence vs. free block count).
   - Forest registry and system domain presence/flags.
3. Populates `VerifyResult` with `DiagnosticAction::NoAction` for all findings.
4. Returns `VerifyResult` with `ok == true` if no violations found.

### What verify does NOT do

- **Never modifies** any byte of the image.
- **Never repairs** any structural inconsistency.
- **Never substitutes** for load recovery — verify on an unloaded image reports
  `HeaderCorruption` / `Aborted`, it does not attempt to load.

### Implementation

```
verify() → shared_lock → verify_image_unlocked(result) → return result
```

Code: `persist_memory_manager.h:1084–1095`, `verify_repair_mixin.inc:17–57`.

### Test coverage

- `test_issue245_verify_repair.cpp` — "clean image has no violations"
- `test_issue245_verify_repair.cpp` — "verify detects block state inconsistency"
- `test_issue245_verify_repair.cpp` — "verify does not modify the image"
- `test_issue245_verify_repair.cpp` — "verify detects prev_offset mismatch"
- `test_issue245_verify_repair.cpp` — "verify detects forest registry corruption"
- `test_issue245_verify_repair.cpp` — "verify detects missing system domain"
- `test_issue245_verify_repair.cpp` — "verify detects missing system domain flags"
- `test_issue256_verify_repair_contract.cpp` — "verify and load roles do not overlap"
- `test_issue256_verify_repair_contract.cpp` — "load does not run on verify path"
- `test_issue256_verify_repair_contract.cpp` — "verify with bad total_size stops after HeaderCorruption"
- `test_issue256_verify_repair_contract.cpp` — "verify with bad granule_size stops after HeaderCorruption"

---

## Repair contract

### What repair does

Repair is the structural modification phase of `load()`. It is **never** a
standalone operation — it always runs as part of `load(VerifyResult&)`.

1. Acquires a **unique write lock**.
2. **Detects** all violations by running verify functions on the raw image.
3. **Marks** each detected violation with the action that will be applied:
   - `Repaired` — field-level fix (block state, prev_offset).
   - `Rebuilt` — structure rebuilt from scratch (counters, free tree).
4. **Applies** all fixes in a deterministic order.
5. Records every fix in `VerifyResult` — no silent modifications.

### Repair phases (deterministic order)

| Phase | Verify function | Repair function | Action |
|-------|----------------|-----------------|--------|
| 1 | `verify_block_states()` | `recover_state()` (via `rebuild_free_tree()`) | `Repaired` |
| 2 | `verify_linked_list()` | `repair_linked_list()` | `Repaired` |
| 3 | `verify_counters()` | `recompute_counters()` | `Rebuilt` |
| 4 | `verify_free_tree()` | `rebuild_free_tree()` | `Rebuilt` |
| 5 | `verify_forest_registry_unlocked()` | `validate_or_bootstrap_forest_registry_unlocked()` | `Repaired` |

### Scope of allowed reconstruction

| What can be repaired | Method | Rationale |
|---------------------|--------|-----------|
| Block weight/root_offset mismatch | `recover_state()` | Deterministic: weight determines correct root_offset |
| Linked list prev_offset | `repair_linked_list()` | Deterministic: prev_offset derived from next_offset chain |
| Counters (block_count, free_count, alloc_count, used_size) | `recompute_counters()` | Deterministic: recomputed by walking linked list |
| Free tree (AVL structure) | `rebuild_free_tree()` | Deterministic: rebuilt from scratch using linked list |
| Forest registry (missing or invalid) | `validate_or_bootstrap_forest_registry_unlocked()` | Bootstrap if missing; abort if unrecoverable |

### What repair does NOT do

- **Never repairs** corrupted `next_offset` chain — this is the trust anchor.
- **Never repairs** corrupted magic, unsupported image_version, granule_size, or total_size — these are
  non-recoverable and cause `Aborted`.
- **Never repairs** user data inside allocated blocks.
- **Never repairs** user-level AVL trees (`pmap`, `pstringview`).
- **Never runs** outside of `load()` — no "quiet repair" on normal paths.

### Test coverage

- `test_issue245_verify_repair.cpp` — "load(VerifyResult&) reports repairs directly"
- `test_issue245_verify_repair.cpp` — "Repaired vs Rebuilt distinction"
- `test_issue245_verify_repair.cpp` — "Aborted action on header corruption"
- `test_issue256_verify_repair_contract.cpp` — "every repair action is recorded"
- `test_issue256_verify_repair_contract.cpp` — "repair scope is bounded"

---

## Load contract

### What load does

`load(VerifyResult&)` is the sole entry point for restoring a persisted image.
It combines verification and repair in a single atomic operation under a write
lock. The `VerifyResult` parameter receives a complete audit trail.

### What load does NOT do

- **No hidden repair** — every structural modification is recorded in
  `VerifyResult` with an explicit `DiagnosticAction`.
- **No silent normalization** — runtime fields (`owns_memory`,
  `prev_total_size`) are reset to safe defaults, but these are runtime-only
  fields that are never persisted.
- **No masking of corruption** — on non-recoverable corruption (header),
  load returns `false` with `DiagnosticAction::Aborted`.

### Load via file I/O

`load_manager_from_file<Mgr>(filename, result)` adds CRC32 verification
before calling `Mgr::load(result)`. CRC mismatch is rejected before any
structural check runs.

Code: `io.h:164`.

---

## Non-recoverable conditions (hard stop)

These violations cause `load()` to return `false` with `Aborted` action:

| Condition | Detection | Why non-recoverable |
|-----------|-----------|---------------------|
| Invalid magic | `hdr->magic != kMagic` | Cannot identify the image as PMM |
| Size mismatch | `hdr->total_size != backend.total_size()` | Cannot trust block boundaries |
| Granule mismatch | `hdr->granule_size != address_traits::granule_size` | All offsets become meaningless |
| CRC32 mismatch | `stored_crc != computed_crc` (file I/O path) | Raw data corruption detected |
| Forest registry unrecoverable | `validate_or_bootstrap_forest_registry_unlocked()` fails | Bootstrap structure cannot be restored |

---

## Summary of guarantees

1. **Verify, repair, and load have non-overlapping roles.**
   Verify reads. Repair (within load) writes. Load orchestrates both.

2. **No hidden repair on normal paths.**
   `allocate()`, `deallocate()`, `reallocate_typed()`, `save_manager()` — none
   of these perform structural repair. Repair happens only in `load()`.

3. **Every violation class has a clear policy** (see
   [diagnostics_taxonomy.md](diagnostics_taxonomy.md)).

4. **Diagnostics are structured and uniform.**
   Every violation is reported as a `DiagnosticEntry` with type, affected block,
   expected/actual values, and action.

5. **Tasks 08–10 can rely on this contract.**
   The verify/repair boundary is frozen. New violation types or repair policies
   must be added to `ViolationType` enum and documented in
   [diagnostics_taxonomy.md](diagnostics_taxonomy.md).

---

## Related documents

- [Diagnostics Taxonomy](diagnostics_taxonomy.md) — violation types, severity, repair policy
- [Core Invariants](core_invariants.md) — §F: Verify/Repair Contract invariants
- [Recovery](recovery.md) — fault scenarios and 5-phase recovery mechanism
- [Architecture](architecture.md) — layer stack and storage backends
