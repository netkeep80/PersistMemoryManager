# PersistMemoryManager

A header-only C++20 library for persistent memory management with a static API, configurable storage backends, and thread-safety policies.

[![CI](https://github.com/netkeep80/PersistMemoryManager/actions/workflows/ci.yml/badge.svg)](https://github.com/netkeep80/PersistMemoryManager/actions/workflows/ci.yml)
[![License: Unlicense](https://img.shields.io/badge/license-Unlicense-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![Version](https://img.shields.io/badge/version-0.6.0-green.svg)](CHANGELOG.md)
[![Docs](https://img.shields.io/badge/docs-Doxygen-informational)](https://netkeep80.github.io/PersistMemoryManager/)

## Overview

PersistMemoryManager (PMM) is a block-based memory allocator that stores all metadata inside the managed region using granule offsets rather than raw pointers. This makes the heap image portable: you can save it to a file, load it at a different base address, and resume where you left off with no pointer fixups.

**Key properties:**

- **Header-only** — include and use, no compilation step
- **C++20** — uses concepts (`requires`) for policy validation
- **Static API** — no instances, all methods are `static`
- **Multitoning** — multiple independent managers via `InstanceId` template parameter
- **Configurable** — swap storage backend, lock policy, and address width independently
- **Persistent** — save/load heap image to a file; all internal links are offset-based
- **Best-fit allocation** — AVL-tree backed free block management with coalescing
- **16-byte granularity** — 4-byte `pptr<T>` indexes up to 64 GB

## Quick Start

### Option 1: Single-header (recommended)

Download one file and start using the chosen preset — no other headers needed:

```cpp
#include "pmm_single_threaded_heap.h"   // single-threaded, heap storage

using Mgr = pmm::presets::SingleThreadedHeap;

int main() {
    Mgr::create(64 * 1024);  // 64 KB heap

    Mgr::pptr<int> p = Mgr::allocate_typed<int>();
    *p = 42;
    int value = *p;  // 42

    Mgr::deallocate_typed(p);
    Mgr::destroy();
}
```

Available single-header presets:

| File | Preset | Thread Safety | Use Case |
|------|--------|--------------|----------|
| `pmm_single_threaded_heap.h` | `SingleThreadedHeap` | None | Caches, single-threaded tools |
| `pmm_multi_threaded_heap.h` | `MultiThreadedHeap` | `shared_mutex` | Concurrent services |
| `pmm_embedded_heap.h` | `EmbeddedHeap` | None | Embedded / memory-constrained |
| `pmm_industrial_db_heap.h` | `IndustrialDBHeap` | `shared_mutex` | High-load databases |

### Option 2: Multi-header

Include the modular headers from `include/pmm/`:

```cpp
#include "pmm/pmm_presets.h"

using Mgr = pmm::presets::MultiThreadedHeap;

int main() {
    Mgr::create(1024 * 1024);  // 1 MB heap

    Mgr::pptr<double> p = Mgr::allocate_typed<double>(4);  // array of 4 doubles
    (*p) = 3.14;

    Mgr::deallocate_typed(p);
    Mgr::destroy();
}
```

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

**Requirements:** CMake 3.16+, a C++20 compiler (GCC 10+, Clang 10+, MSVC 2019 16.3+).

Optional demo application (requires OpenGL + GLFW):

```bash
cmake -B build -DPMM_BUILD_DEMO=ON
cmake --build build --target pmm_demo
```

## API Reference

### Lifecycle

```cpp
// Create a new heap of initial_size bytes
static bool create(std::size_t initial_size) noexcept;

// Initialise over an already-populated backend (e.g. MMapStorage)
static bool create() noexcept;

// Load state from an existing backend image (validates magic + sizes)
static bool load() noexcept;

// Reset the manager (does not free the backend buffer)
static void destroy() noexcept;

// True if the manager has been successfully initialised
static bool is_initialized() noexcept;
```

### Allocation

```cpp
// Allocate count objects of type T; returns a null pptr on failure
template <typename T>
static pptr<T> allocate_typed(std::size_t count = 1) noexcept;

// Deallocate a block obtained from allocate_typed
template <typename T>
static void deallocate_typed(pptr<T> p) noexcept;

// Raw allocation / deallocation (size in bytes)
static void* allocate(std::size_t size) noexcept;
static void  deallocate(void* ptr) noexcept;
```

### Statistics

```cpp
static std::size_t total_size()    noexcept;  // total managed bytes
static std::size_t used_size()     noexcept;  // bytes in live allocations
static std::size_t free_size()     noexcept;  // bytes available
static double      fragmentation() noexcept;  // 0.0 – 1.0
static MemoryStats get_stats()     noexcept;  // snapshot of all counters
static ManagerInfo get_manager_info() noexcept;
```

### Diagnostics

```cpp
static bool        validate()     noexcept;  // structural integrity check
static bool        dump_stats()   noexcept;  // print stats to stdout
static void*       offset_to_ptr(std::size_t offset) noexcept;
static std::size_t block_data_size_bytes(void* ptr) noexcept;
```

### Persistence (io.h)

```cpp
#include "pmm/io.h"

// Save the managed region to a file
template <typename MgrT>
bool pmm::save_manager(const char* filename);

// Load a previously saved image into an already-allocated backend
template <typename MgrT>
bool pmm::load_manager_from_file(const char* filename);
```

Example:

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

Mgr::pptr<int> p = Mgr::allocate_typed<int>();
*p = 99;

// Save
pmm::save_manager<Mgr>("heap.dat");
Mgr::destroy();

// Restore in a new process (or after restart)
Mgr::create(64 * 1024);
pmm::load_manager_from_file<Mgr>("heap.dat");

std::cout << *p;  // 99 — value preserved
Mgr::destroy();
```

### Permanent block locking

A block can be marked read-only to prevent accidental deallocation:

```cpp
Mgr::lock_block_permanent(p);           // prevent deallocate()
bool ro = Mgr::is_permanently_locked(p);
```

## Persistent Pointer — pptr\<T\>

`pptr<T, ManagerT>` stores a 32-bit granule index (4 bytes) instead of a raw pointer. It is address-independent: the heap image can be mapped at any base address and `pptr` values remain valid.

```cpp
Mgr::pptr<int> p = Mgr::allocate_typed<int>();

if (p) {           // explicit bool conversion
    *p = 42;       // dereference via operator*
    p->field;      // field access via operator->
    p.resolve();   // explicit T* pointer
    p.offset();    // underlying granule index
    p.is_null();   // same as !p
}
```

**Prohibited operations** — pointer arithmetic (`p++`, `p--`) is deleted to enforce safe usage.

### AVL tree node methods (pptr)

`pptr` exposes the block's internal tree node fields, allowing you to build user-level AVL trees on top of PMM blocks:

```cpp
pptr get_tree_left()   const noexcept;
pptr get_tree_right()  const noexcept;
pptr get_tree_parent() const noexcept;
void set_tree_left(pptr left)     noexcept;
void set_tree_right(pptr right)   noexcept;
void set_tree_parent(pptr parent) noexcept;

index_type   get_tree_weight() const noexcept;
void         set_tree_weight(index_type w) noexcept;
std::int16_t get_tree_height() const noexcept;
void         set_tree_height(std::int16_t h) noexcept;
```

> **Warning:** `set_tree_weight()` should only be called on blocks locked with `lock_block_permanent()`.

## Configuration

### Built-in presets

```cpp
#include "pmm/pmm_presets.h"

namespace pmm::presets {
    using SingleThreadedHeap = PersistMemoryManager<CacheManagerConfig, 0>;
    using MultiThreadedHeap  = PersistMemoryManager<PersistentDataConfig, 0>;
    using EmbeddedHeap       = PersistMemoryManager<EmbeddedManagerConfig, 0>;
    using IndustrialDBHeap   = PersistMemoryManager<IndustrialDBConfig, 0>;
}
```

| Preset | Lock policy | Growth | Intended for |
|--------|-------------|--------|--------------|
| `SingleThreadedHeap` | `NoLock` | 25% | Caches, offline tools |
| `MultiThreadedHeap` | `SharedMutexLock` | 25% | Concurrent services |
| `EmbeddedHeap` | `NoLock` | 50% | Memory-constrained devices |
| `IndustrialDBHeap` | `SharedMutexLock` | 100% | High-throughput databases |

### Custom configuration

Compose a configuration struct from the available policies:

```cpp
#include "pmm/address_traits.h"
#include "pmm/config.h"
#include "pmm/heap_storage.h"
#include "pmm/mmap_storage.h"
#include "pmm/free_block_tree.h"

struct MyConfig {
    using address_traits  = pmm::DefaultAddressTraits;          // uint32_t index, 16-byte granule
    using storage_backend = pmm::MMapStorage<address_traits>;   // file-mapped persistent storage
    using free_block_tree = pmm::AvlFreeTree<address_traits>;   // AVL tree (required)
    using lock_policy     = pmm::config::SharedMutexLock;       // multi-threaded

    static constexpr std::size_t grow_numerator   = 3;  // grow by 50% on expansion
    static constexpr std::size_t grow_denominator = 2;
};

using MyMgr = pmm::PersistMemoryManager<MyConfig, 0>;
```

### Multitoning — multiple independent instances

The `InstanceId` template parameter creates separate static state for each value:

```cpp
using Cache0 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 0>;
using Cache1 = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 1>;

Cache0::create(64 * 1024);
Cache1::create(32 * 1024);

Cache0::pptr<int> p0 = Cache0::allocate_typed<int>();
Cache1::pptr<int> p1 = Cache1::allocate_typed<int>();
// p0 and p1 are incompatible types — mixing them is a compile error
```

### Address traits

| Type | Index | Granule | Max addressable |
|------|-------|---------|-----------------|
| `TinyAddressTraits` | `uint8_t` | 8 B | 2 KB |
| `SmallAddressTraits` | `uint16_t` | 16 B | 1 MB |
| `DefaultAddressTraits` | `uint32_t` | 16 B | 64 GB |
| `LargeAddressTraits` | `uint64_t` | 64 B | huge |

### Storage backends

| Class | Description |
|-------|-------------|
| `HeapStorage<A>` | Dynamic allocation via `malloc` / `realloc` |
| `MMapStorage<A>` | File-mapped memory (`mmap` / `MapViewOfFile`) — persistent across restarts |
| `StaticStorage<Size>` | Fixed-size static array — no dynamic allocation, suitable for embedded |

### Lock policies

| Policy | Description |
|--------|-------------|
| `config::NoLock` | No synchronisation — use in single-threaded code |
| `config::SharedMutexLock` | `std::shared_mutex` — concurrent reads, exclusive writes |

## C++20 Concepts

PMM provides concepts for compile-time validation of custom types:

```cpp
#include "pmm/manager_concept.h"
#include "pmm/storage_backend.h"

// Check that a type satisfies the manager interface
static_assert(pmm::PersistMemoryManagerConcept<MyMgr>);

// Use in a template constraint
template <pmm::PersistMemoryManagerConcept MgrT>
void process_heap() { /* ... */ }

// Validate a custom storage backend
static_assert(pmm::StorageBackendConcept<MyStorage>);
```

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                     Public API                       │
│  create / load / destroy / allocate / deallocate    │
├─────────────────────────────────────────────────────┤
│              AllocatorPolicy                         │
│  best-fit search · block splitting · coalescing     │
│  auto-expansion · AVL free-tree rebalancing         │
├─────────────────────────────────────────────────────┤
│               Raw Memory Layer                       │
│  StorageBackend → contiguous byte buffer            │
│  BlockHeader (LinkedList + AVL node, 32 bytes)      │
│  ManagerHeader (magic, sizes, counters, root ptr)   │
└─────────────────────────────────────────────────────┘
```

**Memory layout inside the managed region:**

```
[ManagerHeader][BlockHeader_0][data_0][BlockHeader_1][data_1] ...
```

- `ManagerHeader` is stored at offset 0 inside the managed region
- Every block carries a 32-byte header (LinkedListNode + TreeNode)
- All cross-block references are granule indices (offsets), never raw pointers
- On `load()` the linked list is repaired and the AVL free-tree is rebuilt from the block chain

## Performance

Measured on a single core (Release build, Linux x86-64, GCC 13):

| Operation | Count | Time |
|-----------|-------|------|
| `allocate` | 100 000 | ~7 ms |
| `deallocate` | 100 000 | ~0.8 ms |
| mixed alloc/dealloc | 1 000 000 | ~14 ms (~14 ns/op) |

## Repository Structure

```
PersistMemoryManager/
├── include/
│   ├── pmm/                        # Modular headers
│   │   ├── persist_memory_manager.h  # Main manager class
│   │   ├── pptr.h                    # Persistent pointer
│   │   ├── pmm_presets.h             # Ready-made aliases
│   │   ├── manager_configs.h         # Config structs
│   │   ├── address_traits.h          # Address space traits
│   │   ├── config.h                  # Lock policies
│   │   ├── heap_storage.h            # malloc-based backend
│   │   ├── mmap_storage.h            # file-mapped backend
│   │   ├── static_storage.h          # static-array backend
│   │   ├── storage_backend.h         # StorageBackend concept
│   │   ├── allocator_policy.h        # Alloc/dealloc algorithms
│   │   ├── block_state.h             # Block state machine
│   │   ├── free_block_tree.h         # AVL tree policy
│   │   ├── types.h                   # ManagerInfo, MemoryStats
│   │   ├── io.h                      # save/load utilities
│   │   └── manager_concept.h         # C++20 concepts
│   └── manager_concept.h             # C++20 concepts (continued)
├── single_include/                   # Self-contained single-header presets (Issue #138)
│   └── pmm/
│       ├── pmm_single_threaded_heap.h    # Single-header preset
│       ├── pmm_multi_threaded_heap.h     # Single-header preset
│       ├── pmm_embedded_heap.h           # Single-header preset
│       └── pmm_industrial_db_heap.h      # Single-header preset
├── examples/                         # Usage examples
├── tests/                            # 40+ test files
├── demo/                             # Visual ImGui/OpenGL demo
├── docs/                             # Architecture, API docs
├── scripts/                          # Release helpers
└── CMakeLists.txt
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the full workflow. Key points:

- C++20 required; targets GCC 10+, Clang 10+, MSVC 2019 16.3+
- Run `pre-commit install` to enable local quality gates (clang-format, cppcheck, secrets scan)
- Add a [changelog fragment](changelog.d/README.md) to `changelog.d/` for every PR that touches source code
- File size limit: 1500 lines per source file
- All new features must include tests

```bash
# Format code
clang-format -i include/pmm/your_file.h

# Static analysis
cppcheck --std=c++20 include/

# Run tests
cmake -B build && cmake --build build && ctest --test-dir build
```

## Documentation

- [API Reference (Doxygen)](https://netkeep80.github.io/PersistMemoryManager/)
- [Architecture](docs/architecture.md)
- [API Reference (Markdown)](docs/api_reference.md)
- [Performance](docs/performance.md)
- [Changelog](CHANGELOG.md)

## License

This is free and unencumbered software released into the public domain. See [LICENSE](LICENSE) for details.
