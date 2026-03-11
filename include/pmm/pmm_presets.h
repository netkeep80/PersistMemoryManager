/**
 * @file pmm/pmm_presets.h
 * @brief pmm_presets — готовые инстанции PersistMemoryManager (Issue #87 Phase 8, #110, #123, #146).
 *
 * Предоставляет набор готовых псевдонимов для наиболее распространённых
 * конфигураций менеджера персистентной памяти:
 *
 *   --- Embedded (статические, однопоточные) ---
 *   - `SmallEmbeddedStaticHeap<N>` — 16-bit, NoLock, StaticStorage<N> (до ~1 МБ, small embedded)
 *   - `EmbeddedStaticHeap<N>`      — 32-bit, NoLock, StaticStorage<N> (без heap, фиксированный пул)
 *   - `EmbeddedHeap`               — 32-bit, NoLock, HeapStorage, рост 50% (embedded с heap)
 *
 *   --- Desktop (динамические, однопоточные/многопоточные) ---
 *   - `SingleThreadedHeap`      — 32-bit/16B, NoLock, HeapStorage, рост 25%
 *   - `MultiThreadedHeap`       — 32-bit/16B, SharedMutexLock, HeapStorage, рост 25%
 *
 *   --- Industrial DB (высоконагруженные, многопоточные) ---
 *   - `IndustrialDBHeap`        — 32-bit/16B, SharedMutexLock, HeapStorage, рост 100%
 *
 *   --- Large DB (крупные базы данных, 64-bit) ---
 *   - `LargeDBHeap`             — 64-bit/64B, SharedMutexLock, HeapStorage, рост 100%
 *
 * Использует унифицированный `PersistMemoryManager<ConfigT, InstanceId>` (Issue #110).
 *
 * @see persist_memory_manager.h — PersistMemoryManager (Issue #110)
 * @see manager_configs.h — готовые конфигурации менеджеров
 * @version 0.5 (Issue #146 — добавлена поддержка 16-bit и 64-bit индексов)
 */

#pragma once

#include "pmm/manager_configs.h"
#include "pmm/persist_memory_manager.h"

namespace pmm
{
namespace presets
{

// ─── Embedded пресеты ─────────────────────────────────────────────────────────

/**
 * @brief Tiny embedded-менеджер с фиксированным статическим буфером и 16-bit индексом.
 *
 * - 16-bit адресация (SmallAddressTraits), 16-байтная гранула
 * - pptr<T> хранит 2-байтный индекс — экономия памяти на ARM Cortex-M, AVR, ESP32
 * - StaticStorage<BufferSize, SmallAddressTraits> — фиксированный буфер, нет malloc
 * - Максимальный пул: 65535 × 16 = ~1 МБ
 * - Без блокировок (для однопоточных embedded-систем без heap)
 * - Не расширяется (expand() всегда false)
 * - InstanceId=0 (по умолчанию)
 *
 * @tparam BufferSize Размер статического буфера в байтах (кратно 16, максимум ~1 МБ).
 *                    По умолчанию 1024 байт (1 КБ).
 *
 * Использование:
 * @code
 *   using TinyMgr = pmm::presets::SmallEmbeddedStaticHeap<1024>;
 *   TinyMgr::create(1024);
 *   void* ptr = TinyMgr::allocate(32);
 *   TinyMgr::deallocate(ptr);
 *   // sizeof(TinyMgr::pptr<int>) == 2  (16-bit индекс)
 * @endcode
 */
template <std::size_t BufferSize = 1024>
using SmallEmbeddedStaticHeap = PersistMemoryManager<SmallEmbeddedStaticConfig<BufferSize>, 0>;

/**
 * @brief Embedded-менеджер с фиксированным статическим буфером (без heap).
 *
 * - 32-bit адресация (DefaultAddressTraits), 16-байтная гранула
 * - StaticStorage<BufferSize> — фиксированный буфер в BSS/глобальной области, нет malloc
 * - Без блокировок (для однопоточных embedded-систем без heap)
 * - Не расширяется (expand() всегда false)
 * - InstanceId=0 (по умолчанию)
 *
 * @tparam BufferSize Размер статического буфера в байтах (кратно 16).
 *                    По умолчанию 4096 байт (4 КБ).
 *
 * Использование:
 * @code
 *   using MyEmbMgr = pmm::presets::EmbeddedStaticHeap<8192>; // 8 KiB статический пул
 *   MyEmbMgr::create(8192);
 *   void* ptr = MyEmbMgr::allocate(64);
 *   MyEmbMgr::deallocate(ptr);
 * @endcode
 */
template <std::size_t BufferSize = 4096>
using EmbeddedStaticHeap = PersistMemoryManager<EmbeddedStaticConfig<BufferSize>, 0>;

/**
 * @brief Стандартный embedded-менеджер с динамической памятью.
 *
 * - 32-bit адресация, 16-байтная гранула
 * - Динамическая память через HeapStorage
 * - Без блокировок (однопоточный доступ)
 * - Консервативный коэффициент роста 3/2 (50%) для экономии памяти
 * - InstanceId=0 (по умолчанию)
 *
 * Использование:
 * @code
 *   pmm::presets::EmbeddedHeap::create(4 * 1024); // 4 KiB начальный размер
 *   void* ptr = pmm::presets::EmbeddedHeap::allocate(64);
 *   pmm::presets::EmbeddedHeap::deallocate(ptr);
 * @endcode
 */
using EmbeddedHeap = PersistMemoryManager<EmbeddedManagerConfig, 0>;

// ─── Desktop пресеты ──────────────────────────────────────────────────────────

/**
 * @brief Стандартный однопоточный динамический менеджер.
 *
 * - 32-bit адресация, 16-байтная гранула
 * - Динамическая память через HeapStorage (std::malloc)
 * - Без блокировок (для однопоточных приложений)
 * - Коэффициент роста 5/4 (25%)
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
 * - Коэффициент роста 5/4 (25%)
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

// ─── Industrial DB пресеты ────────────────────────────────────────────────────

/**
 * @brief Менеджер для промышленных баз данных с высокой нагрузкой (32-bit).
 *
 * - 32-bit адресация, 16-байтная гранула, поддержка до 64 ГБ
 * - Динамическая память через HeapStorage
 * - Блокировки через std::shared_mutex
 * - Агрессивный коэффициент роста 2/1 (100%) для минимизации перевыделений
 * - InstanceId=0 (по умолчанию)
 *
 * Использование:
 * @code
 *   pmm::presets::IndustrialDBHeap::create(256 * 1024 * 1024); // 256 MiB начальный размер
 *   void* ptr = pmm::presets::IndustrialDBHeap::allocate(4096);
 *   pmm::presets::IndustrialDBHeap::deallocate(ptr);
 * @endcode
 */
using IndustrialDBHeap = PersistMemoryManager<IndustrialDBConfig, 0>;

// ─── Large DB пресеты (64-bit индекс) ────────────────────────────────────────

/**
 * @brief Менеджер для крупных баз данных с 64-bit индексом.
 *
 * - 64-bit адресация (LargeAddressTraits), 64-байтная гранула
 * - pptr<T> хранит 8-байтный индекс — адресует петабайтный масштаб
 * - Динамическая память через HeapStorage
 * - Блокировки через std::shared_mutex
 * - Агрессивный коэффициент роста 2/1 (100%) для минимизации перевыделений
 * - InstanceId=0 (по умолчанию)
 *
 * Использование:
 * @code
 *   pmm::presets::LargeDBHeap::create(256 * 1024 * 1024); // 256 MiB начальный размер
 *   void* ptr = pmm::presets::LargeDBHeap::allocate(4096);
 *   pmm::presets::LargeDBHeap::deallocate(ptr);
 *   // sizeof(LargeDBHeap::pptr<int>) == 8  (64-bit индекс)
 * @endcode
 */
using LargeDBHeap = PersistMemoryManager<LargeDBConfig, 0>;

} // namespace presets
} // namespace pmm
