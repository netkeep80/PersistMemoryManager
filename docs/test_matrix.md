# Test Matrix

## Document status

Comprehensive test coverage map for PersistMemoryManager.
Organized by test group as defined in issue #258.

Related documents:
- [core_invariants.md](core_invariants.md) — frozen invariant set
- [verify_repair_contract.md](verify_repair_contract.md) — operational boundary
- [diagnostics_taxonomy.md](diagnostics_taxonomy.md) — violation types
- [validation_model.md](validation_model.md) — pointer/block validation

---

## A. Bootstrap tests

Verify that `create()` produces a correct minimal image.

| ID  | What is verified                        | Invariant | Mode  | Test file                         |
|-----|-----------------------------------------|-----------|-------|-----------------------------------|
| A1  | Manager header valid (magic, granule)   | D1a       | —     | test_issue241_bootstrap.cpp       |
| A2  | Free-tree root exists and is non-zero   | E4a       | —     | test_issue241_bootstrap.cpp       |
| A3  | Domain registry exists                  | C2a       | —     | test_issue241_bootstrap.cpp       |
| A4  | Symbol dictionary exists                | C3a       | —     | test_issue241_bootstrap.cpp       |
| A5  | System symbol names interned            | C3a       | —     | test_issue241_bootstrap.cpp       |
| A6  | Domains discoverable by name/symbol     | C1a       | —     | test_issue241_bootstrap.cpp       |
| A7  | Bootstrap deterministic                 | D1d       | —     | test_issue241_bootstrap.cpp       |
| A8  | Bootstrap across preset configurations  | D1a–D1d   | —     | test_issue258_bootstrap.cpp       |
| A9  | Memory stats consistent after create    | D1b       | —     | test_issue258_bootstrap.cpp       |

---

## B. Reload / relocation tests

Verify save/load round-trip, relocation, and root binding survival.

| ID  | What is verified                         | Invariant  | Mode       | Test file                          |
|-----|------------------------------------------|------------|------------|------------------------------------|
| B1  | Basic round-trip (stats preserved)       | D2a        | load       | test_persistence.cpp               |
| B2  | User data preserved after reload         | A2, D2a    | load       | test_persistence.cpp               |
| B3  | Allocate/deallocate after reload         | D2a        | load       | test_persistence.cpp               |
| B4  | Root bindings survive reload             | C2a        | load       | test_issue241_bootstrap.cpp        |
| B5  | Dictionary survives reload               | C3a        | load       | test_issue241_bootstrap.cpp        |
| B6  | Double save/load cycle                   | D2a        | load       | test_persistence.cpp               |
| B7  | Reload at different buffer address       | D2a        | load       | test_issue258_reload.cpp           |
| B8  | Multiple presets save/load round-trip    | D2a        | load       | test_issue258_reload.cpp           |
| B9  | User domains survive reload              | C2a        | load       | test_issue258_reload.cpp           |
| B10 | pstring/pstringview survive reload       | C3a        | load       | test_issue258_reload.cpp           |

---

## C. Structural invariant tests

Verify internal consistency of persistent data structures.

| ID  | What is verified                         | Invariant  | Mode    | Test file                            |
|-----|------------------------------------------|------------|---------|--------------------------------------|
| C1  | Linked-list topology (prev/next chain)   | B1a, B1b   | verify  | test_issue258_structural.cpp         |
| C2  | Block count consistency                  | D2a        | verify  | test_issue258_structural.cpp         |
| C3  | Free-tree AVL balance                    | E1a        | verify  | test_issue258_structural.cpp         |
| C4  | Tree ownership (root_offset match)       | B4a, B4b   | verify  | test_issue258_structural.cpp         |
| C5  | Domain membership consistency            | C1a        | verify  | test_issue258_structural.cpp         |
| C6  | Weight/state consistency for all blocks  | B3b, B4b   | verify  | test_issue258_structural.cpp         |
| C7  | No overlapping blocks                    | B1b        | verify  | test_issue258_structural.cpp         |
| C8  | Total size equals sum of blocks          | B1b        | verify  | test_issue258_structural.cpp         |

---

## D. Corruption tests

Deterministic corruption injection with known violation types.

| ID  | What is corrupted                        | Violation type              | Mode         | Test file                         |
|-----|------------------------------------------|-----------------------------|--------------|-----------------------------------|
| D1  | Block root_offset (wrong value)          | BlockStateInconsistent      | verify       | test_issue258_corruption.cpp      |
| D2  | Block prev_offset (wrong value)          | PrevOffsetMismatch          | verify       | test_issue258_corruption.cpp      |
| D3  | Block weight (free→allocated mismatch)   | BlockStateInconsistent      | verify       | test_issue258_corruption.cpp      |
| D4  | Forest registry magic                    | ForestRegistryMissing       | verify       | test_issue258_corruption.cpp      |
| D5  | System domain name corrupted             | ForestDomainMissing         | verify       | test_issue258_corruption.cpp      |
| D6  | System domain flags cleared              | ForestDomainFlagsMissing    | verify       | test_issue258_corruption.cpp      |
| D7  | Manager header magic                     | HeaderCorruption            | verify/load  | test_issue258_corruption.cpp      |
| D8  | Manager header granule_size              | HeaderCorruption            | verify       | test_issue258_corruption.cpp      |
| D9  | Manager header total_size                | HeaderCorruption            | verify       | test_issue258_corruption.cpp      |
| D10 | Multiple simultaneous corruptions        | Multiple                    | verify       | test_issue258_corruption.cpp      |
| D11 | Corruption repaired after load           | BlockStateInconsistent      | load/verify  | test_issue258_corruption.cpp      |
| D12 | Invalid pointer provenance (out-of-range)| —                           | verify       | test_issue258_corruption.cpp      |

---

## E. Verify / repair behavior tests

Verify operational contract boundaries.

| ID  | What is verified                          | Policy outcome         | Mode        | Test file                              |
|-----|-------------------------------------------|------------------------|-------------|----------------------------------------|
| E1  | Verify only reports (no modification)     | NoAction               | verify      | test_issue256_verify_repair_contract   |
| E2  | Repair records all actions                | Repaired/Rebuilt        | load        | test_issue256_verify_repair_contract   |
| E3  | Load does not mask corruption             | —                      | load        | test_issue256_verify_repair_contract   |
| E4  | Diagnostics reflect real action           | —                      | verify/load | test_issue258_verify_behavior.cpp      |
| E5  | Verify is idempotent                      | —                      | verify      | test_issue258_verify_behavior.cpp      |
| E6  | Repair is idempotent (verify clean after) | —                      | load/verify | test_issue258_verify_behavior.cpp      |
| E7  | Aborted on non-recoverable corruption     | Aborted                | load        | test_issue256_verify_repair_contract   |
| E8  | Verify after repair shows clean state     | ok=true                | verify      | test_issue258_verify_behavior.cpp      |

---

## F. Property / generative tests

Random sequences to find unexpected combinations.

| ID  | Scenario                                        | Test file                          |
|-----|-------------------------------------------------|------------------------------------|
| F1  | Random allocate/deallocate, then verify          | test_issue258_property.cpp         |
| F2  | Random alloc/dealloc + save/load round-trip      | test_issue258_property.cpp         |
| F3  | Random corruption injection + verify             | test_issue258_property.cpp         |
| F4  | Repeated verify after random operations          | test_issue258_property.cpp         |
| F5  | Alloc/dealloc mixed with container ops (pstring) | test_issue258_property.cpp         |
| F6  | Multiple reload cycles with operations between   | test_issue258_property.cpp         |

---

## Coverage summary

| Group | New tests | Existing tests enhanced |
|-------|-----------|------------------------|
| A     | 2         | 0                      |
| B     | 4         | 0                      |
| C     | 8         | 0                      |
| D     | 12        | 0                      |
| E     | 4         | 0                      |
| F     | 6         | 0                      |
| Total | 36        | 0                      |

New test files:
- `test_issue258_bootstrap.cpp` — extended bootstrap coverage
- `test_issue258_reload.cpp` — reload/relocation tests
- `test_issue258_structural.cpp` — structural invariant tests
- `test_issue258_corruption.cpp` — deterministic corruption tests
- `test_issue258_verify_behavior.cpp` — verify/repair behavior tests
- `test_issue258_property.cpp` — property/generative tests
