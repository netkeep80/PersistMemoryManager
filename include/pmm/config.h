/**
 * @file pmm_config.h
 * @brief Политики блокировок для AbstractPersistMemoryManager
 *
 * Содержит:
 *   - pmm::config::SharedMutexLock  — политика с std::shared_mutex (многопоточная)
 *   - pmm::config::NoLock           — заглушка без блокировок (однопоточный режим)
 *   - pmm::config::kDefaultGrowNumerator / kDefaultGrowDenominator — параметры роста
 *
 * Использование:
 * @code
 *   // Однопоточный менеджер без блокировок
 *   using MyMgr = AbstractPersistMemoryManager<DefaultAddressTraits, HeapStorage<...>,
 *                                              AvlFreeTree<...>, pmm::config::NoLock>;
 *
 *   // Многопоточный менеджер с блокировками
 *   using MyMgr = AbstractPersistMemoryManager<DefaultAddressTraits, HeapStorage<...>,
 *                                              AvlFreeTree<...>, pmm::config::SharedMutexLock>;
 * @endcode
 *
 * @see pmm_presets.h — готовые конфигурации менеджеров
 * @version 2.0
 */

#pragma once

#include <mutex>
#include <shared_mutex>

namespace pmm
{
namespace config
{

/// @brief Политика блокировки с std::shared_mutex (многопоточная, по умолчанию).
struct SharedMutexLock
{
    using mutex_type       = std::shared_mutex;
    using shared_lock_type = std::shared_lock<std::shared_mutex>;
    using unique_lock_type = std::unique_lock<std::shared_mutex>;
};

/// @brief Политика без блокировок (однопоточный режим, нет накладных расходов).
struct NoLock
{
    struct mutex_type
    {
        void lock() {}
        void unlock() {}
        void lock_shared() {}
        void unlock_shared() {}
        bool try_lock() { return true; }
        bool try_lock_shared() { return true; }
    };

    struct shared_lock_type
    {
        explicit shared_lock_type( mutex_type& ) {}
    };

    struct unique_lock_type
    {
        explicit unique_lock_type( mutex_type& ) {}
    };
};

/// @brief Default grow ratio numerator (heap grows by 5/4 = 25%).
inline constexpr std::size_t kDefaultGrowNumerator = 5;

/// @brief Default grow ratio denominator (heap grows by 5/4 = 25%).
inline constexpr std::size_t kDefaultGrowDenominator = 4;

} // namespace config
} // namespace pmm
