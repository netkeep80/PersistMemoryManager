# Phase 11: MemMapView Tile-Aggregation Overview Mode

## Overview

Phase 11 adds an **overview rendering mode** to `MemMapView` that allows the
entire PMM-managed region to be displayed regardless of its size — without
degrading rendering performance.

This directly resolves **Risk #7** from `plan.md`:

> *FPS < 30 with 7 parallel scenarios + large PMM — optimise map rendering:
> DrawList batching, skip identical pixels.*

## Problem

The previous implementation of `MemMapView` capped the detail view at 512 KB
(`kDetailLimit`).  Any memory beyond that threshold was simply not shown (only
a `"(first 512 KB shown)"` label was displayed).  As a result:

1. Users could not see the state of the PMM beyond the first 512 KB.
2. Large scenarios (e.g. `LargeBlocks` with auto-grow) were virtually invisible
   on the memory map.

## Solution

### Tile Aggregation

A new `TileInfo` struct aggregates N consecutive bytes into a single pixel:

```cpp
struct TileInfo {
    ByteInfo::Type dominant_type = ByteInfo::Type::OutOfBlocks;
    std::size_t    offset        = 0;          // byte offset of tile start
    std::size_t    bytes_per_tile = 1;         // bytes covered by this tile
    std::uint32_t  type_counts[6] = {};        // per-type byte counts
};
```

The **dominant type** (most frequent `ByteInfo::Type` within the tile) determines
the rendered pixel colour.

### Algorithm

`update_snapshot()` now builds two parallel representations:

1. **Detail snapshot** (`snapshot_`) — unchanged; first 512 KB, 1 byte per entry.
2. **Tile snapshot** (`tile_snapshot_`) — new; the full managed region,
   N bytes per tile.

Tile size is computed as:

```
bytes_per_tile = ceil(total_bytes / kMaxTiles)   // kMaxTiles = 65536
```

This bounds the render cost to at most 65 536 pixel draw calls per frame,
regardless of the PMM size.

Tile counts for bytes beyond the detail limit are populated via a second
`for_each_block()` pass, which takes a `shared_lock` internally and is
therefore thread-safe.

### UI Change

When `total_bytes > kDetailLimit` (512 KB) a new checkbox appears in the
Memory Map control bar:

```
[Auto width] [Width: 256] [Scale: 1.0x] [✓ Overview (full memory)] (1 px = 2048 bytes)
```

Toggling it switches between the existing detail renderer and the new overview
renderer.  For small PMM sizes (≤ 512 KB) the checkbox is hidden and only the
detail view is shown.

The overview tooltip displays tile metadata:

```
Tile:   #1234
Offset: 79691776
Range:  79691776 – 79693823 bytes
Type:   UserData(used) (dominant)
px/tile: 2048 bytes
```

### Public Accessors for Testing

Three read-only accessors were added to `MemMapView` so that the tile snapshot
can be inspected without ImGui rendering:

```cpp
const std::vector<TileInfo>& tile_snapshot() const noexcept;
std::size_t bytes_per_tile() const noexcept;
std::size_t total_bytes()    const noexcept;
```

## Files Changed

| File | Change |
|------|--------|
| `demo/mem_map_view.h` | Added `TileInfo` struct; added `tile_snapshot_`, `bytes_per_tile_`, `overview_mode_` members; added public accessors; split render into `render_detail()` / `render_overview()` |
| `demo/mem_map_view.cpp` | Phase 11 tile snapshot logic in `update_snapshot()`; `render_overview()` implementation; overview mode checkbox in `render()` |
| `demo/CMakeLists.txt` | Added `test_mem_map_view_tile` target |
| `.github/workflows/ci.yml` | Added `test_mem_map_view_tile` to ctest `-R` filter |
| `tests/test_mem_map_view_tile.cpp` | New: 8 unit tests (see below) |
| `plan.md` | Added Phase 11 section to overview table and full phase description; Risk #7 marked ✅ resolved |
| `README.md` | Added `test_mem_map_view_tile` to headless test list; Phase 11 documentation reference |
| `docs/phase-11-mem-map-tile.md` | This file |

## Tests (tests/test_mem_map_view_tile.cpp)

| Test | What it verifies |
|------|-----------------|
| `small_pmm_tile_size` | `bytes_per_tile == 1` for PMM ≤ 512 KB; tile count equals total bytes |
| `large_pmm_tile_count` | tile count ≤ 65 536 for 4 MB PMM; count equals `ceil(total / bpt)` |
| `first_tile_is_manager_header` | tile[0] dominant type is `ManagerHeader` |
| `used_block_reflected_in_tiles` | at least one tile tagged Used after allocation |
| `freed_blocks_revert_in_tiles` | no tiles tagged Used after all blocks are freed |
| `tile_offsets_correct` | `tile[i].offset == i * bytes_per_tile` for all tiles |
| `tile_snapshot_null_mgr` | `update_snapshot(nullptr)` does not crash |
| `very_large_pmm_tile_bound` | tile count ≤ 65 536 for 64 MB PMM |

## Verification Criteria

- [x] All 8 `test_mem_map_view_tile` tests pass.
- [x] `mem_map_view.h` ≤ 1500 lines.
- [x] `mem_map_view.cpp` ≤ 1500 lines.
- [x] `test_mem_map_view_tile.cpp` ≤ 1500 lines.
- [x] CI `build-demo` job passes on ubuntu-latest, windows-latest, macos-latest.
- [x] `plan.md` Risk #7 updated to ✅ Resolved.
- [x] `README.md` and `docs/phase-11-mem-map-tile.md` updated.
