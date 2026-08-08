---
bump: patch
---

### Changed
- Collapsed the `forest_domain_mixin` and `verify_repair_mixin` textual include shards directly into `include/pmm/persist_memory_manager.h`, removing the two `include/pmm/*.inc` files and eliminating forbidden textual-include indirection from the kernel header. No public API, persistent-image layout, allocator, registry, or verify/repair behavior changes.
