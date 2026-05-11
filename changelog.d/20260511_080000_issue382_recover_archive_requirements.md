---
bump: patch
---

### Added
- Recover backlog material from deleted `docs/archive/` into the `req/`
  catalog as `Draft`/`Could`/`Recovered` entries: encryption/compression
  (`fr-036`, `fr-037`, `feat-011`, `qa-sec-001`, `asm-007`), transactions
  (`fr-038`, `feat-012`), garbage collection (`fr-039`, `feat-013`),
  shared-memory backend (`fr-040`, `feat-014`, `if-013`), and the
  byte-offset conversion public API (`fr-035`, `if-012`, `ac-013`).
- New code anchor `pmm-detail-persistmemorytypedapi-pptr_from_byte_offset`
  in `include/pmm/typed_manager_api.h` and an extended `req:` list on
  `pmm-pptr-byte_offset` in `include/pmm/pptr.h` for the recovered
  byte-offset API.
- `req:` traceability block on `tests/test_issue211_byte_offset.cpp` and
  `@see req/...` lines on six other tests to back-link verification to
  the catalog.
- Explicit `Won't`/`Deprecated` entry `feat-015` recording that the
  detailed visual demo specification from `docs/archive/demo.md`
  (memory-map view, metrics view, structure tree, scenario manager,
  FPS/memory constraints) is intentionally out of PMM scope; the
  optional demo-target and its build dependency remain represented via
  `if-011` and `dep-002`.
- Feature-level trace entries for `feat-011…feat-015` and a recovered
  byte-offset API block in `req/13_traceability_matrix.md` so the new
  requirements are surfaced in the matrix.

### Fixed
- Strengthen the per-block encryption policy in `fr-036` / `qa-sec-001`:
  require a unique nonce per encryption via `block_generation_counter`
  (recovered from the original `docs/archive/phase7_4_encryption_compression.md`
  proposal) and require authenticated encryption (AEAD AES-256-GCM, or
  AES-256-CTR with HMAC) so wrong-key / tampered-ciphertext / replayed-nonce
  load attempts are rejected through `PmmError::AuthenticationFailed`.
  Closes the CTR-mode IV-reuse gap raised in PR review.

### Changed
- Bump `scripts/source-loc-baseline.txt` 6352 → 6356 to absorb the new
  doc-only anchor lines in the regenerated single-header. Production
  code is unchanged.
- Regenerate `single_include/pmm/pmm.h` to incorporate the new anchor
  metadata.
- Refresh README badge (release-owned) to match `CMakeLists.txt`
  `6.2.1`; the docs-archive cleanup unavoidably touches `README.md`,
  so the version-consistency check is no longer skipped.

### Removed
- Delete `docs/archive/` (12 historical phase plans / target-model and
  backlog sketches, ~3 000 lines). Each file was audited; valuable
  material moved into `req/` (see Added) or recorded as out-of-scope
  in the PR body. References from `README.md`, `docs/index.md`,
  `docs/repository_shape.md`, and `docs/deletion_policy.md` are
  updated accordingly.
