/**
 * @file demo_globals.h
 * @brief Global shared PMM manager for the visual demo.
 *
 * DemoMgr is a fully static class (PersistMemoryManager), so no instance
 * pointer is needed. g_pmm is a boolean flag indicating whether the manager
 * is currently active (initialized). All demo threads check this flag before
 * calling DemoMgr static methods.
 */

#pragma once

#include "pmm_multi_threaded_heap.h"

#include <atomic>

namespace demo
{

/// @brief Manager type used throughout the demo (multi-threaded, heap-backed).
using DemoMgr = pmm::presets::MultiThreadedHeap;

/// @brief Global flag: true when the DemoMgr static manager is active.
/// Set to true after DemoMgr::create(), false after DemoMgr::destroy().
extern std::atomic<bool> g_pmm;

} // namespace demo
