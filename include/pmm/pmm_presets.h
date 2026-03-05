/**
 * @file pmm/pmm_presets.h
 * @brief pmm_presets — готовые инстанции AbstractPersistMemoryManager (Issue #87 Phase 8).
 *
 * Предоставляет набор готовых псевдонимов для наиболее распространённых
 * конфигураций менеджера персистентной памяти:
 *
 *   - `EmbeddedStatic1K`    — 8-bit, NoLock, StaticStorage<1024> (embedded-системы)
 *   - `SingleThreadedHeap`  — 32-bit, NoLock, HeapStorage (однопоточные приложения)
 *   - `MultiThreadedHeap`   — 32-bit, SharedMutexLock, HeapStorage (многопоточные)
 *   - `PersistentFileMapped`— 32-bit, SharedMutexLock, MMapStorage (персистентность)
 *   - `IndustrialDB`        — 64-bit, SharedMutexLock, MMapStorage (промышленные БД)
 *
 * @see plan_issue87.md §5 «Фаза 8: pmm_presets.h»
 * @see abstract_pmm.h — AbstractPersistMemoryManager
 * @version 0.1 (Issue #87 Phase 8)
 */

#pragma once

#include "pmm/abstract_pmm.h"
#include "pmm/address_traits.h"
#include "pmm/free_block_tree.h"
#include "pmm/heap_storage.h"
#include "pmm/mmap_storage.h"
#include "pmm/static_storage.h"
#include "pmm_config.h"

namespace pmm
{
namespace presets
{

/**
 * @brief Минимальный однопоточный статический менеджер.
 *
 * - 32-bit адресация (DefaultAddressTraits), 16-байтная гранула
 * - Фиксированный буфер 4096 байт, нет динамической памяти
 * - Без блокировок (embedded, однопоточные системы)
 *
 * Примечание: для полноценного 8-bit варианта с TinyAddressTraits потребуется
 * обобщение AvlFreeTree на произвольный IndexType (Phase 4+, будущая работа).
 *
 * Использование:
 * @code
 *   pmm::presets::EmbeddedStatic4K pmm;
 *   pmm.create(); // инициализировать статический буфер
 *   void* ptr = pmm.allocate(64);
 *   pmm.deallocate(ptr);
 * @endcode
 */
using EmbeddedStatic4K = AbstractPersistMemoryManager<DefaultAddressTraits, StaticStorage<4096, DefaultAddressTraits>,
                                                      AvlFreeTree<DefaultAddressTraits>, config::NoLock>;

/// @brief Псевдоним для совместимости с планом (4K вариант для embedded).
using EmbeddedStatic1K = EmbeddedStatic4K;

/**
 * @brief Стандартный однопоточный динамический менеджер.
 *
 * - 32-bit адресация, 16-байтная гранула (совместим с DefaultConfig)
 * - Динамическая память через HeapStorage (std::malloc)
 * - Без блокировок (для однопоточных приложений)
 *
 * Использование:
 * @code
 *   pmm::presets::SingleThreadedHeap pmm;
 *   pmm.create(64 * 1024); // 64 KiB начальный размер
 *   void* ptr = pmm.allocate(256);
 *   pmm.deallocate(ptr);
 * @endcode
 */
using SingleThreadedHeap = AbstractPersistMemoryManager<DefaultAddressTraits, HeapStorage<DefaultAddressTraits>,
                                                        AvlFreeTree<DefaultAddressTraits>, config::NoLock>;

/**
 * @brief Стандартный многопоточный динамический менеджер.
 *
 * - 32-bit адресация, 16-байтная гранула (совместим с DefaultConfig + SharedMutexLock)
 * - Динамическая память через HeapStorage
 * - Блокировки через std::shared_mutex
 */
using MultiThreadedHeap = AbstractPersistMemoryManager<DefaultAddressTraits, HeapStorage<DefaultAddressTraits>,
                                                       AvlFreeTree<DefaultAddressTraits>, config::SharedMutexLock>;

/**
 * @brief Многопоточный файловый менеджер с персистентностью через mmap.
 *
 * - 32-bit адресация, 16-байтная гранула
 * - Данные персистируются в файл через MMapStorage
 * - Блокировки через std::shared_mutex
 *
 * Использование:
 * @code
 *   pmm::presets::PersistentFileMapped pmm;
 *   if (!pmm.backend().open("/tmp/pmm.dat", 64 * 1024)) {
 *       // обработать ошибку
 *   }
 *   if (!pmm.load()) {
 *       pmm.create(); // первый запуск — инициализировать
 *   }
 * @endcode
 */
using PersistentFileMapped = AbstractPersistMemoryManager<DefaultAddressTraits, MMapStorage<DefaultAddressTraits>,
                                                          AvlFreeTree<DefaultAddressTraits>, config::SharedMutexLock>;

/**
 * @brief Крупная промышленная персистентная база данных (32-bit, mmap).
 *
 * - 32-bit адресация, 16-байтная гранула
 * - Данные персистируются через MMapStorage
 * - Блокировки через std::shared_mutex
 *
 * Примечание: 64-bit вариант с LargeAddressTraits потребует обобщения
 * AvlFreeTree на uint64_t (Phase 4+, будущая работа).
 */
using IndustrialDB = AbstractPersistMemoryManager<DefaultAddressTraits, MMapStorage<DefaultAddressTraits>,
                                                  AvlFreeTree<DefaultAddressTraits>, config::SharedMutexLock>;

} // namespace presets
} // namespace pmm
