from pathlib import Path

manager_path = Path("include/pmm/persist_memory_manager.h")
shard_path = Path("include/pmm/forest_domain_mixin.inc")
cmake_path = Path("tests/CMakeLists.txt")
rule_path = Path("req/02_business_rules.md")
fr_path = Path("req/05_functional_requirements.md")

manager = manager_path.read_text()
shard = shard_path.read_text().rstrip()
include = '#include "pmm/forest_domain_mixin.inc"'
if manager.count(include) != 1:
    raise SystemExit(f"expected one forest-domain include, got {manager.count(include)}")


def replace_one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one old block, got {count}")
    return text.replace(old, new)


shard = replace_one(
    shard,
    """static bool register_domain_unlocked( const char* name, uint8_t flags, uint8_t binding_kind,
                                      index_type initial_root ) noexcept
{
    if ( !detail::forest_domain_name_fits( name ) )
        return false;
    forest_registry* reg = forest_registry_root_unlocked();""",
    """static bool register_domain_unlocked( const char* name, uint8_t flags, uint8_t binding_kind,
                                      index_type initial_root ) noexcept
{
    if ( !detail::forest_domain_name_fits( name ) )
        return false;
    detail::relocation_owner<const char, manager_type> name_owner( name );
    forest_registry* reg = forest_registry_root_unlocked();""",
    "register-domain name owner",
)

shard = replace_one(
    shard,
    """            pptr<pstringview> symbol = intern_symbol_unlocked( name );
            existing                 = find_domain_by_name_unlocked( name );
            if ( existing != nullptr && !symbol.is_null() )
                existing->symbol_offset = symbol.offset();""",
    """            pptr<pstringview> symbol = intern_symbol_unlocked( name );
            name                     = name_owner.get();
            existing                 = name != nullptr ? find_domain_by_name_unlocked( name ) : nullptr;
            if ( existing != nullptr && !symbol.is_null() )
                existing->symbol_offset = symbol.offset();""",
    "register-domain post-intern name rebind",
)

shard = replace_one(
    shard,
    """static pptr<pstringview> intern_symbol_unlocked( const char* s ) noexcept
{
    if ( s == nullptr )
        s = "";
    auto symbol_policy = pstringview::forest_domain_ops();""",
    """static pptr<pstringview> intern_symbol_unlocked( const char* s ) noexcept
{
    if ( s == nullptr )
        s = "";
    detail::relocation_owner<const char, manager_type> source( s );
    auto symbol_policy = pstringview::forest_domain_ops();""",
    "intern source owner",
)

shard = replace_one(
    shard,
    """    void*    raw        = allocate_unlocked( alloc_size );
    if ( raw == nullptr )
        return pptr<pstringview>();
    pptr<pstringview> new_node   = make_pptr_from_raw<pstringview>( raw );""",
    """    void*    raw        = allocate_unlocked( alloc_size );
    if ( raw == nullptr )
        return pptr<pstringview>();
    s = source.get();
    if ( s == nullptr )
    {
        deallocate_unlocked( raw );
        return pptr<pstringview>();
    }
    pptr<pstringview> new_node   = make_pptr_from_raw<pstringview>( raw );""",
    "intern post-allocation source rebind",
)

shard = replace_one(
    shard,
    """    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return false;
    for ( uint16_t i = 0; i < reg->domain_count; ++i )
    {
        if ( reg->domains[i].name[0] == '\\0' )
            continue;
        if ( reg->domains[i].symbol_offset != 0 )
            continue;
        pptr<pstringview> symbol = intern_symbol_unlocked( reg->domains[i].name );
        if ( symbol.is_null() )
            return false;
        reg->domains[i].symbol_offset = symbol.offset();
    }
    return true;""",
    """    forest_registry* reg = forest_registry_root_unlocked();
    if ( reg == nullptr )
        return false;
    const uint16_t domain_count = reg->domain_count;
    for ( uint16_t i = 0; i < domain_count; ++i )
    {
        reg = forest_registry_root_unlocked();
        if ( reg == nullptr || i >= reg->domain_count )
            return false;
        if ( reg->domains[i].name[0] == '\\0' || reg->domains[i].symbol_offset != 0 )
            continue;
        const index_type binding_id = reg->domains[i].binding_id;
        pptr<pstringview> symbol = intern_symbol_unlocked( reg->domains[i].name );
        if ( symbol.is_null() )
            return false;
        forest_domain* rec = find_domain_by_binding_unlocked( binding_id );
        if ( rec == nullptr )
            return false;
        rec->symbol_offset = symbol.offset();
    }
    return true;""",
    "bootstrap registry rebind",
)

shard = replace_one(
    shard,
    """static bool validate_or_bootstrap_forest_registry_unlocked() noexcept
{
    detail::ManagerHeader<address_traits>* hdr = get_header( _backend.base_ptr() );
    if ( forest_registry_root_unlocked() != nullptr )""",
    """static bool validate_or_bootstrap_forest_registry_unlocked() noexcept
{
    detail::ManagerHeader<address_traits>* hdr = get_header( _backend.base_ptr() );
    const index_type registry_root = hdr->root_offset;
    if ( forest_registry_root_unlocked() != nullptr )""",
    "bootstrap header snapshot",
)

shard = replace_one(
    shard,
    """        if ( !register_domain_unlocked( detail::kSystemDomainRegistry, detail::kForestDomainFlagSystem,
                                        detail::kForestBindingDirectRoot, hdr->root_offset ) )""",
    """        if ( !register_domain_unlocked( detail::kSystemDomainRegistry, detail::kForestDomainFlagSystem,
                                        detail::kForestBindingDirectRoot, registry_root ) )""",
    "bootstrap stable registry root",
)

manager = manager.replace(include, shard)
# Do not clang-format the canonical PMM header: source anchors intentionally stay at column zero.
for anchor in (
    "\n/*\n## pmm-persistmemorymanager\n",
    "\n/*\n### pmm-persistmemorymanager-create\n",
    "\n/*\n### pmm-persistmemorymanager-load\n",
    "\n/*\n### pmm-persistmemorymanager-destroy\n",
    "\n/*\n### pmm-persistmemorymanager-allocate\n",
):
    if anchor not in manager:
        raise SystemExit(f"required source anchor shape missing after inline: {anchor!r}")
manager_path.write_text(manager)
shard_path.unlink()

cmake = cmake_path.read_text()
marker = "pmm_add_test(test_issue404_pmap_relocation test_issue404_pmap_relocation.cpp)"
line = "pmm_add_test(test_issue410_forest_registry_relocation test_issue410_forest_registry_relocation.cpp)"
if cmake.count(marker) != 1 or line in cmake:
    raise SystemExit("unexpected issue404/issue410 CMake registration state")
cmake_path.write_text(
    cmake.replace(marker, marker + "\n\n# Issue 410: forest registry views survive allocation-driven arena relocation\n" + line)
)

old_ref = "[forest_domain_mixin.inc](../include/pmm/forest_domain_mixin.inc)"
new_ref = "[pmm-persistmemorymanager](../include/pmm/persist_memory_manager.h#pmm-persistmemorymanager)"
for path, label in ((rule_path, "rule-008"), (fr_path, "fr-012")):
    text = path.read_text()
    if text.count(old_ref) != 1:
        raise SystemExit(f"{label}: expected one legacy forest-domain requirement link, got {text.count(old_ref)}")
    path.write_text(text.replace(old_ref, new_ref))
