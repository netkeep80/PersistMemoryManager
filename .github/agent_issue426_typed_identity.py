from pathlib import Path

pmap_path = Path("include/pmm/pmap.h")
types_path = Path("include/pmm/types.h")
version_test_path = Path("tests/test_issue329_image_version.cpp")
cmake_path = Path("tests/CMakeLists.txt")

pmap = pmap_path.read_text()
old = '''template <typename T> struct pmap_type_identity
{
    static constexpr const char* tag = "";
};'''
new = '''template <typename T> struct pmap_type_identity
{
    static constexpr const char* tag = "";
};
template <typename ManagerT> struct pmap_type_identity<pstringview<ManagerT>>
{
    static constexpr const char* tag = "pmm/pstringview/v1";
};'''
if pmap.count(old) != 1:
    raise SystemExit(f"pstringview stable tag insertion point count={pmap.count(old)}")
pmap = pmap.replace(old, new)

old = '''template <typename T> inline constexpr bool pmap_storage_type_v = pmap_storage_type<T>::value;
constexpr uint32_t pmap_fnv1a'''
new = '''template <typename T> inline constexpr bool pmap_storage_type_v = pmap_storage_type<T>::value;
template <typename T> struct pmap_pptr_identity
{
    static constexpr bool value = false;
};
template <typename Pointee, typename ManagerT> struct pmap_pptr_identity<pptr<Pointee, ManagerT>>
{
    static constexpr bool value = true;
    using pointee_type = Pointee;
};
template <typename T> constexpr bool pmap_has_stable_identity() noexcept
{
    if constexpr ( pmap_pptr_identity<T>::value )
    {
        const char* tag = pmap_type_identity<typename pmap_pptr_identity<T>::pointee_type>::tag;
        return tag != nullptr && tag[0] != '\\0';
    }
    return true;
}
template <typename T> inline constexpr bool pmap_has_stable_identity_v = pmap_has_stable_identity<T>();
constexpr uint32_t pmap_fnv1a'''
if pmap.count(old) != 1:
    raise SystemExit(f"typed handle identity trait insertion point count={pmap.count(old)}")
pmap = pmap.replace(old, new)

old = '''template <typename T> constexpr uint32_t pmap_type_fp() noexcept
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
}'''
new = '''template <typename T> constexpr uint32_t pmap_type_fp() noexcept
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
    if constexpr ( pmap_pptr_identity<T>::value )
    {
        for ( const char* m = "pmm/pptr/v1"; *m != '\\0'; ++m )
            h = pmap_fnv1a( h, static_cast<uint8_t>( *m ), 1 );
        using pointee_type = typename pmap_pptr_identity<T>::pointee_type;
        for ( const char* t = pmm::pmap_type_identity<pointee_type>::tag; t != nullptr && *t != '\\0'; ++t )
            h = pmap_fnv1a( h, static_cast<uint8_t>( *t ), 1 );
    }
    else
        for ( const char* t = pmm::pmap_type_identity<T>::tag; t != nullptr && *t != '\\0'; ++t )
            h = pmap_fnv1a( h, static_cast<uint8_t>( *t ), 1 );
    return h;
}'''
if pmap.count(old) != 1:
    raise SystemExit(f"pmap_type_fp replacement count={pmap.count(old)}")
pmap = pmap.replace(old, new)

old = '''    static_assert( detail::pmap_storage_type_v<_V>,
                   "pmap value must be a supported fixed-size trivial persistent representation, not a raw pointer" );
    using manager_type'''
new = '''    static_assert( detail::pmap_storage_type_v<_V>,
                   "pmap value must be a supported fixed-size trivial persistent representation, not a raw pointer" );
    static_assert( detail::pmap_has_stable_identity_v<_K>,
                   "pmap typed-handle key requires a stable pmap_type_identity tag for its pointee type" );
    static_assert( detail::pmap_has_stable_identity_v<_V>,
                   "pmap typed-handle value requires a stable pmap_type_identity tag for its pointee type" );
    using manager_type'''
if pmap.count(old) != 1:
    raise SystemExit(f"pmap stable identity assertion insertion point count={pmap.count(old)}")
pmap_path.write_text(pmap.replace(old, new))

types = types_path.read_text()
old = "inline constexpr uint8_t kCurrentImageVersion           = 2;"
new = "inline constexpr uint8_t kCurrentImageVersion           = 3;"
if types.count(old) != 1:
    raise SystemExit(f"current image version replacement count={types.count(old)}")
types_path.write_text(types.replace(old, new))

version_test = version_test_path.read_text()
old = '''using VersionVerifyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32903>;
using VersionLegacyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32904>;'''
new = '''using VersionVerifyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32903>;
using VersionLegacyMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32904>;
using VersionV2Mgr     = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 32905>;'''
if version_test.count(old) != 1:
    raise SystemExit(f"version manager alias insertion point count={version_test.count(old)}")
version_test = version_test.replace(old, new)
old = '''    static_assert( pmm::detail::kCurrentImageVersion >= 2,
                   "Issue #367 bumps the persisted image version to 2 (or later) to break legacy compatibility" );'''
new = '''    static_assert( pmm::detail::kCurrentImageVersion == 3,
                   "Issue #426 deliberately advances typed-handle pmap semantics to image version 3" );'''
if version_test.count(old) != 1:
    raise SystemExit(f"image version static assertion replacement count={version_test.count(old)}")
version_test = version_test.replace(old, new)
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
if version_test.count(anchor) != 1:
    raise SystemExit(f"v2 rejection test insertion point count={version_test.count(anchor)}")
version_test_path.write_text(version_test.replace(anchor, new_test))

cmake = cmake_path.read_text()
marker = "pmm_add_test(test_issue410_forest_registry_relocation test_issue410_forest_registry_relocation.cpp)"
line = "pmm_add_test(test_issue426_pmap_type_identity test_issue426_pmap_type_identity.cpp)"
if cmake.count(marker) != 1 or line in cmake:
    raise SystemExit("unexpected issue410/issue426 CMake registration state")
cmake_path.write_text(cmake.replace(marker, marker + "\n\n# Issue 426: stable typed-handle pmap identity and image-v3 persistence\n" + line))
