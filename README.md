# PersistMemoryManager

A header-only C++20 library for persistent memory management with a static API, configurable storage backends, and thread-safety policies.

[![CI](https://github.com/netkeep80/PersistMemoryManager/actions/workflows/ci.yml/badge.svg)](https://github.com/netkeep80/PersistMemoryManager/actions/workflows/ci.yml)
[![License: Unlicense](https://img.shields.io/badge/license-Unlicense-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![Version](https://img.shields.io/badge/version-0.13.0-green.svg)](CHANGELOG.md)
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
- **Persistent data types** — built-in `pstringview` (interned strings), `pmap<K,V>` (AVL dictionary), and `pvector<T>` (vector)

## Quick Start

### Option 1: Single-header (recommended)

Download `single_include/pmm/pmm.h` — the full library without any preset — and use any configuration:

```cpp
#include "pmm.h"
#include "pmm/pmm_presets.h"

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

Or use a preset single-header file (includes full library + preset alias in one file):

```cpp
#include "pmm_single_threaded_heap.h"

using Mgr = pmm::presets::SingleThreadedHeap;
```

Available single-header files in `single_include/pmm/`:

| File | Preset | Index | Thread Safety | Use Case |
|------|--------|-------|--------------|----------|
| `pmm.h` | *(none — full library)* | any | any | Custom configs |
| `pmm_small_embedded_static_heap.h` | `SmallEmbeddedStaticHeap<N>` | `uint16_t` (2 B) | None | ARM Cortex-M, AVR, ESP32 |
| `pmm_embedded_static_heap.h` | `EmbeddedStaticHeap<N>` | `uint32_t` (4 B) | None | Bare-metal, RTOS, no heap |
| `pmm_embedded_heap.h` | `EmbeddedHeap` | `uint32_t` (4 B) | None | Embedded with dynamic heap |
| `pmm_single_threaded_heap.h` | `SingleThreadedHeap` | `uint32_t` (4 B) | None | Caches, single-threaded tools |
| `pmm_multi_threaded_heap.h` | `MultiThreadedHeap` | `uint32_t` (4 B) | `shared_mutex` | Concurrent services |
| `pmm_industrial_db_heap.h` | `IndustrialDBHeap` | `uint32_t` (4 B) | `shared_mutex` | High-load databases |
| `pmm_large_db_heap.h` | `LargeDBHeap` | `uint64_t` (8 B) | `shared_mutex` | Petabyte-scale databases |

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

// Allocate + construct a single object of type T with given constructor arguments
// IMPORTANT: T(args...) must be noexcept — enforced via static_assert
template <typename T, typename... Args>
static pptr<T> create_typed(Args&&... args) noexcept;

// Destruct + deallocate an object created via create_typed
// IMPORTANT: ~T() must be noexcept — enforced via static_assert
template <typename T>
static void destroy_typed(pptr<T> p) noexcept;

// Raw allocation / deallocation (size in bytes)
static void* allocate(std::size_t size) noexcept;
static void  deallocate(void* ptr) noexcept;
```

**Note:** `create_typed<T>(args...)` and `destroy_typed<T>(p)` require the type `T` to have `noexcept` constructors and destructors respectively. This is enforced at compile time via `static_assert`. Using a type with a potentially-throwing constructor or destructor will result in a clear compile-time error. See [docs/phase1_safety.md](docs/phase1_safety.md) for details.

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

A block can be marked read-only to prevent accidental deallocation. This is used internally by `pstringview` to ensure interned strings are never freed:

```cpp
Mgr::lock_block_permanent(p);           // prevent deallocate()
bool ro = Mgr::is_permanently_locked(p);
```

## Persistent Pointer — pptr\<T\>

`pptr<T, ManagerT>` stores a granule index (2, 4, or 8 bytes depending on address traits) instead of a raw pointer. It is address-independent: the heap image can be mapped at any base address and `pptr` values remain valid.

```cpp
Mgr::pptr<int> p = Mgr::allocate_typed<int>();

if (p) {           // explicit bool conversion
    *p = 42;       // dereference via operator*
    p->field;      // field access via operator->
    p.offset();    // underlying granule index
    p.is_null();   // same as !p
}
```

**Prohibited operations** — pointer arithmetic (`p++`, `p--`) is deleted to enforce safe usage.

### AVL tree node access (pptr)

`pptr` provides direct access to the block's internal `TreeNode` via `tree_node()`, allowing you to build user-level AVL trees on top of PMM blocks:

```cpp
auto& tn = p.tree_node();  // reference to TreeNode in block header

// Read links (return granule index, or no_block if absent)
tn.get_left();     // index_type — left child granule index
tn.get_right();    // index_type — right child granule index
tn.get_parent();   // index_type — parent granule index
tn.get_weight();   // index_type — node weight (data size in granules)
tn.get_height();   // std::int16_t — AVL subtree height

// Write links (use offset() to convert pptr to granule index)
tn.set_left(child.offset());
tn.set_right(child.offset());
tn.set_parent(parent.offset());
tn.set_height(h);
```

> **Note:** Absent links are stored as `address_traits::no_block` sentinel, not as zero.

## Persistent String — pstringview

`pstringview<ManagerT>` is an interned, read-only persistent string. Equal strings are always stored once in the heap and return the same `pptr` — deduplication is guaranteed.

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

// Intern a string — creates it in PAP on first call
Mgr::pptr<Mgr::pstringview> p = Mgr::pstringview("hello");
if (p) {
    const char* s = p->c_str();   // "hello"
    std::size_t n = p->size();    // 5
}

// Interning the same string again returns the same pptr
Mgr::pptr<Mgr::pstringview> p2 = Mgr::pstringview("hello");
assert(p == p2);  // identical granule index

Mgr::destroy();
```

**API:**

```cpp
const char* c_str()  const noexcept;  // null-terminated string
std::size_t size()   const noexcept;  // length without null terminator
bool        empty()  const noexcept;

bool operator==(const pstringview& o) const noexcept;
bool operator!=(const pstringview& o) const noexcept;
bool operator< (const pstringview& o) const noexcept;

// Explicit interning (same as Mgr::pstringview("..."))
static Mgr::pptr<pstringview> intern(const char* s) noexcept;

// Reset interning dictionary (for test isolation)
static void reset() noexcept;
```

**Notes:**
- All `pstringview` and char blocks are permanently locked via `lock_block_permanent()` — they cannot be freed.
- Deduplication uses a built-in AVL tree whose links live in each block's `TreeNode` fields.
- Requires `persist_memory_manager.h` (auto-included via `Mgr::pstringview`).

## Persistent Map — pmap\<K, V\>

`pmap<_K, _V, ManagerT>` is a persistent AVL dictionary stored entirely inside the managed region. Key-value nodes use the built-in `TreeNode` fields for AVL links — no separate tree allocation is needed.

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

using MyMap = Mgr::pmap<int, int>;

MyMap map;
map.insert(42, 100);
map.insert(10, 200);

auto p = map.find(42);
if (!p.is_null()) {
    int val = p->value;  // 100
}

map.insert(42, 300);  // duplicate key — updates value to 300

Mgr::destroy();
```

Using `pstringview` keys:

```cpp
using StrIntMap = Mgr::pmap<Mgr::pstringview, int>;
StrIntMap dict;
auto key = static_cast<Mgr::pptr<Mgr::pstringview>>(Mgr::pstringview("hello"));
dict.insert(*key.resolve(), 42);
```

**Notes:**
- O(log n) insert, find, and contains.
- Duplicate key on `insert` updates the stored value.
- Nodes are **not** permanently locked (unlike `pstringview` — they can be freed).
- Key type `_K` must support `operator<` and `operator==`.

## Persistent Vector — pvector\<T\>

`pvector<T, ManagerT>` is a persistent sequential container stored in the managed region. Elements are linked using the built-in `TreeNode` fields to form a doubly-linked list with O(1) push_back.

```cpp
using Mgr = pmm::presets::SingleThreadedHeap;
Mgr::create(64 * 1024);

using MyVec = Mgr::pvector<int>;

MyVec vec;
vec.push_back(10);
vec.push_back(20);
vec.push_back(30);

auto p = vec.at(1);
if (!p.is_null()) {
    int val = p->value;  // 20
}

// Iteration
for (auto it = vec.begin(); it != vec.end(); ++it) {
    auto node = *it;
    // node->value is the element
}

vec.pop_back();  // removes 30
vec.clear();     // removes all elements

Mgr::destroy();
```

**Notes:**
- O(1) push_back, front, back, pop_back (tail pointer maintained).
- O(n) at(i) — linear traversal from head.
- Nodes are **not** permanently locked — they can be freed via `pop_back()` or `clear()`.

## Configuration

### Built-in presets

```cpp
#include "pmm/pmm_presets.h"

namespace pmm::presets {
    // Embedded — static buffer, no dynamic heap
    template <std::size_t N = 1024>
    using SmallEmbeddedStaticHeap = PersistMemoryManager<SmallEmbeddedStaticConfig<N>, 0>;  // 16-bit index

    template <std::size_t N = 4096>
    using EmbeddedStaticHeap = PersistMemoryManager<EmbeddedStaticConfig<N>, 0>;           // 32-bit, static

    using EmbeddedHeap        = PersistMemoryManager<EmbeddedManagerConfig, 0>;            // 32-bit, dynamic

    // Desktop / server
    using SingleThreadedHeap  = PersistMemoryManager<CacheManagerConfig, 0>;
    using MultiThreadedHeap   = PersistMemoryManager<PersistentDataConfig, 0>;

    // Industrial DB
    using IndustrialDBHeap    = PersistMemoryManager<IndustrialDBConfig, 0>;

    // Large DB — 64-bit index
    using LargeDBHeap         = PersistMemoryManager<LargeDBConfig, 0>;
}
```

| Preset | Index | pptr size | Lock policy | Growth | Max heap | Intended for |
|--------|-------|-----------|-------------|--------|----------|--------------|
| `SmallEmbeddedStaticHeap<N>` | `uint16_t` | 2 B | `NoLock` | none | ~1 MB | ARM Cortex-M, AVR, ESP32 |
| `EmbeddedStaticHeap<N>` | `uint32_t` | 4 B | `NoLock` | none | 64 GB | Bare-metal, RTOS |
| `EmbeddedHeap` | `uint32_t` | 4 B | `NoLock` | 50% | 64 GB | Embedded with dynamic heap |
| `SingleThreadedHeap` | `uint32_t` | 4 B | `NoLock` | 25% | 64 GB | Caches, offline tools |
| `MultiThreadedHeap` | `uint32_t` | 4 B | `SharedMutexLock` | 25% | 64 GB | Concurrent services |
| `IndustrialDBHeap` | `uint32_t` | 4 B | `SharedMutexLock` | 100% | 64 GB | High-throughput databases |
| `LargeDBHeap` | `uint64_t` | 8 B | `SharedMutexLock` | 100% | petabyte | Large-scale databases |

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

| Type | Index | Granule | pptr size | Max addressable |
|------|-------|---------|-----------|-----------------|
| `TinyAddressTraits` | `uint8_t` | 8 B | 1 B | 2 KB |
| `SmallAddressTraits` | `uint16_t` | 16 B | 2 B | ~1 MB |
| `DefaultAddressTraits` | `uint32_t` | 16 B | 4 B | 64 GB |
| `LargeAddressTraits` | `uint64_t` | 64 B | 8 B | petabyte |

### Storage backends

| Class | Description |
|-------|-------------|
| `HeapStorage<A>` | Dynamic allocation via `malloc` / `realloc` |
| `MMapStorage<A>` | File-mapped memory (`mmap` / `MapViewOfFile`) — persistent across restarts |
| `StaticStorage<Size, A>` | Fixed-size static array — no dynamic allocation, suitable for embedded |

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
│  pptr<T> / pstringview / pmap<K,V>                  │
├─────────────────────────────────────────────────────┤
│              AllocatorPolicy                         │
│  best-fit search · block splitting · coalescing     │
│  auto-expansion · AVL free-tree rebalancing         │
├─────────────────────────────────────────────────────┤
│               Raw Memory Layer                       │
│  StorageBackend → contiguous byte buffer            │
│  Block<AT> (TreeNode + prev/next offsets, 32 bytes) │
│  ManagerHeader (magic, sizes, counters, root ptr)   │
└─────────────────────────────────────────────────────┘
```

**Memory layout inside the managed region:**

```
[ManagerHeader][Block_0][data_0][Block_1][data_1] ...
```

- `ManagerHeader` is stored at offset 0 inside the managed region
- Every block carries a 32-byte header (`TreeNode` fields 0–23 bytes, `prev`/`next` offsets 24–31 bytes)
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
│   └── pmm/                          # Modular headers
│       ├── persist_memory_manager.h  # Main manager class (pptr, pstringview, pmap aliases)
│       ├── pptr.h                    # Persistent pointer
│       ├── pstringview.h             # Interned persistent string (v0.11.0)
│       ├── pmap.h                    # Persistent AVL dictionary (v0.12.0)
│       ├── pvector.h                 # Persistent vector (v0.21.0)
│       ├── avl_tree_mixin.h          # Shared AVL tree helpers (v0.13.0)
│       ├── pmm_presets.h             # Ready-made preset aliases
│       ├── manager_configs.h         # Config structs (including embedded/large DB)
│       ├── address_traits.h          # Address space traits (Tiny/Small/Default/Large)
│       ├── config.h                  # Lock policies
│       ├── heap_storage.h            # malloc-based backend
│       ├── mmap_storage.h            # file-mapped backend
│       ├── static_storage.h          # static-array backend (for embedded)
│       ├── storage_backend.h         # StorageBackend concept
│       ├── allocator_policy.h        # Alloc/dealloc algorithms
│       ├── block.h                   # Block layout (TreeNode + linked list)
│       ├── block_state.h             # Block state machine
│       ├── free_block_tree.h         # AVL free-tree policy
│       ├── tree_node.h               # AVL node fields
│       ├── types.h                   # ManagerInfo, MemoryStats, constants
│       ├── io.h                      # save/load utilities
│       └── manager_concept.h         # C++20 concepts
├── single_include/
│   └── pmm/                          # Self-contained single-header files
│       ├── pmm.h                     # Full library, no preset (v0.10.0)
│       ├── pmm_small_embedded_static_heap.h  # SmallEmbeddedStaticHeap<N> (v0.9.0)
│       ├── pmm_embedded_static_heap.h        # EmbeddedStaticHeap<N> (v0.8.0)
│       ├── pmm_embedded_heap.h               # EmbeddedHeap
│       ├── pmm_single_threaded_heap.h        # SingleThreadedHeap
│       ├── pmm_multi_threaded_heap.h         # MultiThreadedHeap
│       ├── pmm_industrial_db_heap.h          # IndustrialDBHeap
│       └── pmm_large_db_heap.h               # LargeDBHeap (v0.9.0)
├── examples/                         # Usage examples
├── tests/                            # Test suite
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
