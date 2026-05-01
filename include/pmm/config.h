#pragma once
#include <mutex>
#include <shared_mutex>
namespace pmm
{
namespace config
{
/*
### pmm-config-sharedmutexlock
*/
struct SharedMutexLock
{
    using mutex_type       = std::shared_mutex;
    using shared_lock_type = std::shared_lock<std::shared_mutex>;
    using unique_lock_type = std::unique_lock<std::shared_mutex>;
};
/*
### pmm-config-nolock
req: if-006
*/
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
inline constexpr size_t kDefaultGrowNumerator   = 5;
inline constexpr size_t kDefaultGrowDenominator = 4;
}
}
