/**
 * @file manual_alloc_view.h
 * @brief ManualAllocView: interactive panel for step-by-step manual allocation testing.
 *
 * Issue #65: Add manual allocation and deallocation controls to the demo so that
 * the user can press "Alloc" and "Free" buttons to test the memory manager step by step.
 *
 * The panel keeps a list of live manually-allocated blocks and lets the user:
 *  - Specify allocation size and allocate a new block.
 *  - Select any live block and free it.
 *  - See all live blocks with their offsets and sizes.
 */

#pragma once

#include "pmm/legacy_manager.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace demo
{

/**
 * @brief Record of a single manually-allocated block.
 */
struct ManualBlock
{
    pmm::pptr<std::uint8_t> ptr;    ///< Persistent pointer returned by allocate_typed
    std::size_t             size;   ///< Requested size in bytes
    std::ptrdiff_t          offset; ///< Byte offset in managed region (for display)
    std::string             label;  ///< Short human-readable label "Alloc #N"
};

/**
 * @brief ImGui panel providing manual alloc/free controls for step-by-step PMM testing.
 *
 * Issue #65: allows the user to press "Alloc" and "Free" to exercise the memory manager
 * one operation at a time, watching the AVL tree and memory map update in real time.
 */
class ManualAllocView
{
  public:
    /// Render the Manual Allocation ImGui panel.
    void render();

    /// Free all live blocks (called on PMM reset).
    void clear();

    /// Number of live blocks currently held.
    std::size_t live_count() const noexcept { return blocks_.size(); }

  private:
    std::vector<ManualBlock> blocks_;            ///< All live manually-allocated blocks
    int                      selected_idx_ = -1; ///< Index of selected block for free, or -1
    int                      alloc_size_   = 64; ///< Requested size for next allocation (bytes)
    std::uint64_t            alloc_serial_ = 0;  ///< Monotonic counter for block labels
};

} // namespace demo
