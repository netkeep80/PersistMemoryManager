#pragma once

#include "pmm/address_traits.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pmm::detail
{

inline constexpr std::size_t kForestDomainNameCapacity = 48;
inline constexpr std::size_t kMaxForestDomains         = 32;

inline constexpr const char*   kSystemDomainFreeTree         = "system/free_tree";
inline constexpr const char*   kSystemDomainSymbols          = "system/symbols";
inline constexpr const char*   kSystemDomainRegistry         = "system/domain_registry";
inline constexpr const char*   kSystemTypeForestRegistry     = "type/forest_registry";
inline constexpr const char*   kSystemTypeForestDomainRecord = "type/forest_domain_record";
inline constexpr const char*   kSystemTypePstringview        = "type/pstringview";
inline constexpr const char*   kServiceNameLegacyRoot        = "service/legacy_root";
inline constexpr const char*   kServiceNameDomainRoot        = "service/domain_root";
inline constexpr const char*   kServiceNameDomainSymbol      = "service/domain_symbol";
inline constexpr std::uint32_t kForestRegistryMagic          = 0x50465247U; // "PFRG"
inline constexpr std::uint16_t kForestRegistryVersion        = 1;
inline constexpr std::uint8_t  kForestBindingDirectRoot      = 0;
inline constexpr std::uint8_t  kForestBindingFreeTree        = 1;
inline constexpr std::uint8_t  kForestDomainFlagSystem       = 0x01;

template <typename AddressTraitsT> struct ForestDomainRecord
{
    using index_type = typename AddressTraitsT::index_type;

    index_type    binding_id;
    index_type    root_offset;
    index_type    symbol_offset;
    std::uint8_t  binding_kind;
    std::uint8_t  flags;
    std::uint16_t reserved;
    char          name[kForestDomainNameCapacity];

    constexpr ForestDomainRecord() noexcept
        : binding_id( 0 ), root_offset( 0 ), symbol_offset( 0 ), binding_kind( kForestBindingDirectRoot ), flags( 0 ),
          reserved( 0 ), name{}
    {
    }
};

template <typename AddressTraitsT> struct ForestDomainRegistry
{
    using index_type = typename AddressTraitsT::index_type;

    std::uint32_t                      magic;
    std::uint16_t                      version;
    std::uint16_t                      domain_count;
    index_type                         legacy_root_offset;
    index_type                         next_binding_id;
    ForestDomainRecord<AddressTraitsT> domains[kMaxForestDomains];

    constexpr ForestDomainRegistry() noexcept
        : magic( kForestRegistryMagic ), version( kForestRegistryVersion ), domain_count( 0 ), legacy_root_offset( 0 ),
          next_binding_id( 1 ), domains{}
    {
    }
};

template <typename AddressTraitsT>
inline bool forest_domain_name_equals( const ForestDomainRecord<AddressTraitsT>& rec, const char* name ) noexcept
{
    if ( name == nullptr )
        return false;
    return std::strncmp( rec.name, name, kForestDomainNameCapacity ) == 0;
}

inline bool forest_domain_name_fits( const char* name ) noexcept
{
    if ( name == nullptr || name[0] == '\0' )
        return false;
    return std::strlen( name ) < kForestDomainNameCapacity;
}

template <typename AddressTraitsT>
inline bool forest_domain_name_copy( ForestDomainRecord<AddressTraitsT>& rec, const char* name ) noexcept
{
    if ( !forest_domain_name_fits( name ) )
        return false;
    std::memset( rec.name, 0, sizeof( rec.name ) );
    std::memcpy( rec.name, name, std::strlen( name ) );
    return true;
}

static_assert( std::is_trivially_copyable_v<ForestDomainRecord<DefaultAddressTraits>>,
               "ForestDomainRecord must be trivially copyable" );
static_assert( std::is_nothrow_default_constructible_v<ForestDomainRegistry<DefaultAddressTraits>>,
               "ForestDomainRegistry must be nothrow-default-constructible" );

} // namespace pmm::detail
