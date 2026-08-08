from pathlib import Path

pmap_path = Path("include/pmm/pmap.h")
test_path = Path("tests/test_issue404_pmap_relocation.cpp")

pmap = pmap_path.read_text()
old = """namespace detail
{
constexpr uint32_t pmap_fnv1a( uint32_t h, uint64_t v, unsigned bytes ) noexcept"""
new = """namespace detail
{
template <typename T>
inline constexpr bool pmap_storage_type_v = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> &&
                                            !std::is_pointer_v<T> && !std::is_member_pointer_v<T>;
constexpr uint32_t pmap_fnv1a( uint32_t h, uint64_t v, unsigned bytes ) noexcept"""
if pmap.count(old) != 1:
    raise SystemExit(f"pmap storage trait insertion point count={pmap.count(old)}")
pmap = pmap.replace(old, new)

old = """template <typename _K, typename _V, typename ManagerT> struct pmap
{
    using manager_type"""
new = """template <typename _K, typename _V, typename ManagerT> struct pmap
{
    static_assert( detail::pmap_storage_type_v<_K>,
                   "pmap key must be a trivially copyable standard-layout persistent representation, not a raw pointer" );
    static_assert( detail::pmap_storage_type_v<_V>,
                   "pmap value must be a trivially copyable standard-layout persistent representation, not a raw pointer" );
    using manager_type"""
if pmap.count(old) != 1:
    raise SystemExit(f"pmap contract insertion point count={pmap.count(old)}")
pmap_path.write_text(pmap.replace(old, new))

test = test_path.read_text()
old = """namespace
{

template <typename Mgr, typename T>"""
new = """namespace
{

struct NonPersistentValue
{
    int value;
    ~NonPersistentValue() {}
};

using ContractMgr = pmm::PersistMemoryManager<pmm::CacheManagerConfig, 4150>;
static_assert( pmm::detail::pmap_storage_type_v<int> );
static_assert( pmm::detail::pmap_storage_type_v<ContractMgr::pptr<int>> );
static_assert( !pmm::detail::pmap_storage_type_v<int*> );
static_assert( !pmm::detail::pmap_storage_type_v<NonPersistentValue> );
using ContractMap = ContractMgr::pmap<int, ContractMgr::pptr<int>>;
static_assert( std::is_default_constructible_v<ContractMap> );

template <typename Mgr, typename T>"""
if test.count(old) != 1:
    raise SystemExit(f"issue404 compile-time test insertion point count={test.count(old)}")
test_path.write_text(test.replace(old, new))
