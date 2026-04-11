# PersistMemoryManager — Documentation Index

Single entry point for all PMM documentation. Each topic is covered by exactly one canonical document.

## Canonical Documents

| Document | Role |
|----------|------|
| [PMM AVL-Forest](pmm_avl_forest.md) | Canonical architectural model: AVL-forest as first-class abstraction, forest-domains, design constraints |
| [Block and TreeNode Semantics](block_and_treenode_semantics.md) | Field-level specification of `Block` and `TreeNode` headers |
| [Core Invariants](core_invariants.md) | Frozen invariant set after issues 01–07: model boundary, block semantics, forest, bootstrap, free-tree, verify/repair |
| [Architecture](architecture.md) | Layer stack, memory layout, algorithms, storage backends, configuration |
| [API Reference](api_reference.md) | Complete public API: lifecycle, allocation, containers, I/O, error codes |
| [Bootstrap](bootstrap.md) | Deterministic 6-step initialization sequence for new PAP images |
| [Free-Tree Forest Policy](free_tree_forest_policy.md) | Free-tree ordering, weight semantics, best-fit search |
| [Recovery](recovery.md) | Fault recovery: 5-phase load sequence, block state machine, CRC32, atomic writes |
| [Verify/Repair Contract](verify_repair_contract.md) | Operational boundary between verify, repair, and load — frozen contract for tasks 08–10 |
| [Diagnostics Taxonomy](diagnostics_taxonomy.md) | Violation types, severity levels, repair policies, diagnostic entry format |
| [Atomic Writes](atomic_writes.md) | Write criticality analysis, block state transitions, interruption guarantees |
| [Thread Safety](thread_safety.md) | Lock policies, operation contracts, resolve() fast path, concurrent patterns |

## Supporting Documents

| Document | Role |
|----------|------|
| [Repository Shape](repository_shape.md) | Target directory structure and file placement rules |
| [Deletion Policy](deletion_policy.md) | Rules for file lifecycle: keep, move, archive, delete |
| [Comment Policy](comment_policy.md) | Allowed and prohibited comment types in code and docs |

## Archive

Historical, phase, and planning documents are preserved in [`archive/`](archive/) for reference.
They do not participate in the official reading path.

## Reading Order

For newcomers, the recommended path is:

1. **[PMM AVL-Forest](pmm_avl_forest.md)** — understand the core model
2. **[Core Invariants](core_invariants.md)** — frozen invariant set with traceability to code and tests
3. **[Block and TreeNode Semantics](block_and_treenode_semantics.md)** — understand the data structures
4. **[Architecture](architecture.md)** — understand the implementation layers
5. **[API Reference](api_reference.md)** — use the library
6. **[Bootstrap](bootstrap.md)** / **[Recovery](recovery.md)** — understand lifecycle guarantees
7. **[Verify/Repair Contract](verify_repair_contract.md)** / **[Diagnostics Taxonomy](diagnostics_taxonomy.md)** — understand verify/repair boundary
8. **[Thread Safety](thread_safety.md)** — for concurrent usage
