/**
 * @file pmm/manager_configs.h
 * @brief Готовые конфигурационные структуры для менеджеров ПАП (Issue #100, #110).
 *
 * Предоставляет набор предопределённых конфигурационных структур для использования
 * с `PersistMemoryManager<ConfigT, InstanceId>`. Каждая конфигурация описывает
 * типичный сценарий использования менеджера персистентной памяти.
 *
 * Конфигурация включает:
 *   - `address_traits`   — тип адресного пространства (размер индекса, гранулы)
 *   - `storage_backend`  — бэкенд хранилища (HeapStorage, StaticStorage, MMapStorage)
 *   - `free_block_tree`  — политика дерева свободных блоков (AvlFreeTree)
 *   - `lock_policy`      — политика многопоточности (NoLock, SharedMutexLock)
 *   - `granule_size`     — размер гранулы в байтах
 *   - `max_memory_gb`    — максимальный объём памяти в ГБ
 *   - `grow_numerator` / `grow_denominator` — коэффициент роста хранилища
 *
 * Доступные конфигурации:
 *   - `CacheManagerConfig`      — кеш-менеджер (однопоточный, NoLock, HeapStorage)
 *   - `PersistentDataConfig`    — персистентные данные (многопоточный, SharedMutexLock, HeapStorage)
 *   - `EmbeddedManagerConfig`   — embedded-системы (однопоточный, NoLock, HeapStorage)
 *   - `IndustrialDBConfig`      — промышленная БД (многопоточный, SharedMutexLock, HeapStorage)
 *
 * Пример использования:
 * @code
 *   // Используем готовую конфигурацию для создания менеджера
 *   using AppCache = pmm::PersistMemoryManager<pmm::CacheManagerConfig>;
 *
 *   AppCache::create(64 * 1024);
 *
 *   AppCache::pptr<int> ptr = AppCache::allocate_typed<int>();
 *   *ptr = 42;
 * @endcode
 *
 * @see persist_memory_manager.h — PersistMemoryManager (Issue #110)
 * @see config.h — базовые политики блокировок (NoLock, SharedMutexLock)
 * @version 0.2 (Issue #110 — добавлены address_traits, storage_backend, free_block_tree в конфигурации)
 */

#pragma once

#include "pmm/address_traits.h"
#include "pmm/config.h"
#include "pmm/free_block_tree.h"
#include "pmm/heap_storage.h"
#include "pmm/storage_backend.h"

namespace pmm
{

/**
 * @brief Конфигурация кеш-менеджера.
 *
 * Оптимизирован для временного кеша с однопоточным доступом:
 *   - Нет блокировок (NoLock) — максимальная производительность
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память
 *   - Коэффициент роста 5/4 (25%)
 *
 * Типичный сценарий: кеш вычислений, временные буферы в однопоточном коде.
 */
struct CacheManagerConfig
{
    using address_traits                          = DefaultAddressTraits;
    using storage_backend                         = HeapStorage<DefaultAddressTraits>;
    using free_block_tree                         = AvlFreeTree<DefaultAddressTraits>;
    using lock_policy                             = config::NoLock;
    static constexpr std::size_t granule_size     = 16;
    static constexpr std::size_t max_memory_gb    = 64;
    static constexpr std::size_t grow_numerator   = config::kDefaultGrowNumerator;
    static constexpr std::size_t grow_denominator = config::kDefaultGrowDenominator;
};

/**
 * @brief Конфигурация менеджера персистентных данных.
 *
 * Оптимизирован для хранения персистентных данных с многопоточным доступом:
 *   - SharedMutexLock — потокобезопасность
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память
 *   - Коэффициент роста 5/4 (25%)
 *
 * Типичный сценарий: долговременное хранение данных, файловые менеджеры.
 */
struct PersistentDataConfig
{
    using address_traits                          = DefaultAddressTraits;
    using storage_backend                         = HeapStorage<DefaultAddressTraits>;
    using free_block_tree                         = AvlFreeTree<DefaultAddressTraits>;
    using lock_policy                             = config::SharedMutexLock;
    static constexpr std::size_t granule_size     = 16;
    static constexpr std::size_t max_memory_gb    = 64;
    static constexpr std::size_t grow_numerator   = config::kDefaultGrowNumerator;
    static constexpr std::size_t grow_denominator = config::kDefaultGrowDenominator;
};

/**
 * @brief Конфигурация embedded-менеджера.
 *
 * Оптимизирован для встраиваемых систем с ограниченными ресурсами:
 *   - Нет блокировок (NoLock) — минимальные накладные расходы
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память
 *   - Консервативный коэффициент роста 3/2 (50%) для лучшего использования памяти
 *
 * Типичный сценарий: микроконтроллеры, RTOS, системы с ограниченной памятью.
 */
struct EmbeddedManagerConfig
{
    using address_traits                          = DefaultAddressTraits;
    using storage_backend                         = HeapStorage<DefaultAddressTraits>;
    using free_block_tree                         = AvlFreeTree<DefaultAddressTraits>;
    using lock_policy                             = config::NoLock;
    static constexpr std::size_t granule_size     = 16;
    static constexpr std::size_t max_memory_gb    = 64;
    static constexpr std::size_t grow_numerator   = 3;
    static constexpr std::size_t grow_denominator = 2;
};

/**
 * @brief Конфигурация промышленной базы данных.
 *
 * Оптимизирован для высоконагруженных промышленных систем:
 *   - SharedMutexLock — потокобезопасность с поддержкой конкурентного чтения
 *   - 16-байтная гранула (DefaultAddressTraits), поддержка до 64 ГБ
 *   - HeapStorage — динамическая память
 *   - Агрессивный коэффициент роста 2/1 (100%) для минимизации перевыделений
 *
 * Типичный сценарий: промышленные базы данных, time-series хранилища.
 */
struct IndustrialDBConfig
{
    using address_traits                          = DefaultAddressTraits;
    using storage_backend                         = HeapStorage<DefaultAddressTraits>;
    using free_block_tree                         = AvlFreeTree<DefaultAddressTraits>;
    using lock_policy                             = config::SharedMutexLock;
    static constexpr std::size_t granule_size     = 16;
    static constexpr std::size_t max_memory_gb    = 64;
    static constexpr std::size_t grow_numerator   = 2;
    static constexpr std::size_t grow_denominator = 1;
};

} // namespace pmm
