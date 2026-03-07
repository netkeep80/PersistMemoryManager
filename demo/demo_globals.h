/**
 * @file demo_globals.h
 * @brief Global shared PMM manager for the visual demo.
 *
 * Provides a global std::atomic<DemoMgr*> accessed by all demo threads
 * and panels. The manager itself is owned by DemoApp.
 */

#pragma once

#include "pmm/pmm_presets.h"

#include <atomic>

namespace demo
{

/// @brief Manager type used throughout the demo (multi-threaded, heap-backed).
using DemoMgr = pmm::presets::MultiThreadedHeap;

/// @brief Global pointer to the active manager. nullptr means no active manager.
/// Owned by DemoApp; all other code reads via load().
extern std::atomic<DemoMgr*> g_pmm;

} // namespace demo
