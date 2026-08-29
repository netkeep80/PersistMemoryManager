from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label} count={count}")
    return text.replace(old, new)


pmap_path = Path("include/pmm/pmap.h")
types_path = Path("include/pmm/types.h")
version_test_path = Path("tests/test_issue329_image_version.cpp")
cmake_path = Path("tests/CMakeLists.txt")

pmap = pmap_path.read_text()
pmap = replace_once(
    pmap,
    '''template <typename T> struct pmap_type_identity
{
    static constexpr const char* tag = "";
};''',
    '''template <typename T> struct pmap_type_identity
{
    static constexpr const char* tag = "";
};
template <typename ManagerT> struct pmap_type_identity<pstringview<ManagerT>>
{
    static constexpr const char* tag = "pmm/pstringview/v1";
};''',
    "pstringview stable tag insertion point",
)

pmap = replace_once(
    pmap,
    '''template <typename T> inline constexpr bool pmap_storage_type_v = pmap_storage_type<T>::value;
constexpr uint32_t pmap_fnv1a( uint32_t h, uint64_t v, unsigned bytes ) noexcept
{
    for ( unsigned i = 0; i < bytes; ++i, v >>= 8 )
    {
        h ^= static_cast<uint8_t>( v & 0xffull );
        h *= 16777619u;
    }
    return h;
}
template <typename T> constexpr uint32_t pmap_type_fp() noexcept
{
    const uint64_t traits =
        ( uint64_t{ std::is_integral_v<T> } << 0 ) | ( uint64_t{ std::is_floating_point_v<T> } << 1 ) |
        ( uint64_t{ std::is_signed_v<T> } << 2 ) | ( uint64_t{ std::is_unsigned_v<T> } << 3 ) |
        ( uint64_t{ std::is_pointer_v<T> } << 4 ) | ( uint64_t{ std::is_class_v<T> } << 5 ) |
        ( uint64_t{ std::is_enum_v<T> } << 6 ) | ( uint64_t{ std::is_trivially_copyable_v<T> } << 7 ) |
        ( uint64_t{ std::is_standard_layout_v<T> } << 8 );
    uint32_t h = 2166136261u;
    h          = pmap_fnv1a( h, sizeof( T ), 8 );
    h          = pmap_fnv1a( h, alignof( T ), 8 );
    h          = pmap_fnv1a( h, traits, 8 );
    for ( const char* t = pmm::pmap_type_identity<T>::tag; t != nullptr && *t != '\\0'; ++t )
        h = pmap_fnv1a( h, static_cast<uint8_t>( *t ), 1 );
    return h;
}
inline uint64_t pmap_key_hash( const char* key ) noexcept
{
    uint64_t h = 14695981039346656037ull;
    for ( ; key != nullptr && *key != '\\0'; ++key )
    {
        h ^= static_cast<uint8_t>( *key );
        h *= 1099511628211ull;
    }
    return h;
}''',
    '''template <typename T> inline constexpr bool pmap_storage_type_v = pmap_storage_type<T>::value;
// clang-format off
template <typename T> inline constexpr const char* pmap_pptr_tag_v = nullptr;
template <typename Pointee, typename ManagerT> inline constexpr const char* pmap_pptr_tag_v<pptr<Pointee, ManagerT>> = pmap_type_identity<Pointee>::tag;
template <typename T> inline constexpr bool pmap_has_stable_identity_v = pmap_pptr_tag_v<T> == nullptr || pmap_pptr_tag_v<T>[0] != '\\0';
constexpr uint32_t pmap_fnv1a( uint32_t h, uint64_t v, unsigned bytes ) noexcept { for ( unsigned i = 0; i < bytes; ++i, v >>= 8 ) h = ( h ^ static_cast<uint8_t>( v & 0xffull ) ) * 16777619u; return h; }
template <typename T> constexpr uint32_t pmap_type_fp() noexcept {
    constexpr const char* tag = pmap_pptr_tag_v<T>;
    if constexpr ( tag != nullptr ) { static_assert( pmap_has_stable_identity_v<T>, "pmap typed handle requires stable pointee identity" ); uint32_t h = 0x749ed278u; for ( const char* t = tag; *t != '\\0'; ++t ) h = pmap_fnv1a( h, static_cast<uint8_t>( *t ), 1 ); return h; }
    const uint64_t traits = ( uint64_t{ std::is_integral_v<T> } << 0 ) | ( uint64_t{ std::is_floating_point_v<T> } << 1 ) | ( uint64_t{ std::is_signed_v<T> } << 2 ) | ( uint64_t{ std::is_unsigned_v<T> } << 3 ) | ( uint64_t{ std::is_pointer_v<T> } << 4 ) | ( uint64_t{ std::is_class_v<T> } << 5 ) | ( uint64_t{ std::is_enum_v<T> } << 6 ) | ( uint64_t{ std::is_trivially_copyable_v<T> } << 7 ) | ( uint64_t{ std::is_standard_layout_v<T> } << 8 );
    uint32_t h = 2166136261u; h = pmap_fnv1a( h, sizeof( T ), 8 ); h = pmap_fnv1a( h, alignof( T ), 8 ); h = pmap_fnv1a( h, traits, 8 );
    for ( const char* t = pmm::pmap_type_identity<T>::tag; t != nullptr && *t != '\\0'; ++t ) h = pmap_fnv1a( h, static_cast<uint8_t>( *t ), 1 );
    return h;
}
inline uint64_t pmap_key_hash( const char* key ) noexcept { uint64_t h = 14695981039346656037ull; for ( ; key != nullptr && *key != '\\0'; ++key ) h = ( h ^ static_cast<uint8_t>( *key ) ) * 1099511628211ull; return h; }
// clang-format on''',
    "identity/hash replacement",
)

pmap = replace_once(
    pmap,
    '''    constexpr const char* kPrefix = "container/pmap/";
    const unsigned        needed  = 15 + 8 + 1 + 1 + value_hex_digits + 1;
    if ( needed > kForestDomainNameCapacity )
        return false;
    size_t p = 0;
    for ( const char* s = kPrefix; *s != '\\0'; ++s )
        out[p++] = *s;
    auto put_hex = [&]( uint64_t v, unsigned digits )
    {
        for ( unsigned i = digits; i-- > 0; )
        {
            const uint8_t nib = static_cast<uint8_t>( ( v >> ( i * 4 ) ) & 0x0full );
            out[p++]          = static_cast<char>( nib < 10 ? ( '0' + nib ) : ( 'a' + ( nib - 10 ) ) );
        }
    };''',
    '''    if ( 15 + 8 + 1 + 1 + value_hex_digits + 1 > kForestDomainNameCapacity )
        return false;
    size_t p = 0;
    for ( const char* s = "container/pmap/"; *s != '\\0'; ++s )
        out[p++] = *s;
    auto put_hex = [&]( uint64_t v, unsigned digits )
    {
        for ( unsigned i = digits; i-- > 0; )
            out[p++] = "0123456789abcdef"[( v >> ( i * 4 ) ) & 0x0full];
    };''',
    "pmap name writer replacement",
)
pmap_path.write_text(pmap)

types = types_path.read_text()
types = replace_once(
    types,
    "inline constexpr uint8_t kCurrentImageVersion           = 2;",
    "inline constexpr uint8_t kCurrentImageVersion           = 3;",
    "current image version replacement",
)
types_path.write_text(types)

version_test = version_test_path.read_text()
version_test = replace_once(
    version_test,
    '''using VersionVerifyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32903>;
using VersionLegacyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32904>;''',
    '''using VersionVerifyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32903>;
using VersionLegacyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32904>;
using VersionV2Mgr     = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32905>;''',
    "version manager alias insertion point",
)
version_test = replace_once(
    version_test,
    '''    static_assert( pmm::detail::kCurrentImageVersion >= 2,
                   "Issue #367 bumps the persisted image version to 2 (or later) to break legacy compatibility" );''',
    '''    static_assert( pmm::detail::kCurrentImageVersion == 3,
                   "Issue #426 deliberately advances typed-handle pmap semantics to image version 3" );''',
    "image version static assertion replacement",
)
anchor = '''TEST_CASE( "issue329/issue367: legacy unversioned images are rejected, not migrated", "[issue329][issue367][load]" )'''
new_test = '''TEST_CASE( "issue426: image version 2 is rejected after typed-handle identity break", "[issue329][issue426][load]" )
{
    VersionV2Mgr::destroy();
    REQUIRE( VersionV2Mgr::create( 64 * 1024 ) );
    VersionV2Mgr::destroy();

    auto* hdr = pmm::detail::manager_header_at<pmm::DefaultAddressTraits>( VersionV2Mgr::backend().base_ptr() );
    hdr->image_version = 2;

    VersionV2Mgr::clear_error();
    pmm::VerifyResult result;
    REQUIRE_FALSE( VersionV2Mgr::load( result ) );
    REQUIRE( VersionV2Mgr::last_error() == pmm::PmmError::UnsupportedImageVersion );
    REQUIRE( result.entry_count == 1 );
    REQUIRE( result.entries[0].expected == 3 );
    REQUIRE( result.entries[0].actual == 2 );
    REQUIRE_FALSE( pmm::detail::image_version_requires_migration( 2 ) );
    VersionV2Mgr::destroy();
}

''' + anchor
version_test = replace_once(version_test, anchor, new_test, "v2 rejection test insertion point")
version_test_path.write_text(version_test)

cmake = cmake_path.read_text()
marker = "pmm_add_test(test_issue410_forest_registry_relocation test_issue410_forest_registry_relocation.cpp)"
line = "pmm_add_test(test_issue426_pmap_type_identity test_issue426_pmap_type_identity.cpp)"
if cmake.count(marker) != 1 or line in cmake:
    raise SystemExit("unexpected issue410/issue426 CMake registration state")
cmake_path.write_text(cmake.replace(marker, marker + "\n\n# Issue 426: stable typed-handle pmap identity and image-v3 persistence\n" + line))
