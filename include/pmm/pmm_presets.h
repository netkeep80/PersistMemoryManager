/**
 * @file pmm/pmm_presets.h
 * @brief pmm_presets — готовые инстанции PersistMemoryManager (Issue #87 Phase 8, #110).
 *
 * Предоставляет набор готовых псевдонимов для наиболее распространённых
 * конфигураций менеджера персистентной памяти:
 *
 *   - `SingleThreadedHeap`  — 32-bit, NoLock, HeapStorage (однопоточные приложения)
 *   - `MultiThreadedHeap`   — 32-bit, SharedMutexLock, HeapStorage (многопоточные)
 *
 * Использует унифицированный `PersistMemoryManager<ConfigT, InstanceId>` (Issue #110).
 *
 * @see persist_memory_manager.h — PersistMemoryManager (Issue #110)
 * @see manager_configs.h — готовые конфигурации менеджеров
 * @version 0.2 (Issue #110 — унификация, переход на PersistMemoryManager)
 */

#pragma once

#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"

namespace pmm
{
namespace presets
{

/**
 * @brief Стандартный однопоточный динамический менеджер.
 *
 * - 32-bit адресация, 16-байтная гранула
 * - Динамическая память через HeapStorage (std::malloc)
 * - Без блокировок (для однопоточных приложений)
 * - InstanceId=0 (по умолчанию)
 *
 * Использование:
 * @code
 *   pmm::presets::SingleThreadedHeap::create(64 * 1024); // 64 KiB начальный размер
 *   void* ptr = pmm::presets::SingleThreadedHeap::allocate(256);
 *   pmm::presets::SingleThreadedHeap::deallocate(ptr);
 * @endcode
 */
using SingleThreadedHeap = PersistMemoryManager<CacheManagerConfig, 0>;

/**
 * @brief Стандартный многопоточный динамический менеджер.
 *
 * - 32-bit адресация, 16-байтная гранула
 * - Динамическая память через HeapStorage
 * - Блокировки через std::shared_mutex
 * - InstanceId=0 (по умолчанию)
 *
 * Использование:
 * @code
 *   pmm::presets::MultiThreadedHeap::create(64 * 1024);
 *   void* ptr = pmm::presets::MultiThreadedHeap::allocate(256);
 *   pmm::presets::MultiThreadedHeap::deallocate(ptr);
 * @endcode
 */
using MultiThreadedHeap = PersistMemoryManager<PersistentDataConfig, 0>;

} // namespace presets
} // namespace pmm
