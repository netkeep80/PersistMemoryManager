---
bump: major
---

### Changed
- Moved `pmap` root ownership from the local `_root_idx` field to type-scoped `container/pmap/...` forest-domain bindings.
- `pmap` objects now carry only domain identity, so independent maps and different `_K`/`_V` node layouts do not share one root accidentally.
- Replaced the compiler-specific `__PRETTY_FUNCTION__` / `__FUNCSIG__` type-signature hash in the `pmap` domain name with a deterministic fingerprint derived from `sizeof`, `alignof`, and standard `<type_traits>` category bits, plus an explicit `pmm::pmap_type_identity<T>::tag` customization point for applications that need to pin persistent type identity.

### Added
- `pmm::pmap_type_identity<T>` trait: specialize it to pin an application-controlled ASCII tag for persistent `pmap<_K, _V>` domain identity, independent of toolchain.

### Removed
- Removed `pmap::_root_idx` as a public/local root model.
- Removed compiler-specific `__PRETTY_FUNCTION__` / `__FUNCSIG__` from the persistent `pmap` domain-name derivation.
