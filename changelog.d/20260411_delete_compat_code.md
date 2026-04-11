---
bump: minor
---

### Removed
- Deprecated `bytes_to_granules()`, `granules_to_bytes()`, `idx_to_byte_off()`, `byte_off_to_idx()`, `required_block_granules()` — use templated `_t` variants or `AddressTraits` methods
- Identity functions `to_u32_idx<AT>()` and `from_u32_idx<AT>()`
- Non-templated `is_valid_block()` and `block_idx()` overloads
- Deprecated `load()` no-arg overload — use `load(VerifyResult&)`
- Deprecated `load_manager_from_file<MgrT>(filename)` no-arg overload — use `load_manager_from_file<MgrT>(filename, result)`
- `FreeBlockTreePolicyConcept` and `is_free_block_tree_policy_v` — use `FreeBlockTreePolicyForTraitsConcept<P, AT>`
- `PersistentAvlTree` type alias — use `AvlFreeTree<DefaultAddressTraits>`
- CRC32 zero-value backward compatibility (images without CRC32 are no longer accepted)

### Added
- `docs/compatibility_audit.md` — audit of all compatibility paths with keep/delete decisions
