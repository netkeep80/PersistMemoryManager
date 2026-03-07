/**
 * @file demo_app.h
 * @brief DemoApp: top-level application class for the PMM visual demo.
 *
 * DemoApp owns all UI panels and the PMM memory manager. Each frame:
 *  1. Acquire snapshots from live PMM state.
 *  2. Render all ImGui panels.
 *
 * The manager is a DemoMgr (MultiThreadedHeap) whose raw pointer is exposed
 * globally via demo::g_pmm for access by scenario threads.
 *
 * Issue #65 addition: AvlTreeView and ManualAllocView panels.
 */

#pragma once

#include "avl_tree_view.h"
#include "demo_globals.h"
#include "manual_alloc_view.h"
#include "mem_map_view.h"
#include "metrics_view.h"
#include "scenario_manager.h"
#include "struct_tree_view.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace demo
{

/**
 * @brief Main application object: renders all panels and manages PMM lifecycle.
 */
class DemoApp
{
  public:
    DemoApp();
    ~DemoApp();

    /// Called once per frame from the main render loop.
    void render();

  private:
    void render_main_menu();
    void render_dockspace();
    void render_help_window();
    void render_settings_window();

    // ── Panels ────────────────────────────────────────────────────────────────
    std::unique_ptr<MemMapView>      mem_map_view_;
    std::unique_ptr<MetricsView>     metrics_view_;
    std::unique_ptr<StructTreeView>  struct_tree_view_;
    std::unique_ptr<ScenarioManager> scenario_manager_;
    std::unique_ptr<AvlTreeView>     avl_tree_view_;     ///< Issue #65: AVL free-block tree
    std::unique_ptr<ManualAllocView> manual_alloc_view_; ///< Issue #65: manual alloc/free

    // ── UI state ──────────────────────────────────────────────────────────────
    bool        show_help_         = false;
    bool        show_settings_     = false;
    std::size_t highlighted_block_ = static_cast<std::size_t>( -1 );

    // ── PMM manager ───────────────────────────────────────────────────────────
    std::unique_ptr<DemoMgr> pmm_manager_;
    std::size_t              pmm_size_ = 8 * 1024 * 1024; // 8 MiB default

    // ── ops/s measurement ─────────────────────────────────────────────────────
    std::atomic<uint64_t>                 ops_counter_{ 0 };
    float                                 ops_per_sec_ = 0.0f;
    std::chrono::steady_clock::time_point last_ops_sample_;

    // ── Settings state ────────────────────────────────────────────────────────
    int pmm_size_idx_ = 1; // 0=1MB, 1=8MB, 2=32MB, 3=256MB
    int fps_limit_    = 60;
    int theme_idx_    = 0; // 0=Dark, 1=Light, 2=Classic

    void apply_pmm_size();

  public:
    // ── Phase 12: Background integrity validation ─────────────────────────────
    /// How many seconds between automatic is_initialized() checks (public for testing).
    static constexpr long long kValidateIntervalSec = 5;

  private:
    ValidationResult                      last_validation_{};
    std::chrono::steady_clock::time_point last_validate_time_{};
    bool                                  first_validate_ = true; ///< run check on first frame

    /// Run is_initialized() check and update last_validation_.
    void run_validate();
};

} // namespace demo
