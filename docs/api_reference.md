# PersistMemoryManager API Reference

## Overview

`PersistMemoryManager` is a header-only C++20 library for persistent heap memory management.
All metadata is stored inside the managed region, which allows saving and loading a memory
image from a file or shared memory. Interaction with data in managed memory is done through
persistent typed pointers `pptr<T>`.

The manager is a fully static class template — there are no instances, no raw pointers in
user code. All API is accessible through static methods. Multiple independent manager
instances of the same configuration can coexist through the `InstanceId` template parameter
(multiton pattern).

### Include structure

**Modular headers** (include individually as needed):
```cpp
#include "pmm/persist_memory_manager.h"  // core manager
#include "pmm/manager_configs.h"         // predefined configurations
#include "pmm/pmm_presets.h"             // named preset aliases
#include "pmm/io.h"                      // file save / load utilities
```

**Single-header presets** (include one file, get a ready-to-use manager type).
Located in `single_include/pmm/` (Issue #138):
```cpp
#include "pmm_single_threaded_heap.h"    // SingleThreadedHeap preset
#include "pmm_multi_threaded_heap.h"     // MultiThreadedHeap preset
#include "pmm_embedded_heap.h"           // EmbeddedHeap preset
#include "pmm_industrial_db_heap.h"      // IndustrialDBHeap preset
```

Namespace: `pmm`

---

## Class `PersistMemoryManager<ConfigT, InstanceId>`

```cpp
namespace pmm {
    template <typename ConfigT = CacheManagerConfig, std::size_t InstanceId = 0>
    class PersistMemoryManager;
}
```

A fully static class template. All state (storage backend, mutex, initialization flag) is
stored in `static inline` members. No instances need to be created. Each unique combination
of `ConfigT` and `InstanceId` is an independent manager with its own separate storage.

**Template parameters:**
- `ConfigT` — configuration struct that provides:
  - `address_traits` — address space type (index size, granule size)
  - `storage_backend` — storage backend type (`HeapStorage`, `StaticStorage`, `MMapStorage`)
  - `free_block_tree` — free block search policy (`AvlFreeTree`)
  - `lock_policy` — thread safety policy (`NoLock`, `SharedMutexLock`)
  - `granule_size` — granule size in bytes
  - `grow_numerator` / `grow_denominator` — growth ratio
- `InstanceId` — instance identifier (default `0`). Allows multiple independent managers
  with the same configuration.

**Nested type alias:**
```cpp
template <typename T>
using pptr = pmm::pptr<T, PersistMemoryManager>;
```

### Lifecycle

#### `create(initial_size)`

```cpp
static bool create(std::size_t initial_size) noexcept;
```

Initializes the manager with the given initial size. Allocates the storage backend and
sets up the memory layout.

**Parameters:**
- `initial_size` — initial size in bytes. Must be `>= kMinMemorySize` (4096).

**Returns:** `true` on success, `false` on error.

**Example:**
```cpp
using MyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
bool ok = MyMgr::create(1024 * 1024); // 1 MiB
```

---

#### `create()`

```cpp
static bool create() noexcept;
```

Initializes the manager over an already-allocated backend buffer. Use this when the
storage backend has been set up externally (e.g., `MMapStorage` or `StaticStorage`).

**Returns:** `true` on success, `false` if the backend is not ready.

---

#### `load()`

```cpp
static bool load() noexcept;
```

Loads an existing manager state from the backend buffer. Validates the magic number,
total size, and granule size. Rebuilds the free block AVL tree, repairs the linked list,
and recomputes counters.

**Returns:** `true` on success, `false` if the image is invalid.

**Example:**
```cpp
// After filling the backend buffer with a saved image:
bool ok = MyMgr::load();
```

---

#### `destroy()`

```cpp
static void destroy() noexcept;
```

Resets the manager state. Clears the initialization flag. Does **not** free the backend
buffer. Required for test isolation and before re-initialization.

**Example:**
```cpp
MyMgr::destroy();
```

---

#### `is_initialized()`

```cpp
static bool is_initialized() noexcept;
```

Returns `true` if the manager has been initialized via `create()` or `load()`.

---

### Typed allocation (primary API)

#### `allocate_typed<T>()`

```cpp
template <typename T>
static pptr<T> allocate_typed() noexcept;

template <typename T>
static pptr<T> allocate_typed(std::size_t count) noexcept;
```

Allocates `sizeof(T)` bytes (or `sizeof(T) * count` for arrays) aligned to the granule
size. If memory is insufficient, the manager automatically expands the storage backend.

**Returns:** `pptr<T>` pointing to the allocated block, or a null `pptr<T>()` on error.

**Example:**
```cpp
using MyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
MyMgr::create(1024 * 1024);

MyMgr::pptr<int>    p1 = MyMgr::allocate_typed<int>();
MyMgr::pptr<double> p2 = MyMgr::allocate_typed<double>(10); // array of 10 doubles
*p1 = 42;
p2[1] = 3.14;
```

---

#### `deallocate_typed<T>()`

```cpp
template <typename T>
static void deallocate_typed(pptr<T> p) noexcept;
```

Frees the block pointed to by `p`. A null `pptr` is a no-op. Permanently locked blocks
(see `lock_block_permanent`) cannot be freed.

**Example:**
```cpp
MyMgr::deallocate_typed(p1);
```

---

#### `reallocate_typed<T>()`

There is no `reallocate_typed` method in the current API. To resize, allocate a new block,
copy data manually, and deallocate the old block.

---

### Raw allocation

#### `allocate()`

```cpp
static void* allocate(std::size_t user_size) noexcept;
```

Allocates `user_size` bytes and returns a raw pointer. Lower-level than `allocate_typed`.

**Returns:** pointer to user data, or `nullptr` on error.

---

#### `deallocate()`

```cpp
static void deallocate(void* ptr) noexcept;
```

Frees the block at `ptr`. Null pointer is a no-op.

---

### Block locking (permanent)

#### `lock_block_permanent()`

```cpp
static bool lock_block_permanent(void* ptr) noexcept;
```

Permanently locks a block, making it impossible to free via `deallocate()`. Intended for
blocks containing permanent data (e.g., a persistent string dictionary). The block's
`node_type` is set to `kNodeReadOnly`.

**Returns:** `true` if the block was successfully locked, `false` if not found or already free.

---

#### `is_permanently_locked()`

```cpp
static bool is_permanently_locked(const void* ptr) noexcept;
```

Returns `true` if the block at `ptr` is permanently locked (`node_type == kNodeReadOnly`).

---

### Pointer resolution

#### `resolve<T>()`

```cpp
template <typename T>
static T* resolve(pptr<T> p) noexcept;
```

Converts a persistent pointer to a raw pointer. Called internally by `pptr<T>::resolve()`,
`operator*`, and `operator->`.

**Returns:** `T*` pointer to user data, or `nullptr` for a null or uninitialized manager.

---

#### `resolve_at<T>()`

```cpp
template <typename T>
static T* resolve_at(pptr<T> p, std::size_t i) noexcept;
```

Returns a pointer to the `i`-th element of the array pointed to by `p`.

---

### Statistics

All statistics methods are static and thread-safe (use `shared_lock`).

#### `total_size()`

```cpp
static std::size_t total_size() noexcept;
```

Total size of the managed region in bytes. Returns `0` if not initialized.

---

#### `used_size()`

```cpp
static std::size_t used_size() noexcept;
```

Amount of used memory: block headers plus user data, in bytes.

---

#### `free_size()`

```cpp
static std::size_t free_size() noexcept;
```

Amount of available free memory in bytes.

**Invariant:** `used_size() + free_size() <= total_size()`.

---

#### `block_count()`

```cpp
static std::size_t block_count() noexcept;
```

Total number of blocks (both allocated and free).

---

#### `free_block_count()`

```cpp
static std::size_t free_block_count() noexcept;
```

Number of free blocks.

---

#### `alloc_block_count()`

```cpp
static std::size_t alloc_block_count() noexcept;
```

Number of allocated blocks.

---

### Iteration

#### `for_each_block()`

```cpp
template <typename Callback>
static bool for_each_block(Callback&& callback) noexcept;
// Callback: void(const pmm::BlockView&)
```

Iterates all blocks in address order (from smallest to largest offset) and calls
`callback` for each. Thread-safe (`shared_lock`).

**Returns:** `false` if not initialized, `true` otherwise.

**Note:** Do not call `allocate` or `deallocate` from the callback — this will cause a deadlock.

---

#### `for_each_free_block()`

```cpp
template <typename Callback>
static bool for_each_free_block(Callback&& callback) noexcept;
// Callback: void(const pmm::FreeBlockView&)
```

Iterates free blocks in the AVL tree in-order (by ascending block size) and calls
`callback` for each.

**Returns:** `false` if not initialized, `true` otherwise.

---

### AVL tree node access (advanced)

These methods allow reading and writing AVL tree metadata for a block pointed to by a `pptr`.
They are intended for advanced use cases, such as implementing persistent data structures
(e.g., a persistent AVL tree using PMM blocks as nodes).

> **Warning:** Modifying AVL tree fields on regular allocated blocks can corrupt the free
> block tree. Only use these methods on blocks that are permanently locked via
> `lock_block_permanent()`.

| Method | Description |
|--------|-------------|
| `get_tree_left_offset<T>(p)` | Get granule index of left child |
| `get_tree_right_offset<T>(p)` | Get granule index of right child |
| `get_tree_parent_offset<T>(p)` | Get granule index of parent node |
| `set_tree_left_offset<T>(p, idx)` | Set left child granule index |
| `set_tree_right_offset<T>(p, idx)` | Set right child granule index |
| `set_tree_parent_offset<T>(p, idx)` | Set parent node granule index |
| `get_tree_weight<T>(p)` | Get node weight (data size in granules) |
| `set_tree_weight<T>(p, w)` | Set node weight |
| `get_tree_height<T>(p)` | Get AVL subtree height |
| `set_tree_height<T>(p, h)` | Set AVL subtree height |

---

### Backend access

#### `backend()`

```cpp
static storage_backend& backend() noexcept;
```

Returns a reference to the static storage backend. For advanced scenarios (e.g., accessing
`MMapStorage` to get `base_ptr()` before calling `load()`).

---

## Class `pptr<T, ManagerT>`

```cpp
namespace pmm {
    template <class T, class ManagerT>
        requires (!std::is_void_v<ManagerT>)
    class pptr;
}
```

A persistent typed pointer. Stores a granule index (offset-based, not address-based),
which makes it address-independent: it remains valid after loading the image at a different
base address.

**Requirement:** `sizeof(pptr<T, ManagerT>) == sizeof(index_type)` (typically 4 bytes).

The preferred way to obtain a `pptr` is through the nested alias in the manager:
```cpp
using MyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
MyMgr::pptr<int> p = MyMgr::allocate_typed<int>();
```

### Member types

```cpp
using element_type = T;
using manager_type = ManagerT;
using index_type   = typename ManagerT::address_traits::index_type; // typically uint32_t
```

### Constructors

```cpp
constexpr pptr() noexcept;                              // null pointer (index 0)
explicit constexpr pptr(index_type idx) noexcept;       // from granule index
pptr(const pptr&) = default;
pptr& operator=(const pptr&) = default;
```

### Null check

```cpp
bool is_null() const noexcept;
explicit operator bool() const noexcept;  // true if not null
```

### Granule index access

```cpp
index_type offset() const noexcept;  // granule index of user data
```

### Dereference (static manager model)

```cpp
T*  resolve() const noexcept;              // raw pointer to object
T&  operator*() const noexcept;            // dereference
T*  operator->() const noexcept;           // member access
T&  operator[](std::size_t i) const noexcept; // array element access
```

All dereference operations call `ManagerT::resolve(p)` internally.

### Comparison operators

```cpp
bool operator==(const pptr<T, ManagerT>& other) const noexcept;
bool operator!=(const pptr<T, ManagerT>& other) const noexcept;
```

### Example

```cpp
using MyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
MyMgr::create(1 << 20);

// Allocate and use
MyMgr::pptr<int> p = MyMgr::allocate_typed<int>();
*p = 123;

// Save granule index for later recovery
uint32_t saved_idx = p.offset();

// Save image to file
pmm::save_manager<MyMgr>("heap.dat");
MyMgr::destroy();

// Restore from file
MyMgr::create(1 << 20);
pmm::load_manager_from_file<MyMgr>("heap.dat");

MyMgr::pptr<int> p2(saved_idx);
assert(*p2 == 123);

MyMgr::destroy();
```

---

## Free functions (from `pmm/io.h`)

### `save_manager<MgrT>()`

```cpp
namespace pmm {
    template <typename MgrT>
    bool save_manager(const char* filename);
}
```

Saves the entire managed memory image to a binary file. Since all metadata uses offsets
from the buffer start, the image can be loaded at any base address.

**Parameters:**
- `filename` — path to output file. Must not be `nullptr`.

**Precondition:** `MgrT::is_initialized() == true`.

**Returns:** `true` on success, `false` on I/O error or if not initialized.

**Example:**
```cpp
#include "pmm/io.h"

if (!pmm::save_manager<MyMgr>("heap.dat")) {
    // write error
}
```

---

### `load_manager_from_file<MgrT>()`

```cpp
namespace pmm {
    template <typename MgrT>
    bool load_manager_from_file(const char* filename);
}
```

Loads a manager image from a file into the backend buffer, then calls `MgrT::load()` to
validate the header and restore state.

**Precondition:** The backend must have an allocated buffer of sufficient size. For
`HeapStorage`, call `MgrT::create(size)` before calling this function.

**Parameters:**
- `filename` — path to the image file.

**Returns:** `true` on success, `false` on error.

**Example:**
```cpp
#include "pmm/io.h"

MyMgr::create(1024 * 1024);  // allocate buffer
bool ok = pmm::load_manager_from_file<MyMgr>("heap.dat");
if (ok) {
    // manager restored from file
}
```

---

## Preset types (from `pmm/pmm_presets.h`)

Ready-to-use type aliases in namespace `pmm::presets`:

| Type | Lock policy | Growth | Use case |
|------|-------------|--------|----------|
| `SingleThreadedHeap` | `NoLock` | 25% | Single-threaded caches, tools |
| `MultiThreadedHeap` | `SharedMutexLock` | 25% | Concurrent services |
| `EmbeddedHeap` | `NoLock` | 50% | Memory-constrained devices |
| `IndustrialDBHeap` | `SharedMutexLock` | 100% | High-throughput databases |

All presets use 32-bit addressing, 16-byte granules, and `HeapStorage`.

**Example using a preset:**
```cpp
#include "pmm/pmm_presets.h"

using Heap = pmm::presets::SingleThreadedHeap;

Heap::create(64 * 1024);  // 64 KiB
Heap::pptr<int> p = Heap::allocate_typed<int>();
*p = 99;
Heap::deallocate_typed(p);
Heap::destroy();
```

**Or using single-header preset files:**
```cpp
#include "pmm_single_threaded_heap.h"

pmm::presets::SingleThreadedHeap::create(64 * 1024);
```

---

## Data structures

### `BlockView`

Describes a block when iterating via `for_each_block()`:

```cpp
struct BlockView {
    std::uint32_t index;       // granule index of the block header
    std::ptrdiff_t offset;     // byte offset from buffer start
    std::size_t total_size;    // total block size in bytes (header + data)
    std::size_t header_size;   // block header size in bytes (sizeof(Block<A>))
    std::size_t user_size;     // user data size in bytes (0 if free)
    std::size_t alignment;     // granule size (alignment)
    bool used;                 // true if allocated, false if free
};
```

### `FreeBlockView`

Describes a free block when iterating via `for_each_free_block()`:

```cpp
struct FreeBlockView {
    std::ptrdiff_t offset;        // byte offset from buffer start
    std::size_t total_size;       // total block size in bytes
    std::size_t free_size;        // available user data size in bytes
    std::ptrdiff_t left_offset;   // left child byte offset (-1 if none)
    std::ptrdiff_t right_offset;  // right child byte offset (-1 if none)
    std::ptrdiff_t parent_offset; // parent byte offset (-1 if none)
    std::int16_t avl_height;      // AVL subtree height
    int avl_depth;                // depth from root (0 = root)
};
```

---

## Predefined configurations (from `pmm/manager_configs.h`)

| Config struct | Lock | Growth | Storage | Use case |
|---------------|------|--------|---------|----------|
| `CacheManagerConfig` | `NoLock` | 25% | `HeapStorage` | Single-threaded cache |
| `PersistentDataConfig` | `SharedMutexLock` | 25% | `HeapStorage` | Multi-threaded persistent storage |
| `EmbeddedManagerConfig` | `NoLock` | 50% | `HeapStorage` | Embedded systems |
| `IndustrialDBConfig` | `SharedMutexLock` | 100% | `HeapStorage` | Industrial databases |

---

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `kGranuleSize` | 16 | Granule size in bytes (addressing unit) |
| `kMinAlignment` | 16 | Minimum alignment in bytes |
| `kMinMemorySize` | 4096 | Minimum buffer size in bytes |
| `kMinBlockSize` | 16 | Minimum user data block size in bytes |
| `config::kDefaultGrowNumerator` | 5 | Growth ratio numerator (5/4 = 25%) |
| `config::kDefaultGrowDenominator` | 4 | Growth ratio denominator |

---

## Edge case behavior

| Condition | Behavior |
|-----------|----------|
| `create(size < 4096)` | Returns `false` |
| `create()` with no backend buffer | Returns `false` |
| `load()` with invalid magic | Returns `false` |
| `load()` with mismatched total size | Returns `false` |
| `allocate_typed<T>()` when out of memory | Auto-expands by growth ratio |
| `allocate_typed<T>(0)` | Returns null `pptr` |
| `deallocate_typed(null pptr)` | No-op |
| `deallocate` on permanently locked block | No-op |
| `save_manager(nullptr)` | Returns `false` |
| `load_manager_from_file(nullptr)` | Returns `false` |
| `load_manager_from_file(nonexistent file)` | Returns `false` |
| `load_manager_from_file(file > buffer size)` | Returns `false` |

---

## Thread safety

Thread safety depends on the `lock_policy` in the configuration:

- **`SharedMutexLock`**: All public methods are thread-safe using `std::shared_mutex`.
  Read operations (`total_size`, `used_size`, `free_size`, `block_count`,
  `free_block_count`, `alloc_block_count`, `for_each_block`, `for_each_free_block`,
  `is_initialized`, `resolve`, `get_tree_*`, `is_permanently_locked`) acquire a
  `shared_lock` and can run concurrently. Write operations (`create`, `load`, `destroy`,
  `allocate`, `deallocate`, `allocate_typed`, `deallocate_typed`, `lock_block_permanent`,
  `set_tree_*`) acquire a `unique_lock`.

- **`NoLock`**: No synchronization is performed. All operations are safe only in
  single-threaded contexts.

> Do **not** call allocate or deallocate from inside `for_each_block` or `for_each_free_block`
> callbacks — this will deadlock under `SharedMutexLock`.

---

## Constraints

- I/O uses stdio only (`fopen` / `fread` / `fwrite` / `fclose`).
- Free block search: best-fit via AVL tree — O(log n).
- No image compression or encryption.
- Only one active instance per `(ConfigT, InstanceId)` specialization at a time.
- Maximum addressable memory: 64 GB (2³² × 16 bytes/granule with 32-bit index).
- Requires C++20 compiler (GCC 10+, Clang 10+, MSVC 2019 16.3+).

---

*Document version 0.6.0. Reflects the library API as of v0.6.0.*
