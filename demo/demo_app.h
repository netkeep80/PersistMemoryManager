/**
 * @file demo_app.h
 * @brief DemoApp: top-level application class for the PMM visual demo.
 *
 * DemoApp owns all UI panels and the PMM memory buffer. Each frame:
 *  1. Acquire shared_lock once.
 *  2. Update all snapshots from live PMM state.
 *  3. Release lock.
 *  4. Render all ImGui panels.
 *
 * Phase 12 addition: DemoApp periodically calls validate() every
 * kValidateIntervalSec seconds and forwards the result to MetricsView.
 * The user can also trigger an immediate validate() via the "Validate now"
 * button in the Metrics panel.
 */

#pragma once

#include "mem_map_view.h"
#include "metrics_view.h"
#include "scenario_manager.h"
#include "struct_tree_view.h"

#include "persist_memory_manager.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

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

    // ── UI state ──────────────────────────────────────────────────────────────
    bool        show_help_         = false;
    bool        show_settings_     = false;
    std::size_t highlighted_block_ = static_cast<std::size_t>( -1 );

    // ── PMM buffer ────────────────────────────────────────────────────────────
    std::vector<std::uint8_t> pmm_buffer_;
    std::size_t               pmm_size_ = 8 * 1024 * 1024; // 8 MiB default

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
    /// How many seconds between automatic validate() calls (public for testing).
    static constexpr long long kValidateIntervalSec = 5;

  private:
    ValidationResult                      last_validation_{};
    std::chrono::steady_clock::time_point last_validate_time_{};
    bool                                  first_validate_ = true; ///< run validate() on first frame

    /// Run validate() on mgr and update last_validation_.
    void run_validate( pmm::PersistMemoryManager* mgr );
};

} // namespace demo
