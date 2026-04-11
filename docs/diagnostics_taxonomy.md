# Diagnostics Taxonomy

> **Canonical invariant reference:** [core_invariants.md](core_invariants.md) §F (Verify/Repair Contract).
> **Operational contract:** [verify_repair_contract.md](verify_repair_contract.md).

This document classifies every violation type PMM can detect, defines its
severity, and specifies the allowed response in verify and repair modes.

---

## Violation classes

PMM violations fall into six structural classes. Each class has a well-defined
detection method, severity level, and repair policy.

### Class 1: Header corruption

Corruption of the image header (`ManagerHeader`) fields that anchor all
addressing. Non-recoverable — the image cannot be trusted.

| ViolationType | Trigger | Severity | Verify action | Repair action |
|---------------|---------|----------|---------------|---------------|
| `HeaderCorruption` | `magic != kMagic` | **Fatal** | `Aborted` | `Aborted` (load returns false) |
| `HeaderCorruption` | `total_size != backend.total_size()` | **Fatal** | `Aborted` | `Aborted` (load returns false) |
| `HeaderCorruption` | `granule_size != address_traits::granule_size` | **Fatal** | `Aborted` | `Aborted` (load returns false) |

**Detection:** `verify_image_unlocked()` (`verify_repair_mixin.inc:25–41`),
`load()` header validation (`persist_memory_manager.h:316–339`).

**Why non-recoverable:** these fields define the image format and addressing
scheme. If any is wrong, all block offsets and sizes become meaningless.

---

### Class 2: Block state corruption (transitional states)

A block's `weight` and `root_offset` are inconsistent, indicating a crash
during allocation or deallocation.

| ViolationType | Trigger | Severity | Verify action | Repair action |
|---------------|---------|----------|---------------|---------------|
| `BlockStateInconsistent` | `weight > 0 && root_offset != own_idx` | **Recoverable** | `NoAction` | `Repaired` |
| `BlockStateInconsistent` | `weight == 0 && root_offset != 0` | **Recoverable** | `NoAction` | `Repaired` |

**Detection:** `verify_block_states()` (`allocator_policy.h:491–503`),
`BlockStateBase::verify_state()` (`block_state.h:184–198`).

**Repair:** `BlockStateBase::recover_state()` (`block_state.h:163–172`) —
deterministic: `weight` determines the correct `root_offset`.

**DiagnosticEntry fields:**
- `block_index` — granule index of the affected block.
- `expected` — correct `root_offset` value (`own_idx` for allocated, `0` for free).
- `actual` — observed `root_offset` value.

---

### Class 3: Linked list corruption

The doubly-linked block list has a `prev_offset` that does not match the
preceding block's index. This can happen when a split or coalesce is
interrupted mid-operation.

| ViolationType | Trigger | Severity | Verify action | Repair action |
|---------------|---------|----------|---------------|---------------|
| `PrevOffsetMismatch` | `stored_prev != expected_prev` | **Recoverable** | `NoAction` | `Repaired` |

**Detection:** `verify_linked_list()` (`allocator_policy.h:413–434`).

**Repair:** `repair_linked_list()` (`allocator_policy.h:340–355`) —
deterministic: walks `next_offset` chain and sets each block's `prev_offset`
to the index of the preceding block.

**DiagnosticEntry fields:**
- `block_index` — granule index of the block with wrong `prev_offset`.
- `expected` — correct `prev_offset` (index of preceding block).
- `actual` — observed `prev_offset` value.

**Trust anchor:** `next_offset` is trusted. If `next_offset` is corrupted,
the linked list is unrecoverably broken — this is detected as an out-of-bounds
access and the walk terminates.

---

### Class 4: Counter corruption

Stored counters (`block_count`, `free_count`, `alloc_count`, `used_size`) in
the header do not match values recomputed by walking the linked list.

| ViolationType | Trigger | Severity | Verify action | Repair action |
|---------------|---------|----------|---------------|---------------|
| `CounterMismatch` | any counter differs from recomputed value | **Recoverable** | `NoAction` | `Rebuilt` |

**Detection:** `verify_counters()` (`allocator_policy.h:446–479`).

**Repair:** `recompute_counters()` (`allocator_policy.h:367–399`) —
deterministic: recomputes all four counters by walking the linked list.

**DiagnosticEntry fields:**
- `block_index` — 0 (header-level violation).
- `expected` — recomputed `block_count`.
- `actual` — stored `block_count` in header.

---

### Class 5: Free tree corruption

The AVL free tree root is inconsistent with the presence of free blocks.
After a file round-trip, AVL fields (left/right/parent/height) are not
persisted, so the free tree is always stale after loading from file.

| ViolationType | Trigger | Severity | Verify action | Repair action |
|---------------|---------|----------|---------------|---------------|
| `FreeTreeStale` | free blocks exist but `free_tree_root == no_block` | **Recoverable** | `NoAction` | `Rebuilt` |
| `FreeTreeStale` | no free blocks but `free_tree_root != no_block` | **Recoverable** | `NoAction` | `Rebuilt` |

**Detection:** `verify_free_tree()` (`allocator_policy.h:516–539`).

**Repair:** `rebuild_free_tree()` (`allocator_policy.h:303–329`) —
deterministic: resets all AVL fields and re-inserts every free block.

**DiagnosticEntry fields:**
- `block_index` — 0 (structure-level violation).
- `expected` — recomputed free block count.
- `actual` — stored `free_tree_root` value.

---

### Class 6: Forest registry corruption

The persistent forest domain registry is missing, has invalid magic/version,
or lacks required system domains or their flags.

| ViolationType | Trigger | Severity | Verify action | Repair action |
|---------------|---------|----------|---------------|---------------|
| `ForestRegistryMissing` | registry pointer null or magic/version invalid | **Conditional** | `NoAction` | `Repaired` or `Aborted` |
| `ForestDomainMissing` | required system domain not found | **Conditional** | `NoAction` | `Repaired` or `Aborted` |
| `ForestDomainFlagsMissing` | system domain lacks `kForestDomainFlagSystem` | **Recoverable** | `NoAction` | `Repaired` |

**Detection:** `verify_forest_registry_unlocked()` (`verify_repair_mixin.inc:64–95`).

**Repair:** `validate_or_bootstrap_forest_registry_unlocked()`
(`forest_domain_mixin.inc:397–451`) — attempts to bootstrap missing registry
or domains. If bootstrap fails, entries are marked `Aborted` and load fails.

**Required system domains:**
- `system/free_tree` — free block AVL tree domain.
- `system/symbols` — symbol dictionary (`pstringview`) domain.
- `system/domain_registry` — domain registry meta-domain.

**DiagnosticEntry fields:**
- `block_index` — 0 (registry-level violation).
- `expected` — expected magic (`kForestRegistryMagic`) for `ForestRegistryMissing`.
- `actual` — observed magic value.

---

## Severity levels

| Severity | Meaning | Verify behavior | Repair behavior |
|----------|---------|-----------------|-----------------|
| **Fatal** | Image format unrecognizable | Report `Aborted`, stop | Return `false`, report `Aborted` |
| **Recoverable** | Deterministic reconstruction possible | Report `NoAction` | Apply fix, report `Repaired` or `Rebuilt` |
| **Conditional** | May be recoverable via bootstrap | Report `NoAction` | Attempt bootstrap; `Repaired` on success, `Aborted` on failure |

---

## Diagnostic actions

| DiagnosticAction | Meaning | Used in verify | Used in repair |
|------------------|---------|----------------|----------------|
| `NoAction` | Violation detected, no modification | Yes | No |
| `Repaired` | Individual field corrected to deterministic value | No | Yes |
| `Rebuilt` | Entire structure reconstructed from scratch | No | Yes |
| `Aborted` | Corruption too severe, operation stopped | Yes (for fatal) | Yes (for fatal) |

---

## Diagnostic entry format

Every violation is reported as a `DiagnosticEntry` (`diagnostics.h:59–66`):

```cpp
struct DiagnosticEntry {
    ViolationType    type;        // Kind of violation (enum)
    DiagnosticAction action;      // Action taken or proposed (enum)
    std::uint64_t    block_index; // Affected block granule index (0 if N/A)
    std::uint64_t    expected;    // Expected value (interpretation depends on type)
    std::uint64_t    actual;      // Actual value found
};
```

**Aggregated result** (`VerifyResult`, `diagnostics.h:75–105`):

```cpp
struct VerifyResult {
    RecoveryMode mode;             // Verify or Repair
    bool         ok;               // true if no violations
    std::size_t  violation_count;  // Total violations (may exceed entry_count)
    DiagnosticEntry entries[64];   // Detailed entries (up to kMaxDiagnosticEntries)
    std::size_t  entry_count;      // Entries stored (≤ violation_count)
};
```

If more than 64 violations occur, `violation_count` continues to increment
but additional entries are not stored (overflow is silent but counted).

---

## Violation-to-action mapping (complete)

| ViolationType | Verify action | Load action (recoverable) | Load action (unrecoverable) |
|---------------|---------------|---------------------------|----------------------------|
| `HeaderCorruption` | `Aborted` | — | `Aborted`, return false |
| `BlockStateInconsistent` | `NoAction` | `Repaired` | — |
| `PrevOffsetMismatch` | `NoAction` | `Repaired` | — |
| `CounterMismatch` | `NoAction` | `Rebuilt` | — |
| `FreeTreeStale` | `NoAction` | `Rebuilt` | — |
| `ForestRegistryMissing` | `NoAction` | `Repaired` | `Aborted`, return false |
| `ForestDomainMissing` | `NoAction` | `Repaired` | `Aborted`, return false |
| `ForestDomainFlagsMissing` | `NoAction` | `Repaired` | — |

---

## Extension policy

New violation types for tasks 08–10 must:

1. Add a value to `ViolationType` enum in `diagnostics.h`.
2. Implement a `verify_*()` read-only detection function.
3. Implement a repair function (if recoverable) or document as fatal.
4. Add detection to `verify_image_unlocked()`.
5. Add repair to `load()` with proper `DiagnosticAction` marking.
6. Add entries to this taxonomy.
7. Add test cases covering both verify and repair paths.

---

## Related documents

- [Verify/Repair Contract](verify_repair_contract.md) — operational boundary definition
- [Core Invariants](core_invariants.md) — §F: Verify/Repair Contract invariants
- [Recovery](recovery.md) — fault scenarios and 5-phase recovery mechanism
