/**
 * @file pmm/pmm_presets.h
 * @brief pmm_presets — готовые инстанции PersistMemoryManager.
 *
 * Public aliases over PersistMemoryManager<ConfigT, InstanceId>.
 *
 * @see persist_memory_manager.h — PersistMemoryManager
 * @see manager_configs.h — готовые конфигурации менеджеров
 * @version 0.5
 */

#pragma once

#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"

namespace pmm
{
namespace presets
{

// ─── Embedded пресеты ─────────────────────────────────────────────────────────

/// Static NoLock manager with 16-bit indexes, 16-byte granules, and no growth.
template <std::size_t BufferSize = 1024>
using SmallEmbeddedStaticHeap = PersistMemoryManager<SmallEmbeddedStaticConfig<BufferSize>, 0>;

/// Static NoLock manager with 32-bit indexes, 16-byte granules, and no growth.
template <std::size_t BufferSize = 4096>
using EmbeddedStaticHeap = PersistMemoryManager<EmbeddedStaticConfig<BufferSize>, 0>;

/// HeapStorage NoLock manager with 32-bit indexes, 16-byte granules, and 50% growth.
using EmbeddedHeap = PersistMemoryManager<EmbeddedManagerConfig, 0>;

// ─── Desktop пресеты ──────────────────────────────────────────────────────────

/// HeapStorage NoLock manager with 32-bit indexes, 16-byte granules, and 25% growth.
using SingleThreadedHeap = PersistMemoryManager<CacheManagerConfig, 0>;

/// HeapStorage SharedMutexLock manager with 32-bit indexes, 16-byte granules, and 25% growth.
using MultiThreadedHeap = PersistMemoryManager<PersistentDataConfig, 0>;

// ─── Industrial DB пресеты ────────────────────────────────────────────────────

/// HeapStorage SharedMutexLock manager with 32-bit indexes, 16-byte granules, and 100% growth.
using IndustrialDBHeap = PersistMemoryManager<IndustrialDBConfig, 0>;

// ─── Large DB пресеты (64-bit индекс) ────────────────────────────────────────

/// HeapStorage SharedMutexLock manager with 64-bit indexes, 64-byte granules, and 100% growth.
using LargeDBHeap = PersistMemoryManager<LargeDBConfig, 0>;

} // namespace presets
} // namespace pmm
