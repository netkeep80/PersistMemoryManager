# PersistMemoryManager Documentation Index

Single entry point for PMM documentation. The canonical set below must match
`repo-policy.json` `paths.canonical_docs`.

## Canonical Documents

| Document | Role |
|----------|------|
| [PMM Target Model](pmm_target_model.md) | Normative top-level model: PMM as compact persistent storage kernel; boundary vs `pjson` / `pjson_db` / execution / product layers |
| [PMM Transformation Rules](pmm_transformation_rules.md) | Normative operational rulebook: allowed issue types, atomic-issue / no-mixed-PR / extraction-first / surface-compression rules, PR review semantics |
| [Comment Policy](comment_policy.md) | Canonical text discipline for comments, docs placement, and text-surface review |
| [Block and TreeNode Semantics](block_and_treenode_semantics.md) | Field-level specification of `Block` and `TreeNode` headers |
| [Architecture](architecture.md) | Layer stack, memory layout, algorithms, storage backends, configuration |
| [API Reference](api_reference.md) | Complete public API: lifecycle, allocation, containers, I/O, error codes |
| [Validation Model](validation_model.md) | Low-level pointer and block validation: cheap vs full modes, conversion paths, error categories |
| [Atomic Writes](atomic_writes.md) | Write criticality analysis, block state transitions, interruption guarantees |
| [Thread Safety](thread_safety.md) | Lock policies, operation contracts, resolve() fast path, concurrent patterns |

## Supporting Documents

| Document | Role |
|----------|------|
| [PMM AVL-Forest](pmm_avl_forest.md) | Architectural model for AVL-forest design constraints |
| [Core Invariants](core_invariants.md) | Frozen invariant set after issues 01-07 |
| [Bootstrap](bootstrap.md) | Deterministic initialization sequence for new PAP images |
| [Free-Tree Forest Policy](free_tree_forest_policy.md) | Free-tree ordering, weight semantics, best-fit search |
| [Recovery](recovery.md) | Fault recovery sequence and block state machine |
| [Verify/Repair Contract](verify_repair_contract.md) | Operational boundary between verify, repair, and load |
| [Diagnostics Taxonomy](diagnostics_taxonomy.md) | Violation types, severity levels, repair policies |
| [Storage Seams](storage_seams.md) | Extension points for encryption, compression, journaling |
| [Mutation Ordering](mutation_ordering.md) | Write ordering rules and crash-consistency analysis |
| [Repository Shape](repository_shape.md) | Target directory structure and file placement rules |
| [Deletion Policy](deletion_policy.md) | Rules for file lifecycle: keep, move, archive, delete |
| [Code Reduction Report](code_reduction_report.md) | Supporting report on code-volume reduction |
| [Compatibility Audit](compatibility_audit.md) | Supporting compatibility inventory |
| [Internal Structure](internal_structure.md) | Supporting implementation structure notes |
| [Test Matrix](test_matrix.md) | Supporting test coverage matrix |

## Archive

Historical, phase, and planning documents are preserved in [`archive/`](archive/) for reference.
They do not participate in the official reading path.

## Reading Order

For newcomers, the recommended path is:

1. **[PMM Target Model](pmm_target_model.md)** — understand the PMM boundary
2. **[PMM Transformation Rules](pmm_transformation_rules.md)** — understand allowed changes
3. **[Block and TreeNode Semantics](block_and_treenode_semantics.md)** — understand the data structures
4. **[Architecture](architecture.md)** — understand the implementation layers
5. **[API Reference](api_reference.md)** — use the library
6. **[Validation Model](validation_model.md)** / **[Atomic Writes](atomic_writes.md)** — understand safety checks
7. **[Thread Safety](thread_safety.md)** — for concurrent usage
