# Storage Seams

## Document status

Canonical design document for storage-layer extension points (seams) in
PersistMemoryManager. Defines where future capabilities — encryption,
compression, journaling, crash-consistency enhancements — can be introduced
without breaking the existing architecture.

This document completes the task-10 design surface by consolidating
storage-related concerns currently distributed across
[atomic_writes.md](atomic_writes.md), [recovery.md](recovery.md),
[bootstrap.md](bootstrap.md), and [architecture.md](architecture.md).

Related documents:

- mutation ordering: [mutation_ordering.md](mutation_ordering.md)
- core invariants: [core_invariants.md](core_invariants.md)
- verify/repair contract: [verify_repair_contract.md](verify_repair_contract.md)
- diagnostics taxonomy: [diagnostics_taxonomy.md](diagnostics_taxonomy.md)
- recovery: [recovery.md](recovery.md)
- atomic writes: [atomic_writes.md](atomic_writes.md)

---

## Design principles

1. **PMM remains a type-erased storage kernel.** Seams must not introduce
   `pjson` semantics, application-level transaction logic, or database
   business rules into the PMM layer.
2. **Seams are extension points, not implementations.** This document
   identifies *where* hooks can be inserted and *what contracts* they must
   satisfy. Actual encryption, compression, or journaling implementations
   are out of scope.
3. **Structural metadata must remain accessible.** Any transformation
   (encryption, compression) that hides block headers or linked-list
   pointers breaks verify, repair, and the entire recovery model. Seams
   must preserve structural recoverability.
4. **No premature abstraction.** Seam points are documented for future
   use. No new interfaces, virtual functions, or template parameters are
   introduced until a concrete implementation requires them.

---

## Operating modes

PMM images can be used in three distinct modes. Each mode has different
requirements for storage seams.

### Mode 1: Normal persistent image operation

The primary mode. A single process owns the image (in-memory via
`HeapStorage` or memory-mapped via `MMapStorage`). Mutations happen
in-place. Persistence is achieved via periodic `save_manager()` calls
or through OS-level `mmap` writeback.

**Seam requirements:**
- Per-block payload processing (encryption/compression) is feasible
  because the process has exclusive write access.
- Whole-image processing at save time is feasible.
- Online structural recovery (`load()` repair) must work on the
  in-memory image without external metadata.

### Mode 2: Backup / export mode

The image is serialized to a file via `save_manager()` for archival,
transfer, or cold storage. The file is a complete snapshot — no
incremental updates.

**Seam requirements:**
- Whole-image encryption/compression is the natural fit: process the
  entire buffer before writing to file, reverse before loading.
- CRC32 (already implemented) provides integrity verification.
- Per-block processing is possible but adds complexity for backup use.

### Mode 3: Future journal-assisted mode

A write-ahead journal records mutations before they are applied to the
main image. This enables true atomic multi-block operations and
point-in-time recovery.

**Seam requirements:**
- Journal entries must capture pre-images or logical operations.
- The journal layer sits *above* PMM — PMM provides the mutation
  ordering rules (see [mutation_ordering.md](mutation_ordering.md));
  the journal layer uses those rules to record and replay operations.
- PMM must not become a transaction engine. The journal is an external
  concern that consumes PMM's mutation stream.

---

## Seam points

### Seam 1: Whole-image processing (save/load pipeline)

**Location in pipeline:**

```
save path:  PMM image (in-memory) → [SEAM: transform] → write to file
load path:  read from file → [SEAM: reverse-transform] → PMM image (in-memory)
```

**Current implementation** (`io.h`):

```
save_manager<MgrT>(filename):
  1. Zero hdr->crc32
  2. Compute CRC32 over entire image
  3. Store CRC32 in hdr->crc32
  4. Write image to filename.tmp
  5. Atomic rename filename.tmp → filename

load_manager_from_file<MgrT>(filename):
  1. Read file into backend buffer
  2. Verify CRC32
  3. Call Mgr::load(result) — 7-phase load/repair (see mutation_ordering.md M6)
```

**Seam insertion points:**

| Point | Location | Hook | Contract |
|-------|----------|------|----------|
| S1a | After CRC32 computation, before file write | `transform_image(buffer, size)` | Must be reversible. Output size may differ from input (compression). Buffer must be self-describing (include metadata for reverse transform). |
| S1b | After file read, before CRC32 verify | `reverse_transform_image(buffer, size)` | Must restore exact original bytes. On failure: report error, do not proceed to `load()`. |

**What this enables:**
- Whole-image encryption (AES-GCM, ChaCha20-Poly1305).
- Whole-image compression (LZ4, zstd).
- Combined encrypt-then-compress or compress-then-encrypt.

**Constraints:**
- The transform must be invertible — `reverse(transform(image)) == image`.
- CRC32 must be computed on the *original* (pre-transform) image so that
  post-`load()` verification works against the structural data.
- Alternatively, CRC32 can be computed on the *transformed* image if the
  reverse transform is verified by its own integrity mechanism (e.g.,
  AEAD authentication tag).

---

### Seam 2: Per-block / payload processing

**Location in pipeline:**

```
allocate path:  user data → [SEAM: encode payload] → write to block
resolve path:   read from block → [SEAM: decode payload] → return to user
```

**Current implementation:**

Block payloads are stored as raw bytes. `resolve<T>()` returns a direct
pointer to the block's user-data region. There is no encode/decode step.

**Seam insertion points:**

| Point | Location | Hook | Contract |
|-------|----------|------|----------|
| S2a | After `allocate_typed<T>()`, before returning pointer | `encode_payload(block_ptr, size)` | Block header must remain untouched. Only user-data region may be transformed. |
| S2b | Inside `resolve<T>()`, before returning pointer | `decode_payload(block_ptr, size)` | Must return pointer to decoded data. May use a thread-local decode buffer for read-only access. |

**What this enables:**
- Per-object encryption (encrypt user data, leave headers in clear).
- Per-object compression.
- Transparent data-at-rest protection.

**Constraints:**
- **Block headers must remain in cleartext.** The linked-list pointers
  (`prev_offset`, `next_offset`), state fields (`weight`, `root_offset`),
  and AVL tree fields are required for structural recovery. Encrypting
  them would make `repair_linked_list()` and `rebuild_free_tree()`
  impossible.
- **`pstringview` blocks are permanently locked and used as AVL nodes.**
  Their tree fields and string content must remain readable for the
  symbol dictionary to function. Per-block encryption of `pstringview`
  blocks requires careful consideration — the string comparison function
  used during AVL traversal must operate on decrypted data.
- **Overhead per block:** encryption/compression adds per-block metadata
  (nonce, tag, compressed-size). This must fit within the existing
  granule-aligned block layout or require a layout extension.

---

### Seam 3: Metadata that must remain in cleartext

Regardless of what encryption or compression is applied, the following
metadata must remain accessible for structural recovery:

| Metadata | Location | Why |
|----------|----------|-----|
| `ManagerHeader` (64 bytes) | Block_0 user data | Magic validation, size checks, `first_block_offset` — required by `load()` phase 1. |
| `prev_offset` / `next_offset` | Every block header | Linked-list traversal — required by `repair_linked_list()` and `recompute_counters()`. |
| `weight` / `root_offset` | Every block header | Block state determination — required by `recover_state()` and `rebuild_free_tree()`. |
| `node_type` | Every block header | `kNodeReadOnly` check — prevents deallocation of system blocks. |
| `ForestDomainRegistry` | Allocated block pointed to by `hdr->root_offset` | Registry magic/version validation, domain enumeration. |
| `free_tree_root` | ManagerHeader | Not strictly required (rebuilt on `load()`), but useful for hot-path validation. |

**Implication:** whole-image encryption is simpler than per-block
encryption because it avoids the complexity of selectively encrypting
payloads while preserving structural headers. Per-block encryption
requires a clear boundary between "structural header" (always cleartext)
and "user payload" (encrypted).

---

### Seam 4: Online vs. backup-oriented processing

| Aspect | Online (Mode 1) | Backup (Mode 2) |
|--------|-----------------|------------------|
| Transform timing | On every `resolve()` / write | Once at save, once at load |
| Performance sensitivity | High — on hot path | Low — batch operation |
| Partial failure | Must handle per-block errors | All-or-nothing |
| Metadata visibility | Headers always in cleartext | Entire file may be encrypted |
| Recovery | `load()` repair on in-memory image | `reverse_transform()` then `load()` |

**Design choice:** backup-oriented processing (Seam 1) should be
implemented first because it has minimal impact on the hot path and
does not require changes to `resolve<T>()`. Online per-block processing
(Seam 2) is a future optimization for data-at-rest protection.

---

## Seam points for crash-consistency support

### Seam 5: Mutation journal hook

**Location:**

```
mutation path:  [SEAM: pre-mutation hook] → apply mutation → [SEAM: post-mutation hook]
```

**What this enables:**
- Write-ahead logging (WAL): record pre-images before mutations.
- Redo logging: record logical operations after mutations.
- Point-in-time recovery by replaying or undoing journal entries.

**Seam insertion points:**

| Point | When | Data available |
|-------|------|----------------|
| S5a | Before `allocate_from_block()` begins | Block index, requested size |
| S5b | Before `deallocate_block()` begins | Block index, block state |
| S5c | Before `coalesce()` begins | Block index, neighbor indices |
| S5d | Before `repair_linked_list()` | Full pre-repair image state |
| S5e | Before `rebuild_free_tree()` | Full pre-rebuild image state |

**Contract for journal hook:**
- The hook receives a description of the mutation that is about to happen.
- The hook may write to an external journal (file, memory-mapped segment).
- The hook must not modify the PMM image.
- If the hook fails (e.g., journal full), the mutation must not proceed.
- The journal is owned by the upper layer, not by PMM.

**Constraints:**
- PMM does not implement the journal. PMM provides the seam; the journal
  implementation is an upper-layer concern.
- The mutation ordering rules in [mutation_ordering.md](mutation_ordering.md)
  define the exact sequence of writes for each operation. A journal
  implementation must follow these rules to correctly record mutations.

---

### Seam 6: Snapshot hook

**Location:**

```
save path:  [SEAM: pre-snapshot hook] → save_manager() → [SEAM: post-snapshot hook]
```

**What this enables:**
- Consistent snapshots: notify upper layer before/after a full image save.
- Incremental snapshots: upper layer tracks dirty pages between snapshots.
- Journal truncation: after a successful snapshot, journal entries before
  the snapshot point can be discarded.

**Contract:**
- Pre-snapshot hook: upper layer may flush pending operations.
- Post-snapshot hook: upper layer may truncate the journal.
- Hooks must not modify the PMM image.

---

## Separation from upper layers

This section formally documents what storage seams must NOT do.

### PMM does not become a transaction engine

Storage seams provide hooks for external transaction implementations.
PMM itself does not implement:
- `begin()` / `commit()` / `rollback()` semantics.
- Multi-operation atomicity (beyond single-allocation crash consistency).
- Conflict detection or serialization guarantees beyond its lock policy.

Transaction semantics belong to upper layers (`pjson_db`, AVM) that
consume PMM's mutation stream through the journal hook (Seam 5).

### PMM does not absorb `pjson` semantics

Storage seams are type-erased. Encryption, compression, and journaling
hooks operate on raw byte buffers and block indices — they do not
interpret user-data structure or schema.

The fact that a block contains a JSON node, a pmap entry, or a
pstringview symbol is irrelevant to the storage seam layer.

### PMM does not merge with `pjson_db` or AVM

Storage seams are internal to PMM's persistence pipeline. They do not
create dependencies on:
- `pjson_db` query engine or indexing.
- AVM execution model or instruction set.
- Application-level domain objects.

PMM remains independently testable, deployable, and usable without any
upper-layer component.

---

## Impact on existing guarantees

### Verify / repair compatibility

| Seam | Impact on verify | Impact on repair |
|------|-----------------|-----------------|
| S1 (whole-image) | None — verify operates on decoded image | None — repair operates on decoded image |
| S2 (per-block) | Verify must skip encrypted payloads (header-only check) | Repair must preserve encrypted payloads during linked-list repair |
| S5 (journal) | None — journal is external | Journal may enable undo-based repair (future) |

### Pointer validation compatibility

The validation model ([validation_model.md](validation_model.md)) operates
on block headers and granule indices. All validation checks target
structural metadata that must remain in cleartext (Seam 3). Therefore:

- Cheap validation (fast-path) is unaffected by any seam.
- Full validation (verify-level) is unaffected by whole-image seams.
- Full validation may need to skip per-block payload checks if payloads
  are encrypted.

### Test matrix compatibility

Existing tests ([test_matrix.md](test_matrix.md)) operate on unencrypted,
uncompressed images. Storage seams do not invalidate existing tests.
New tests for each seam implementation should cover:

- Round-trip: `transform → save → load → reverse_transform → verify`.
- Crash simulation: interrupt at each seam point, verify recovery.
- Corruption: tamper with transformed data, verify detection.

---

## Summary of seam points

| Seam | Granularity | Pipeline stage | Enables |
|------|-------------|----------------|---------|
| S1a/S1b | Whole image | Save/load file I/O | Image encryption, image compression |
| S2a/S2b | Per block | Allocate/resolve | Payload encryption, payload compression |
| S3 | N/A (constraint) | All stages | Structural metadata always in cleartext |
| S4 | N/A (design choice) | Mode selection | Online vs. backup processing strategy |
| S5a–S5e | Per mutation | Before each critical operation | Write-ahead logging, redo logging |
| S6 | Per snapshot | Before/after save_manager | Journal truncation, incremental snapshots |

---

## Minimal contract for upper journal/snapshot layer

An upper-layer journal/snapshot implementation that consumes PMM seams
must satisfy these minimal requirements:

1. **Journal durability:** journal entries must be durable (fsync'd)
   before the corresponding PMM mutation proceeds.
2. **Ordering fidelity:** journal entries must be written in the same
   order as PMM mutation steps (see [mutation_ordering.md](mutation_ordering.md)).
3. **Snapshot consistency:** a snapshot represents a consistent PMM state
   — all mutations before the snapshot are fully applied, none after.
4. **Recovery protocol:**
   - On crash: read journal entries after the last snapshot.
   - For each incomplete mutation: either redo (if post-commit marker
     found) or undo (if no commit marker).
   - After replay: call `Mgr::load(result)` to verify structural
     consistency.
5. **Independence:** the journal must not depend on PMM internals beyond
   the documented seam hooks and mutation ordering rules. PMM may change
   internal algorithms as long as seam contracts and mutation ordering
   are preserved.
