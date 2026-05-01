#pragma once
#include "pmm/address_traits.h"
#include "pmm/storage_backend.h"
#include <cstddef>
#include <cstdint>
namespace pmm
{
/*
## pmm-staticstorage
req: feat-001, fr-001, ur-006, con-005, con-008, feat-006, if-005, sys-003, ur-010
*/
template <size_t Size, typename AT = DefaultAddressTraits> class StaticStorage
{
    static_assert( Size > 0, "" );
    static_assert( Size % AT::granule_size == 0, "" );

  public:
    using address_traits                             = AT;
    StaticStorage() noexcept                         = default;
    StaticStorage( const StaticStorage& )            = delete;
    StaticStorage& operator=( const StaticStorage& ) = delete;
    StaticStorage( StaticStorage&& )                 = delete;
    StaticStorage&   operator=( StaticStorage&& )    = delete;
    uint8_t*         base_ptr() noexcept { return _buffer; }
    const uint8_t*   base_ptr() const noexcept { return _buffer; }
    constexpr size_t total_size() const noexcept { return Size; }
/*
### pmm-staticstorage-expand
*/
    bool           resize_to( size_t ) noexcept { return false; }
    constexpr bool owns_memory() const noexcept { return false; }

  private:
    alignas( AT::granule_size ) uint8_t _buffer[Size]{};
};
static_assert( is_storage_backend_v<StaticStorage<64>>, "" );
}
