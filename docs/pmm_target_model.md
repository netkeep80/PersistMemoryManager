# PMM Target Model

## Document status

This is the **canonical top-level document** that fixes the target model of
`PersistMemoryManager`. It is normative: subsequent issues, repo-guard policy,
transformation rules, and review criteria must be consistent with it.

It does not duplicate lower-level documents. Architectural detail lives in
[pmm_avl_forest.md](pmm_avl_forest.md) and [architecture.md](architecture.md);
invariants live in [core_invariants.md](core_invariants.md); repository shape
lives in [repository_shape.md](repository_shape.md).

## 1. Canonical definition

**PMM = compact persistent storage kernel.**

PMM manages a persistent address space and the intrusive substrate over it.
Everything above that substrate belongs to upper layers.

## 2. Core responsibility

PMM owns, and only owns:

- the persistent address space (PAP) and its lifecycle;
- the allocator over PAP;
- offsets instead of raw pointers as the canonical reference form;
- block headers as the atom of PAP and of the intrusive forest;
- the intrusive persistent index / forest substrate (AVL-forest, free-tree);
- `create` / `load` / `destroy` lifecycle;
- `verify` / `repair` discipline over its own structures;
- the root / domain registry;
- storage hooks for encryption, compression, and journaling seams.

## 3. What PMM is not

PMM is not, and must not become:

- `pjson` — PMM does not interpret payload schema;
- `pjson_db` — PMM is not a database engine;
- an execution engine — no VM, no query evaluator;
- a product / application layer;
- a sync, replication, or business-logic layer.

Any semantics that require knowledge of upper-layer schema belong above PMM,
not inside it.

## 4. Allowed directions of evolution

PMM may grow only along these axes:

- **kernel hardening** — stronger invariants, validation, recovery;
- **kernel compaction** — fewer files, fewer primitives, fewer code paths;
- **extraction prep** — clean seams toward future separation of concerns;
- **intrusive index formalization** — explicit contracts for forest domains;
- **validation / invariants / recovery strengthening** — crash-consistency
  and verify/repair discipline.

Every change should be traceable to one of these axes.

## 5. Anti-goals

The following are explicit anti-goals:

- accumulation of upper-layer semantics inside PMM;
- growth of convenience surface without necessity;
- mixed-purpose PRs combining hardening, refactoring, docs, generated
  surface, and governance in one change;
- growth of docs and comments without corresponding canonical reduction;
- mixing generated surface (e.g. `single_include/`) with core changes.

## 6. Success criterion

A series of merges is considered successful if it makes PMM **stronger as a
kernel and smaller as a repository surface**.

Concretely, over time:

- net repository surface should trend down, not up;
- invariants, verify / repair, and recovery should get sharper;
- low-level helper sprawl should shrink;
- canonical documents should consolidate, not multiply.

Growth of PMM as a product is out of scope of this model.

## 7. Exit criteria

This document is considered fit for its role when:

- it is short enough to be genuinely normative;
- subsequent issues can cite it by section instead of restating it;
- it does not duplicate other canonical documents verbosely;
- it draws a clear boundary between PMM and `pjson` / `pjson_db` / execution
  / product layers.

If this document starts to grow into a long explainer, that growth itself
violates the success criterion of section 6 and must be reversed.
