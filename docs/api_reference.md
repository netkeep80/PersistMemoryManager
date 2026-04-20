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
#include "pmm/persist_memory_manager.h"  // core manager (includes pmap, pstringview)
#include "pmm/manager_configs.h"         // predefined configurations
#include "pmm/pmm_presets.h"             // named preset aliases
#include "pmm/io.h"                      // file save / load utilities
#include "pmm/avl_tree_mixin.h"          // shared AVL helpers (included via pmap/pstringview)
```

**Single-header presets** (include one file, get a ready-to-use manager type).
Located in `single_include/pmm/`:
```cpp
#include "pmm.h"                              // full library, any configuration
#include "pmm_single_threaded_heap.h"         // SingleThreadedHeap preset
#include "pmm_multi_threaded_heap.h"          // MultiThreadedHeap preset
#include "pmm_embedded_heap.h"               // EmbeddedHeap preset
#include "pmm_industrial_db_heap.h"          // IndustrialDBHeap preset
#include "pmm_embedded_static_heap.h"        // EmbeddedStaticHeap preset (no heap, 32-bit)
#include "pmm_small_embedded_static_heap.h"  // SmallEmbeddedStaticHeap (no heap, 16-bit)
#include "pmm_large_db_heap.h"               // LargeDBHeap preset (64-bit index)
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

**Nested type aliases:**
```cpp
// Persistent typed pointer bound to this manager
template <typename T>
using pptr = pmm::pptr<T, PersistMemoryManager>;

// Persistent interned read-only string
using pstringview = pmm::pstringview<PersistMemoryManager>;

// Persistent AVL tree dictionary
template <typename _K, typename _V>
using pmap = pmm::pmap<_K, _V, PersistMemoryManager>;

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

#### `load(result)`

```cpp
static bool load(VerifyResult& result) noexcept;
```

Loads an existing manager state from the backend buffer. Validates the magic number,
total size, and granule size. Rebuilds the free block AVL tree, repairs the linked list,
and recomputes counters. Detected violations and repair actions are reported through
`result`.

**Returns:** `true` on success, `false` if the image is invalid.

**Example:**
```cpp
// After filling the backend buffer with a saved image:
pmm::VerifyResult diagnostics;
bool ok = MyMgr::load(diagnostics);
```

---

#### `destroy()`

```cpp
static void destroy() noexcept;
```

Resets the runtime manager state. Clears the initialization flag. Does **not** free the
backend buffer and does **not** modify the persisted image, so a valid backend image
remains loadable with `load(result)`. Required for test isolation, normal shutdown, and
before re-initialization.

**Example:**
```cpp
MyMgr::destroy();
```

---

#### `destroy_image()`

```cpp
static void destroy_image() noexcept;
```

Explicitly invalidates the current backend image by clearing the header magic, then
resets the runtime manager state. This is a destructive helper for tests and corruption
simulation. Use `destroy()` for normal shutdown.

**Example:**
```cpp
MyMgr::destroy_image(); // subsequent load(result) fails with InvalidMagic
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

#### `tree_node<T>()`

```cpp
template <typename T>
static TreeNode<address_traits>& tree_node(pptr<T> p) noexcept;
```

Returns a direct reference to the `TreeNode` embedded in the block header for `p`.
Provides unified access to all AVL fields via `get_left()`, `set_left()`, `get_right()`,
`set_right()`, `get_parent()`, `set_parent()`, `get_height()`, `set_height()`,
`get_weight()`, `set_weight()`.

> **Warning:** The returned reference is only valid while the manager is initialized and
> the block has not been freed. Do not store the reference beyond the current operation.
>
> Absent links are stored as `address_traits::no_block` sentinel, not as zero.

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

**Requirement:** `sizeof(pptr<T, ManagerT>) == sizeof(index_type)` (2, 4, or 8 bytes
depending on `address_traits`).

The preferred way to obtain a `pptr` is through the nested alias in the manager:
```cpp
using MyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
MyMgr::pptr<int> p = MyMgr::allocate_typed<int>();
```

### Member types

```cpp
using element_type = T;
using manager_type = ManagerT;
using index_type   = typename ManagerT::address_traits::index_type; // uint16_t, uint32_t, or uint64_t
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
T&  operator*() const noexcept;   // dereference
T*  operator->() const noexcept;  // member access
```

Both operations call `ManagerT::resolve<T>(p)` internally.

### AVL tree node access

```cpp
TreeNode<address_traits>& tree_node() const noexcept;  // direct reference to TreeNode
```

Returns a reference to the `TreeNode` embedded in the block header. All AVL fields
are accessed through the returned `TreeNode` reference:

```cpp
auto& tn = p.tree_node();
tn.get_left();              // index_type — left child granule index or no_block
tn.get_right();             // index_type — right child granule index or no_block
tn.get_parent();            // index_type — parent granule index or no_block
tn.get_weight();            // index_type — node weight
tn.get_height();            // std::int16_t — AVL subtree height
tn.set_left(idx);
tn.set_right(idx);
tn.set_parent(idx);
tn.set_weight(w);
tn.set_height(h);
```

> **Note:** Absent links are stored as `address_traits::no_block` sentinel. Use
> `p.offset()` to convert a `pptr` to a granule index for storage in tree fields.

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

## Class `pstringview<ManagerT>`

```cpp
namespace pmm {
    template <typename ManagerT>
    struct pstringview;
}
```

A persistent interned read-only string. Provides automatic deduplication: two calls to
`pstringview("hello")` return the same `pptr` (same granule index). The string data and
the `pstringview` blocks are permanently locked via `lock_block_permanent()`.

**Accessed via manager nested alias:**
```cpp
using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
Mgr::pptr<Mgr::pstringview> p = Mgr::pstringview("hello");
```

### Data fields

```cpp
std::uint32_t length;     // string length (without null terminator)
char          str[1];     // embedded null-terminated chars (flexible-array pattern)
```

### Constructor (interning helper)

```cpp
explicit pstringview(const char* s) noexcept;
```

Creates a temporary stack object that interns `s` into PAP. Converts implicitly to
`pptr<pstringview<ManagerT>>`:
```cpp
Mgr::pptr<Mgr::pstringview> p = Mgr::pstringview("world");
```

### Member methods

```cpp
const char*  c_str() const noexcept;   // raw C-string pointer (valid while manager is initialized)
std::size_t  size()  const noexcept;   // length without null terminator
bool         empty() const noexcept;   // true if length == 0
```

### Comparison operators

```cpp
bool operator==(const char* s)         const noexcept;
bool operator==(const pstringview& o)  const noexcept;
bool operator!=(const char* s)         const noexcept;
bool operator!=(const pstringview& o)  const noexcept;
bool operator<(const pstringview& o)   const noexcept;  // lexicographic; for use as pmap key
```

### Static methods

```cpp
static psview_pptr intern(const char* s) noexcept;  // explicit interning
static void        reset() noexcept;                // clears system/symbols domain root (tests)
```

### Example

```cpp
using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
Mgr::create(64 * 1024);

Mgr::pptr<Mgr::pstringview> p  = Mgr::pstringview("hello");
Mgr::pptr<Mgr::pstringview> p2 = Mgr::pstringview("hello");
assert(p == p2);           // same pptr — deduplication guaranteed
assert(p->size() == 5);
assert(p->c_str()[0] == 'h');

Mgr::destroy();
```

---

## Class `pmap<_K, _V, ManagerT>`

```cpp
namespace pmm {
    template <typename _K, typename _V, typename ManagerT>
    struct pmap;
}
```

A persistent associative container (dictionary) based on an AVL tree. Each node is an
allocated block in PAP containing a key-value pair. The built-in `TreeNode` fields of
each block serve as AVL tree links (no separate node allocations). Inserting a duplicate
key updates the existing value.

**Accessed via manager nested alias:**
```cpp
using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
Mgr::pmap<int, int> map;
map.insert(42, 100);
```

**Key type requirements:** `_K` must support `operator<` and `operator==`.

### Data fields

```cpp
index_type _root_idx;  // granule index of AVL tree root; 0 = empty map
```

### Constructors

```cpp
pmap() noexcept;  // creates an empty map (_root_idx = 0)
```

### Methods

```cpp
bool        empty()                          const noexcept;
node_pptr   insert(const _K& key, const _V& val) noexcept;  // insert or update; returns node pptr
node_pptr   find  (const _K& key)            const noexcept; // returns null pptr if not found
bool        contains(const _K& key)          const noexcept;
bool        erase(const _K& key)             noexcept;       // O(log n) remove by key; returns false if not found
std::size_t size()                           const noexcept; // O(n) element count via avl_subtree_count
void        clear()                          noexcept;       // O(n) remove all elements with deallocation
void        reset()                          noexcept;       // reset root for test isolation
```

### Iterator

Uses the shared `AvlInorderIterator<NodePPtr>` template from `avl_tree_mixin.h`:

```cpp
using iterator = pmm::detail::AvlInorderIterator<node_pptr>;

iterator begin() const noexcept;  // leftmost node (smallest key)
iterator end()   const noexcept;  // sentinel (null)
```

The iterator traverses nodes in ascending key order via `avl_inorder_successor`.

### `pmap_node<_K, _V>`

```cpp
template <typename _K, typename _V>
struct pmap_node {
    _K key;
    _V value;
};
```

Each node is a separate PAP block. Access via the returned `pptr`:
```cpp
auto p = map.find(42);
if (!p.is_null()) {
    int val = p->value;  // 100
}
```

### Example

```cpp
using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
Mgr::create(64 * 1024);

Mgr::pmap<int, int> map;
map.insert(42, 100);
map.insert(10, 200);
map.insert(42, 300);  // updates existing value

auto p = map.find(42);
assert(!p.is_null() && p->value == 300);
assert(map.contains(10));
assert(!map.contains(99));

// Size and iteration
assert(map.size() == 2);
for (auto it = map.begin(); it != map.end(); ++it) {
    auto node = *it;
    // node->key, node->value — in ascending key order
}

// Erase and clear
map.erase(42);       // true — removes node and deallocates
map.clear();         // removes all remaining elements
assert(map.empty());

Mgr::destroy();
```

**Using `pstringview` as a key:**
```cpp
using Mgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
Mgr::create(64 * 1024);

Mgr::pmap<Mgr::pstringview, int> dict;
Mgr::pptr<Mgr::pstringview> key = Mgr::pstringview("hello");
dict.insert(*key, 42);

auto p = dict.find(*key);
assert(!p.is_null() && p->value == 42);

Mgr::destroy();
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

Saves the entire managed memory image to a binary file. `save_manager()` takes
the manager lock, copies a stable snapshot, computes CRC32 on that copy, and
does not mutate the live manager header while saving. Since all metadata uses
offsets from the buffer start, the image can be loaded at any base address.

The save path writes `filename.tmp`, flushes it to stable storage, atomically
renames it to `filename`, and fsyncs the parent directory where the platform
supports that operation.

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

| Type | Lock policy | Growth | Index | Use case |
|------|-------------|--------|-------|----------|
| `SingleThreadedHeap` | `NoLock` | 25% | 32-bit | Single-threaded caches, tools |
| `MultiThreadedHeap` | `SharedMutexLock` | 25% | 32-bit | Concurrent services |
| `EmbeddedHeap` | `NoLock` | 50% | 32-bit | Memory-constrained devices |
| `IndustrialDBHeap` | `SharedMutexLock` | 100% | 32-bit | High-throughput databases |
| `EmbeddedStaticHeap<N>` | `NoLock` | — | 32-bit | Embedded without heap (static buffer) |
| `SmallEmbeddedStaticHeap<N>` | `NoLock` | — | 16-bit | Tiny embedded, up to ~1 MB |
| `LargeDBHeap` | `SharedMutexLock` | 100% | 64-bit | Petabyte-scale databases |

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

| Config struct | Lock | Growth | Storage | Index | Use case |
|---------------|------|--------|---------|-------|----------|
| `CacheManagerConfig` | `NoLock` | 25% | `HeapStorage` | 32-bit | Single-threaded cache |
| `PersistentDataConfig` | `SharedMutexLock` | 25% | `HeapStorage` | 32-bit | Multi-threaded persistent storage |
| `EmbeddedManagerConfig` | `NoLock` | 50% | `HeapStorage` | 32-bit | Embedded systems |
| `IndustrialDBConfig` | `SharedMutexLock` | 100% | `HeapStorage` | 32-bit | Industrial databases |
| `EmbeddedStaticConfig<N>` | `NoLock` | — | `StaticStorage` | 32-bit | Embedded without heap, fixed pool |
| `SmallEmbeddedStaticConfig<N>` | `NoLock` | — | `StaticStorage` | 16-bit | Tiny embedded, up to ~1 MB |
| `LargeDBConfig` | `SharedMutexLock` | 100% | `HeapStorage` | 64-bit | Petabyte-scale databases |

---

## Address traits (from `pmm/address_traits.h`)

| Traits alias | Index type | Granule | Max addressable | Use case |
|--------------|------------|---------|-----------------|----------|
| `TinyAddressTraits` | `uint8_t` | 8 B | ~2 KB | Experimental / ultra-tiny embedded |
| `SmallAddressTraits` | `uint16_t` | 16 B | ~1 MB | Tiny embedded, microcontrollers |
| `DefaultAddressTraits` | `uint32_t` | 16 B | 64 GB | General purpose (default) |
| `LargeAddressTraits` | `uint64_t` | 64 B | Petabyte+ | Large-scale databases |

`sizeof(pptr<T, Mgr>)` equals `sizeof(index_type)`: 1, 2, 4, or 8 bytes respectively.

---

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `kGranuleSize` | 16 | Default granule size in bytes (DefaultAddressTraits) |
| `kMinAlignment` | 16 | Minimum alignment in bytes |
| `kMinMemorySize` | 4096 | Minimum buffer size in bytes |
| `kMinBlockSize` | 16 | Minimum user data block size in bytes |
| `kMinGranuleSize` | 4 | Minimum allowed granule size (architecture word size) |
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
| `load()` with mismatched granule size | Returns `false` |
| `allocate_typed<T>()` when out of memory | Auto-expands by growth ratio |
| `allocate_typed<T>(0)` | Returns null `pptr` |
| `deallocate_typed(null pptr)` | No-op |
| `deallocate` on permanently locked block | No-op |
| `save_manager(nullptr)` | Returns `false` |
| `load_manager_from_file(nullptr)` | Returns `false` |
| `load_manager_from_file(nonexistent file)` | Returns `false` |
| `load_manager_from_file(file > buffer size)` | Returns `false` |
| `pstringview(nullptr)` | Treated as `""` |
| `pmap::find(key)` for missing key | Returns null `pptr` |
| `pmap::insert(key, val)` for existing key | Updates value |
| `EmbeddedStaticConfig` backend expansion | Always fails (`StaticStorage::expand()` returns `false`) |

---

## Thread safety

Thread safety depends on the `lock_policy` in the configuration:

- **`SharedMutexLock`**: All public methods are thread-safe using `std::shared_mutex`.
  Read operations (`total_size`, `used_size`, `free_size`, `block_count`,
  `free_block_count`, `alloc_block_count`, `for_each_block`, `for_each_free_block`,
  `is_initialized`, `resolve`, `tree_node`, `is_permanently_locked`) acquire a
  `shared_lock` and can run concurrently. Write operations (`create`, `load`, `destroy`,
  `allocate`, `deallocate`, `allocate_typed`, `deallocate_typed`, `lock_block_permanent`)
  acquire a `unique_lock`. Note: `tree_node()` returns a reference — writes through
  that reference are not guarded by the manager lock.

- **`NoLock`**: No synchronization is performed. All operations are safe only in
  single-threaded contexts.

> Do **not** call allocate or deallocate from inside `for_each_block` or `for_each_free_block`
> callbacks — this will deadlock under `SharedMutexLock`.

> `pstringview` and `pmap` are **not** independently thread-safe — their safety depends
> entirely on the manager's lock policy.

---

## Constraints

- I/O uses stdio only (`fopen` / `fread` / `fwrite` / `fclose`).
- Free block search: best-fit via AVL tree — O(log n).
- No image compression or encryption.
- Only one active instance per `(ConfigT, InstanceId)` specialization at a time.
- Maximum addressable memory with `DefaultAddressTraits`: 64 GB (2³² × 16 bytes/granule).
- Maximum addressable memory with `LargeAddressTraits`: petabyte scale.
- Maximum addressable memory with `SmallAddressTraits`: ~1 MB.
- Requires C++20 compiler (GCC 10+, Clang 10+, MSVC 2019 16.3+).

---
