/**
 * @file pmm/manager_configs.h
 * @brief Готовые конфигурационные структуры для менеджеров ПАП (Issue #100).
 *
 * Предоставляет набор предопределённых конфигурационных структур для использования
 * с `StaticPersistMemoryManager<ConfigT, Tag>`. Каждая конфигурация описывает
 * типичный сценарий использования менеджера персистентной памяти.
 *
 * Доступные конфигурации:
 *   - `CacheManagerConfig`      — кеш-менеджер (однопоточный, NoLock)
 *   - `PersistentDataConfig`    — персистентные данные (многопоточный, SharedMutexLock)
 *   - `EmbeddedManagerConfig`   — embedded-системы (однопоточный, NoLock, малый объём)
 *   - `IndustrialDBConfig`      — промышленная БД (многопоточный, SharedMutexLock)
 *
 * Пример использования:
 * @code
 *   // Используем готовую конфигурацию для создания менеджера
 *   struct AppCacheTag {};
 *   using AppCache = pmm::StaticPersistMemoryManager<pmm::CacheManagerConfig, AppCacheTag>;
 *
 *   AppCache cache;
 *   cache.create(64 * 1024);
 *
 *   AppCache::pptr<int> ptr = cache.allocate_typed<int>();
 *   *ptr.resolve(cache) = 42;
 * @endcode
 *
 * @see static_manager_factory.h — StaticPersistMemoryManager
 * @see config.h — базовые политики блокировок (NoLock, SharedMutexLock, PMMConfig)
 * @version 0.1 (Issue #100 — Phase 2: Static Manager Factories)
 */

#pragma once

#include "pmm/config.h"

namespace pmm
{

/**
 * @brief Конфигурация кеш-менеджера.
 *
 * Оптимизирован для временного кеша с однопоточным доступом:
 *   - Нет блокировок (NoLock) — максимальная производительность
 *   - 16-байтная гранула, поддержка до 64 ГБ
 *   - Коэффициент роста 5/4 (25%)
 *
 * Типичный сценарий: кеш вычислений, временные буферы в однопоточном коде.
 */
struct CacheManagerConfig
{
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
 *   - 16-байтная гранула, поддержка до 64 ГБ
 *   - Коэффициент роста 5/4 (25%)
 *
 * Типичный сценарий: долговременное хранение данных, файловые менеджеры.
 */
struct PersistentDataConfig
{
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
 *   - 16-байтная гранула, поддержка до 64 ГБ
 *   - Консервативный коэффициент роста 3/2 (50%) для лучшего использования памяти
 *
 * Типичный сценарий: микроконтроллеры, RTOS, системы с ограниченной памятью.
 */
struct EmbeddedManagerConfig
{
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
 *   - 16-байтная гранула, поддержка до 64 ГБ
 *   - Агрессивный коэффициент роста 2/1 (100%) для минимизации перевыделений
 *
 * Типичный сценарий: промышленные базы данных, time-series хранилища.
 */
struct IndustrialDBConfig
{
    using lock_policy                             = config::SharedMutexLock;
    static constexpr std::size_t granule_size     = 16;
    static constexpr std::size_t max_memory_gb    = 64;
    static constexpr std::size_t grow_numerator   = 2;
    static constexpr std::size_t grow_denominator = 1;
};

} // namespace pmm
