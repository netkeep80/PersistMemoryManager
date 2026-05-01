#pragma once
#include "pmm/address_traits.h"
#include "pmm/config.h"
#include "pmm/free_block_tree.h"
#include "pmm/heap_storage.h"
#include "pmm/logging_policy.h"
#include "pmm/static_storage.h"
#include "pmm/storage_backend.h"
#include <concepts>
#include <cstddef>
namespace pmm
{
inline constexpr size_t kMinGranuleSize = 4;
template <typename AT>
concept ValidPmmAddressTraits =
    ( AT::granule_size >= kMinGranuleSize ) && ( ( AT::granule_size & ( AT::granule_size - 1 ) ) == 0 );
static_assert( ValidPmmAddressTraits<DefaultAddressTraits>, "" );
static_assert( ValidPmmAddressTraits<SmallAddressTraits>, "" );
static_assert( ValidPmmAddressTraits<LargeAddressTraits>, "" );
template <typename AT = DefaultAddressTraits, typename LockPolicyT = config::NoLock,
          size_t GrowNum = config::kDefaultGrowNumerator, size_t GrowDen = config::kDefaultGrowDenominator,
          size_t MaxMemoryGB = 64, typename LoggingPolicyT = logging::NoLogging>
/*
## pmm-basicconfig
req: feat-001, fr-001, ur-001, ur-006, if-008, con-005, if-006
*/
struct BasicConfig
{
    static_assert( ValidPmmAddressTraits<AT>, "" );
    using address_traits                     = AT;
    using storage_backend                    = HeapStorage<AT>;
    using free_block_tree                    = AvlFreeTree<AT>;
    using lock_policy                        = LockPolicyT;
    using logging_policy                     = LoggingPolicyT;
    static constexpr size_t granule_size     = AT::granule_size;
    static constexpr size_t max_memory_gb    = MaxMemoryGB;
    static constexpr size_t grow_numerator   = GrowNum;
    static constexpr size_t grow_denominator = GrowDen;
};
template <typename AT, size_t BufferSize, size_t GrowNum = 3, size_t GrowDen = 2>
/*
## pmm-staticconfig
req: feat-001, fr-001, ur-001, ur-006, con-005
*/
struct StaticConfig
{
    static_assert( ValidPmmAddressTraits<AT>, "" );
    using address_traits                     = AT;
    using storage_backend                    = StaticStorage<BufferSize, AT>;
    using free_block_tree                    = AvlFreeTree<AT>;
    using lock_policy                        = config::NoLock;
    using logging_policy                     = logging::NoLogging;
    static constexpr size_t granule_size     = AT::granule_size;
    static constexpr size_t max_memory_gb    = 0;
    static constexpr size_t grow_numerator   = GrowNum;
    static constexpr size_t grow_denominator = GrowDen;
};
template <size_t BufferSize = 1024> using SmallEmbeddedStaticConfig = StaticConfig<SmallAddressTraits, BufferSize>;
template <size_t BufferSize = 4096> using EmbeddedStaticConfig      = StaticConfig<DefaultAddressTraits, BufferSize>;
using CacheManagerConfig    = BasicConfig<DefaultAddressTraits, config::NoLock, config::kDefaultGrowNumerator,
                                          config::kDefaultGrowDenominator, 64>;
using PersistentDataConfig  = BasicConfig<DefaultAddressTraits, config::SharedMutexLock, config::kDefaultGrowNumerator,
                                          config::kDefaultGrowDenominator, 64>;
using EmbeddedManagerConfig = BasicConfig<DefaultAddressTraits, config::NoLock, 3, 2, 64>;
using IndustrialDBConfig    = BasicConfig<DefaultAddressTraits, config::SharedMutexLock, 2, 1, 64>;
using LargeDBConfig         = BasicConfig<LargeAddressTraits, config::SharedMutexLock, 2, 1, 0>;
}
