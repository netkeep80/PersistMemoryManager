from pathlib import Path

manager_path = Path("include/pmm/persist_memory_manager.h")
shard_path = Path("include/pmm/forest_domain_mixin.inc")
manager = manager_path.read_text()
shard = shard_path.read_text().rstrip()
include = '#include "pmm/forest_domain_mixin.inc"'
if manager.count(include) != 1:
    raise SystemExit(f"expected one forest-domain include, got {manager.count(include)}")
manager = manager.replace(include, shard)

replacements = []

old = """static bool register_domain_unlocked( const char* name, uint8_t flags, uint8_t binding_kind,
                                              index_type initial_root ) noexcept
        {
            if ( !detail::forest_domain_name_fits( name ) )
                return false;
            forest_registry* reg = forest_registry_root_unlocked();"""
new = """static bool register_domain_unlocked( const char* name, uint8_t flags, uint8_t binding_kind,
                                              index_type initial_root ) noexcept
        {
            if ( !detail::forest_domain_name_fits( name ) )
                return false;
            detail::relocation_owner<const char, manager_type> name_owner( name );
            forest_registry* reg = forest_registry_root_unlocked();"""
replacements.append((old, new, "register-domain name owner"))

old = """                pptr<pstringview> symbol = intern_symbol_unlocked( name );
                existing                 = find_domain_by_name_unlocked( name );
                if ( existing != nullptr && !symbol.is_null() )
                    existing->symbol_offset = symbol.offset();"""
new = """                pptr<pstringview> symbol = intern_symbol_unlocked( name );
                name                     = name_owner.get();
                existing                 = name != nullptr ? find_domain_by_name_unlocked( name ) : nullptr;
                if ( existing != nullptr && !symbol.is_null() )
                    existing->symbol_offset = symbol.offset();"""
replacements.append((old, new, "register-domain post-intern name rebind"))

old = """        static pptr<pstringview> intern_symbol_unlocked( const char* s ) noexcept
        {
            if ( s == nullptr )
                s = "";
            auto symbol_policy = pstringview::forest_domain_ops();"""
new = """        static pptr<pstringview> intern_symbol_unlocked( const char* s ) noexcept
        {
            if ( s == nullptr )
                s = "";
            detail::relocation_owner<const char, manager_type> source( s );
            auto symbol_policy = pstringview::forest_domain_ops();"""
replacements.append((old, new, "intern source owner"))

old = """            void*    raw        = allocate_unlocked( alloc_size );
            if ( raw == nullptr )
                return pptr<pstringview>();
            pptr<pstringview> new_node   = make_pptr_from_raw<pstringview>( raw );"""
new = """            void*    raw        = allocate_unlocked( alloc_size );
            if ( raw == nullptr )
                return pptr<pstringview>();
            s = source.get();
            if ( s == nullptr )
            {
                deallocate_unlocked( raw );
                return pptr<pstringview>();
            }
            pptr<pstringview> new_node   = make_pptr_from_raw<pstringview>( raw );"""
replacements.append((old, new, "intern post-allocation source rebind"))

old = """            forest_registry* reg = forest_registry_root_unlocked();
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
            return true;"""
new = """            forest_registry* reg = forest_registry_root_unlocked();
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
            return true;"""
replacements.append((old, new, "bootstrap registry rebind"))

old = """        static bool validate_or_bootstrap_forest_registry_unlocked() noexcept
        {
            detail::ManagerHeader<address_traits>* hdr = get_header( _backend.base_ptr() );
            if ( forest_registry_root_unlocked() != nullptr )"""
new = """        static bool validate_or_bootstrap_forest_registry_unlocked() noexcept
        {
            detail::ManagerHeader<address_traits>* hdr = get_header( _backend.base_ptr() );
            const index_type registry_root = hdr->root_offset;
            if ( forest_registry_root_unlocked() != nullptr )"""
replacements.append((old, new, "bootstrap header snapshot"))

old = """                if ( !register_domain_unlocked( detail::kSystemDomainRegistry, detail::kForestDomainFlagSystem,
                                                detail::kForestBindingDirectRoot, hdr->root_offset ) )"""
new = """                if ( !register_domain_unlocked( detail::kSystemDomainRegistry, detail::kForestDomainFlagSystem,
                                                detail::kForestBindingDirectRoot, registry_root ) )"""
replacements.append((old, new, "bootstrap stable registry root"))

for old, new, label in replacements:
    count = manager.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one old block, got {count}")
    manager = manager.replace(old, new)

manager_path.write_text(manager)
shard_path.unlink()

cmake = Path("tests/CMakeLists.txt")
text = cmake.read_text()
marker = "pmm_add_test(test_issue403_embedded_containers test_issue403_embedded_containers.cpp)"
line = "pmm_add_test(test_issue410_forest_registry_relocation test_issue410_forest_registry_relocation.cpp)"
if text.count(marker) != 1 or line in text:
    raise SystemExit("unexpected CMake issue403/issue410 registration state")
cmake.write_text(text.replace(marker, marker + "\n\n# Issue 410: forest registry views survive allocation-driven arena relocation\n" + line))
