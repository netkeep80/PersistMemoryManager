/**
 * @file pmm_config.h
 * @brief Политики конфигурации для PersistMemoryManager (Issue #73)
 *
 * Содержит:
 *   - pmm::config::SharedMutexLock  — политика с std::shared_mutex (по умолчанию)
 *   - pmm::config::NoLock           — заглушка без блокировок (однопоточный режим)
 *   - pmm::config::PMMConfig<>      — конфигурационный шаблон (Issue #73 FR-05)
 *
 * Использование:
 * @code
 *   // Default config (SharedMutexLock, 16-byte granule, 64 GB max)
 *   using PMM = pmm::PersistMemoryManager<>;
 *
 *   // Custom single-threaded config
 *   using MyConfig = pmm::config::PMMConfig<16, 64, pmm::config::NoLock>;
 *   using PMM = pmm::PersistMemoryManager<MyConfig>;
 * @endcode
 *
 * @version 1.0 (Issue #73 refactoring)
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

/// @brief Issue #83: Default grow ratio numerator (heap grows by 5/4 = 25%).
inline constexpr std::size_t kDefaultGrowNumerator = 5;

/// @brief Issue #83: Default grow ratio denominator (heap grows by 5/4 = 25%).
inline constexpr std::size_t kDefaultGrowDenominator = 4;

/// @brief Конфигурационный шаблон PersistMemoryManager (Issue #73 FR-05, #83).
/// @tparam GranuleSizeV       Размер гранулы в байтах (только 16 поддерживается, Issue #59).
/// @tparam MaxMemoryGB        Максимальный объём памяти в ГБ (до 64 ГБ, Issue #59).
/// @tparam LockPolicy         Политика блокировок (SharedMutexLock или NoLock).
/// @tparam GrowNumeratorV     Issue #83: Числитель коэффициента роста (по умолчанию 5).
/// @tparam GrowDenominatorV   Issue #83: Знаменатель коэффициента роста (по умолчанию 4).
template <std::size_t GranuleSizeV = 16, std::size_t MaxMemoryGB = 64, typename LockPolicy = SharedMutexLock,
          std::size_t GrowNumeratorV = kDefaultGrowNumerator, std::size_t GrowDenominatorV = kDefaultGrowDenominator>
struct PMMConfig
{
    static constexpr std::size_t granule_size     = GranuleSizeV;
    static constexpr std::size_t max_memory_gb    = MaxMemoryGB;
    static constexpr std::size_t grow_numerator   = GrowNumeratorV;
    static constexpr std::size_t grow_denominator = GrowDenominatorV;
    using lock_policy                             = LockPolicy;
};

/// @brief Конфигурация по умолчанию.
using DefaultConfig = PMMConfig<16, 64, SharedMutexLock>;

} // namespace config
} // namespace pmm
