---
bump: minor
---

### Added
- `pallocator<T, ManagerT>` — STL-compatible allocator for persistent address space (Issue #198, Phase 3.5). Allows using STL containers like `std::vector<T, Mgr::pallocator<T>>` with persistent memory managed by PersistMemoryManager.
